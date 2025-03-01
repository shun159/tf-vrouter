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

#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include "vr_os.h"
#include "vr_packet.h"
#include "vr_proto.h"
#include "vrouter.h"
#include "vr_message.h"
#include "vr_sandesh.h"
#include "vr_afxdp.h"
#include "afxdp_interface.h"
#include "afxdp_host.h"
#include "afxdp_global_umem.h"
#include "host/vr_host_packet.h"
#include "ulinux.h"

#define PAGE_SIZE 4096

void
add_timer(struct dummy_timer_list *dummy)
{
  return;
}

void
init_timer(struct dummy_timer_list *dummy)
{
  return;
}

time_t
get_time()
{
  return time(NULL);
}

void
ulinux_timer(unsigned long data)
{
  return;
}

static void *
vr_lib_malloc(__u32 size, __u32 object)
{
  return malloc(size);
}

static void *
vr_lib_zalloc(__u32 size, __u32 object)
{
  return calloc(size, 1);
}

static void
vr_lib_free(void *mem, __u32 object)
{
  if (mem)
    free(mem);
  return;
}

static int
vr_lib_printf(const char *format, ...)
{
  int printed;
  va_list args;

  va_start(args, format);
  printed = printf(format, args);
  va_end(args);

  return printed;
}

static struct vr_packet *
vr_lib_get_packet(struct vr_hpacket *hpkt, struct vr_interface *vif)
{
  struct vr_packet *pkt;

  pkt = &hpkt->hp_packet;
  pkt->vp_head = hpkt->hp_head;
  pkt->vp_data = hpkt->hp_data;
  pkt->vp_tail = hpkt->hp_tail;
  pkt->vp_end = hpkt->hp_end;
  pkt->vp_len = hpkt_head_len(hpkt);
  pkt->vp_if = vif;

  return pkt;
}

int
vr_hpacket_copy(unsigned char *dst, struct vr_hpacket *hpkt_src, __u32 offset, __u32 len)
{
  __u16 tocopy, copied;
  unsigned char *src;

  while (hpkt_src && offset > hpkt_src->hp_end) {
    offset -= (hpkt_src->hp_tail - hpkt_src->hp_data);
    hpkt_src = hpkt_src->hp_next;
  }

  if (!hpkt_src)
    return -EINVAL;

  tocopy = len;
  src = hpkt_src->hp_head + hpkt_src->hp_data + offset;
  copied = 0;

  while (len) {
    if (len > hpkt_src->hp_tail - (hpkt_src->hp_data + offset))
      tocopy = hpkt_src->hp_tail - (hpkt_src->hp_data + offset);
    memcpy(dst + copied, src, tocopy);
    len -= tocopy;
    copied += tocopy;
    hpkt_src = hpkt_src->hp_next;
    if (!hpkt_src)
      return copied;
    src = hpkt_data(hpkt_src);
  }

  return copied;
}

void
vr_hpacket_free(struct vr_hpacket *hpkt)
{
  struct vr_hpacket_tail *hpkt_tail;
  struct vr_hpacket *hpkt_next;

  while (hpkt) {
    hpkt_next = hpkt->hp_next;
    hpkt_tail = (struct vr_hpacket_tail *)hpkt_end(hpkt);
    hpkt_tail->hp_users--;
    if (hpkt->hp_flags & VR_HPACKET_FLAGS_CLONED) {
      if (!hpkt_tail->hp_users)
        free(hpkt->hp_head);
      free(hpkt);
      return;
    }

    if (hpkt->hp_pool) {
      if (hpkt_tail->hp_users) {
        hpkt->hp_head = malloc(hpkt->hp_end + sizeof(struct vr_hpacket_tail));
        hpkt_tail = (struct vr_hpacket_tail *)hpkt_end(hpkt);
        hpkt_tail->hp_users = 1;
      }
      vr_hpacket_pool_free(hpkt);
    } else {
      free(hpkt->hp_head);
      free(hpkt);
    }

    hpkt = hpkt_next;
  }

  return;
}

