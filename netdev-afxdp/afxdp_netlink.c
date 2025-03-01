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

#include <linux/netlink.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <linux/genetlink.h>

#include "nl_util.h"
#include "vr_afxdp.h"
#include "vr_message.h"
#include "vr_genetlink.h"
#include "afxdp_usocket.h"
#include "afxdp_netlink.h"

#define HDR_LEN (NLMSG_HDRLEN + GENL_HDRLEN + sizeof(struct nlattr))

int vr_usocket_message_write(struct vr_usocket *, struct vr_message *);

void
afxdp_netlink_wakeup(void)
{
  if (likely(vr_afxdp.netlink_event_sock != NULL)) {
    if (vr_usocket_eventfd_write(vr_afxdp.netlink_event_sock) < 0) {
      vr_usocket_close(vr_afxdp.netlink_event_sock);
      vr_afxdp.netlink_event_sock = NULL;
    }
  }
}

static void
afxdp_nl_process_response(void *usockp, struct nlmsghdr *nlh)
{
  unsigned int seq;
  unsigned int multi_flag = 0;
  bool write = true;

  struct vr_message *resp;
  struct nlmsghdr *resp_nlh;
  struct genlmsghdr *genlh, *resp_genlh;
  struct nlattr *resp_nla;

  seq = nlh->nlmsg_seq;
  genlh = (struct genlmsghdr *)((unsigned char *)nlh + NLMSG_HDRLEN);

  // process responses
  while ((resp = (struct vr_message *)vr_message_dequeue_response())) {
    multi_flag = 0;

    if (!write) {
      vr_message_free(resp);
      continue;
    }

    if (!vr_response_queue_empty())
      multi_flag = NLM_F_MULTI;

    // update netlink headers
    resp_nlh = afxdp_nl_message_hdr(resp);
    resp_nlh->nlmsg_len = afxdp_nl_message_len(resp);
    resp_nlh->nlmsg_type = nlh->nlmsg_type;
    resp_nlh->nlmsg_flags = multi_flag;
    resp_nlh->nlmsg_seq = seq;
    resp_nlh->nlmsg_pid = 0;

    resp_genlh = (struct genlmsghdr *)((unsigned char *)resp_nlh + NLMSG_HDRLEN);
    memcpy(resp_genlh, genlh, sizeof(*genlh));

    resp_nla = (struct nlattr *)((__u8 *)resp_genlh + GENL_HDRLEN);
    resp_nla->nla_len = resp->vr_message_len;
    resp_nla->nla_type = NL_ATTR_VR_MESSAGE_PROTOCOL;

    if (vr_usocket_message_write(usockp, resp) < 0) {
      write = false;
      vr_usocket_close(usockp);
    }
  }

  return;
}

int
afxdp_netlink_receive(void *usockp, char *nl_buf, unsigned int nl_len)
{
  int ret;
  struct vr_message request;

  memset(&request, 0, sizeof(request));
  request.vr_message_buf = nl_buf + HDR_LEN;
  request.vr_message_len = nl_len - HDR_LEN;

  ret = vr_message_request(&request);
  if (ret < 0)
    vr_send_response(ret);

  afxdp_nl_process_response(usockp, (struct nlmsghdr *)nl_buf);

  return 0;
}

unsigned int
afxdp_nl_message_len(struct vr_message *message)
{
  return message->vr_message_len + HDR_LEN;
}

struct nlmsghdr *
afxdp_nl_message_hdr(struct vr_message *message)
{
  return (struct nlmsghdr *)(message->vr_message_buf - HDR_LEN);
}

static void
afxdp_nl_trans_free(char *buf)
{
  buf -= HDR_LEN;
  vr_free(buf, VR_MESSAGE_OBJECT);

  return;
}

static char *
afxdp_nl_trans_alloc(unsigned int size)
{
  char *buf;

  buf = vr_malloc(size + HDR_LEN, VR_MESSAGE_OBJECT);
  if (!buf)
    return NULL;

  return buf + HDR_LEN;
}

static struct vr_mtransport afxdp_nl_transport = {
    .mtrans_alloc = afxdp_nl_trans_alloc,
    .mtrans_free = afxdp_nl_trans_free,
};

// init netlink socket
int
vr_afxdp_netlink_init(void)
{
  int ret;

  fprintf(stdout, "starting netlink...\n");
  ret = vr_message_transport_register(&afxdp_nl_transport);
  if (ret)
    return ret;

  vr_afxdp.netlink_sock = vr_usocket(NETLINK, UNIX);
  if (!vr_afxdp.netlink_sock) {
    fprintf(stderr, "error creating netlink server socket: %s (%d)\n", strerror(errno), errno);
    goto error;
  }

  return 0;

error:
  vr_message_transport_unregister(&afxdp_nl_transport);
  vr_usocket_close(vr_afxdp.netlink_sock);
  return -1;
}

int
vr_afxdp_netlink_loop(void)
{
  while (1) {
    if (vr_afxdp_netlink_init() == 0)
      vr_usocket_io(vr_afxdp.netlink_sock);

    usleep(VR_AFXDP_SLEEP_SERVICE_US);
  }

  return 0;
}
