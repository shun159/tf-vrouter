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

#include <unistd.h>
#include <xdp/xsk.h>

#include "vr_afxdp.h"

#ifndef __VR_AFXDP_UMEM_GLOBAL_H__
#define __VR_AFXDP_UMEM_GLOBAL_H__

/* Define necessary parameters. These may be tuned as needed. */
#define FRAME_SIZE 4096
#define FRAME_SHIFT 12
#define MAX_FRAMES 4096

#define NUM_FRAMES XSK_UMEM__DEFAULT_FRAME_SIZE
#define PROD_NUM_DESCS XSK_RING_PROD__DEFAULT_NUM_DESCS
#define CONS_NUM_DESCS XSK_RING_CONS__DEFAULT_NUM_DESCS

extern struct xsk_umem_info *global_umem;

struct xsk_umem_info {
  struct xsk_ring_prod fq;
  struct xsk_ring_cons cq;
  struct xsk_umem *umem;
  void *buffer;

  // pool members
  uint32_t n_frames;        // nb frames
  uint32_t top;             // top of the free stack
  void *frames[MAX_FRAMES]; // array of head address of freme

  struct vr_xpacket meta_bufs[NUM_FRAMES]; // metadata
};

__u32 afxdp_desc_to_index(uint64_t addr);
struct vr_xpacket *afxdp_global_pool_alloc_frame(void);
void afxdp_global_pool_free_frame(struct vr_xpacket *);

int afxdp_global_umem_init(void);

#endif // __VR_AFXDP_UMEM_GLOBAL_H__
