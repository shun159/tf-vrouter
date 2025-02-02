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

#include <linux/if_link.h>
#include <linux/if_xdp.h>

#include <bpf/libbpf.h>
#include <xdp/xsk.h>

#include "afxdp_interface.h"

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

static int configure_xsk_umem(void *buffer, uint64_t size) {
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

  ret = xsk_umem__create(&global_umem->umem, buffer, size, &global_umem->fq,
                         &global_umem->cq, &uconfig);
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
int afxdp_init(void) {
  struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
  size_t umem_size = NUM_FRAMES * FRAME_SIZE;
  void *umem_buf;

  // Allow unlimited locking of memory, so all memory needed for packet
  // buffers can be locked.
  if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
    fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK)  \"%s\"\n",
            strerror(errno));
    return errno;
  }

  // allocate memory for NUM_FRAMES of the default XDP frame size
  if (posix_memalign(&umem_buf, sysconf(_SC_PAGESIZE), umem_size)) {
    fprintf(stderr, "ERROR: Can't allocate buffer memory  \"%s\"\n",
            strerror(errno));
    return errno;
  }
  memset(umem_buf, 0, NUM_FRAMES * FRAME_SIZE);

  if (configure_xsk_umem(umem_buf, umem_size)) {
    fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
    return errno;
  }

  return 0;
}

void afxdp_exit(void) {
  printf("AF_XDP: afxdp_exit() called\n");
  return;
}

int vr_afxdp_if_init(struct vr_interface *vif) {
  printf("AF_XDP: vr_afxdp_if_init() called for vif index %d\n", vif->vif_idx);
  return 0;
}

int afxdp_if_tx(struct vr_interface *vif, struct vr_packet *pkt) {
  printf("AF_XDP: afxdp_if_tx() called on vif index %d, packet size %d\n",
         vif->vif_idx, pkt->vp_len);
  return 0;
}

int afxdp_if_rx(struct vr_interface *vif, unsigned short queue_id) {
  printf("AF_XDP: afxdp_if_rx() called on vif index %d, queue id %u\n",
         vif->vif_idx, queue_id);
  return 0;
}
