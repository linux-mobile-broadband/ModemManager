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
 * Copyright (C) 2011-2020 Red Hat, Inc.
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc.
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
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#if defined WITH_QMI
#include <libqmi-glib.h>
#endif

#if defined WITH_MBIM
#include <libmbim-glib.h>
#endif

#if defined WITH_SYSTEMD_JOURNAL
#define SD_JOURNAL_SUPPRESS_LOCATION
#include <systemd/sd-journal.h>
#endif

#include "mm-log.h"
#include "mm-log-object.h"

enum {
    TS_FLAG_NONE = 0,
    TS_FLAG_WALL,
    TS_FLAG_REL
};

static gboolean ts_flags = TS_FLAG_NONE;
static guint32  log_level = MM_LOG_LEVEL_MSG | MM_LOG_LEVEL_WARN | MM_LOG_LEVEL_ERR;
static GTimeVal rel_start = { 0, 0 };
static int      logfd = -1;
static gboolean append_log_level_text = TRUE;
static gboolean personal_info = FALSE;

static void (*log_backend) (const char *loc,
                            const char *func,
                            int         syslog_level,
                            const char *message,
                            size_t      length);

typedef struct {
    guint32 num;
    const char *name;
} LogDesc;

static const LogDesc level_descs[] = {
    { MM_LOG_LEVEL_ERR, "ERR" },
    { MM_LOG_LEVEL_WARN  | MM_LOG_LEVEL_ERR, "WARN" },
    { MM_LOG_LEVEL_MSG   | MM_LOG_LEVEL_WARN | MM_LOG_LEVEL_ERR, "MSG" },
    { MM_LOG_LEVEL_INFO  | MM_LOG_LEVEL_MSG  | MM_LOG_LEVEL_WARN | MM_LOG_LEVEL_ERR, "INFO" },
    { MM_LOG_LEVEL_DEBUG | MM_LOG_LEVEL_INFO | MM_LOG_LEVEL_MSG  | MM_LOG_LEVEL_WARN | MM_LOG_LEVEL_ERR, "DEBUG" },
    { 0, NULL }
};

static GString *msgbuf = NULL;
static gsize msgbuf_once = 0;

static int
mm_to_syslog_priority (MMLogLevel level)
{
    switch (level) {
    case MM_LOG_LEVEL_ERR:
        return LOG_ERR;
    case MM_LOG_LEVEL_WARN:
        return LOG_WARNING;
    case MM_LOG_LEVEL_MSG:
        return LOG_NOTICE;
    case MM_LOG_LEVEL_INFO:
        return LOG_INFO;
    case MM_LOG_LEVEL_DEBUG:
        return LOG_DEBUG;
    default:
        break;
    }
    g_assert_not_reached ();
    return 0;
}

static MMLogLevel
glib_level_to_mm_level (GLogLevelFlags level)
{
    /* if the log was flagged as fatal (e.g. G_DEBUG=fatal-warnings), ignore
     * the fatal flag for logging purposes */
    if (level & G_LOG_FLAG_FATAL)
        level &= ~G_LOG_FLAG_FATAL;

    switch (level) {
    case G_LOG_LEVEL_ERROR:
    case G_LOG_LEVEL_CRITICAL:
        return MM_LOG_LEVEL_ERR;
    case G_LOG_LEVEL_WARNING:
        return MM_LOG_LEVEL_WARN;
    case G_LOG_LEVEL_MESSAGE:
        return MM_LOG_LEVEL_MSG;
    case G_LOG_LEVEL_INFO:
        return MM_LOG_LEVEL_INFO;
    case G_LOG_LEVEL_DEBUG:
        return MM_LOG_LEVEL_DEBUG;
    case G_LOG_LEVEL_MASK:
    case G_LOG_FLAG_FATAL:
    case G_LOG_FLAG_RECURSION:
    default:
        g_assert_not_reached ();
    }
}

static const char *
log_level_description (MMLogLevel level)
{
    switch (level) {
    case MM_LOG_LEVEL_ERR:
        return "<err>";
    case MM_LOG_LEVEL_WARN:
        return "<wrn>";
    case MM_LOG_LEVEL_MSG:
        return "<msg>";
    case MM_LOG_LEVEL_INFO:
        return "<inf>";
    case MM_LOG_LEVEL_DEBUG:
        return "<dbg>";
    default:
        break;
    }
    g_assert_not_reached ();
    return NULL;
}

static void
log_backend_file (const char *loc,
                  const char *func,
                  int         syslog_level,
                  const char *message,
                  size_t      length)
{
    ssize_t ign;

    ign = write (logfd, message, length);
    if (ign) {} /* whatever; really shut up about unused result */

    fsync (logfd);  /* Make sure output is dumped to disk immediately  */
}

static void
log_backend_syslog (const char *loc,
                    const char *func,
                    int         syslog_level,
                    const char *message,
                    size_t      length)
{
    syslog (syslog_level, "%s", message);
}

