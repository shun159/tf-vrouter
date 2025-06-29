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
  __u32 tag;
  struct afxdp_thread *ctrl;
  struct afxdp_rx_arg *next;
};

// forwarding thread impl

#define NUM_FWD_THREADS 4
#define MAX_Q_PER_FWD_CAP 32
#define MIN_Q_PER_FWD_CAP 4
#define IOURING_DEPTH 512
#define TX_FLUSH_US 50

struct forwarding_ctx {
  struct io_uring ring;
  pthread_mutex_t lock;
  pthread_t tid;
  struct vr_afxdp_xsk_socket_info *xsks[MAX_Q_PER_FWD_CAP];
  struct vr_afxdp_tx_cache txc[MAX_Q_PER_FWD_CAP];
  struct afxdp_rx_arg *pending;
  __s32 affinity;
  __s32 eventfd;
  __u32 n_active;
  volatile bool stop;
};

uint32_t detect_total_rxq(void);
void spawn_forward_loops();
void *forwarder_loop(void *arg);

void add_vif_to_forwarder(struct vr_afxdp_xsk_socket_info *xi,
                          uint32_t qid,
                          int fd,
                          bool add);

__u32 choose_forwarder(void);
__u64 now_usec(void);

typedef void *(*afxdp_thread_func_t)(void *);

int afxdp_spawn_threads(void);
void afxdp_stop_threads(void);
void afxdp_register_ethdev(struct vr_afxdp_ethdev *ethdev);

void *afxdp_rx_register(void *arg);
struct afxdp_thread *spawn_dynamic_thread(afxdp_thread_func_t func,
                                          void *arg,
                                          afxdp_thread_type_t type);

#endif
