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
#include <unistd.h>
#include <pthread.h>
#include <poll.h>

#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <xdp/xsk.h>
#include <net/if.h>

#include "afxdp_ethdev.h"
#include "afxdp_bcache.h"
#include "afxdp_interface.h"
#include "vr_afxdp.h"

int
afxdp_pkt_recycle(struct vr_packet *pkt)
{
  struct vr_afxdp_xsk_socket_info *xsk;
  struct afxdp_meta *m = AFXDP_META(pkt);
  xsk = m->xsk;

  __u32 pos = 0;
  __u64 addr = m->umem_addr;

  if (!xsk)
    return -EAGAIN;

  struct xsk_ring_prod *fq = &xsk->fq;
  int idx = xsk_ring_prod__reserve(fq, 1, &pos);
  if (idx < 0)
    return -ENOSPC;

  *xsk_ring_prod__fill_addr(fq, idx) = addr;
  xsk_ring_prod__submit(fq, 1);
  return 0;
}

static int
xsk_configure_umem(struct vr_afxdp_xsk_socket_info *xsk)
{
  __u32 umem_fq_size = 0;

  xsk->bpool = bpool;
  if (!xsk->bpool) {
    fprintf(stderr, "bpool_init failed\n");
    goto err;
  }

  memcpy(&xsk->bpool_params,
         &bpool_params_default,
         sizeof(struct bpool_params));
  memcpy(&xsk->bpool->umem_cfg,
         &umem_cfg_default,
         sizeof(struct xsk_umem_config));

  umem_fq_size = xsk->bpool->umem_cfg.fill_size;
  xsk->bcache = bcache_init(xsk->bpool);
  if (!xsk->bcache || (bcache_slab_size(xsk->bcache) < umem_fq_size) ||
      (bcache_cons_check(xsk->bcache, umem_fq_size) < umem_fq_size)) {
    fprintf(stderr, "bcache_init failed");
    goto err;
  }
  return 0;

err:
  bcache_free(xsk->bcache);
  return -1;
}

static int
xsk_configure_queue(struct vr_afxdp_ethdev *ethdev, __u32 queue_id)
{
  struct vr_afxdp_xsk_socket_info *xsk_info;
  struct xsk_socket_config cfg;
  char devname[IF_NAMESIZE];
  __u32 umem_fq_size, idx = 0;
  int ret, i;

  xsk_info = calloc(1, sizeof(*xsk_info));
  if (!xsk_info) {
    fprintf(stderr, "calloc xsk_info failed\n");
    return -1;
  }

  ret = xsk_configure_umem(xsk_info);
  if (ret) {
    fprintf(stderr, "xsk_configure_umem failed (queue_id=%u)\n", queue_id);
    goto err_free_xsk;
  }

  if (if_indextoname(ethdev->os_ifidx, devname) == NULL) {
    fprintf(stderr,
            "ifindex %d to devname failed (%s)\n",
            ethdev->os_ifidx,
            strerror(errno));
    goto err_free_umem;
  }

  memcpy(&cfg, &xsk_cfg_default, sizeof(cfg));
  ret = xsk_socket__create_shared(&xsk_info->xsk,
                                  devname,
                                  queue_id,
                                  xsk_info->bpool->umem,
                                  &xsk_info->rx,
                                  &xsk_info->tx,
                                  &xsk_info->fq,
                                  &xsk_info->cq,
                                  &cfg);
  if (ret) {
    fprintf(stderr, "xsk_socket__create failed %s\n", strerror(errno));
    goto err_free_umem;
  }

  umem_fq_size = xsk_info->bpool->umem_cfg.fill_size;
  if (!xsk_ring_prod__reserve(&xsk_info->fq, umem_fq_size, &idx)) {
    fprintf(stderr, "xsk_ring_prod__reserve (queue_id=%u) failed\n", queue_id);
    goto err_delete_socket;
  }

  for (i = 0; i < umem_fq_size; i++) {
    __u64 addr = bcache_cons(xsk_info->bcache);
    *xsk_ring_prod__fill_addr(&xsk_info->fq, idx + i) = addr;
  }
  xsk_ring_prod__submit(&xsk_info->fq, umem_fq_size);

  xsk_info->available_rx = PROD_NUM_DESCS;
  xsk_info->outstanding_tx = 0;
  ethdev->xsks[queue_id] = xsk_info;

  return 0;

err_delete_socket:
  xsk_socket__delete(xsk_info->xsk);

err_free_umem:
  bpool_free(xsk_info->bpool);
  bcache_free(xsk_info->bcache);

err_free_xsk:
  free(xsk_info);
  ethdev->xsks[queue_id] = NULL;
  return -1;
}

int
xsk_configure(struct vr_afxdp_ethdev *ethdev)
{
  __u32 n_rxq;
  int ret, i;

  n_rxq = get_nb_rxq_by_ifindex(ethdev->os_ifidx);
  fprintf(stderr, "iface: %d n_rxq: %d\n", ethdev->os_ifidx, n_rxq);
  ethdev->xsks = calloc(n_rxq, sizeof(struct vr_afxdp_xsk_socket_info));

  for (i = 0; i < n_rxq; i++) {
    ret = xsk_configure_queue(ethdev, i);
    if (ret) {
      fprintf(stderr,
              "xsk_configure_queue failed: ifidx %d: queue_id:%d\n",
              ethdev->os_ifidx,
              i);
      return -1;
    }
  }

  return 0;
}

