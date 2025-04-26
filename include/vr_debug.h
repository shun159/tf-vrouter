/* debug.h ------------------------------------------------------------- */
#include <stdio.h>
#include <time.h>

#include "vr_nexthop.h"

#define DUMP_PTRS(pkt, fmd, router, rtable)                                    \
  do {                                                                         \
    fprintf(stderr,                                                            \
            "[%ld.%06ld] %s:%d  pkt=%p vp_if=%p fmd=%p "                       \
            "router=%p rtable=%p\n",                                           \
            time(NULL),                                                        \
            (long)clock(),                                                     \
            __FILE__,                                                          \
            __LINE__,                                                          \
            (pkt),                                                             \
            (pkt)->vp_if,                                                      \
            (fmd),                                                             \
            (router),                                                          \
            (router) ? (router)->vr_inet_rtable : NULL);                       \
  } while (0)

#define DBG(fmt, ...)                                                          \
  do {                                                                         \
    fprintf(stderr,                                                            \
            "[%ld.%06ld] " fmt "\n",                                           \
            time(NULL),                                                        \
            (long)clock(),                                                     \
            ##__VA_ARGS__);                                                    \
  } while (0)

#define DUMP_BR(pkt, fmd, be, nh)                                              \
  DBG("pkt=%p vp_if=%p vp_type=%u flags=0x%x fmd=%p to_me=%d "                 \
      "be=%p nh=%p",                                                           \
      (pkt),                                                                   \
      (pkt)->vp_if,                                                            \
      (pkt)->vp_type,                                                          \
      (pkt)->vp_flags,                                                         \
      (fmd),                                                                   \
      (fmd)->fmd_to_me,                                                        \
      (be),                                                                    \
      (nh))