struct vr_hpacket *
vr_hpacket_alloc(__u32 size)
{
  struct vr_hpacket *hpkt;
  struct vr_hpacket_tail *hpkt_tail;
  struct vr_packet *pkt;

  hpkt = (struct vr_hpacket *)malloc(sizeof(*hpkt));
  if (!hpkt)
    return NULL;

  hpkt->hp_head = malloc(size + VR_HPACKET_HEAD_SPACE + sizeof(struct vr_hpacket_tail));
  if (!hpkt->hp_head) {
    free(hpkt);
    return NULL;
  }

  hpkt->hp_data = hpkt->hp_tail = VR_HPACKET_HEAD_SPACE;
  hpkt->hp_end = size - 1;
  hpkt_tail = (struct vr_hpacket_tail *)hpkt_end(hpkt);
  hpkt_tail->hp_users = 1;
  pkt = &hpkt->hp_packet;
  pkt->vp_head = hpkt->hp_head;
  pkt->vp_data = hpkt->hp_data;
  pkt->vp_end = hpkt->hp_end;
  pkt->vp_len = 0;
  pkt->vp_if = NULL;

  return hpkt;
}

struct vr_hpacket *
vr_hpacket_clone(struct vr_hpacket *hpkt)
{
  struct vr_hpacket *hpkt_c;
  struct vr_hpacket_tail *hpkt_tail;

  hpkt_c = (struct vr_hpacket *)malloc(sizeof(struct vr_hpacket));
  if (!hpkt_c)
    return NULL;

  memcpy(hpkt_c, hpkt, sizeof(*hpkt));

  /* increase the reference count for the buffer */
  hpkt_tail = (struct vr_hpacket_tail *)hpkt_end(hpkt);
  hpkt_tail->hp_users++;

  hpkt_c->hp_flags |= VR_HPACKET_FLAGS_CLONED;
  return hpkt_c;
}

struct vr_hpacket *
vr_hpacket_pool_alloc(struct vr_hpacket_pool *pool)
{
  struct vr_hpacket *hpkt;
  struct vr_packet *pkt;

  hpkt = pool->pool_head;
  pool->pool_head = hpkt->hp_next;
  hpkt->hp_next = NULL;
  pkt = &hpkt->hp_packet;
  pkt->vp_data = hpkt->hp_data;
  return hpkt;
}

void
vr_hpacket_pool_free(struct vr_hpacket *hpkt)
{
  struct vr_hpacket_pool *pool = hpkt->hp_pool;
  struct vr_packet *pkt;

  hpkt->hp_next = pool->pool_head;
  pool->pool_head = hpkt;
  pkt = &hpkt->hp_packet;
  pkt->vp_data = hpkt->hp_data;
  pkt->vp_len = 0;
  pkt->vp_if = NULL;

  return;
}

void
vr_hpacket_pool_destroy(struct vr_hpacket_pool *pool)
{
  struct vr_hpacket *hpkt, *n_hpkt;

  hpkt = pool->pool_head;
  while (hpkt) {
    n_hpkt = hpkt->hp_next;
    hpkt->hp_next = NULL;
    hpkt->hp_pool = NULL;
    vr_hpacket_free(hpkt);
    hpkt = n_hpkt;
  }
  vr_free(pool, VR_HPACKET_POOL_OBJECT);

  return;
}

struct vr_hpacket_pool *
vr_hpacket_pool_create(__u32 pool_size, __u32 psize)
{
  __u32 i;
  struct vr_hpacket_pool *pool;
  struct vr_hpacket *hpkt;

  if (!pool_size)
    return NULL;

  pool = vr_zalloc(sizeof(*pool), VR_HPACKET_POOL_OBJECT);
  if (!pool)
    goto cleanup;

  for (i = 0; i < pool_size; i++) {
    hpkt = vr_hpacket_alloc(psize);
    if (!hpkt)
      goto cleanup;

    if (!pool->pool_head)
      pool->pool_head = hpkt;
    else {
      hpkt->hp_next = pool->pool_head->hp_next;
      pool->pool_head->hp_next = hpkt;
    }
    hpkt->hp_pool = pool;
  }

  return pool;

cleanup:
  if (pool)
    vr_hpacket_pool_destroy(pool);

  return NULL;
}

