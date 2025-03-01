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

#include <stdbool.h>
#include <pthread.h>

#ifndef __VR_AFXDP_NETLINK_H__
#define __VR_AFXDP_NETLINK_H__

typedef enum {
  VR_AFXDP_THREAD_NETLINK,
  VR_AFXDP_THREAD_PACKET,
  VR_AFXDP_THREAD_TAP,
  VR_AFXDP_THREAD_TIMER,
  VR_AFXDP_THREAD_IO,
  VR_AFXDP_THREAD_FWD,
  VR_AFXDP_THREAD_MAX
} afxdp_thread_type_t;

struct afxdp_thread {
  pthread_t thr_id;
  afxdp_thread_type_t thr_type;
  bool is_thr_stop;
};

#define NUM_FWD_THREADS 2

int afxdp_spawn_threads(void);
void afxdp_stop_threads(void);

#endif
