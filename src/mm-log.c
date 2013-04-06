/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2011 Red Hat, Inc.
 */

#define _GNU_SOURCE
#include <config.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#if defined WITH_QMI
#include <libqmi-glib.h>
#endif

#if defined WITH_MBIM
#include <libmbim-glib.h>
#endif

#include "mm-log.h"

enum {
    TS_FLAG_NONE = 0,
    TS_FLAG_WALL,
    TS_FLAG_REL
};

static gboolean ts_flags = TS_FLAG_NONE;
static guint32 log_level = LOGL_INFO | LOGL_WARN | LOGL_ERR;
static GTimeVal rel_start = { 0, 0 };
static int logfd = -1;
static gboolean func_loc = FALSE;

typedef struct {
    guint32 num;
    const char *name;
} LogDesc;

static const LogDesc level_descs[] = {
    { LOGL_ERR, "ERR" },
    { LOGL_WARN | LOGL_ERR, "WARN" },
    { LOGL_INFO | LOGL_WARN | LOGL_ERR, "INFO" },
    { LOGL_DEBUG | LOGL_INFO | LOGL_WARN | LOGL_ERR, "DEBUG" },
    { 0, NULL }
};

void
_mm_log (const char *loc,
         const char *func,
         guint32 level,
         const char *fmt,
         ...)
{
    va_list args;
    char *msg;
    GTimeVal tv;
    char tsbuf[100] = { 0 };
    char msgbuf[512] = { 0 };
    int syslog_priority = LOG_INFO;
    const char *prefix = NULL;
    ssize_t ign;

    if (!(log_level & level))
        return;

    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);

    if (ts_flags == TS_FLAG_WALL) {
        g_get_current_time (&tv);
        snprintf (&tsbuf[0], sizeof (tsbuf), " [%09ld.%06ld]", tv.tv_sec, tv.tv_usec);
    } else if (ts_flags == TS_FLAG_REL) {
        glong secs;
        glong usecs;

        g_get_current_time (&tv);
        secs = tv.tv_sec - rel_start.tv_sec;
        usecs = tv.tv_usec - rel_start.tv_usec;
        if (usecs < 0) {
            secs--;
            usecs += 1000000;
        }

        snprintf (&tsbuf[0], sizeof (tsbuf), " [%06ld.%06ld]", secs, usecs);
    }

    if ((log_level & LOGL_DEBUG) && (level == LOGL_DEBUG))
        prefix = "<debug>";
    else if ((log_level & LOGL_INFO) && (level == LOGL_INFO))
        prefix = "<info> ";
    else if ((log_level & LOGL_WARN) && (level == LOGL_WARN)) {
        prefix = "<warn> ";
        syslog_priority = LOG_WARNING;
    } else if ((log_level & LOGL_ERR) && (level == LOGL_ERR)) {
        prefix = "<error>";
        syslog_priority = LOG_ERR;
    } else
        g_warn_if_reached ();

    if (prefix) {
        if (func_loc && log_level & LOGL_DEBUG)
            snprintf (msgbuf, sizeof (msgbuf), "%s%s [%s] %s(): %s\n", prefix, tsbuf, loc, func, msg);
        else
            snprintf (msgbuf, sizeof (msgbuf), "%s%s %s\n", prefix, tsbuf, msg);

        if (logfd < 0)
            syslog (syslog_priority, "%s", msgbuf);
        else {
            ign = write (logfd, msgbuf, strlen (msgbuf));
            if (ign) {} /* whatever; really shut up about unused result */

            fsync (logfd);  /* Make sure output is dumped to disk immediately */
        }
    }

    g_free (msg);
}

static void
log_handler (const gchar *log_domain,
             GLogLevelFlags level,
             const gchar *message,
             gpointer ignored)
{
    int syslog_priority;
    ssize_t ign;

    switch (level) {
    case G_LOG_LEVEL_ERROR:
        syslog_priority = LOG_CRIT;
        break;
    case G_LOG_LEVEL_CRITICAL:
        syslog_priority = LOG_ERR;
        break;
    case G_LOG_LEVEL_WARNING:
        syslog_priority = LOG_WARNING;
        break;
    case G_LOG_LEVEL_MESSAGE:
        syslog_priority = LOG_NOTICE;
        break;
    case G_LOG_LEVEL_DEBUG:
        syslog_priority = LOG_DEBUG;
        break;
    case G_LOG_LEVEL_INFO:
    default:
        syslog_priority = LOG_INFO;
        break;
    }

    if (logfd < 0)
        syslog (syslog_priority, "%s", message);
    else {
        ign = write (logfd, message, strlen (message));
        if (ign) {} /* whatever; really shut up about unused result */
    }
}

gboolean
mm_log_set_level (const char *level, GError **error)
{
    gboolean found = FALSE;
    const LogDesc *diter;

    for (diter = &level_descs[0]; diter->name; diter++) {
        if (!strcasecmp (diter->name, level)) {
            log_level = diter->num;
            found = TRUE;
            break;
        }
    }

    if (!found)
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Unknown log level '%s'", level);

#if defined WITH_QMI
    qmi_utils_set_traces_enabled (log_level & LOGL_DEBUG ? TRUE : FALSE);
#endif

#if defined WITH_MBIM
    mbim_utils_set_traces_enabled (log_level & LOGL_DEBUG ? TRUE : FALSE);
#endif

    return found;
}

gboolean
mm_log_setup (const char *level,
              const char *log_file,
              gboolean show_timestamps,
              gboolean rel_timestamps,
              gboolean debug_func_loc,
              GError **error)
{
    /* levels */
    if (level && strlen (level) && !mm_log_set_level (level, error))
        return FALSE;

    func_loc = debug_func_loc;

    if (show_timestamps)
        ts_flags = TS_FLAG_WALL;
    else if (rel_timestamps)
        ts_flags = TS_FLAG_REL;

    /* Grab start time for relative timestamps */
    g_get_current_time (&rel_start);

    if (log_file == NULL)
        openlog (G_LOG_DOMAIN, LOG_CONS | LOG_PID | LOG_PERROR, LOG_DAEMON);
    else {
        logfd = open (log_file,
                      O_CREAT | O_APPEND | O_WRONLY,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (logfd < 0) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't open log file: (%d) %s",
                         errno, strerror (errno));
            return FALSE;
        }
    }

    g_log_set_handler (G_LOG_DOMAIN,
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);

#if defined WITH_QMI
    g_log_set_handler ("Qmi",
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);
#endif

#if defined WITH_MBIM
    g_log_set_handler ("Mbim",
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);
#endif

    return TRUE;
}

void
mm_log_shutdown (void)
{
    if (logfd < 0)
        closelog ();
    else
        close (logfd);
}
