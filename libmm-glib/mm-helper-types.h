/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <ModemManager.h>
#include <glib.h>

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#ifndef _MM_HELPER_TYPES_H_
#define _MM_HELPER_TYPES_H_

/**
 * MMModemModeCombination:
 * @allowed: Mask of #MMModemMode values specifying allowed modes.
 * @preferred: A single #MMModemMode value specifying the preferred mode.
 *
 * #MMModemModeCombination is a simple struct holding a pair of #MMModemMode
 * values.
 *
 * Since: 1.0
 */
typedef struct _MMModemModeCombination MMModemModeCombination;
struct _MMModemModeCombination {
    MMModemMode allowed;
    MMModemMode preferred;
};

/**
 * MMModemPortInfo:
 * @name: Name of the port.
 * @type: A #MMModemPortType value.
 *
 * Information of a given port.
 *
 * Since: 1.0
 */
typedef struct _MMModemPortInfo MMModemPortInfo;
struct _MMModemPortInfo {
    gchar *name;
    MMModemPortType type;
};

void mm_modem_port_info_array_free (MMModemPortInfo *array,
                                    guint array_size);

/**
 * MMOmaPendingNetworkInitiatedSession:
 * @session_type: A #MMOmaSessionType.
 * @session_id: Unique ID of the network-initiated OMA session.
 *
 * #MMOmaPendingNetworkInitiatedSession is a simple struct specifying the
 * information available for a pending network-initiated OMA session.
 *
 * Since: 1.2
 */
typedef struct _MMOmaPendingNetworkInitiatedSession MMOmaPendingNetworkInitiatedSession;
struct _MMOmaPendingNetworkInitiatedSession {
    MMOmaSessionType session_type;
    guint session_id;
};

#endif /* _MM_HELPER_TYPES_H_ */
