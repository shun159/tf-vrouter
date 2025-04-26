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

#include <stdio.h>
#include <pthread.h>

#include "vr_afxdp.h"
#include "afxdp_thread.h"
#include "afxdp_netlink.h"
#include "afxdp_usocket.h"
#include "afxdp_ethdev.h"

#define NUM_STATIC_THREADS 1
static struct afxdp_thread g_static_threads[NUM_STATIC_THREADS];

#define NUM_DYNAMIC_THREADS_MAX (VR_AFXDP_THREAD_MAX + NUM_FWD_THREADS)
static struct afxdp_thread   *g_dyn_threads[NUM_DYNAMIC_THREADS_MAX];
static afxdp_thread_func_t    g_dyn_funcs  [NUM_DYNAMIC_THREADS_MAX];
static void                  *g_dyn_args   [NUM_DYNAMIC_THREADS_MAX];
static int                    g_dyn_cnt;

static void *afxdp_netlink_thread_func(void *arg)
{
    struct afxdp_thread *ti = arg;

    while (!ti->is_thr_stop) {
        if (vr_afxdp_netlink_init() == 0)
            vr_usocket_io(vr_afxdp.netlink_sock);

        if (ti->is_thr_stop) break;
        usleep(VR_AFXDP_SLEEP_SERVICE_US);
    }
    fprintf(stderr, "Netlink thread: exiting.\n");
    return NULL;
}

void *afxdp_rx_thread_func(void *arg)
{
    struct afxdp_rx_arg *rx = arg;

    while (!rx->ctrl->is_thr_stop)
        afxdp_recv(rx->vif, rx->queue_id);

    return NULL;
}

static void spawn_static_threads(void)
{
    /* Slot 0 ＝ Netlink */
    g_static_threads[0].thr_type   = VR_AFXDP_THREAD_NETLINK;
    g_static_threads[0].is_thr_stop = false;

    pthread_create(&g_static_threads[0].thr_id, NULL,
                   afxdp_netlink_thread_func, &g_static_threads[0]);
}

static void stop_static_threads(void)
{
    for (int i = 0; i < NUM_STATIC_THREADS; i++)
        g_static_threads[i].is_thr_stop = true;
    for (int i = 0; i < NUM_STATIC_THREADS; i++)
        pthread_join(g_static_threads[i].thr_id, NULL);
}

struct afxdp_thread *
spawn_dynamic_thread(afxdp_thread_func_t func, void *arg, afxdp_thread_type_t type)
{
    if (g_dyn_cnt >= NUM_DYNAMIC_THREADS_MAX)
        return NULL;

    struct afxdp_thread *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;

    t->thr_type    = type;
    t->is_thr_stop = false;

    pthread_create(&t->thr_id, NULL, func, arg ? arg : t);

    g_dyn_threads[g_dyn_cnt] = t;
    g_dyn_funcs  [g_dyn_cnt] = func;
    g_dyn_args   [g_dyn_cnt] = arg ? arg : t;
    g_dyn_cnt++;
    return t;
}

static void stop_dynamic_threads(void)
{
    for (int i = 0; i < g_dyn_cnt; i++)
        g_dyn_threads[i]->is_thr_stop = true;
    for (int i = 0; i < g_dyn_cnt; i++)
        pthread_join(g_dyn_threads[i]->thr_id, NULL);
    for (int i = 0; i < g_dyn_cnt; i++)
        free(g_dyn_threads[i]);
    g_dyn_cnt = 0;
}

int afxdp_spawn_threads(void)
{
    spawn_static_threads(); 
    return 0;
}

void afxdp_stop_threads(void)
{
    stop_dynamic_threads();
    stop_static_threads();
}
