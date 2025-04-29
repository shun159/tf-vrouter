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

#include "vr_afxdp.h"

int afxdp_recv(struct vr_interface *vif, __u32 queue_id);
int afxdp_pkt_recycle(struct vr_packet *pkt);
int afxdp_tx_burst(struct vr_afxdp_xsk_socket_info *xi,
                   struct vr_afxdp_tx_cache *c);
void afxdp_tx_complete(struct vr_afxdp_xsk_socket_info *xi);
