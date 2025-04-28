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
#include <linux/types.h>

#include "afxdp_ethdev.h"

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

struct afxdp_rx_arg {
  struct vr_interface *vif;
  __u32 queue_id;
  struct afxdp_thread *ctrl;
};

#define NUM_FWD_THREADS 2

typedef void *(*afxdp_thread_func_t)(void *);

int afxdp_spawn_threads(void);
void afxdp_stop_threads(void);
void afxdp_register_ethdev(struct vr_afxdp_ethdev *ethdev);

void *afxdp_rx_thread_func(void *arg);
struct afxdp_thread *spawn_dynamic_thread(afxdp_thread_func_t func,
                                          void *arg,
                                          afxdp_thread_type_t type);

#endif
