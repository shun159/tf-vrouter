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
#include <sys/eventfd.h>
#include <sys/sysinfo.h>

#include "vr_afxdp.h"
#include "afxdp_thread.h"
#include "afxdp_netlink.h"
#include "afxdp_usocket.h"
#include "afxdp_ethdev.h"

#define NUM_STATIC_THREADS 1
static struct afxdp_thread g_static_threads[NUM_STATIC_THREADS];

#define NUM_DYNAMIC_THREADS_MAX (VR_AFXDP_THREAD_MAX + NUM_FWD_THREADS)
static struct afxdp_thread *g_dyn_threads[NUM_DYNAMIC_THREADS_MAX];
static int g_dyn_cnt;

#define MAX_ETHDEVS 32
static struct vr_afxdp_ethdev *g_ethdevs[MAX_ETHDEVS];
static int g_ethdev_cnt;

static void stop_static_threads(void);
static void stop_dynamic_threads(void);
static void destroy_all_ethdevs(void);

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

void *
afxdp_rx_thread_func(void *arg)
{
  struct afxdp_rx_arg *rx = arg;
  set_affinity_to_core(rx->vif->vif_idx % get_nprocs());

  while (!stop_req && !rx->ctrl->is_thr_stop)
    afxdp_recv(rx->vif, rx->queue_id);

  if (rx->vif && rx->vif->vif_os)
    xsk_destroy_all(rx->vif->vif_os);

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

struct afxdp_thread *
spawn_dynamic_thread(afxdp_thread_func_t fn, void *arg, afxdp_thread_type_t t)
{
  if (g_dyn_cnt >= NUM_DYNAMIC_THREADS_MAX)
    return NULL;
  struct afxdp_thread *th = calloc(1, sizeof(*th));
  th->thr_type = t;
  th->is_thr_stop = false;
  pthread_create(&th->thr_id, NULL, fn, arg ? arg : th);
  g_dyn_threads[g_dyn_cnt++] = th;
  return th;
}

static void
stop_dynamic_threads(void)
{
  for (int i = 0; i < g_dyn_cnt; i++)
    g_dyn_threads[i]->is_thr_stop = true;
  for (int i = 0; i < g_dyn_cnt; i++)
    pthread_join(g_dyn_threads[i]->thr_id, NULL);
  for (int i = 0; i < g_dyn_cnt; i++)
    free(g_dyn_threads[i]);
  g_dyn_cnt = 0;
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
