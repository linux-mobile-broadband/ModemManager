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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_AT_H
#define MM_AT_H

#include <gio/gio.h>

#include "mm-at-serial-port.h"

/*
 * The expected result depends on the specific operation, so the GVariant
 * created by the response processor needs to match the signature given
 * in this setup.
 *
 * TRUE must be returned when the operation is to be considered successful,
 * and a result (NULL allowed, if no signature specified) is given.
 *
 * FALSE must be returned when:
 *  - A GError is propagated into result_error, which will be treated as a
 *    critical error and therefore the operation will be aborted.
 *  - When no result_error is given, to instruct the operation to go on with
 *    the next scheduled command.
 *
 * This setup, therefore allows:
 *  - Running a single command and processing its result.
 *  - Running a set of N commands, providing a global result after all have
 *    been executed.
 *  - Running a set of N commands out of M (N<M), where the global result is
 *    obtained without having executed all configured commands.
 */
typedef gboolean (* MMAtResponseProcessor) (GObject *owner,
                                            gpointer response_processor_context,
                                            const gchar *command,
                                            const gchar *response,
                                            const GError *error,
                                            GVariant **result,
                                            GError **result_error);

/* Struct to configure AT command operations */
typedef struct {
    /* The AT command */
    gchar *command;
    /* Timeout of the command, in seconds */
    guint timeout;
    /* The response processor */
    MMAtResponseProcessor response_processor;
} MMAtCommand;

void     mm_at_sequence         (GObject *owner,
                                 MMAtSerialPort *port,
                                 MMAtCommand *sequence,
                                 gpointer response_processor_context,
                                 gboolean free_sequence,
                                 const gchar *result_signature,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
GVariant *mm_at_sequence_finish (GObject *owner,
                                 GAsyncResult *res,
                                 GError **error);

void     mm_at_command         (GObject *owner,
                                MMAtSerialPort *port,
                                const gchar *command,
                                guint timeout,
                                const MMAtResponseProcessor response_processor,
                                gpointer response_processor_context,
                                const gchar *result_signature,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
GVariant *mm_at_command_finish (GObject *owner,
                                GAsyncResult *res,
                                GError **error);

#endif /* MM_AT_H */

