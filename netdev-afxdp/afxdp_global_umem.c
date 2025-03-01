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
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

#include <linux/if_link.h>
#include <linux/if_xdp.h>

#include <bpf/libbpf.h>
#include <xdp/xsk.h>

#include "afxdp_global_umem.h"
#include "vr_afxdp.h"

static int
configure_xsk_umem(void *buffer, __u64 size)
{
  int ret;

  struct xsk_umem_config uconfig;

  memset(&uconfig, 0, sizeof uconfig);
  uconfig.fill_size = PROD_NUM_DESCS * 2;
  uconfig.comp_size = CONS_NUM_DESCS;
  uconfig.frame_size = FRAME_SIZE;
  uconfig.frame_headroom = AFXDP_PKT_HEADROOM;

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

  __u32 n_frames = size / FRAME_SIZE;
  global_umem->n_frames = n_frames;
  global_umem->top = n_frames;
  memset(&global_umem->meta_bufs, 0, sizeof(global_umem->meta_bufs));

  for (uint32_t i = 0; i < n_frames; i++)
    global_umem->frames[i] = (char *)buffer + (i * FRAME_SIZE);

  return 0;
}

int
afxdp_global_umem_init(void)
{
  size_t umem_size = NUM_FRAMES * FRAME_SIZE;
  void *umem_buf;

  // reserve memory for the umem. use hugepages if unaligned chunk mode.
  umem_buf = mmap(NULL, umem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (umem_buf == MAP_FAILED) {
    fprintf(stderr, "ERROR: mmap failed: %s\n", strerror(errno));
    return errno;
  }

  if (configure_xsk_umem(umem_buf, umem_size)) {
    fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
    return errno;
  }

  return 0;
}
