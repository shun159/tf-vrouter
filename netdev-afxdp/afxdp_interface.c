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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <linux/if_link.h>
#include <linux/if_xdp.h>

#include <bpf/libbpf.h>
#include <xdp/xsk.h>

#include "afxdp_interface.h"
#include "vr_afxdp.h"

extern void vhost_remove_xconnect(void);

void
vhost_remove_xconnect(void)
{
  return;
}

void
vr_host_interface_exit(void)
{
  return;
}

static uint64_t prng_state = 0;
static int prng_inited = 0;

static inline uint64_t
xorshift64(void)
{
  uint64_t x = prng_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  prng_state = x;
  return x * 2685821657736338717ULL;
}

static void
seed_random(void)
{
  if (prng_inited) {
    return;
  }
  prng_inited = 1;

  /*
   * Below is a trivial seeding example from current time.
   * If you want better unpredictability, read from /dev/urandom here (once).
   */
  prng_state = (uint64_t)time(NULL);
  (void)xorshift64();
}

void
get_random_bytes(void *buf, int nbytes)
{
  int offset = 0;

  if (!prng_inited) {
    seed_random();
  }

  while (offset < nbytes) {
    uint64_t rnd = xorshift64();

    int chunk = nbytes - offset;
    if (chunk >= 8) {
      memcpy((uint8_t *)buf + offset, &rnd, 8);
      offset += 8;
    } else {
      memcpy((uint8_t *)buf + offset, &rnd, chunk);
      offset += chunk;
    }
  }
}

/* Define necessary parameters. These may be tuned as needed. */
#define FRAME_SIZE 4096
#define NUM_FRAMES XSK_UMEM__DEFAULT_FRAME_SIZE
#define PROD_NUM_DESCS XSK_RING_PROD__DEFAULT_NUM_DESCS
#define CONS_NUM_DESCS XSK_RING_CONS__DEFAULT_NUM_DESCS
#define XDP_HEADROOM 64

struct xsk_umem_info {
  struct xsk_ring_prod fq;
  struct xsk_ring_cons cq;
  struct xsk_umem *umem;
  void *buffer;
};

struct xsk_umem_info *global_umem = NULL;

static int
configure_xsk_umem(void *buffer, uint64_t size)
{
  int ret;

  struct xsk_umem_config uconfig;

  memset(&uconfig, 0, sizeof uconfig);
  uconfig.fill_size = PROD_NUM_DESCS;
  uconfig.comp_size = CONS_NUM_DESCS;
  uconfig.frame_size = FRAME_SIZE;
  uconfig.frame_headroom = XDP_HEADROOM;

  global_umem = calloc(1, sizeof(*global_umem));
  if (!global_umem)
    return -1;

  ret = xsk_umem__create(&global_umem->umem,
                         buffer,
                         size,
                         &global_umem->fq,
                         &global_umem->cq,
                         &uconfig);
  if (ret)
    return errno;

  global_umem->buffer = buffer;
  return 0;
}

// afxdp_init initializes AF_XDP resources
//
// this function performs the following steps:
//   1. allocate a umem buffer of size NUM_FRAMES x FRAME_SIZE
//   2. create a umem obj via xsk_umem__create().
int
afxdp_init(void)
{
  struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
  size_t umem_size = NUM_FRAMES * FRAME_SIZE;
  void *umem_buf;

  // Allow unlimited locking of memory, so all memory needed for packet
  // buffers can be locked.
  if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
    fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK)  \"%s\"\n", strerror(errno));
    return errno;
  }

  // allocate memory for NUM_FRAMES of the default XDP frame size
  if (posix_memalign(&umem_buf, sysconf(_SC_PAGESIZE), umem_size)) {
    fprintf(stderr, "ERROR: Can't allocate buffer memory  \"%s\"\n", strerror(errno));
    return errno;
  }
  memset(umem_buf, 0, NUM_FRAMES * FRAME_SIZE);

  if (configure_xsk_umem(umem_buf, umem_size)) {
    fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
    return errno;
  }

  return 0;
}

