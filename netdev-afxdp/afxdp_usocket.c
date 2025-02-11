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

#include <fcntl.h>
#include <poll.h>
#include <linux/netlink.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdint.h>
#include <netinet/tcp.h>

#include "nl_util.h"
#include "vr_message.h"
#include "vr_afxdp.h"
#include "vr_genetlink.h"
#include "afxdp_usocket.h"
#include "afxdp_netlink.h"

int
vr_usocket_eventfd_write(struct vr_usocket *usockp)
{
  if (usockp->usock_proto != EVENT)
    return -1;

  return eventfd_write(usockp->usock_fd, 1);
}

static char vr_packet_unix_file[VR_UNIX_PATH_MAX];
char *vr_socket_dir = VR_DEF_SOCKET_DIR;
uint16_t vr_netlink_port = VR_DEF_NETLINK_PORT;

#define INFINITE_TIMEOUT -1

static void
usock_set_error(struct vr_usocket *usockp, int error)
{
  usockp->usock_error = error;
  usockp->usock_errno = errno;

  return;
}

static int
usock_read_init(struct vr_usocket *usockp)
{
  usockp->usock_read_offset = 0;

  switch (usockp->usock_proto) {
  case NETLINK:
    if (usockp->usock_parent) {
      usockp->usock_read_len = NLMSG_HDRLEN;
      usockp->usock_state = READING_HEADER;
    }
    break;

  default:
    break;
  }

  return 0;
}

static int
usock_init_poll(struct vr_usocket *usockp)
{
  unsigned int i;
  unsigned int proto;

  if (!usockp)
    return -EINVAL;

  proto = usockp->usock_proto;
  if ((proto != NETLINK) && (proto != PACKET)) {
    usock_set_error(usockp, -EINVAL);
    goto error_return;
  }

  if (!usockp->usock_max_cfds) {
    usock_set_error(usockp, -EINVAL);
    goto error_return;
  }

  if (!usockp->usock_pfds) {
    usockp->usock_pfds =
        vr_zalloc(sizeof(struct pollfd) * usockp->usock_max_cfds + 1, VR_USOCK_POLL_OBJECT);
    if (!usockp->usock_pfds) {
      usock_set_error(usockp, -ENOMEM);
      goto error_return;
    }

    for (i = 1; i <= usockp->usock_max_cfds; i++) {
      usockp->usock_pfds[i].fd = -1;
    }
  }

  return 0;

error_return:
  return usockp->usock_error;
}

static int
usock_bind_usockets(struct vr_usocket *parent, struct vr_usocket *child)
{
  unsigned int i;
  int ret;
  struct vr_usocket *child_pair;

  if (parent->usock_state == LIMITED)
    return -ENOSPC;

  if (child->usock_proto == EVENT) {
    child_pair = vr_usocket(EVENT, RAW);
    if (!child_pair)
      return -ENOMEM;

    close(child->usock_fd);
    child->usock_fd = child_pair->usock_fd;
    child = child_pair;
  }

  ret = usock_init_poll(parent);
  if (ret)
    return ret;

  if (!parent->usock_children) {
    parent->usock_children =
        vr_zalloc(sizeof(struct vr_usocket *) * USOCK_MAX_CHILD_FDS + 1, VR_USOCK_OBJECT);
    if (!parent->usock_children) {
      usock_set_error(parent, -ENOMEM);
      return -ENOMEM;
    }
  }

  child->usock_parent = parent;
  parent->usock_cfds++;
  if (parent->usock_cfds >= USOCK_MAX_CHILD_FDS) {
    parent->usock_state = LIMITED;
  }

  for (i = 1; i <= parent->usock_max_cfds; i++) {
    if (!parent->usock_children[i]) {
      parent->usock_children[i] = child;
      parent->usock_pfds[i].fd = child->usock_fd;
      parent->usock_pfds[i].events = POLLIN;
      child->usock_child_index = i;
      break;
    }
  }

  if (child->usock_proto == EVENT)
    child->usock_state = READING_DATA;

  usock_read_init(child);

  return 0;
}

static void
usock_deinit_poll(struct vr_usocket *usockp)
{
  if (!usockp)
    return;

  if (usockp->usock_pfds) {
    vr_free(usockp->usock_pfds, VR_USOCK_POLL_OBJECT);
    usockp->usock_pfds = NULL;
  }

  return;
}

