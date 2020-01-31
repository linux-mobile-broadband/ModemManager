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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_PORT_PROBE_AT_H
#define MM_PORT_PROBE_AT_H

#include <glib.h>

/* The response processor. The expected result depends on the
 * probing type:
 *  - AT             --> Boolean
 *  - Vendor         --> String
 *  - Product        --> String
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
 * A special case to consider is the initialization commands, used by
 * some plugins. In this case, there is no expected result, but plugins may
 * set an optional boolean result, specifying whether the port is an AT port
 * or not.
 *  - Initialization --> None or Boolean
 * When the initialization is considered enough, TRUE is returned, and
 * FALSE otherwise.
 */
typedef gboolean (* MMPortProbeAtResponseProcessor) (const gchar *command,
                                                     const gchar *response,
                                                     gboolean last_command,
                                                     const GError *error,
                                                     GVariant **result,
                                                     GError **result_error);

/* Struct to configure port probing commands */
typedef struct {
    /* The AT command */
    const gchar *command;
    /* Timeout of the command, in seconds */
    guint timeout;
    /* The response processor */
    MMPortProbeAtResponseProcessor response_processor;
} MMPortProbeAtCommand;

/* Common helper response processors */

/* Every string received as response, will be set as result */
gboolean mm_port_probe_response_processor_string (const gchar *command,
                                                  const gchar *response,
                                                  gboolean last_command,
                                                  const GError *error,
                                                  GVariant **result,
                                                  GError **result_error);
/* Generic response parser for AT probing, useful also in custom init commands */
gboolean mm_port_probe_response_processor_is_at (const gchar *command,
                                                 const gchar *response,
                                                 gboolean last_command,
                                                 const GError *error,
                                                 GVariant **result,
                                                 GError **result_error);

#endif /* MM_PORT_PROBE_AT_H */