void
xsk_destroy_all(struct vr_afxdp_ethdev *ethdev)
{
  __u32 n_rxq = get_nb_rxq_by_ifindex(ethdev->os_ifidx);
  struct bpool *shared_bp = NULL;
  int i;

  if (!ethdev->xsks)
    return;

  for (i = 0; i < n_rxq; i++) {
    struct vr_afxdp_xsk_socket_info *xi = ethdev->xsks[i];
    if (!xi)
      continue;

    if (!shared_bp && xi->bpool)
      shared_bp = xi->bpool;

    xsk_socket__delete(xi->xsk);

    if (xi->bcache) {
      bcache_free(xi->bcache);
      xi->bcache = NULL;
    }

    free(xi);
  }

  free(ethdev->xsks);
  ethdev->xsks = NULL;

  if (shared_bp) {
    bpool_free(shared_bp);
  }
}

static inline void
xsk_rx_wakeup_if_needed(struct vr_afxdp_xsk_socket_info *xsk)
{
  if (xsk_ring_prod__needs_wakeup(&xsk->fq)) {
    struct pollfd pollfd = {
        .fd = xsk_socket__fd(xsk->xsk),
        .events = POLLIN,
    };
    poll(&pollfd, 1, 0);
  }
}

static inline void
prepare_fill_queue(struct vr_afxdp_xsk_socket_info *xsk, __u32 n_pkts)
{
  __u32 i;
  __u32 idx_fq;

  for (;;) {
    int status = xsk_ring_prod__reserve(&xsk->fq, n_pkts, &idx_fq);
    if (n_pkts == status)
      break;
    xsk_rx_wakeup_if_needed(xsk);
  }

  for (i = 0; i < n_pkts; i++) {
    __u64 addr = bcache_cons(xsk->bcache);
    *xsk_ring_prod__fill_addr(&xsk->fq, idx_fq + i) = addr;
  }

  xsk_ring_prod__submit(&xsk->fq, n_pkts);
  xsk->available_rx += n_pkts;
}

int
afxdp_recv(struct vr_interface *vif, __u32 queue_id)
{
  struct vr_afxdp_ethdev *ethdev;
  struct vr_afxdp_xsk_socket_info *xsk;
  struct vr_packet *pkt;

  __u32 idx_rx = 0;
  __u32 rcvd, i;
  __u16 vlan_id = VLAN_ID_INVALID;

  ethdev = vif->vif_os;
  if (!ethdev)
    return -EAGAIN;

  xsk = ethdev->xsks[queue_id];
  if (!xsk)
    return -EAGAIN;

  rcvd = bcache_cons_check(xsk->bcache, BATCH_SIZE);
  if (!rcvd)
    return -EAGAIN;

  rcvd = xsk_ring_cons__peek(&xsk->rx, rcvd, &idx_rx);
  if (!rcvd) {
    xsk_rx_wakeup_if_needed(xsk);
    return -EAGAIN;
  }

  prepare_fill_queue(xsk, rcvd);

  for (i = 0; i < rcvd; i++) {
    const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx + i);
    __u64 addr = xsk_umem__add_offset_to_addr(desc->addr);
    char *data = xsk_umem__get_data(xsk->bpool->addr, addr);
    pkt = vr_afxdp_get_packet(xsk, desc, vif, data, queue_id);
    if (!pkt) {
      fprintf(stderr, "failed to alloc pkt\n");
      continue;
    }
    vif->vif_rx(vif, pkt, vlan_id);
  }

  xsk_ring_cons__release(&xsk->rx, rcvd);
  xsk->available_rx -= rcvd;

  return rcvd;
}

// safer afxdp_tx_complete
inline void
afxdp_tx_complete(struct vr_afxdp_xsk_socket_info *xi)
{
  __u32 idx, n_pkts;

  n_pkts = umem_cfg_default.comp_size;
  n_pkts = xsk_ring_cons__peek(&xi->cq, n_pkts, &idx);

  for (__u32 i = 0; i < n_pkts; i++) {
    __u64 addr = *xsk_ring_cons__comp_addr(&xi->cq, idx + i);
    bcache_prod(xi->bcache, addr);
  }

  xsk_ring_cons__release(&xi->cq, n_pkts);
}

static inline void
afxdp_kick_tx(struct vr_afxdp_xsk_socket_info *xi)
{
  if (xsk_ring_prod__needs_wakeup(&xi->tx)) {
    sendto(xsk_socket__fd(xi->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
  }
}

static inline void
prepare_tx_queue(struct vr_afxdp_xsk_socket_info *xi,
                 struct vr_afxdp_tx_cache *c)
{
  struct xdp_desc *tx_desc;
  __u32 i, idx_tx = 0;
  __u32 pkts_to_send = c->n_pkts;

  for (;;) {
    int ret = xsk_ring_prod__reserve(&xi->tx, c->n_pkts, &idx_tx);
    if (ret == (int)c->n_pkts)
      break;
    afxdp_kick_tx(xi);
  }

  for (i = 0; i < pkts_to_send; i++) {
    tx_desc = xsk_ring_prod__tx_desc(&xi->tx, idx_tx + i);
    tx_desc->addr = c->addr[i];
    tx_desc->len = c->len[i];
  }

  xsk_ring_prod__submit(&xi->tx, pkts_to_send);

  return;
}

inline int
afxdp_tx_burst(struct vr_afxdp_xsk_socket_info *xi, struct vr_afxdp_tx_cache *c)
{
  __u32 pkts_to_send = c->n_pkts;

  if (!pkts_to_send)
    return 0;

  afxdp_tx_complete(xi);
  prepare_tx_queue(xi, c);
  afxdp_kick_tx(xi);

  xi->outstanding_tx += c->n_pkts;
  c->n_pkts = 0;

  return 0;
}
