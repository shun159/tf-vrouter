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

#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/sysinfo.h>

#include "vr_afxdp.h"
#include "afxdp_thread.h"
#include "afxdp_netlink.h"
#include "afxdp_usocket.h"
#include "afxdp_ethdev.h"

#define NUM_STATIC_THREADS 1
static struct afxdp_thread g_static_threads[NUM_STATIC_THREADS];

#define MAX_ETHDEVS 32
static struct vr_afxdp_ethdev *g_ethdevs[MAX_ETHDEVS];
static int g_ethdev_cnt;

static void stop_static_threads(void);
static void stop_dynamic_threads(void);
static void destroy_all_ethdevs(void);

struct forwarding_ctx io_uring_ctx[NUM_FWD_THREADS];

void
set_affinity_to_core(int core)
{
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static volatile sig_atomic_t stop_req = 0;
static void
sig_handler(int sig)
{
  (void)sig;
  stop_req = 1;

  if (vr_afxdp.netlink_sock) {
    struct vr_usocket *usockp = (struct vr_usocket *)vr_afxdp.netlink_sock;
    usockp->usock_should_close = 1;
    close(usockp->usock_fd);
  }
}

static void *
afxdp_netlink_thread_func(void *arg)
{
  struct afxdp_thread *ti = arg;

  while (!stop_req && !ti->is_thr_stop) {
    if (vr_afxdp_netlink_init() == 0)
      vr_usocket_io(vr_afxdp.netlink_sock);
    usleep(VR_AFXDP_SLEEP_SERVICE_US);
  }
  return NULL;
}

static inline struct forwarding_ctx *
select_fwd_thread_loop()
{
  struct forwarding_ctx *best = &io_uring_ctx[0];
  for (int i = 1; i < NUM_FWD_THREADS; i++)
    if (io_uring_ctx[i].n_active < best->n_active)
      best = &io_uring_ctx[i];
  return best;
}

void *
afxdp_rx_register(void *arg)
{
  struct afxdp_rx_arg *rx = arg;
  struct forwarding_ctx *ctx = select_fwd_thread_loop();

  rx->tag = ((__u64)rx->vif->vif_idx << 8) | rx->queue_id;
  pthread_mutex_lock(&ctx->lock);
  rx->next = ctx->pending;
  ctx->pending = rx;
  pthread_mutex_unlock(&ctx->lock);

  __u64 one = 1;
  write(ctx->eventfd, &one, 8);

  return NULL;
}

static inline void
uring_rearm_poll(struct io_uring *ring, int fd, uint64_t tag)
{
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_poll_add(sqe, fd, POLLIN | POLLERR | IORING_POLL_ADD_MULTI);
  sqe->user_data = tag;
}

static void *
uring_handler_func(void *arg)
{
  struct forwarding_ctx *ctx = arg;

  io_uring_queue_init(512, &ctx->ring, 0);
  ctx->eventfd = eventfd(0, EFD_NONBLOCK);

  struct io_uring_sqe *s = io_uring_get_sqe(&ctx->ring);
  s->user_data = UINT64_MAX;
  io_uring_prep_poll_add(s, ctx->eventfd, POLLIN | IORING_POLL_ADD_MULTI);
  io_uring_submit(&ctx->ring);

  while (!ctx->stop) {
    io_uring_submit_and_wait(&ctx->ring, 1);

    struct io_uring_cqe *cqe;
    unsigned head;
    io_uring_for_each_cqe(&ctx->ring, head, cqe)
    {
      if (cqe->user_data == UINT64_MAX) {
        if (!(cqe->flags & IORING_CQE_F_MORE))
          uring_rearm_poll(&ctx->ring, ctx->eventfd, cqe->user_data);

        __u64 tmp;
        read(ctx->eventfd, &tmp, sizeof(tmp));
        pthread_mutex_lock(&ctx->lock);
        struct afxdp_rx_arg *e = ctx->pending;
        ctx->pending = NULL;
        pthread_mutex_unlock(&ctx->lock);

        while (e) {
          struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
          struct vr_afxdp_ethdev *dev = e->vif->vif_os;
          afxdp_register_ethdev(dev);
          struct vr_afxdp_xsk_socket_info *xi = dev->xsks[e->queue_id];
          io_uring_prep_poll_add(sqe,
                                 xsk_socket__fd(xi->xsk),
                                 POLLIN | POLLERR | IORING_POLL_ADD_MULTI);
          ctx->n_active++;
          sqe->user_data = e->tag;

          struct afxdp_rx_arg *tmp = e;
          e = e->next;
          free(tmp);
        }

        io_uring_submit(&ctx->ring);
      } else {
        __u32 vif_id = cqe->user_data >> 8;
        __u16 qid = cqe->user_data & 0xFF;

        struct vr_interface *vif = vrouter_get_interface(0, vif_id);
        struct vr_afxdp_ethdev *eth = vif->vif_os;
        struct vr_afxdp_xsk_socket_info *xi = eth->xsks[qid];

        if (!(cqe->flags & IORING_CQE_F_MORE))
          uring_rearm_poll(&ctx->ring, xsk_socket__fd(xi->xsk), cqe->user_data);

        if (likely(vif))
          afxdp_recv(vif, qid);
      }

      io_uring_cqe_seen(&ctx->ring, cqe);
    }
  }

  io_uring_queue_exit(&ctx->ring);
  close(ctx->eventfd);
  pthread_mutex_destroy(&ctx->lock);

  return NULL;
}

void
spawn_static_threads(void)
{
  g_static_threads[0].thr_type = VR_AFXDP_THREAD_NETLINK;
  g_static_threads[0].is_thr_stop = false;
  pthread_create(&g_static_threads[0].thr_id,
                 NULL,
                 afxdp_netlink_thread_func,
                 &g_static_threads[0]);
}

void
spawn_forward_loops(void)
{
  for (__s32 i = 0; i < NUM_FWD_THREADS; i++) {
    memset(&io_uring_ctx[i], 0, sizeof(struct forwarding_ctx));
    pthread_mutex_init(&io_uring_ctx[i].lock, NULL);
    fprintf(stderr, "loop thread %d creating\n", i);
    if (pthread_create(&io_uring_ctx[i].tid,
                       NULL,
                       uring_handler_func,
                       &io_uring_ctx[i])) {
      fprintf(stderr, "loop thread %d create failed\n", i);
      return;
    }
  }
}

static void
stop_dynamic_threads(void)
{
  for (int i = 0; i < NUM_FWD_THREADS; i++)
    io_uring_ctx[i].stop = true;
  for (int i = 0; i < NUM_FWD_THREADS; i++)
    pthread_join(io_uring_ctx[i].tid, NULL);
}

static void
stop_static_threads(void)
{
  for (int i = 0; i < NUM_STATIC_THREADS; i++)
    g_static_threads[i].is_thr_stop = true;
  for (int i = 0; i < NUM_STATIC_THREADS; i++)
    pthread_kill(g_static_threads[i].thr_id, SIGUSR1);
}

void
afxdp_register_ethdev(struct vr_afxdp_ethdev *e)
{
  if (g_ethdev_cnt < MAX_ETHDEVS)
    g_ethdevs[g_ethdev_cnt++] = e;
}

static void
destroy_all_ethdevs(void)
{
  for (int i = 0; i < g_ethdev_cnt; i++)
    xsk_destroy_all(g_ethdevs[i]);
  g_ethdev_cnt = 0;
}

int
afxdp_spawn_threads(void)
{
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  spawn_static_threads();
  spawn_forward_loops();

  while (!stop_req)
    sleep(1);

  fprintf(stderr, "\nCaught signal, shutting down …\n");

  afxdp_stop_threads();

  return 0;
}

void
afxdp_stop_threads(void)
{
  stop_dynamic_threads();
  stop_static_threads();
  destroy_all_ethdevs();
}
