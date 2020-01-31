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

#ifndef MM_BASE_MODEM_AT_H
#define MM_BASE_MODEM_AT_H

#include <gio/gio.h>

#include "mm-base-modem.h"
#include "mm-port-serial-at.h"

/*
 * The expected result depends on the specific operation, so the GVariant
 * created by the response processor needs to match the one expected in
 * finish().
 *
 * TRUE must be returned when the operation is to be considered successful,
 * and a result may be given.
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
typedef gboolean (* MMBaseModemAtResponseProcessor) (MMBaseModem *self,
                                                     gpointer response_processor_context,
                                                     const gchar *command,
                                                     const gchar *response,
                                                     gboolean last_command,
                                                     const GError *error,
                                                     GVariant **result,
                                                     GError **result_error);

/* Struct to configure AT command operations (constant) */
typedef struct {
    /* The AT command */
    const gchar *command;
    /* Timeout of the command, in seconds */
    guint timeout;
    /* Flag to allow cached replies */
    gboolean allow_cached;
    /* The response processor */
    MMBaseModemAtResponseProcessor response_processor;
} MMBaseModemAtCommand;

/* Generic AT sequence handling, using the best AT port available and without
 * explicit cancellations. */
void     mm_base_modem_at_sequence         (MMBaseModem *self,
                                            const MMBaseModemAtCommand *sequence,
                                            gpointer response_processor_context,
                                            GDestroyNotify response_processor_context_free,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
GVariant *mm_base_modem_at_sequence_finish (MMBaseModem *self,
                                            GAsyncResult *res,
                                            gpointer *response_processor_context,
                                            GError **error);

/* Fully detailed AT sequence handling, when specific AT port and/or explicit
 * cancellations need to be used. */
void     mm_base_modem_at_sequence_full         (MMBaseModem *self,
                                                 MMPortSerialAt *port,
                                                 const MMBaseModemAtCommand *sequence,
                                                 gpointer response_processor_context,
                                                 GDestroyNotify response_processor_context_free,
                                                 GCancellable *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
GVariant *mm_base_modem_at_sequence_full_finish (MMBaseModem *self,
                                                 GAsyncResult *res,
                                                 gpointer *response_processor_context,
                                                 GError **error);

/* Common helper response processors */

/* Every string received as response, will be set as result */
gboolean mm_base_modem_response_processor_string (MMBaseModem *self,
                                                  gpointer none,
                                                  const gchar *command,
                                                  const gchar *response,
                                                  gboolean last_command,
                                                  const GError *error,
                                                  GVariant **result,
                                                  GError **result_error);
/* Just abort if error without result set, otherwise finish sequence */
gboolean mm_base_modem_response_processor_no_result (MMBaseModem *self,
                                                     gpointer none,
                                                     const gchar *command,
                                                     const gchar *response,
                                                     gboolean last_command,
                                                     const GError *error,
                                                     GVariant **result,
                                                     GError **result_error);
/* Just abort if error without result set, otherwise continue sequence */
gboolean mm_base_modem_response_processor_no_result_continue (MMBaseModem *self,
                                                              gpointer none,
                                                              const gchar *command,
                                                              const gchar *response,
                                                              gboolean last_command,
                                                              const GError *error,
                                                              GVariant **result,
                                                              GError **result_error);
/* If error, continue sequence, otherwise finish it */
gboolean mm_base_modem_response_processor_continue_on_error (MMBaseModem *self,
                                                             gpointer none,
                                                             const gchar *command,
                                                             const gchar *response,
                                                             gboolean last_command,
                                                             const GError *error,
                                                             GVariant **result,
                                                             GError **result_error);

/* Generic AT command handling, using the best AT port available and without
 * explicit cancellations. */
void mm_base_modem_at_command                (MMBaseModem *self,
                                              const gchar *command,
                                              guint timeout,
                                              gboolean allow_cached,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
/* Like mm_base_modem_at_command() except does not prefix with AT */
void mm_base_modem_at_command_raw            (MMBaseModem *self,
                                              const gchar *command,
                                              guint timeout,
                                              gboolean allow_cached,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
const gchar *mm_base_modem_at_command_finish (MMBaseModem *self,
                                              GAsyncResult *res,
                                              GError **error);

/* Fully detailed AT command handling, when specific AT port and/or explicit
 * cancellations need to be used. */
void mm_base_modem_at_command_full                (MMBaseModem *self,
                                                   MMPortSerialAt *port,
                                                   const gchar *command,
                                                   guint timeout,
                                                   gboolean allow_cached,
                                                   gboolean is_raw,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
const gchar *mm_base_modem_at_command_full_finish (MMBaseModem *self,
                                                   GAsyncResult *res,
                                                   GError **error);

/******************************************************************************/
/* Support for MMBaseModemAtCommand with heap allocated contents */

/* Exactly same format as MMBaseModemAtCommand, just without
 * a constant command string. */
typedef struct {
    gchar    *command;
    guint     timeout;
    gboolean  allow_cached;
    MMBaseModemAtResponseProcessor response_processor;
} MMBaseModemAtCommandAlloc;

G_STATIC_ASSERT (sizeof (MMBaseModemAtCommandAlloc) == sizeof (MMBaseModemAtCommand));
G_STATIC_ASSERT (G_STRUCT_OFFSET (MMBaseModemAtCommandAlloc, command)            == G_STRUCT_OFFSET (MMBaseModemAtCommand, command));
G_STATIC_ASSERT (G_STRUCT_OFFSET (MMBaseModemAtCommandAlloc, timeout)            == G_STRUCT_OFFSET (MMBaseModemAtCommand, timeout));
G_STATIC_ASSERT (G_STRUCT_OFFSET (MMBaseModemAtCommandAlloc, allow_cached)       == G_STRUCT_OFFSET (MMBaseModemAtCommand, allow_cached));
G_STATIC_ASSERT (G_STRUCT_OFFSET (MMBaseModemAtCommandAlloc, response_processor) == G_STRUCT_OFFSET (MMBaseModemAtCommand, response_processor));

void mm_base_modem_at_command_alloc_clear (MMBaseModemAtCommandAlloc *command);

#endif /* MM_BASE_MODEM_AT_H */
