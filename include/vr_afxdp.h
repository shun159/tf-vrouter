/*
 *
 * Copyright (C) 2025 Eishun Kondoh
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VR_AFXDP_H_
#define _VR_AFXDP_H_

#include <linux/types.h>

#include "vr_os.h"
#include "vr_interface.h"
#include "vr_packet.h"
#include "vr_fragment.h"
#include "vr_cpuid.h"
#include "vr_flow.h"

#include <xdp/libxdp.h>
#include <xdp/xsk.h>
#include <urcu-qsbr.h>
#include <liburing.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define VR_AFXDP_MAX_FLOW_TABLE_HOLD_COUNT 1000
#define VR_AFXDP_SLEEP_SERVICE_US 100
#define VR_AFXDP_MAX_FRAGMENT_ELEMENTS 1024

/* Define necessary parameters. These may be tuned as needed. */
#define FRAME_SIZE 4096
#define FRAME_SHIFT 12
#define MAX_FRAMES 4096

#define NUM_FRAMES XSK_UMEM__DEFAULT_FRAME_SIZE
#define PROD_NUM_DESCS XSK_RING_PROD__DEFAULT_NUM_DESCS
#define CONS_NUM_DESCS XSK_RING_CONS__DEFAULT_NUM_DESCS

#define BATCH_SIZE 32

/* XDP section */

#define XDP_FLAGS_UPDATE_IF_NOEXIST (1U << 0)
#define XDP_FLAGS_SKB_MODE (1U << 1)
#define XDP_FLAGS_DRV_MODE (1U << 2)
#define XDP_FLAGS_HW_MODE (1U << 3)
#define XDP_FLAGS_REPLACE (1U << 4)
#define XDP_FLAGS_MODES (XDP_FLAGS_SKB_MODE | XDP_FLAGS_DRV_MODE | XDP_FLAGS_HW_MODE)
#define XDP_FLAGS_MASK (XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_MODES | XDP_FLAGS_REPLACE)

//  Buffer pool and buffer cache
struct bpool_params {
    __u32 n_buffers;
    __u32 buffer_size;
    __s32 mmap_flags;

    __u32 n_users_max;
    __u32 n_buffers_per_slab;
};

/* This buffer pool implementation organizes the buffers into equally sized
 * slabs of *n_buffers_per_slab*. Initially, there are *n_slabs* slabs in the
 * pool that are completely filled with buffer pointers (full slabs).
 *
 * Each buffer cache has a slab for buffer allocation and a slab for buffer
 * free, with both of these slabs initially empty. When the cache's allocation
 * slab goes empty, it is swapped with one of the available full slabs from the
 * pool, if any is available. When the cache's free slab goes full, it is
 * swapped for one of the empty slabs from the pool, which is guaranteed to
 * succeed.
 *
 * Partially filled slabs never get traded between the cache and the pool
 * (except when the cache itself is destroyed), which enables fast operation
 * through pointer swapping.
 */
struct bpool {
    struct bpool_params params;
    pthread_mutex_t lock;
    void *addr;

    __u64 **slabs;
    __u64 **slabs_reserved;
    __u64 *buffers;
    __u64 *buffers_reserved;

    __u64 n_slabs;
    __u64 n_slabs_reserved;
    __u64 n_buffers;

    __u64 n_slabs_available;
    __u64 n_slabs_reserved_available;

    struct xsk_umem_config umem_cfg;
    struct xsk_ring_prod umem_fq;
    struct xsk_ring_cons umem_cq;
    struct xsk_umem *umem;
};

/* This buffer pool implementation organizes the buffers into equally sized
 * slabs of *n_buffers_per_slab*. Initially, there are *n_slabs* slabs in the
 * pool that are completely filled with buffer pointers (full slabs).
 *
 * Each buffer cache has a slab for buffer allocation and a slab for buffer
 * free, with both of these slabs initially empty. When the cache's allocation
 * slab goes empty, it is swapped with one of the available full slabs from the
 * pool, if any is available. When the cache's free slab goes full, it is
 * swapped for one of the empty slabs from the pool, which is guaranteed to
 * succeed.
 *
 * Partially filled slabs never get traded between the cache and the pool
 * (except when the cache itself is destroyed), which enables fast operation
 * through pointer swapping.
 */
struct bcache {
    struct bpool *bp;

    __u64 *slab_cons;
    __u64 *slab_prod;

    __u64 n_buffers_cons;
    __u64 n_buffers_prod;
};

static const struct xsk_umem_config umem_cfg_default = {
    .fill_size = 4096,
    .comp_size = 4096,
    .frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE,
    .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
    .flags = 0,
};

static const struct bpool_params bpool_params_default = {
    .n_buffers = 64 * 1024,
    .buffer_size = XSK_UMEM__DEFAULT_FRAME_SIZE,
    .mmap_flags = 0,
    .n_users_max = 16,
    .n_buffers_per_slab = PROD_NUM_DESCS * 2,
};

/*
 * vr_xdp_buf <=> vr_packet conversion
 *
 * In the AF_XDP design, the packet buffer layout in UMEM is arranged as
 * follows:
 *
 *      [ struct vr_xdp_buf ] + [ struct vr_packet ] + headroom + data +
 * tailroom
 *
 * Here, 'struct vr_xdp_buf' serves a role similar to afxdp's 'struct rte_mbuf',
 * holding metadata about the allocated UMEM frame.
 *
 * Typical fields include:
 *   - umem:      Pointer to the UMEM context from which this buffer is
 * allocated.
 *   - addr:      Offset (in bytes) within the UMEM where this frame begins.
 *   - buf_len:   Total length of the allocated frame (constant: FRAME_SIZE).
 *   - data_len:  Length of the received packet data (set by XDP, from
 * xdp_desc).
 *
 * The 'struct vr_packet' immediately follows this metadata structure.
 *
 * Conversion functions:
 *
 *  To convert a pointer to vr_packet into its associated vr_xdp_buf pointer:
 *      Subtract sizeof(struct vr_xdp_buf) from the vr_packet pointer.
 *
 *  To convert a pointer to vr_xdp_buf into its associated vr_packet pointer:
 *      Add sizeof(struct vr_xdp_buf) to the vr_xdp_buf pointer.
 */