static struct vr_packet *
vr_lib_palloc(__u32 size)
{
  struct vr_hpacket *hpkt;

  hpkt = vr_hpacket_alloc(size);
  if (!hpkt)
    return NULL;

  return vr_lib_get_packet(hpkt, NULL);
}

static struct vr_packet *
vr_lib_palloc_head(struct vr_packet *pkt, __u32 size)
{
  struct vr_hpacket *hpkt_head, *hpkt;

  hpkt_head = vr_hpacket_alloc(size);
  if (!hpkt_head)
    return NULL;

  hpkt = VR_PACKET_TO_HPACKET(pkt);
  hpkt_head->hp_len = hpkt->hp_len;
  hpkt_head->hp_next = hpkt;

  return &hpkt_head->hp_packet;
}

static void
vr_lib_pfree(struct vr_packet *pkt, __u16 reason)
{
  struct vr_hpacket *hpkt;

  /* Handle Vrouter statistics */
  pkt_drop_stats(pkt->vp_if, reason, pkt->vp_cpu);

  hpkt = VR_PACKET_TO_HPACKET(pkt);
  vr_hpacket_free(hpkt);
  return;
}

static int
vr_lib_pcopy(unsigned char *dst, struct vr_packet *p_src, __u32 offset, __u32 len)
{
  struct vr_hpacket *src_hpkt = VR_PACKET_TO_HPACKET(p_src);

  return vr_hpacket_copy(dst, src_hpkt, offset, len);
}

static __u16
vr_lib_pfrag_len(struct vr_packet *pkt)
{
  struct vr_hpacket *hpkt;

  hpkt = VR_PACKET_TO_HPACKET(pkt);
  if (!hpkt->hp_next)
    return 0;

  return hpkt->hp_next->hp_len;
}

static void
vr_lib_get_time(uint64_t *sec, uint64_t *usec)
{
  struct timeval tv;

  *sec = *usec = 0;
  if (gettimeofday(&tv, NULL) < 0)
    return;

  *sec = tv.tv_sec;
  *usec = tv.tv_usec;

  return;
}

static struct vr_packet *
vr_lib_pclone(struct vr_packet *pkt)
{
  struct vr_hpacket *hpkt, *hpkt_c;

  hpkt = VR_PACKET_TO_HPACKET(pkt);
  hpkt_c = vr_hpacket_clone(hpkt);
  if (!hpkt_c)
    return NULL;

  return &hpkt_c->hp_packet;
}

static void
vr_lib_preset(struct vr_packet *pkt)
{
  struct vr_hpacket *hpkt;

  hpkt = VR_PACKET_TO_HPACKET(pkt);

  pkt->vp_data = hpkt->hp_data;
  pkt->vp_tail = hpkt->hp_tail;
  pkt->vp_len = pkt->vp_tail - pkt->vp_data + 1;

  return;
}

static __u32
vr_lib_get_cpu(void)
{
  return 0;
}

static int
vr_lib_schedule_work(__u32 cpu, void (*fn)(void *), void *arg)
{
  return -EOPNOTSUPP;
}

static void
vr_lib_delay_op(void)
{
  return;
}

static void *
vr_lib_page_alloc(__u32 size)
{
  int pages;

  pages = size / PAGE_SIZE;
  if (size % PAGE_SIZE)
    pages++;

  return calloc(pages, PAGE_SIZE);
}

static void
vr_lib_page_free(void *address, __u32 size)
{
  if (address)
    free(address);
}

static int
vr_lib_create_timer(struct vr_timer *vtimer)
{
  struct dummy_timer_list *timer;

  timer = vr_zalloc(sizeof(*timer), VR_TIMER_OBJECT);
  if (!timer)
    return -1;
  init_timer(timer);

  vtimer->vt_os_arg = (void *)timer;
  timer->data = (uint64_t)vtimer;
  timer->function = ulinux_timer;
  timer->expires = get_time() + vtimer->vt_msecs;
  add_timer(timer);

  return 0;
}

static void
vr_lib_delete_timer(struct vr_timer *vtimer)
{
  vr_free(vtimer->vt_os_arg, VR_TIMER_OBJECT);
}

static void *
afxdp_network_header(struct vr_packet *pkt)
{
  if (pkt->vp_network_h < pkt->vp_end)
    return pkt->vp_head + pkt->vp_network_h;

  return NULL;
}

