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

#include "afxdp_interface.h"

#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <xdp/xsk.h>

int afxdp_init(void)
{
    printf("AF_XDP: afxdp_init() called\n");
    return 0;
}

void afxdp_exit(void)
{
    printf("AF_XDP: afxdp_exit() called\n");
    return;
}

int vr_afxdp_if_init(struct vr_interface* vif)
{
    printf("AF_XDP: vr_afxdp_if_init() called for vif index %d\n", vif->vif_idx);
    return 0;
}

int afxdp_if_tx(struct vr_interface* vif, struct vr_packet* pkt)
{
    printf("AF_XDP: afxdp_if_tx() called on vif index %d, packet size %d\n", vif->vif_idx, pkt->vp_len);
    return 0;
}

int afxdp_if_rx(struct vr_interface* vif, unsigned short queue_id)
{
    printf("AF_XDP: afxdp_if_rx() called on vif index %d, queue id %u\n", vif->vif_idx, queue_id);
    return 0;
}