struct vr_xdp_buf {
    void *buf_addr; // Offset within the UMEM where this frame starts
    __u32 buf_len;  // Total length of this frame (FRAME_SIZE)
    __u32 data_len; // Length of valid packet data (from xdp_desc->len)
    __u32 data_off;

    __u64 desc_addr;
    __u16 queue_id;
    __u16 flags;

    __u32 nb_segs;
    __u32 tso_segsz;
    struct vr_xdp_buf *next;

    // Optionally, add fields for flags or other metadata as needed
};

// packet struct container for AF_XDP
struct vr_xpacket {
    struct vr_xdp_buf xbuf;
    struct vr_packet pkt;
};

// tx burst cache
#define AFXDP_TX_BURST_SZ 64

struct vr_afxdp_tx_cache {
    __u64 addr[AFXDP_TX_BURST_SZ];
    __u32 len[AFXDP_TX_BURST_SZ];
    __u32 n_pkts;
};

struct vr_afxdp_xsk_socket_info {
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem *umem;
    struct xsk_socket *xsk;
    struct bpool *bpool;
    struct bcache *bcache;
    struct bpool_params bpool_params;
    struct vr_afxdp_tx_cache tx_cache;
    __u32 outstanding_tx; /* Number of descriptors filled in tx and cq. */
    __u32 available_rx;   /* Number of descriptors filled in rx and fq. */
};

// Ethdev configuration
struct vr_afxdp_ethdev {
    __u16 os_ifidx;
    __u32 vif_idx;
    __u32 batch_size;
    __u32 num_queues;

    struct xsk_umem_config umem_cfg;
    struct vr_afxdp_xsk_socket_info **xsks;
};

/* Tapdev configuration. */
struct vr_afxdp_tapdev {
    /* Tapdev file descriptor. */
    volatile int tapdev_fd;
    /* Tapdev file descriptor for vhost0. */
    int tapdev_vhost_fd;
    /* Pointer to vif. */
    struct vr_interface *tapdev_vif;
    /* Name of the corresponding device on kernel. */
    char tapdev_name[VR_INTERFACE_NAME_LEN];
};

#define AFXDP_PKT_HEADROOM 128

static const struct xsk_socket_config xsk_cfg_default = {
    .rx_size = 4096,
    .tx_size = 4096,
    .libbpf_flags = 0,
    .bind_flags = XDP_USE_NEED_WAKEUP | XDP_COPY,
    .xdp_flags = XDP_FLAGS_DRV_MODE,
};

struct vr_xpacket *afxdp_xpacket_from_pkt(struct vr_packet *pkt);
struct vr_xdp_buf *vr_afxdp_pkt_to_xdp_buf(struct vr_packet *pkt);
struct afxdp_meta *vr_afxdp_pkt_to_afxdp_meta(struct vr_packet *pkt);
struct vr_packet *vr_afxdp_xdp_buf_to_pkt(struct vr_xdp_buf *xdp_buf);
struct vr_xdp_buf *afxdp_xdp_buf_copy(struct vr_xdp_buf *src, void *pool);
void afxdp_xdp_buf_free(struct vr_xdp_buf *xdp_buf);
struct vr_xdp_buf *afxdp_xdp_buf_alloc(void *pool);
struct vr_packet *vr_afxdp_get_packet(struct vr_afxdp_xsk_socket_info *xsk,
                                      const struct xdp_desc *desc,
                                      struct vr_interface *vif,
                                      char *data,
                                      __u32 queue_id);

int xsk_configure(struct vr_afxdp_ethdev *ethdev);
void xsk_destroy_all(struct vr_afxdp_ethdev *ethdev);

int vr_afxdp_table_mem_init(unsigned int table,
                            unsigned int entries,
                            unsigned long size,
                            unsigned int oentries,
                            unsigned long osize);
int vr_afxdp_bridge_init(void);
int vr_afxdp_flow_init(void);

struct vr_afxdp_global {
    void *packet_event_sock;
    // netlink event socket
    void *netlink_event_sock;
    // netlink socket handler
    void *netlink_sock;
    /* Interface configuration mutex
     * ATM we use it just to synchronize access between the NetLink interface
     * and kernel KNI events. The datapath is not affected. */
    pthread_mutex_t if_lock;
    /* Pointer to IP fragmentation memory pool (direct) */

    void *flow_table;
    void *bridge_table;

    struct vr_afxdp_ethdev ethdevs[VR_MAX_INTERFACES];
};

extern struct vr_afxdp_global vr_afxdp;
extern struct bpool *bpool;

extern int no_huge_set;

/* Check if the stop flag is set */
bool vr_afxdp_is_stop_flag_set(void);

struct nlmsghdr *afxdp_nl_message_hdr(struct vr_message *);
unsigned int afxdp_nl_message_len(struct vr_message *);

void vr_afxdp_buf_reset(struct vr_packet *pkt);

struct vr_afxdp_rcu_cb_data {
    struct rcu_head rcd_rcu;
    vr_defer_cb rcd_user_cb;
    struct vrouter *rcd_router;
    unsigned char rcd_user_data[0];
};

#endif // _VR_AFXDP_H_