static void *
afxdp_inner_network_header(struct vr_packet *pkt)
{
  /* TODO: not used? */
  fprintf(stderr, "%s: not implemented\n", __func__);

  return NULL;
}

static void *
afxdp_data_at_offset(struct vr_packet *pkt, __u16 off)
{
  if (off < pkt->vp_end)
    return pkt->vp_head + off;

  /* TODO: for buffer chain? */
  fprintf(stderr, "%s: buffer chain not supported\n", __func__);

  return NULL;
}

// return pointer to data at pkt->vp_data offset if hdr_len bytes
// in continuous memory.
static void *
afxdp_pheader_pointer(struct vr_packet *pkt, unsigned short hdr_len, void *buf)
{
  int pkt_len = pkt->vp_tail - pkt->vp_data;
  if (hdr_len <= pkt_len) {
    return (void *)((uintptr_t)pkt->vp_head + pkt->vp_data);
  } else {
    if (pkt_len <= 0)
      return NULL;

    memcpy(buf, (__u8 *)pkt->vp_head + pkt->vp_data, pkt_len);
    hdr_len -= pkt_len;

    if (hdr_len > 0)
      return NULL;

    return buf;
  }
}

// copy packet buf
static int
afxdp_pcow(struct vr_packet **pktp, unsigned short head_room)
{
  struct vr_packet *new_pkt, *pkt = *pktp;
  struct vr_xpacket *xpkt = NULL;

  int data_len;
  __u8 *new_data, *old_data;

  if (head_room <= pkt->vp_data)
    // We already have enough headroom—nothing to do
    return 0;

  xpkt = afxdp_global_pool_alloc_frame();
  if (!xpkt)
    return -ENOMEM;

  new_pkt = &xpkt->pkt;
  memset(new_pkt, 0, sizeof(*new_pkt));

  *new_pkt = *pkt;
  data_len = pkt->vp_tail - pkt->vp_data;

  new_pkt->vp_data = head_room;
  new_pkt->vp_tail = head_room + data_len;

  new_data = (__u8 *)new_pkt->vp_head + new_pkt->vp_data;
  old_data = (__u8 *)pkt->vp_head + pkt->vp_data;
  memcpy(new_data, old_data, data_len);

  *pktp = new_pkt;

  struct vr_xpacket *xpkt_old = afxdp_xpacket_from_pkt(pkt);
  afxdp_global_pool_free_frame(xpkt_old);

  return 0;
}

// adjusting TCP MSS
void
afxdp_adjust_tcp_mss(struct tcphdr *tcph, __u16 overlay_len, __u8 iph_len)
{
  int opt_off = sizeof(struct tcphdr);
  __u8 port_id;
  __u8 *opt_ptr = (__u8 *)tcph;
  __u16 pkt_mss, max_mss, mtu;
  __u32 csum;

  struct vrouter *router = vrouter_get(0);

  if ((tcph == NULL) || !(tcph->syn) || (router == NULL))
    return;

  if (router->vr_eth_if[0] == NULL)
    return;

  while (opt_off < (tcph->doff * 4)) {
    switch (opt_ptr[opt_off]) {
    case TCPOPT_EOL:
      return;

    case TCPOPT_NOP:
      opt_off++;
      continue;

    case TCPOPT_MAXSEG:
      if ((opt_off + TCPOLEN_MAXSEG) > (tcph->doff * 4))
        return;

      if (opt_ptr[opt_off + 1] != TCPOLEN_MAXSEG)
        return;

      pkt_mss = (opt_ptr[opt_off + 2] << 8 | opt_ptr[opt_off + 3]);
      if (router->vr_eth_if[0] == NULL)
        return;

      port_id = (((struct vr_afxdp_ethdev *)(router->vr_eth_if[0]->vif_os))->os_ifidx);
      mtu = get_mtu_by_ifindex(port_id);
      max_mss = mtu - (overlay_len + iph_len + sizeof(struct tcphdr));
      if (pkt_mss > max_mss) {
        opt_ptr[opt_off + 2] = (max_mss & 0xff00) >> 8;
        opt_ptr[opt_off + 3] = max_mss & 0xff;
        csum = (__u16)(~cpu_to_be_16(tcph->check));
        csum = csum + (__u16)~pkt_mss;
        csum = (csum & 0xffff) + (csum >> 16);
        csum += max_mss;
        csum = (csum & 0xffff) + (csum >> 16);
        tcph->check = cpu_to_be_16(~((__u16)csum));
      }
      return;

    default:
      if ((opt_off + 1) == (tcph->doff * 4))
        return;
      if (opt_ptr[opt_off + 1])
        opt_off += opt_ptr[opt_off + 1];
      else
        opt_off++;
      continue;
    }
  }

  return;
}