static void
usock_unbind(struct vr_usocket *child)
{
  struct vr_usocket *parent;

  if (!child)
    return;

  parent = child->usock_parent;
  if (!parent)
    return;

  parent->usock_children[child->usock_child_index] = NULL;
  if (parent->usock_pfds)
    parent->usock_pfds[child->usock_child_index].fd = -1;

  parent->usock_disconnects++;
  parent->usock_cfds--;
  if ((parent->usock_state == LIMITED) && (parent->usock_cfds < USOCK_MAX_CHILD_FDS))
    parent->usock_state = LISTENING;

  child->usock_parent = NULL;

  return;
}

static void
usock_close(struct vr_usocket *usockp)
{
  int i;

  if (!usockp)
    return;

  usock_unbind(usockp);
  usock_deinit_poll(usockp);

  for (i = 0; i < usockp->usock_cfds; i++) {
    usock_close(usockp->usock_children[i]);
  }

  close(usockp->usock_fd);

  if (!usockp->usock_mbuf_pool && usockp->usock_rx_buf) {
    vr_free(usockp->usock_rx_buf, VR_USOCK_BUF_OBJECT);
    usockp->usock_rx_buf = NULL;
  }

  if (usockp->usock_iovec) {
    vr_free(usockp->usock_iovec, VR_USOCK_IOVEC_OBJECT);
    usockp->usock_iovec = NULL;
  }

  if (usockp->usock_mbuf_pool) {
    /* no api to destroy a pool */
  }

  if (usockp->usock_proto == PACKET)
    unlink(vr_packet_unix_file);

  usockp->usock_io_in_progress = 0;

  vr_free(usockp, VR_USOCK_OBJECT);

  return;
}

static bool
valid_usock(int proto, int type)
{
  if ((proto != PACKET) && (proto != NETLINK) && (proto != EVENT))
    return -EINVAL;

  if (((proto == PACKET) || (proto == EVENT)) && (type != RAW)) {
    return -EINVAL;
  } else {
    if (type != TCP && type != UNIX)
      return -EINVAL;
  }

  return true;
}

static int
__usock_read(struct vr_usocket *usockp)
{
  int ret;
  unsigned int offset = usockp->usock_read_offset;
  unsigned int len = usockp->usock_read_len;
  unsigned int toread = len - offset;

  struct nlmsghdr *nlh;
  unsigned int proto = usockp->usock_proto;
  char *buf = usockp->usock_rx_buf;

  if (toread > usockp->usock_buf_len) {
    toread = usockp->usock_buf_len - offset;
  }

retry_read:
  if (usockp->usock_owner != pthread_self())
    usockp->usock_owner = pthread_self();
  ret = read(usockp->usock_fd, buf + offset, toread);
  if (ret <= 0) {
    if (!ret)
      return -1;

    if (errno == EINTR)
      goto retry_read;

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
      return 0;

    return ret;
  }

  offset += ret;
  usockp->usock_read_offset = offset;

  if (proto == NETLINK) {
    if (usockp->usock_state == READING_HEADER) {
      if (usockp->usock_read_offset == usockp->usock_read_len) {
        usockp->usock_state = READING_DATA;
        nlh = (struct nlmsghdr *)(usockp->usock_rx_buf);
        usockp->usock_read_len = nlh->nlmsg_len;
      }
    }

    if (usockp->usock_buf_len < usockp->usock_read_len) {
      usockp->usock_rx_buf = vr_malloc(usockp->usock_read_len, VR_USOCK_BUF_OBJECT);
      if (!usockp->usock_rx_buf) {
        /* bad, but let's recover */
        usockp->usock_rx_buf = buf;
        usockp->usock_read_len -= usockp->usock_read_offset;
        usockp->usock_read_offset = 0;
        usockp->usock_state = READING_FAULTY_DATA;
      } else {
        memcpy(usockp->usock_rx_buf, buf, usockp->usock_read_offset);
        vr_free(buf, VR_USOCK_BUF_OBJECT);
        usockp->usock_buf_len = usockp->usock_read_len;
        buf = usockp->usock_rx_buf;
      }
    }
  }

  return ret;
}

