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

#ifndef __VR_AFXDP_USOCKET_H__
#define __VR_AFXDP_USOCKET_H__

#include "vr_queue.h"
#include "nl_util.h"
#include <pthread.h>

struct vr_usocket {
  unsigned short usock_type;
  unsigned short usock_proto;
  short usock_poll_block;
  unsigned short usock_io_in_progress;
  unsigned short usock_should_close;

  int usock_fd;
  unsigned int usock_state;

  int usock_error;
  int usock_errno;

  int usock_cfds;
  int usock_child_index;

  unsigned int usock_max_cfds;
  unsigned int usock_disconnects;

  struct vr_usocket *usock_parent;
  struct vr_usocket **usock_children;

  unsigned int usock_read_offset;
  unsigned int usock_read_len;

  unsigned int usock_buf_len;
  unsigned int usock_pkt_truncated;

  char *usock_rx_buf;

  struct rte_mbuf *usock_mbuf;
  struct rte_mempool *usock_mbuf_pool;

  unsigned int usock_write_offset;
  unsigned int usock_write_len;
  unsigned char *usock_tx_buf;
  struct vr_qhead usock_nl_responses;

  struct iovec *usock_iovec;

  struct vr_interface *usock_vif;
  struct pollfd *usock_pfds;
  pthread_t usock_owner;
};

/* protocol type */
#define NETLINK 1
#define PACKET 2
#define EVENT 3

/* socket type */
#define TCP 1
#define UNIX 2
#define RAW 3

/* usocket state */
#define ALLOCED 0
#define INITED 1
#define LISTENING 2
#define READING_HEADER 3
#define READING_DATA 4
#define READING_FAULTY_DATA 5
#define LIMITED 6

#define USOCK_MAX_CHILD_FDS 64
#define USOCK_RX_BUF_LEN 4096
#define USOCK_EVENT_BUF_LEN sizeof(uint64_t)

#define PKT0_MBUF_POOL_SIZE 8192
#define PKT0_MBUF_POOL_CACHE_SZ (VR_DPDK_RX_BURST_SZ * 8)
#define PKT0_MBUF_PACKET_SIZE 2048
#define PKT0_MAX_IOV_LEN 64
#define PKT0_MBUF_RING_SIZE 65536

#define VR_DEF_SOCKET_DIR_MODE (S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define VR_NETLINK_UNIX_NAME "dpdk_netlink"
#define VR_PACKET_UNIX_NAME "dpdk_pkt0"
#define VR_PACKET_AGENT_UNIX_NAME "agent_pkt0"

int vr_usocket_eventfd_write(struct vr_usocket *usockp);
void vr_usocket_close(void *sock);
void *vr_usocket(int, int);
int vr_usocket_bind_usockets(void *usock1, void *usock2);
int vr_usocket_io(void *transport);

#endif
