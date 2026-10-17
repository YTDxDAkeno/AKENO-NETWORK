/*
 * Connection checks of the native app, all read-only:
 *   1. the local AKENO daemon: GET http://127.0.0.1:<port>/api/instance (existing, unauthenticated
 *      liveness answer; nothing else on the daemon is called),
 *   2. the server address in the daemon's settings and the presence of its credential file,
 *   3. GET /api/v1/devices/current with the stored device token (existing endpoint), through the
 *      existing client library. The credential file is never written, renamed or deleted, and
 *      no realtime connection is opened.
 * Blocking (seconds at most); call from a worker thread.
 */
#ifndef AKN_NET_H
#define AKN_NET_H

#include "account.h"
#include "log.h"
#include "akn_platform.h"

/* Probes the daemon; fills view->daemon and view->daemon_version. */
void akn_net_probe_daemon(int port, int timeout_ms, akn_account_view *view);

/* Runs all three checks into view (its counters are kept). */
void akn_net_check(const akn_platform_info *platform, akn_account_view *view, akn_log *log);

#endif
