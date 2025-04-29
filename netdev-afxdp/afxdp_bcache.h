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

#include <linux/types.h>
#include <pthread.h>
#include <xdp/xsk.h>

#include "vr_afxdp.h"

struct bpool *bpool_init(struct bpool_params *params,
                         struct xsk_umem_config *umem_cfg);
void bpool_free(struct bpool *bp);

__u32 bcache_slab_size(struct bcache *bc);
struct bcache *bcache_init(struct bpool *bp);
void bcache_free(struct bcache *bc);
__u32 bcache_cons_check(struct bcache *bc, __u32 n_buffers);
__u64 bcache_cons(struct bcache *bc);
void bcache_prod(struct bcache *bc, __u64 buffer);
int bcache_pop(struct bcache *bc, void **elem);
void bcache_push(struct bcache *bc, void *elem);
