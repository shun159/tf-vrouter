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

#ifndef _VR_AFXDP_H_
#define _VR_AFXDP_H_

#include "vr_os.h"
#include "vr_interface.h"
#include "vr_packet.h"
#include "vr_fragment.h"
#include "vr_cpuid.h"

#include <urcu-qsbr.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define VR_DPDK_SLEEP_SERVICE_US 100

struct vr_afxdp_global {
  void *packet_event_sock;
  // netlink event socket
  void *netlink_event_sock;
  // netlink socket handler
  void *netlink_sock;
  /* Interface configuration mutex
   * ATM we use it just to synchronize access between the NetLink interface
   * and kernel KNI events. The datapath is not affected. */
  pthread_mutex_t if_lock;
  /* Pointer to IP fragmentation memory pool (direct) */
};

extern struct vr_afxdp_global vr_afxdp;

/* Check if the stop flag is set */
bool vr_afxdp_is_stop_flag_set(void);

struct nlmsghdr *afxdp_nl_message_hdr(struct vr_message *);
unsigned int afxdp_nl_message_len(struct vr_message *);

#endif // _VR_AFXDP_H_
