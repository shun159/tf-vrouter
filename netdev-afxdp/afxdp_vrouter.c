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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "afxdp_interface.h"

static volatile int afxdp_stop = 0;

static void afxdp_signal_handler(int signum) {
  fprintf(stdout, "Signal %d received, stopping...\n", signum);
  afxdp_stop = 1;
}

int main(int argc, char *argv[]) {
  if (signal(SIGINT, afxdp_signal_handler) == SIG_ERR) {
    perror("signal(SIGINT)");
    exit(EXIT_FAILURE);
  }
  if (signal(SIGTERM, afxdp_signal_handler) == SIG_ERR) {
    perror("signal(SIGTERM)");
    exit(EXIT_FAILURE);
  }

  if (afxdp_init() != 0) {
    fprintf(stderr, "AF_XDP initialization failed\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stdout, "Exiting AF_XDP vRouter skeleton\n");
  return 0;
}