void
afxdp_exit(void)
{
  printf("AF_XDP: afxdp_exit() called\n");
  return;
}

int
vr_afxdp_if_init(struct vr_interface *vif)
{
  printf("AF_XDP: vr_afxdp_if_init() called for vif index %d\n", vif->vif_idx);
  return 0;
}

static int
afxdp_if_tx(struct vr_interface *vif, struct vr_packet *pkt)
{
  printf("AF_XDP: afxdp_if_tx() called on vif index %d, packet size %d\n",
         vif->vif_idx,
         pkt->vp_len);
  return 0;
}

static int
afxdp_if_rx(struct vr_interface *vif, struct vr_packet *pkt)
{
  printf("AF_XDP: afxdp_if_rx() called on vif index %d\n", vif->vif_idx);
  return 0;
}

static void
afxdp_if_unlock(void)
{
  pthread_mutex_lock(&vr_afxdp.if_lock);
}

static void
afxdp_if_lock(void)
{
  pthread_mutex_unlock(&vr_afxdp.if_lock);
}

static int
afxdp_if_add(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_del(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_add_tap(struct vr_interface *vif, vr_interface_req *vifr)
{
  return 0;
}

static int
afxdp_if_del_tap(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_add_tun_tap(struct vr_interface *vif, vr_interface_req *vifr)
{
  return 0;
}

static int
afxdp_if_del_tun_tap(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_get_settings(struct vr_interface *vif, struct vr_interface_settings *settings)
{
  return 0;
}

static unsigned int
afxdp_if_get_mtu(struct vr_interface *vif)
{
  return 0;
}

static void
afxdp_if_stats_update(struct vr_interface *vif, unsigned core)
{
}

static unsigned short
afxdp_if_get_encap(struct vr_interface *vif)
{
  return VIF_ENCAP_TYPE_ETHER;
}

static int
afxdp_if_get_bond_info(struct vr_interface *vif, struct vr_interface_bond_info *bond_info)
{
  return 0;
}

static int
afxdp_if_get_vlan_info(struct vr_interface *vif, struct vr_interface_vlan_info *vlan_info)
{
  return 0;
}

static int
afxdp_if_clear_stats(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_get_host_ip_mask(struct vr_interface *vif, unsigned int *ip, unsigned int *mask)
{
  return 0;
}

static int
afxdp_get_host_mac_addr(struct vr_interface *vif, unsigned char **mac)
{
  return 0;
}

struct vr_host_interface_ops afxdp_interface_ops = {
    .hif_lock = afxdp_if_lock,
    .hif_unlock = afxdp_if_unlock,
    .hif_add = afxdp_if_add,
    .hif_del = afxdp_if_del,
    .hif_add_tap = afxdp_if_add_tap, /* not implemented */
    .hif_del_tap = afxdp_if_del_tap, /* not implemented */
    .hif_add_tun_tap = afxdp_if_add_tun_tap,
    .hif_del_tun_tap = afxdp_if_del_tun_tap,
    .hif_tx = afxdp_if_tx,
    .hif_rx = afxdp_if_rx,
    .hif_get_settings = afxdp_if_get_settings,
    .hif_get_mtu = afxdp_if_get_mtu,
    .hif_get_encap = afxdp_if_get_encap, /* always returns VIF_ENCAP_TYPE_ETHER */
    .hif_stats_update = afxdp_if_stats_update,
    .hif_get_bond_info = afxdp_if_get_bond_info,
    .hif_get_vlan_info = afxdp_if_get_vlan_info,
    .hif_clear_stats = afxdp_if_clear_stats,
    .hif_get_host_ip_mask = afxdp_get_host_ip_mask,
    .hif_get_host_mac_addr = afxdp_get_host_mac_addr,
    .hif_rx_pass = NULL,
};

void
vr_host_vif_init(struct vrouter *router)
{
  return;
}

struct vr_host_interface_ops *
vr_host_interface_init(void)
{
  return &afxdp_interface_ops;
}