static int
vr_usocket_bind(struct vr_usocket *usockp)
{
  int error = 0;
  struct sockaddr_un sun;
  struct sockaddr *addr = NULL;
  socklen_t addrlen = 0;
  int optval;
  bool server;

  optval = 1;
  if (setsockopt(usockp->usock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)))
    return -errno;

  switch (usockp->usock_type) {
  case UNIX:
    sun.sun_family = AF_UNIX;
    memset(sun.sun_path, 0, sizeof(sun.sun_path));
    strncpy(sun.sun_path, vr_socket_dir, sizeof(sun.sun_path) - 1);
    strncat(sun.sun_path,
            "/" VR_NETLINK_UNIX_NAME,
            sizeof(sun.sun_path) - strlen(sun.sun_path) - 1);

    addr = (struct sockaddr *)&sun;
    addrlen = sizeof(sun);
    server = true;
    mkdir(vr_socket_dir, VR_DEF_SOCKET_DIR_MODE);
    unlink(sun.sun_path);

    break;

  case RAW:
    sun.sun_family = AF_UNIX;
    strncpy(vr_packet_unix_file, vr_socket_dir, sizeof(vr_packet_unix_file) - 1);
    strncat(vr_packet_unix_file,
            "/" VR_PACKET_UNIX_NAME,
            sizeof(vr_packet_unix_file) - strlen(vr_packet_unix_file) - 1);
    memset(sun.sun_path, 0, sizeof(sun.sun_path));
    memcpy(sun.sun_path, vr_packet_unix_file, sizeof(sun.sun_path) - 1);

    addr = (struct sockaddr *)&sun;
    addrlen = sizeof(sun);
    server = false;
    mkdir(vr_socket_dir, VR_DEF_SOCKET_DIR_MODE);
    unlink(sun.sun_path);

    break;

  default:
    return -EINVAL;
  }

#ifdef VR_DPDK_USOCK_DUMP
  RTE_LOG_DP(DEBUG, USOCK, "%s[%lx]: FD %d binding\n", __func__, pthread_self(), usockp->usock_fd);
  rte_hexdump(stdout, "usock address dump:", addr, addrlen);
#endif
  error = bind(usockp->usock_fd, addr, addrlen);
  if (error < 0)
    return error;

  if (server) {
    error = listen(usockp->usock_fd, 1);
    if (error < 0)
      return error;
    usockp->usock_state = LISTENING;
  }

  return 0;
}

static struct vr_usocket *
usock_alloc(unsigned short proto, unsigned short type)
{
  bool is_socket = true;
  unsigned int buf_len;
  int sock_fd = -1, domain, ret;
  /* socket TX buffer size = (hold flow table entries * size of jumbo frame) */
  int setsocksndbuff = vr_flow_hold_limit * 9 * 1024;
  int getsocksndbuff;
  socklen_t getsocksndbufflen = sizeof(getsocksndbuff);

  int error = 0, flags;
  struct vr_usocket *usockp = NULL;
  unsigned short sock_type;

  switch (type) {
  case UNIX:
    domain = AF_UNIX;
    sock_type = SOCK_STREAM;
    break;
  default: // raw
    domain = AF_UNIX;
    sock_type = SOCK_DGRAM;
    break;
  }

  if (is_socket) {
    sock_fd = socket(domain, sock_type, 0);
    if (sock_fd < 0)
      return NULL;

    /* set socket send buffer size */
    ret = setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &setsocksndbuff, sizeof(setsocksndbuff));
    if (ret == 0) {
      /* check if setting buffer succeeded */
      ret = getsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &getsocksndbuff, &getsocksndbufflen);
    }
  }

  usockp = vr_zalloc(sizeof(*usockp), VR_USOCK_OBJECT);
  if (!usockp)
    goto error_exit;

  usockp->usock_type = type;
  usockp->usock_proto = proto;
  usockp->usock_fd = sock_fd;
  usockp->usock_state = INITED;

  if (is_socket) {
    error = vr_usocket_bind(usockp);
    if (error < 0)
      goto error_exit;
  }

  switch (proto) {
  case NETLINK:
    usockp->usock_max_cfds = USOCK_MAX_CHILD_FDS;
    buf_len = 0;
    break;

  default:
    buf_len = 0;
    break;
  }

  if (buf_len) {
    usockp->usock_rx_buf = vr_zalloc(buf_len, VR_USOCK_BUF_OBJECT);
    if (!usockp->usock_rx_buf)
      goto error_exit;

    usockp->usock_buf_len = buf_len;
    usock_read_init(usockp);
  }

  flags = fcntl(usockp->usock_fd, F_GETFL);
  if (flags == -1)
    goto error_exit;

  error = fcntl(usockp->usock_fd, F_SETFL, flags | O_NONBLOCK);
  if (error == -1)
    goto error_exit;

  usockp->usock_poll_block = 1;

  return usockp;

