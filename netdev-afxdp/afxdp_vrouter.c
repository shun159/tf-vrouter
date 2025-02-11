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

#include "afxdp_netlink.h"
#include "afxdp_interface.h"
#include "vr_afxdp.h"
#include "afxdp_host.h"
#include "vrouter.h"
#include "vr_sandesh.h"

/* Max number of CPUs. We adjust it later in vr_dpdk_host_init() */
unsigned int vr_num_cpus = 1;

static volatile int afxdp_stop = 0;
static int no_daemon_set = 0;

extern char *ContrailBuildInfo;

struct vr_afxdp_global vr_afxdp;

// command line options
static struct option long_options[] = {
    {"no-daemon", no_argument, &no_daemon_set, 1},
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'v'},
    {0, 0, 0, 0},
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
         "  --version     Print version information\n");
  exit(1);
}

static void
afxdp_signal_handler(int signum)
{
  fprintf(stdout, "Signal %d received, stopping...\n", signum);
  afxdp_stop = 1;
}

int
main(int argc, char *argv[])
{
  int opt, option_index = 0;

  if (signal(SIGINT, afxdp_signal_handler) == SIG_ERR) {
    perror("signal(SIGINT)");
    exit(EXIT_FAILURE);
  }
  if (signal(SIGTERM, afxdp_signal_handler) == SIG_ERR) {
    perror("signal(SIGTERM)");
    exit(EXIT_FAILURE);
  }

  while ((opt = getopt_long(argc, argv, "hv", long_options, &option_index)) != -1) {
    switch (opt) {
    case 0:
      break;
    case 'h':
      Usage();
      break;
    case 'v':
      version_print();
      exit(0);
      break;
    default:
      Usage();
      break;
    }
  }

  if (!vrouter_host) {
    vrouter_host = vrouter_get_host();
  }

  int ret;

  ret = vrouter_init();
  if (ret)
    return ret;

  ret = vr_sandesh_init();
  if (ret)
    return ret;

  vr_afxdp_netlink_loop();

  if (afxdp_init() != 0) {
    fprintf(stderr, "AF_XDP initialization failed\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stdout, "Exiting AF_XDP vRouter skeleton\n");
  return 0;
}
