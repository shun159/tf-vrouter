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
 * afxdp_vrouter.c -- vRouter/AF_XDP application
 *
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "afxdp_thread.h"
#include "afxdp_interface.h"
#include "vr_afxdp.h"
#include "afxdp_host.h"
#include "vrouter.h"
#include "vr_sandesh.h"

/* Max number of CPUs. We adjust it later in vr_dpdk_host_init() */
unsigned int vr_num_cpus = 1;
int no_huge_set;

static volatile int afxdp_stop = 0;
static int no_daemon_set = 0;

extern char *ContrailBuildInfo;

struct vr_afxdp_global vr_afxdp;
struct xsk_umem_info *global_umem;

/* vRouter/DPDK command-line options. */
enum vr_opt_index {
#define NO_DAEMON_OPT "no-daemon"
  NO_DAEMON_OPT_INDEX,
#define NO_HUGE_OPT "no-huge"
  NO_HUGE_OPT_INDEX,
#define VERSION_OPT "version"
  VERSION_OPT_INDEX,
#define BRIDGE_ENTRIES_OPT "vr_bridge_entries"
  BRIDGE_ENTRIES_OPT_INDEX,
#define BRIDGE_OENTRIES_OPT "vr_bridge_oentries"
  BRIDGE_OENTRIES_OPT_INDEX,
#define FLOW_ENTRIES_OPT "vr_flow_entries"
  FLOW_ENTRIES_OPT_INDEX,
#define OFLOW_ENTRIES_OPT "vr_oflow_entries"
  OFLOW_ENTRIES_OPT_INDEX,
};

/* dp-core parameters */
extern unsigned int vr_bridge_entries;
extern unsigned int vr_bridge_oentries;
extern unsigned int vr_mpls_labels;
extern unsigned int vr_nexthops;
extern unsigned int vr_vrfs;
extern unsigned int datapath_offloads;
extern unsigned int vr_pkt_droplog_bufsz;
extern unsigned int vr_uncond_close_flow_on_tcp_rst;

// command line options
static struct option long_options[] = {
    [NO_DAEMON_OPT_INDEX] = {NO_DAEMON_OPT, no_argument, &no_daemon_set, 1},
    [NO_HUGE_OPT_INDEX] = {NO_HUGE_OPT, no_argument, &no_huge_set, 1},
    [VERSION_OPT_INDEX] = {VERSION_OPT, no_argument, NULL, 0},
    [BRIDGE_ENTRIES_OPT_INDEX] = {BRIDGE_ENTRIES_OPT,
                                  required_argument,
                                  NULL,
                                  0},
    [BRIDGE_OENTRIES_OPT_INDEX] = {BRIDGE_OENTRIES_OPT,
                                   required_argument,
                                   NULL,
                                   0},
    [FLOW_ENTRIES_OPT_INDEX] = {FLOW_ENTRIES_OPT, required_argument, NULL, 0},
    [OFLOW_ENTRIES_OPT_INDEX] = {OFLOW_ENTRIES_OPT, required_argument, NULL, 0},
};

// Print version information
static void
version_print(void)
{
  /* Version information could be generated during build time. */
  printf("vRouter/AF_XDP version: %s\n", ContrailBuildInfo);
}

// Print usage information
static void
Usage(void)
{
  printf("Usage: contrail-vrouter-afxdp [options]\n"
         "  --no-daemon   Do not daemonize the process\n"
         "  --help        Show this help message\n"
         "  --" NO_HUGE_OPT "    Use malloc instead of hugetlbfs\n"
         "  --" BRIDGE_ENTRIES_OPT " NUM   Bridge table limit\n"
         "  --" BRIDGE_OENTRIES_OPT " NUM  Bridge table overflow limit\n"
         "  --" FLOW_ENTRIES_OPT " NUM     Flow table limit\n"
         "  --" OFLOW_ENTRIES_OPT " NUM    Flow overflow table limit\n"
         "  --version     Print version information\n");
  exit(1);
}

