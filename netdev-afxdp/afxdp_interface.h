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

#ifndef __VR_AFXDP_H__
#define __VR_AFXDP_H__

#include "vr_interface.h"
#include "vr_packet.h"
#include <linux/types.h>

// initializers for AF_XDP
int afxdp_init(void);
void afxdp_exit(void);

__s32 get_mtu_by_ifindex(__u32 ifindex);
__s32 get_nb_rxq_by_ifindex(__u32 ifindex);

struct afxdp_meta {
  __u64 umem_addr;
  __u64 len;
  struct vr_afxdp_xsk_socket_info *xsk;
} __attribute__((packed));

#define AFXDP_META(pkt)                                                        \
  ((struct afxdp_meta *)((char *)(pkt) - sizeof(struct afxdp_meta)))

#endif /* __VR_AFXDP_H__ */