#if defined WITH_SYSTEMD_JOURNAL
static void
log_backend_systemd_journal (const char *loc,
                             const char *func,
                             int         syslog_level,
                             const char *message,
                             size_t      length)
{
    const char *line;
    size_t      file_length;

    if (loc == NULL) {
        sd_journal_send ("MESSAGE=%s",  message,
                         "PRIORITY=%d", syslog_level,
                         NULL);
        return;
    }

    line = strstr (loc, ":");
    if (line) {
        file_length = line - loc;
        line++;
    } else {
        /* This is not supposed to happen but we must be prepared for this */
        line = loc;
        file_length = 0;
    }

    sd_journal_send ("MESSAGE=%s",     message,
                     "PRIORITY=%d",    syslog_level,
                     "CODE_FUNC=%s",   func,
                     "CODE_FILE=%.*s", file_length, loc,
                     "CODE_LINE=%s",   line,
                     NULL);
}
#endif

gboolean
mm_log_get_show_personal_info (void)
{
    return personal_info;
}

gboolean
mm_log_check_level_enabled (MMLogLevel level)
{
    return (log_level & level);
}

void
_mm_log (gpointer     obj,
         const gchar *module,
         const gchar *loc,
         const gchar *func,
         MMLogLevel   level,
         const gchar *fmt,
         ...)
{
    va_list  args;
    GTimeVal tv;

    if (!mm_log_check_level_enabled (level))
        return;

    if (g_once_init_enter (&msgbuf_once)) {
        msgbuf = g_string_sized_new (512);
        g_once_init_leave (&msgbuf_once, 1);
    } else
        g_string_truncate (msgbuf, 0);

    if (append_log_level_text)
        g_string_append_printf (msgbuf, "%s ", log_level_description (level));

    if (ts_flags == TS_FLAG_WALL) {
        g_get_current_time (&tv);
        g_string_append_printf (msgbuf, "[%09ld.%06ld] ", tv.tv_sec, tv.tv_usec);
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

        g_string_append_printf (msgbuf, "[%06ld.%06ld] ", secs, usecs);
    }

#if defined MM_LOG_FUNC_LOC
    if (loc && func)
        g_string_append_printf (msgbuf, "[%s] %s(): ", loc, func);
#endif

    if (obj)
        g_string_append_printf (msgbuf, "[%s] ", mm_log_object_get_id (MM_LOG_OBJECT (obj)));
    if (module)
        g_string_append_printf (msgbuf, "(%s) ", module);

    va_start (args, fmt);
    g_string_append_vprintf (msgbuf, fmt, args);
    va_end (args);

    g_string_append_c (msgbuf, '\n');

    log_backend (loc, func, mm_to_syslog_priority (level), msgbuf->str, msgbuf->len);
}

static void
log_handler (const gchar    *log_domain,
             GLogLevelFlags  glib_level,
             const gchar    *message,
             gpointer        ignored)
{
    _mm_log (NULL, /* obj */
             NULL, /* module */
             NULL, /* loc */
             NULL, /* func */
             glib_level_to_mm_level (glib_level),
             "%s",
             message);
}

gboolean
mm_log_set_level (const gchar  *level,
                  GError      **error)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (level_descs); i++) {
        if (!g_ascii_strcasecmp (level_descs[i].name, level)) {
            log_level = level_descs[i].num;
            break;
        }
    }

    if (i == G_N_ELEMENTS (level_descs)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Unknown log level '%s'", level);
        return FALSE;
    }

#if defined WITH_QMI
    qmi_utils_set_traces_enabled (log_level & MM_LOG_LEVEL_DEBUG ? TRUE : FALSE);
#endif

#if defined WITH_MBIM
    mbim_utils_set_traces_enabled (log_level & MM_LOG_LEVEL_DEBUG ? TRUE : FALSE);
#endif

    return TRUE;
}

gboolean
mm_log_setup (const gchar  *level,
              const gchar  *log_file,
              gboolean      log_journal,
              gboolean      show_timestamps,
              gboolean      rel_timestamps,
              gboolean      show_personal_info,
              GError      **error)
{
    /* levels */
    if (level && strlen (level) && !mm_log_set_level (level, error))
        return FALSE;

    personal_info = show_personal_info;

    if (show_timestamps)
        ts_flags = TS_FLAG_WALL;
    else if (rel_timestamps)
        ts_flags = TS_FLAG_REL;

    /* Grab start time for relative timestamps */
    g_get_current_time (&rel_start);

#if defined WITH_SYSTEMD_JOURNAL
    if (log_journal) {
        log_backend = log_backend_systemd_journal;
        append_log_level_text = FALSE;
    } else
#endif
    if (!log_file) {
        openlog (G_LOG_DOMAIN, LOG_CONS | LOG_PID | LOG_PERROR, LOG_DAEMON);
        log_backend = log_backend_syslog;
    } else {
        logfd = open (log_file,
                      O_CREAT | O_APPEND | O_WRONLY,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (logfd < 0) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't open log file: (%d) %s",
                         errno, strerror (errno));
            return FALSE;
        }
        log_backend = log_backend_file;
    }

    g_log_set_handler (G_LOG_DOMAIN,
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);

#if defined WITH_QMI
    qmi_utils_set_show_personal_info (show_personal_info);
    g_log_set_handler ("Qmi",
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);
#endif

#if defined WITH_QRTR
    g_log_set_handler ("Qrtr",
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);
#endif

#if defined WITH_MBIM
    mbim_utils_set_show_personal_info (show_personal_info);
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

/******************************************************************************/

const gchar *
mm_log_str_personal_info (const gchar *str)
{
    return mm_common_str_personal_info (str, personal_info);
}
