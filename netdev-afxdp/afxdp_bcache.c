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
#include <sys/resource.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <xdp/xsk.h>

#include "afxdp_bcache.h"

struct bpool *
bpool_init(struct bpool_params *params, struct xsk_umem_config *umem_cfg)
{
  __u64 n_slabs, n_slabs_reserved, n_buffers, n_buffers_reserved;
  __u64 slabs_size, slabs_reserved_size;
  __u64 buffers_size, buffers_reserved_size;
  __u64 total_size, i;
  __s32 status;
  __u8 *p;

  struct bpool *bp;
  struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};

  if (setrlimit(RLIMIT_MEMLOCK, &r))
    return NULL;

  /* bpool internals dimensioning. */
  n_slabs = (params->n_buffers + params->n_buffers_per_slab - 1) /
            params->n_buffers_per_slab;
  n_slabs_reserved = params->n_users_max * 2;
  n_buffers = n_slabs * params->n_buffers_per_slab;
  n_buffers_reserved = n_slabs_reserved * params->n_buffers_per_slab;

  slabs_size = n_slabs * sizeof(__u64 *);
  slabs_reserved_size = n_slabs_reserved * sizeof(__u64 *);
  buffers_size = n_buffers * sizeof(__u64);
  buffers_reserved_size = n_buffers_reserved * sizeof(__u64);

  total_size = sizeof(struct bpool) + slabs_size + slabs_reserved_size +
               buffers_size + buffers_reserved_size;

  /* bpool memory allocation. */
  p = calloc(total_size, sizeof(__u8));
  if (!p) {
    fprintf(stderr, "bpool_init: failed calloc\n");
    return NULL;
  }

  bp = (struct bpool *)p;
  memcpy(&bp->params, params, sizeof(*params));
  bp->params.n_buffers = n_buffers;

  bp->slabs = (__u64 **)&p[sizeof(struct bpool)];
  bp->slabs_reserved = (__u64 **)&p[sizeof(struct bpool) + slabs_size];
  bp->buffers =
      (__u64 *)&p[sizeof(struct bpool) + slabs_size + slabs_reserved_size];
  bp->buffers_reserved = (__u64 *)&p[sizeof(struct bpool) + slabs_size +
                                     slabs_reserved_size + buffers_size];

  bp->n_slabs = n_slabs;
  bp->n_slabs_reserved = n_slabs_reserved;
  bp->n_buffers = n_buffers;

  for (i = 0; i < n_slabs; i++)
    bp->slabs[i] = &bp->buffers[i * params->n_buffers_per_slab];
  bp->n_slabs_available = n_slabs;

  for (i = 0; i < n_slabs_reserved; i++)
    bp->slabs_reserved[i] =
        &bp->buffers_reserved[i * params->n_buffers_per_slab];
  bp->n_slabs_reserved_available = n_slabs_reserved;

  for (i = 0; i < n_buffers; i++)
    bp->buffers[i] = i * params->buffer_size;

  /* lock. */
  status = pthread_mutex_init(&bp->lock, NULL);
  if (status) {
    fprintf(stderr, "bpool_init: mutex init\n");
    free(p);
    return NULL;
  }

  /* mmap. */
  bp->addr = mmap(NULL,
                  n_buffers * params->buffer_size,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | params->mmap_flags,
                  -1,
                  0);
  if (bp->addr == MAP_FAILED) {
    fprintf(stderr, "bpool_init: mmap failed\n");
    pthread_mutex_destroy(&bp->lock);
    free(p);
    return NULL;
  }

  /* umem. */
  status = xsk_umem__create(&bp->umem,
                            bp->addr,
                            bp->params.n_buffers * bp->params.buffer_size,
                            &bp->umem_fq,
                            &bp->umem_cq,
                            umem_cfg);
  if (status) {
    fprintf(stderr, "bpool_init: umem create failed: %s\n", strerror(errno));
    munmap(bp->addr, bp->params.n_buffers * bp->params.buffer_size);
    pthread_mutex_destroy(&bp->lock);
    free(p);
    return NULL;
  }
  memcpy(&bp->umem_cfg, umem_cfg, sizeof(*umem_cfg));

  return bp;
}

void
bpool_free(struct bpool *bp)
{
  if (!bp)
    return;

  xsk_umem__delete(bp->umem);
  munmap(bp->addr, bp->params.n_buffers * bp->params.buffer_size);
  pthread_mutex_destroy(&bp->lock);
  free(bp);
}

__u32
bcache_slab_size(struct bcache *bc)
{
  struct bpool *bp = bc->bp;

  return bp->params.n_buffers_per_slab;
}