error_exit:

  error = errno;
  if (sock_fd >= 0) {
    close(sock_fd);
    sock_fd = -1;
  }

  usock_close(usockp);
  usockp = NULL;
  errno = error;

  return usockp;
}

static int
usock_clone(struct vr_usocket *parent, int cfd)
{
  struct vr_usocket *child;

  child = vr_zalloc(sizeof(struct vr_usocket), VR_USOCK_OBJECT);
  if (!child) {
    usock_set_error(parent, -ENOMEM);
    goto error_return;
  }

  child->usock_rx_buf = vr_malloc(USOCK_RX_BUF_LEN, VR_USOCK_BUF_OBJECT);
  if (!child->usock_rx_buf) {
    usock_set_error(parent, -ENOMEM);
    goto error_return;
  }
  child->usock_buf_len = USOCK_RX_BUF_LEN;

  child->usock_type = parent->usock_type;
  child->usock_proto = parent->usock_proto;
  child->usock_fd = cfd;

  if (usock_bind_usockets(parent, child))
    goto error_return;

  return 0;

error_return:
  if (child) {
    if (child->usock_rx_buf)
      vr_free(child->usock_rx_buf, VR_USOCK_BUF_OBJECT);
    vr_free(child, VR_USOCK_OBJECT);
  }

  return parent->usock_error;
}

static int
__usock_write(struct vr_usocket *usockp)
{
  int ret;
  unsigned int len;
  unsigned char *buf;
  struct vr_usocket *parent = NULL;

  if (usockp->usock_proto != EVENT) {
    parent = usockp->usock_parent;
    if (!parent)
      return -1;
  }

  buf = usockp->usock_tx_buf;
  if (!buf || !usockp->usock_write_len)
    return 0;

  len = usockp->usock_write_len;

  buf += usockp->usock_write_offset;
  len -= usockp->usock_write_offset;

retry_write:
  if (usockp->usock_owner != pthread_self())
    usockp->usock_owner = pthread_self();
  ret = write(usockp->usock_fd, buf, len);
  if (ret > 0) {
    usockp->usock_write_offset += ret;
    if (usockp->usock_write_offset == usockp->usock_write_len) {
      /* remove from output poll */
      if (parent)
        parent->usock_pfds[usockp->usock_child_index].events = POLLIN;
      usockp->usock_tx_buf = NULL;
    } else {
      if (parent)
        parent->usock_pfds[usockp->usock_child_index].events = POLLOUT;
    }
  } else if (ret < 0) {
    usock_set_error(usockp, ret);

    if (errno == EINTR)
      goto retry_write;

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      if (parent) {
        parent->usock_pfds[usockp->usock_child_index].events = POLLOUT;
        return 0;
      }
    }
    usockp->usock_tx_buf = NULL;
  }

  return ret;
}

static void
usock_netlink_write_responses(struct vr_usocket *usockp)
{
  int ret;
  struct vr_message *resp;

  while ((resp = (struct vr_message *)vr_queue_dequeue(&usockp->usock_nl_responses))) {
    usockp->usock_tx_buf = (unsigned char *)afxdp_nl_message_hdr(resp);
    usockp->usock_write_len = afxdp_nl_message_len(resp);
    usockp->usock_write_offset = 0;
    ret = __usock_write(usockp);
    if ((ret < 0) || (ret == usockp->usock_write_len)) {
      vr_message_free(resp);
    } else {
      break;
    }
  }

  return;
}

static int
usock_read_done(struct vr_usocket *usockp)
{
  if (usockp->usock_state == READING_FAULTY_DATA)
    return 0;

  switch (usockp->usock_proto) {
  case NETLINK:
    afxdp_netlink_receive(usockp, usockp->usock_rx_buf, usockp->usock_read_len);
    break;

  default:
    break;
  }

  return 0;
}

static int
usock_write(struct vr_usocket *usockp)
{
  int ret;

  if (!usockp || usockp->usock_fd < 0)
    return 0;

  ret = __usock_write(usockp);
  if (ret < 0) {
    usock_close(usockp);
    return ret;
  }

  if (usockp->usock_proto == NETLINK) {
    if (usockp->usock_write_offset == usockp->usock_write_len) {
      usock_netlink_write_responses(usockp);
    }
  }

  return 0;
}

int
vr_usocket_write(struct vr_usocket *usockp, unsigned char *buf, unsigned int len)
{
  if (usockp->usock_tx_buf)
    return -1;

  usockp->usock_tx_buf = buf;
  usockp->usock_write_offset = 0;
  usockp->usock_write_len = len;

  return __usock_write(usockp);
}

