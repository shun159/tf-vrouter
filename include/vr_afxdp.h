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

#include <xdp/libxdp.h>
#include <xdp/xsk.h>
#include <urcu-qsbr.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define VR_AFXDP_SLEEP_SERVICE_US 100
#define VR_AFXDP_MAX_FRAGMENT_ELEMENTS 1024

/*
 * vr_xdp_buf <=> vr_packet conversion
 *
 * In the AF_XDP design, the packet buffer layout in UMEM is arranged as follows:
 *
 *      [ struct vr_xdp_buf ] + [ struct vr_packet ] + headroom + data + tailroom
 *
 * Here, 'struct vr_xdp_buf' serves a role similar to afxdp's 'struct rte_mbuf',
 * holding metadata about the allocated UMEM frame.
 *
 * Typical fields include:
 *   - umem:      Pointer to the UMEM context from which this buffer is allocated.
 *   - addr:      Offset (in bytes) within the UMEM where this frame begins.
 *   - buf_len:   Total length of the allocated frame (constant: FRAME_SIZE).
 *   - data_len:  Length of the received packet data (set by XDP, from xdp_desc).
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

struct vr_afxdp_umem_info {
  struct xsk_umem *umem;
  struct xsk_ring_prod fq0;
  struct xsk_ring_cons cq0;
  __u32 nbfqs;
  __u32 size;
  void *buffer;
  __u8 flags;
};

// Ethdev configuration
struct vr_afxdp_ethdev {
  __u16 os_ifidx;
  __u16 queue_id;

  struct xsk_umem_config umem_cfg;
  struct xsk_socket *socket;
  struct xsk_ring_prod tx;
  struct xsk_ring_cons rx;
  struct xsk_ring_prod *fill_ring;
  struct xsk_ring_cons *comp_ring;
  struct xsk_umem *umem;
  struct vr_afxdp_umem_info *umem_info;

  __u32 libbpf_flags;
  __u32 xdp_flags;
  __u16 bind_flags;
  __u32 batch_size;
  __u8 busy_poll;
  __u16 tx_pkt_size;
  __u32 outstanding_tx;
  __u8 vif_idx;
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

struct vr_xpacket *afxdp_xpacket_from_pkt(struct vr_packet *pkt);
struct vr_xdp_buf *vr_afxdp_pkt_to_xdp_buf(struct vr_packet *pkt);
struct vr_packet *vr_afxdp_xdp_buf_to_pkt(struct vr_xdp_buf *xdp_buf);
struct vr_xdp_buf *afxdp_xdp_buf_copy(struct vr_xdp_buf *src, void *pool);
void afxdp_xdp_buf_free(struct vr_xdp_buf *xdp_buf);
struct vr_xdp_buf *afxdp_xdp_buf_alloc(void *pool);

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
};

extern struct vr_afxdp_global vr_afxdp;

/* Check if the stop flag is set */
bool vr_afxdp_is_stop_flag_set(void);

struct nlmsghdr *afxdp_nl_message_hdr(struct vr_message *);
unsigned int afxdp_nl_message_len(struct vr_message *);

void vr_afxdp_buf_reset(struct vr_packet *pkt);

#endif // _VR_AFXDP_H_
