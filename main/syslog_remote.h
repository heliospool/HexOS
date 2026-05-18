#pragma once

#include <stdarg.h>

// Initialise the remote syslog sender.
// If host is NULL or empty, the sender is disabled.
void syslog_remote_init(const char *host);

// Reconfigure the remote syslog host at runtime (e.g. after settings change).
void syslog_remote_set_host(const char *host);

// Send a pre-formatted log line to the syslog server.
// Safe to call from any task; no-op when disabled.
void syslog_remote_send(const char *line);
