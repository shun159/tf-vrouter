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

struct bpool *bpool_init(struct bpool_params *params, struct xsk_umem_config *umem_cfg);
void bpool_free(struct bpool *bp);

__u32 bcache_slab_size(struct bcache *bc);
struct bcache *bcache_init(struct bpool *bp);
void bcache_free(struct bcache *bc);
__u32 bcache_cons_check(struct bcache *bc, __u32 n_buffers);
__u64 bcache_cons(struct bcache *bc);
void bcache_prod(struct bcache *bc, __u64 buffer);