static int
afxdp_pkt_from_vm_tcp_mss_adj(struct vr_packet *pkt, __u16 overlay_len)
{
  struct vr_xdp_buf *buf = vr_afxdp_pkt_to_xdp_buf(pkt);
  struct vr_ip *ip4h = NULL;
  struct vr_ip6 *ip6h = NULL;
  struct tcphdr *tcph;
  __s32 offset;
  __u8 iph_len = 0, iph_proto = 0;

  // check if whole ip header is in the packet
  if (pkt->vp_type == VP_TYPE_IP) {
    offset = sizeof(struct vr_ip);
    if (pkt->vp_data + offset < pkt->vp_end)
      ip4h = (struct vr_ip *)((uintptr_t)buf->buf_addr + pkt->vp_data);
    else {
      fprintf(stderr, "%s: ip header not in first buffer\n", __func__);
      return -1;
    }
    iph_proto = ip4h->ip_proto;
    iph_len = ip4h->ip_hl * 4;

    // if this is a fragment and not the first one, it can be ignored
    if (ip4h->ip_frag_off & cpu_to_be_16(IP_OFFMASK))
      goto out;
  } else if (pkt->vp_type == VP_TYPE_IP6) {
    iph_len = offset = sizeof(struct vr_ip6);
    if (pkt->vp_data + offset < pkt->vp_end)
      ip6h = (struct vr_ip6 *)((uintptr_t)buf->buf_addr + pkt->vp_data);
    else {
      fprintf(stderr, "%s: ip header not in first buffer\n", __func__);
      return -1;
    }
    iph_proto = ip6h->ip6_nxt;
  }

  if (iph_proto != VR_IP_PROTO_TCP)
    goto out;

  // check if whole tcp header is also in the packet
  offset = iph_len + sizeof(struct tcphdr);
  if (pkt->vp_data + offset < pkt->vp_end)
    tcph = (struct tcphdr *)pkt_data_at_offset(pkt, pkt->vp_data + iph_len);
  else {
    fprintf(stderr, "%s: tcp header not in first buffer\n", __func__);
    return -1;
  }

  if ((tcph->doff << 2) <= (sizeof(struct tcphdr)))
    goto out;

  offset += (tcph->doff << 2) - sizeof(struct tcphdr);
  if (pkt->vp_data + offset > pkt->vp_end) {
    fprintf(stderr, "%s: tcp header outside first buffer\n", __func__);
    return -1;
  }
  afxdp_adjust_tcp_mss(tcph, overlay_len, iph_len);

out:
  return 0;
}

static __u32
afxdp_pgso_size(struct vr_packet *pkt)
{
  struct vr_xdp_buf *buf = vr_afxdp_pkt_to_xdp_buf(pkt);
  return buf->tso_segsz;
}

static __s32
afxdp_pkt_may_pull(struct vr_packet *pkt, __u32 len)
{
  struct vr_xdp_buf *buf = vr_afxdp_pkt_to_xdp_buf(pkt);
  if (len > buf->data_len)
    return -1;

  vr_afxdp_buf_reset(pkt);
  return 0;
}

static int
afxdp_is_frag_limit_exceeded(void)
{
  struct vrouter *router = vrouter_get(0);
  struct vr_malloc_stats *stats;
  uint64_t sum = 0;
  unsigned int cpu;

  if (router->vr_malloc_stats) {
    for (cpu = 0; cpu < vr_num_cpus; cpu++) {
      if (router->vr_malloc_stats[cpu]) {
        stats = &router->vr_malloc_stats[cpu][VR_FRAGMENT_QUEUE_ELEMENT_OBJECT];
        sum += stats->ms_alloc;
        sum -= stats->ms_free;
      }
    }

    if (sum > VR_AFXDP_MAX_FRAGMENT_ELEMENTS)
      return 1;
  }

  return 0;
}