static int
vr_usocket_accept(struct vr_usocket *usockp)
{
  int ret;

  ret = accept(usockp->usock_fd, NULL, NULL);
  if (ret < 0) {
    usock_set_error(usockp, ret);
    return ret;
  }

  if ((usockp->usock_state == LIMITED) || (usock_clone(usockp, ret)))
    close(ret);

  return 0;
}

static int
vr_usocket_read(struct vr_usocket *usockp)
{
  int ret;

  if (!usockp || usockp->usock_fd < 0)
    return -1;

  switch (usockp->usock_state) {
  case LISTENING:
  case LIMITED:
    ret = vr_usocket_accept(usockp);
    if (ret < 0)
      return ret;

    break;

  case READING_HEADER:
  case READING_DATA:
  case READING_FAULTY_DATA:
    ret = __usock_read(usockp);
    if (ret < 0) {
      usock_close(usockp);
      return ret;
    }

    if (usockp->usock_read_offset == usockp->usock_read_len) {
      usock_read_done(usockp);
      /* we have the complete message */
      usock_read_init(usockp);
    }

    break;

  default:
    return -1;
  }

  return ret;
}

int
vr_usocket_bind_usockets(void *usock1, void *usock2)
{
  struct vr_usocket *parent = (struct vr_usocket *)usock1;
  struct vr_usocket *child = (struct vr_usocket *)usock2;

  return usock_bind_usockets(parent, child);
}

int
vr_usocket_message_write(struct vr_usocket *usockp, struct vr_message *message)
{
  int ret;
  unsigned int len;
  unsigned char *buf;

  if ((usockp->usock_proto != NETLINK) && (usockp->usock_type != TCP))
    return -EINVAL;

  if (usockp->usock_tx_buf || !vr_queue_empty(&usockp->usock_nl_responses)) {
    vr_queue_enqueue(&usockp->usock_nl_responses, &message->vr_message_queue);
    return 0;
  }

  buf = (unsigned char *)afxdp_nl_message_hdr(message);
  len = afxdp_nl_message_len(message);
  ret = vr_usocket_write(usockp, buf, len);
  if (ret == len) {
    vr_message_free(message);
  }

  return ret;
}

void *
vr_usocket(int proto, int type)
{
  if (!valid_usock(proto, type))
    return NULL;

  return (void *)usock_alloc(proto, type);
}

void
vr_usocket_close(void *sock)
{
  struct vr_usocket *usockp = (struct vr_usocket *)sock;
  if (!usockp)
    return;

  usock_close(usockp);
  return;
}

int
vr_usocket_io(void *transport)
{
  int ret, i, processed;
  int timeout;
  struct pollfd *pfd;
  struct vr_usocket *usockp = (struct vr_usocket *)transport;

  if (!usockp)
    return -1;

  if ((ret = usock_init_poll(usockp)))
    goto return_from_io;

  pfd = &usockp->usock_pfds[0];
  pfd->fd = usockp->usock_fd;
  pfd->events = POLLIN;

  usockp->usock_io_in_progress = 1;

  timeout = usockp->usock_poll_block ? INFINITE_TIMEOUT : 0;
  while (1) {
    if (usockp->usock_should_close) {
      usock_close(usockp);
      return -1;
    }

    rcu_thread_offline();
    ret = poll(usockp->usock_pfds, usockp->usock_max_cfds, timeout);
    if (ret < 0) {
      usock_set_error(usockp, ret);
      /* all other errors are fatal */
      if (errno != EINTR)
        goto return_from_io;
    }

    rcu_thread_online();

    processed = 0;
    pfd = usockp->usock_pfds;
    for (i = 0; (i < usockp->usock_max_cfds) && (processed < ret); i++, pfd++) {
      if ((pfd->fd >= 0)) {
        if (pfd->revents & POLLIN) {
          if (i == 0) {
            ret = vr_usocket_read(usockp);
            if (ret < 0)
              return ret;
          } else {
            vr_usocket_read(usockp->usock_children[i]);
          }
        }

        if (pfd->revents & POLLOUT) {
          usock_write(usockp->usock_children[i]);
        }

        if (pfd->revents & POLLHUP) {
          if (i) {
            usock_close(usockp->usock_children[i]);
          } else {
            break;
          }
        }

        if (pfd->revents)
          processed++;
      }
    }

    if (!timeout)
      return 0;
  }

return_from_io:
  usockp->usock_io_in_progress = 0;
  usock_deinit_poll(usockp);

  return ret;
}
