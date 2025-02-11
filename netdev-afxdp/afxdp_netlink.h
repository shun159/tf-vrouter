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

#ifndef __VR_AFXDP_NETLINK_H__

int vr_afxdp_netlink_init(void);
int vr_afxdp_netlink_loop(void);
int afxdp_netlink_receive(void *, char *, unsigned int);

#endif //  __VR_AFXDP_NETLINK_H__