struct bcache *
bcache_init(struct bpool *bp)
{
  struct bcache *bc;

  bc = calloc(1, sizeof(struct bcache));
  if (!bc)
    return NULL;

  bc->bp = bp;
  bc->n_buffers_cons = 0;
  bc->n_buffers_prod = 0;

  pthread_mutex_lock(&bp->lock);
  if (bp->n_slabs_reserved_available < 2) {
    fprintf(stderr,
            "bcache_init: Not enough reserved slabs. Need 2, have %llu\n",
            bp->n_slabs_reserved_available);
    pthread_mutex_unlock(&bp->lock);
    free(bc);
    return NULL;
  }

  bc->slab_cons = bp->slabs_reserved[bp->n_slabs_reserved_available - 1];
  bc->slab_prod = bp->slabs_reserved[bp->n_slabs_reserved_available - 2];
  bp->n_slabs_reserved_available -= 2;
  pthread_mutex_unlock(&bp->lock);

  return bc;
}

void
bcache_free(struct bcache *bc)
{
  struct bpool *bp;

  if (!bc)
    return;

  /* In order to keep this example simple, the case of freeing any
   * existing buffers from the cache back to the pool is ignored.
   */

  bp = bc->bp;
  pthread_mutex_lock(&bp->lock);
  bp->slabs_reserved[bp->n_slabs_reserved_available] = bc->slab_prod;
  bp->slabs_reserved[bp->n_slabs_reserved_available + 1] = bc->slab_cons;
  bp->n_slabs_reserved_available += 2;
  pthread_mutex_unlock(&bp->lock);

  free(bc);
}

__u32
bcache_cons_check(struct bcache *bc, __u32 n_buffers)
{
  struct bpool *bp = bc->bp;
  __u64 n_buffers_per_slab = bp->params.n_buffers_per_slab;
  __u64 n_buffers_cons = bc->n_buffers_cons;
  __u64 n_slabs_available;
  __u64 *slab_full;

  /*
   * Consumer slab is not empty: Use what's available locally. Do not
   * look for more buffers from the pool when the ask can only be
   * partially satisfied.
   */
  if (n_buffers_cons)
    return (n_buffers_cons < n_buffers) ? n_buffers_cons : n_buffers;

  /*
   * Consumer slab is empty: look to trade the current consumer slab
   * (full) for a full slab from the pool, if any is available.
   */
  pthread_mutex_lock(&bp->lock);
  n_slabs_available = bp->n_slabs_available;
  if (!n_slabs_available) {
    pthread_mutex_unlock(&bp->lock);
    return 0;
  }

  n_slabs_available--;
  slab_full = bp->slabs[n_slabs_available];
  bp->slabs[n_slabs_available] = bc->slab_cons;
  bp->n_slabs_available = n_slabs_available;
  pthread_mutex_unlock(&bp->lock);

  bc->slab_cons = slab_full;
  bc->n_buffers_cons = n_buffers_per_slab;
  return n_buffers;
}

__u64
bcache_cons(struct bcache *bc)
{
  __u64 n_buffers_cons = bc->n_buffers_cons - 1;
  __u64 buffer;

  buffer = bc->slab_cons[n_buffers_cons];
  bc->n_buffers_cons = n_buffers_cons;
  return buffer;
}

void
bcache_prod(struct bcache *bc, __u64 buffer)
{
  struct bpool *bp = bc->bp;
  __u64 n_buffers_per_slab = bp->params.n_buffers_per_slab;
  __u64 n_buffers_prod = bc->n_buffers_prod;
  __u64 n_slabs_available;
  __u64 *slab_empty;

  /*
   * Producer slab is not yet full: store the current buffer to it.
   */
  if (n_buffers_prod < n_buffers_per_slab) {
    bc->slab_prod[n_buffers_prod] = buffer;
    bc->n_buffers_prod = n_buffers_prod + 1;
    return;
  }

  /*
   * Producer slab is full: trade the cache's current producer slab
   * (full) for an empty slab from the pool, then store the current
   * buffer to the new producer slab. As one full slab exists in the
   * cache, it is guaranteed that there is at least one empty slab
   * available in the pool.
   */
  pthread_mutex_lock(&bp->lock);
  n_slabs_available = bp->n_slabs_available;

  if (n_slabs_available >= bp->n_slabs) {
    pthread_mutex_unlock(&bp->lock);
    fprintf(stderr,
            "bcache_prod: No empty slabs available in the pool to swap with.");
    return;
  }

  slab_empty = bp->slabs[n_slabs_available];
  bp->slabs[n_slabs_available] = bc->slab_prod;
  bp->n_slabs_available = n_slabs_available + 1;
  pthread_mutex_unlock(&bp->lock);

  slab_empty[0] = buffer;
  bc->slab_prod = slab_empty;
  bc->n_buffers_prod = 1;
}
