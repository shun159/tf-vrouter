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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>

#include <linux/if_link.h>
#include <linux/if_xdp.h>

#include <bpf/libbpf.h>
#include <xdp/xsk.h>

#include "afxdp_interface.h"
#include "afxdp_global_umem.h"
#include "vr_afxdp.h"

extern void vhost_remove_xconnect(void);

__s32
get_mtu_by_ifindex(__u32 ifindex)
{
  char ifname[IFNAMSIZ];
  struct ifreq ifr;
  int sock;
  int mtu;

  if (!if_indextoname(ifindex, ifname)) {
    fprintf(stderr, "if_indextoname failed: %s\n", strerror(errno));
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    fprintf(stderr, "socket failed: %s\n", strerror(errno));
    return -1;
  }

  if (ioctl(sock, SIOCGIFMTU, &ifr) < 0) {
    fprintf(stderr, "ioctl(SIOCGIFMTU) failed: %s\n", strerror(errno));
    close(sock);
    return -1;
  }

  close(sock);

  mtu = ifr.ifr_mtu;
  return mtu;
}

void
vhost_remove_xconnect(void)
{
  return;
}

void
vr_host_interface_exit(void)
{
  return;
}

// afxdp_init initializes AF_XDP resources
//
// this function performs the following steps:
//   1. allocate a umem buffer of size NUM_FRAMES x FRAME_SIZE
//   2. create a umem obj via xsk_umem__create().
int
afxdp_init(void)
{
  struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
  global_umem = NULL;

  // Allow unlimited locking of memory, so all memory needed for packet
  // buffers can be locked.
  if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
    fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK)  \"%s\"\n", strerror(errno));
    return errno;
  }

  if (afxdp_global_umem_init()) {
    fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
    return errno;
  }

  return 0;
}

void
afxdp_exit(void)
{
  printf("AF_XDP: afxdp_exit() called\n");
  return;
}

int
vr_afxdp_if_init(struct vr_interface *vif)
{
  printf("AF_XDP: vr_afxdp_if_init() called for vif index %d\n", vif->vif_idx);
  return 0;
}

static int
afxdp_if_tx(struct vr_interface *vif, struct vr_packet *pkt)
{
  printf("AF_XDP: afxdp_if_tx() called on vif index %d, packet size %d\n",
         vif->vif_idx,
         pkt->vp_len);
  return 0;
}

static int
afxdp_if_rx(struct vr_interface *vif, struct vr_packet *pkt)
{
  printf("AF_XDP: afxdp_if_rx() called on vif index %d\n", vif->vif_idx);
  return 0;
}

static void
afxdp_if_unlock(void)
{
  pthread_mutex_lock(&vr_afxdp.if_lock);
}

static void
afxdp_if_lock(void)
{
  pthread_mutex_unlock(&vr_afxdp.if_lock);
}

static int
afxdp_if_add(struct vr_interface *vif)
{
  fprintf(stdout, "%s: %s\n", __func__, vif->vif_name);
  return 0;
}

static int
afxdp_if_del(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_add_tap(struct vr_interface *vif, vr_interface_req *vifr)
{
  return 0;
}

static int
afxdp_if_del_tap(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_add_tun_tap(struct vr_interface *vif, vr_interface_req *vifr)
{
  fprintf(stdout, "%s: %s\n", __func__, vifr->vifr_name);
  return 0;
}

static int
afxdp_if_del_tun_tap(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_if_get_settings(struct vr_interface *vif, struct vr_interface_settings *settings)
{
  return 0;
}

static unsigned int
afxdp_if_get_mtu(struct vr_interface *vif)
{
  return 0;
}

static void
afxdp_if_stats_update(struct vr_interface *vif, unsigned core)
{
}

static unsigned short
afxdp_if_get_encap(struct vr_interface *vif)
{
  return VIF_ENCAP_TYPE_ETHER;
}

static int
afxdp_if_get_bond_info(struct vr_interface *vif, struct vr_interface_bond_info *bond_info)
{
  return 0;
}

static int
afxdp_if_get_vlan_info(struct vr_interface *vif, struct vr_interface_vlan_info *vlan_info)
{
  return 0;
}

static int
afxdp_if_clear_stats(struct vr_interface *vif)
{
  return 0;
}

static int
afxdp_get_host_ip_mask(struct vr_interface *vif, unsigned int *ip, unsigned int *mask)
{
  return 0;
}

static int
afxdp_get_host_mac_addr(struct vr_interface *vif, unsigned char **mac)
{
  return 0;
}

struct vr_host_interface_ops afxdp_interface_ops = {
    .hif_lock = afxdp_if_lock,
    .hif_unlock = afxdp_if_unlock,
    .hif_add = afxdp_if_add,
    .hif_del = afxdp_if_del,
    .hif_add_tap = afxdp_if_add_tap, /* not implemented */
    .hif_del_tap = afxdp_if_del_tap, /* not implemented */
    .hif_add_tun_tap = afxdp_if_add_tun_tap,
    .hif_del_tun_tap = afxdp_if_del_tun_tap,
    .hif_tx = afxdp_if_tx,
    .hif_rx = afxdp_if_rx,
    .hif_get_settings = afxdp_if_get_settings,
    .hif_get_mtu = afxdp_if_get_mtu,
    .hif_get_encap = afxdp_if_get_encap, /* always returns VIF_ENCAP_TYPE_ETHER */
    .hif_stats_update = afxdp_if_stats_update,
    .hif_get_bond_info = afxdp_if_get_bond_info,
    .hif_get_vlan_info = afxdp_if_get_vlan_info,
    .hif_clear_stats = afxdp_if_clear_stats,
    .hif_get_host_ip_mask = afxdp_get_host_ip_mask,
    .hif_get_host_mac_addr = afxdp_get_host_mac_addr,
    .hif_rx_pass = NULL,
};

void
vr_host_vif_init(struct vrouter *router)
{
  return;
}

struct vr_host_interface_ops *
vr_host_interface_init(void)
{
  return &afxdp_interface_ops;
}
