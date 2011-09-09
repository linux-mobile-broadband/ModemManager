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

#ifndef MM_PORT_PROBE_AT_COMMAND_H
#define MM_PORT_PROBE_AT_COMMAND_H

#include <glib.h>

/* Struct to configure port probing commands */
typedef struct {
    /* The AT command */
    const gchar *command;

    /* The response processor. The expected result depends on the
     * probing type:
     *  - AT             --> G_TYPE_BOOLEAN
     *  - Capabilities   --> G_TYPE_UINT
     *  - Vendor         --> G_TYPE_STRING
     *  - Product        --> G_TYPE_STRING
     * When a result is given, TRUE is returned.
     * When no result is given, FALSE is returned and:
     *   - If result_error given, it should be treated as a critical error,
     *     and abort the probing.
     *   - If no result_error is given, we can just go on to the next command
     *     in the group.
     *
     * A special case to consider is the initialization commands, used by
     * some plugins. In this case, there is no expected result, but plugins may
     * set an optional boolean result, specifying whether the port is an AT port
     * or not.
     *  - Initialization --> NONE | G_TYPE_BOOLEAN
     * When the initialization is considered enough, TRUE is returned, and
     * FALSE otherwise.
     */
    gboolean (* response_processor) (const gchar *response,
                                     const GError *error,
                                     GValue *result,
                                     GError **result_error);
} MMPortProbeAtCommand;

/* Default commands used during probing */
const MMPortProbeAtCommand *mm_port_probe_at_command_get_probing (void);

#endif /* MM_PORT_PROBE_AT_COMMAND_H */

