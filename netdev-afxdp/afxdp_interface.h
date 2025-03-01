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

// initializers for AF_XDP
int afxdp_init(void);
void afxdp_exit(void);

__s32 get_mtu_by_ifindex(__u32 ifindex);

#endif /* __VR_AFXDP_H__ */