static void
afxdp_register_nic(struct vr_interface *vif __attribute__((unused)),
                   vr_interface_req *vifr __attribute__((unused)))
{
}

struct host_os vr_lib_host = {
    .hos_printf = vr_lib_printf,
    .hos_malloc = vr_lib_malloc,
    .hos_zalloc = vr_lib_zalloc,
    .hos_free = vr_lib_free,

    .hos_palloc = vr_lib_palloc,
    .hos_palloc_head = vr_lib_palloc_head,
    .hos_pfree = vr_lib_pfree,
    .hos_preset = vr_lib_preset,
    .hos_pclone = vr_lib_pclone,
    .hos_pcopy = vr_lib_pcopy,
    .hos_pfrag_len = vr_lib_pfrag_len,

    .hos_get_cpu = vr_lib_get_cpu,
    .hos_schedule_work = vr_lib_schedule_work,
    .hos_delay_op = vr_lib_delay_op,
    .hos_get_time = vr_lib_get_time,
    .hos_page_alloc = vr_lib_page_alloc,
    .hos_page_free = vr_lib_page_free,
    .hos_create_timer = vr_lib_create_timer,
    .hos_delete_timer = vr_lib_delete_timer,

    .hos_network_header = afxdp_network_header,
    .hos_inner_network_header = afxdp_inner_network_header,
    .hos_data_at_offset = afxdp_data_at_offset, /* for chains? */
    .hos_pull_inner_headers = NULL,             /* not necessary */
    .hos_pheader_pointer = afxdp_pheader_pointer,
    .hos_pcow = afxdp_pcow,
    .hos_pull_inner_headers_fast = NULL,
    .hos_pkt_from_vm_tcp_mss_adj = afxdp_pkt_from_vm_tcp_mss_adj,
    .hos_pgso_size = afxdp_pgso_size,
    .hos_pkt_may_pull = afxdp_pkt_may_pull,
    .hos_is_frag_limit_exceeded = afxdp_is_frag_limit_exceeded,
    .hos_register_nic = afxdp_register_nic,
};

struct host_os *
vrouter_get_host(void)
{
  return &vr_lib_host;
}

static uint64_t prng_state = 0;
static int prng_inited = 0;

static inline uint64_t
xorshift64(void)
{
  uint64_t x = prng_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  prng_state = x;
  return x * 2685821657736338717ULL;
}

static void
seed_random(void)
{
  if (prng_inited) {
    return;
  }
  prng_inited = 1;

  /*
   * Below is a trivial seeding example from current time.
   * If you want better unpredictability, read from /dev/urandom here (once).
   */
  prng_state = (uint64_t)time(NULL);
  (void)xorshift64();
}

void
get_random_bytes(void *buf, int nbytes)
{
  int offset = 0;

  if (!prng_inited) {
    seed_random();
  }

  while (offset < nbytes) {
    uint64_t rnd = xorshift64();

    int chunk = nbytes - offset;
    if (chunk >= 8) {
      memcpy((uint8_t *)buf + offset, &rnd, 8);
      offset += 8;
    } else {
      memcpy((uint8_t *)buf + offset, &rnd, chunk);
      offset += chunk;
    }
  }
}

//  vr_afxdp_pkt_to_xdp_buf - Convert a pointer to vr_packet into the associated vr_xdp_buf ptr
struct vr_xdp_buf *
vr_afxdp_pkt_to_xdp_buf(struct vr_packet *pkt)
{
  /* Assumes that 'struct vr_packet' is placed immediately after vr_xdp_buf */
  return (struct vr_xdp_buf *)((uintptr_t)pkt - sizeof(struct vr_xdp_buf));
}

// vr_afxdp_xdp_buf_to_pkt - Convert a pointer to vr_xdp_buf into the associated vr_packet ptr
struct vr_packet *
vr_afxdp_xdp_buf_to_pkt(struct vr_xdp_buf *xdp_buf)
{
  return (struct vr_packet *)((uintptr_t)xdp_buf + sizeof(struct vr_xdp_buf));
}

