/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef _MM_COMPAT_H_
#define _MM_COMPAT_H_

#ifndef MM_DISABLE_DEPRECATED

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include "mm-modem-oma.h"

/**
 * SECTION:mm-compat
 * @short_description: Deprecated types and methods.
 *
 * These types and methods are flagged as deprecated and therefore
 * shouldn't be used in newly written code. They are provided to avoid
 * innecessary API/ABI breaks, for compatibility purposes only.
 */

/**
 * mm_modem_get_pending_network_initiated_sessions:
 * @self: A #MMModem.
 * @sessions: (out) (array length=n_sessions): Return location for the array of
 *  #MMOmaPendingNetworkInitiatedSession structs. The returned array should be
 *  freed with g_free() when no longer needed.
 * @n_sessions: (out): Return location for the number of values in @sessions.
 *
 * Gets the list of pending network-initiated OMA sessions.
 *
 * Returns: %TRUE if @sessions and @n_sessions are set, %FALSE otherwise.
 *
 * Since: 1.2
 * Deprecated: 1.18: Use mm_modem_oma_get_pending_network_initiated_sessions() instead.
 */
G_DEPRECATED_FOR (mm_modem_oma_get_pending_network_initiated_sessions)
gboolean mm_modem_get_pending_network_initiated_sessions (MMModemOma                           *self,
                                                          MMOmaPendingNetworkInitiatedSession **sessions,
                                                          guint                                *n_sessions);

/**
 * mm_modem_peek_pending_network_initiated_sessions:
 * @self: A #MMModem.
 * @sessions: (out) (array length=n_sessions): Return location for the array of
 *  #MMOmaPendingNetworkInitiatedSession values. Do not free the returned array,
 *  it is owned by @self.
 * @n_sessions: (out): Return location for the number of values in @sessions.
 *
 * Gets the list of pending network-initiated OMA sessions.
 *
 * Returns: %TRUE if @sessions and @n_sessions are set, %FALSE otherwise.
 *
 * Since: 1.2
 * Deprecated: 1.18: Use mm_modem_oma_peek_pending_network_initiated_sessions() instead.
 */
G_DEPRECATED_FOR (mm_modem_oma_peek_pending_network_initiated_sessions)
gboolean mm_modem_peek_pending_network_initiated_sessions (MMModemOma                                 *self,
                                                           const MMOmaPendingNetworkInitiatedSession **sessions,
                                                           guint                                      *n_sessions);

#endif /* MM_DISABLE_DEPRECATED */

#endif /* _MM_COMPAT_H_ */
