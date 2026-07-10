/* SPDX-License-Identifier: GPL-2.0 */
#include "common.h"
#include <stdarg.h>

static bool use_syslog = false;

void log_init(bool syslog_enabled)
{
    use_syslog = syslog_enabled;
    if (use_syslog)
        openlog("atri-main", LOG_PID, LOG_DAEMON);
}

void log_close(void)
{
    if (use_syslog)
        closelog();
}

void log_msg(int priority, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (use_syslog) {
        vsyslog(priority, fmt, ap);
    } else {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
    va_end(ap);
}