struct vr_xpacket *
afxdp_xpacket_from_pkt(struct vr_packet *pkt)
{
  return CONTAINER_OF(pkt, struct vr_xpacket, pkt);
}

// fxdp_xdp_buf_copy - Copy the given vr_xdp_buf (including metadata) from the pool.
struct vr_xdp_buf *
afxdp_xdp_buf_copy(struct vr_xdp_buf *src, void *pool)
{
  struct vr_xdp_buf *dst = malloc(sizeof(struct vr_xdp_buf));
  if (!dst)
    return NULL;
  memcpy(dst, src, sizeof(struct vr_xdp_buf));
  return dst;
}

// afxdp_xdp_buf_free - Free the given vr_xdp_buf.
void
afxdp_xdp_buf_free(struct vr_xdp_buf *xdp_buf)
{
  free(xdp_buf);
}

// calcurate frame index by shifting
__u32
afxdp_desc_to_index(uint64_t addr)
{
  return (__u32)(addr >> FRAME_SHIFT);
}

// alloc frame from global umem pool
struct vr_xpacket *
afxdp_global_pool_alloc_frame(void)
{
  if (!global_umem) {
    return NULL;
  }
  if (global_umem->top == 0)
    return NULL;

  global_umem->top--;
  void *frame = global_umem->frames[global_umem->top];

  uintptr_t offset = (uintptr_t)frame - (uintptr_t)global_umem->buffer;
  __u32 index = afxdp_desc_to_index(offset);

  struct vr_xpacket *xpacket = &global_umem->meta_bufs[index];

  memset(xpacket, 0, sizeof(*xpacket));

  xpacket->xbuf.buf_addr = frame;
  xpacket->xbuf.buf_len = FRAME_SIZE;
  xpacket->xbuf.data_off = 0;
  xpacket->xbuf.data_len = 0;

  xpacket->pkt.vp_cpu = vr_get_cpu();
  xpacket->pkt.vp_head = frame;

  return xpacket;
}

// return frame buffer to global umem pool
void
afxdp_global_pool_free_frame(struct vr_xpacket *xpacket)
{
  if (!global_umem) {
    return;
  }
  void *frame = xpacket->xbuf.buf_addr;
  if (global_umem->top >= global_umem->n_frames)
    return;

  global_umem->frames[global_umem->top] = frame;
  global_umem->top++;

  memset(xpacket, 0, sizeof(*xpacket));
}

void
vr_afxdp_packet_init(struct vr_xdp_buf *xbuf,
                     void *frame_ptr,
                     __u16 frame_size,
                     __u16 pkt_len,
                     __u64 desc_addr)
{
  xbuf->buf_addr = frame_ptr;
  xbuf->buf_len = frame_size;
  xbuf->data_len = pkt_len;
  xbuf->desc_addr = desc_addr;
  xbuf->data_off = AFXDP_PKT_HEADROOM;

  struct vr_packet *pkt = vr_afxdp_xdp_buf_to_pkt(xbuf);
  memset(pkt, 0, sizeof(struct vr_packet));

  pkt->vp_head = (unsigned char *)frame_ptr;
  pkt->vp_data = xbuf->data_off;
  pkt->vp_tail = xbuf->data_off + pkt_len;
  pkt->vp_end = frame_size;

  pkt->vp_cpu = vr_get_cpu();
  pkt->vp_if = NULL;
}

void
vr_afxdp_buf_reset(struct vr_packet *pkt)
{
  struct vr_xdp_buf *buf = vr_afxdp_pkt_to_xdp_buf(pkt);

  pkt->vp_head = buf->buf_addr;
  pkt->vp_tail = buf->data_off + buf->data_len;
  pkt->vp_end = buf->buf_len;
  pkt->vp_len = pkt->vp_tail - pkt->vp_data;

  return;
}

__u16
cpu_to_be_16(__u16 x)
{
#if defined(__GNUC__)
  return __builtin_bswap16(x);
#else
  return (uint16_t)(((x & 0x00FFU) << 8) | ((x & 0xFF00U) >> 8));
#endif
}