static void
parse_long_opts(int opt_flow_index, char *optarg)
{
  errno = 0;

  switch (opt_flow_index) {
  case NO_DAEMON_OPT_INDEX:
  case NO_HUGE_OPT_INDEX:
  case VERSION_OPT_INDEX:
    version_print();
    exit(0);
    break;

  case BRIDGE_ENTRIES_OPT_INDEX:
    vr_bridge_entries = (unsigned int)strtoul(optarg, NULL, 0);
    if (errno != 0) {
      vr_bridge_entries = VR_DEF_BRIDGE_ENTRIES;
    }
    break;

  case BRIDGE_OENTRIES_OPT_INDEX:
    vr_bridge_oentries = (unsigned int)strtoul(optarg, NULL, 0);
    if (errno != 0) {
      vr_bridge_oentries = ((vr_bridge_entries / 5) + 1023) & ~1023;
    }
    break;

  case FLOW_ENTRIES_OPT_INDEX:
    vr_flow_entries = (unsigned int)strtoul(optarg, NULL, 0);
    if (errno != 0) {
      vr_flow_entries = VR_DEF_FLOW_ENTRIES;
    }
    break;

  case OFLOW_ENTRIES_OPT_INDEX:
    vr_oflow_entries = (unsigned int)strtoul(optarg, NULL, 0);
    if (errno != 0) {
      /* vr_flow_entries would be either VR_DEF_FLOW_ENTRIES or
       * user value
       */
      vr_oflow_entries = ((vr_flow_entries / 5) + 1023) & ~1023;
    }
    break;

  default:
    Usage();
  }
}

static void
afxdp_signal_handler(int signum)
{
  fprintf(stdout, "Signal %d received, stopping...\n", signum);
  afxdp_stop = 1;
}

bool
vr_afxdp_is_stop_flag_set(void)
{
  return afxdp_stop == 1;
}

static bool
parse_no_arg_cli(int opt_flow_index, int optind, char *argv[])
{
  if (opt_flow_index == NO_DAEMON_OPT_INDEX ||
      opt_flow_index == NO_HUGE_OPT_INDEX ||
      opt_flow_index == VERSION_OPT_INDEX) {
    if (argv[optind] && argv[optind][0] != '-') {
      printf("No arguments required \n");
      Usage();
      return false;
    }
  }
  return true;
}

int
main(int argc, char *argv[])
{
  int ret;
  int opt, option_index;

  if (signal(SIGINT, afxdp_signal_handler) == SIG_ERR) {
    perror("signal(SIGINT)");
    exit(EXIT_FAILURE);
  }
  if (signal(SIGTERM, afxdp_signal_handler) == SIG_ERR) {
    perror("signal(SIGTERM)");
    exit(EXIT_FAILURE);
  }

  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) >=
         0) {

    switch (opt) {
    case 0:
      if (parse_no_arg_cli(option_index, optind, argv)) {
        parse_long_opts(option_index, optarg);
      }
      break;

    case '?':
    default:
      fprintf(stderr, "Invalid option %s\n", argv[optind - 1]);
      Usage();
      break;
    }
  }

  if (!vrouter_host) {
    vrouter_host = vrouter_get_host();

    ret = vr_afxdp_table_mem_init(VR_MEM_FLOW_TABLE_OBJECT,
                                  vr_flow_entries,
                                  VR_FLOW_TABLE_SIZE,
                                  vr_oflow_entries,
                                  VR_OFLOW_TABLE_SIZE);
    if (ret < 0) {
      fprintf(stderr,
              "Error initializing flow table: %s (%d)\n",
              strerror(-ret),
              -ret);
      return ret;
    }

    ret = vr_afxdp_table_mem_init(VR_MEM_BRIDGE_TABLE_OBJECT,
                                  vr_bridge_entries,
                                  VR_BRIDGE_TABLE_SIZE,
                                  vr_bridge_oentries,
                                  VR_BRIDGE_OFLOW_TABLE_SIZE);
    if (ret < 0) {
      fprintf(stderr,
              "Error initializing bridge table: %s (%d)\n",
              strerror(-ret),
              -ret);
      return ret;
    }

    if (vr_afxdp_bridge_init())
      return -1;
    if (vr_afxdp_flow_init())
      return -1;
  }

  if (vrouter_init() != 0) {
    fprintf(stderr, "vrouter_init() failed.\n");
    exit(EXIT_FAILURE);
  }

  if (vr_sandesh_init() != 0) {
    fprintf(stderr, "vr_sandesh_init() failed.\n");
    exit(EXIT_FAILURE);
  }

  if (afxdp_init() != 0) {
    fprintf(stderr, "AF_XDP initialization failed\n");
    exit(EXIT_FAILURE);
  }

  ret = afxdp_spawn_threads();
  if (ret != 0) {
    perror("pthread_create(netlink)");
    exit(EXIT_FAILURE);
  }

  while (!afxdp_stop) {
    sleep(1);
  }

  afxdp_stop_threads();

  fprintf(stdout, "Exiting AF_XDP vRouter skeleton\n");
  return 0;
}
