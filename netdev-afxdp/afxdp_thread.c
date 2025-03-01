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

#include <stdio.h>
#include <pthread.h>

#include "vr_afxdp.h"
#include "afxdp_thread.h"
#include "afxdp_netlink.h"
#include "afxdp_usocket.h"

static struct afxdp_thread g_threads[VR_AFXDP_THREAD_MAX + NUM_FWD_THREADS];

void *
afxdp_netlink_thread_func(void *arg)
{
  struct afxdp_thread *thread_info = (struct afxdp_thread *)arg;

  while (!thread_info->is_thr_stop) {
    if (vr_afxdp_netlink_init() == 0)
      vr_usocket_io(vr_afxdp.netlink_sock);

    if (thread_info->is_thr_stop)
      break;

    usleep(VR_AFXDP_SLEEP_SERVICE_US);
  }

  fprintf(stderr, "Netlink thread: exiting.\n");
  return NULL;
}

int
afxdp_spawn_threads(void)
{
  int ret;

  /* Netlink thread */
  g_threads[0].thr_type = VR_AFXDP_THREAD_NETLINK;
  ret = pthread_create(&g_threads[0].thr_id, NULL, afxdp_netlink_thread_func, &g_threads[0]);

  return ret;
}

void
afxdp_stop_threads(void)
{
  for (int i = 0; i < (6 + NUM_FWD_THREADS); i++) {
    g_threads[i].is_thr_stop = true;
  }

  // for (int i = 0; i < (6 + NUM_FWD_THREADS); i++) {
  //   pthread_join(g_threads[i].thr_id, NULL);
  //}

  return;
}
