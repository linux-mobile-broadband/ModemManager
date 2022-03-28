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
 * Basic netlink support based on the QmiNetPortManagerRmnet from libqmi:
 *   Copyright (C) 2020 Eric Caruso <ejcaruso@chromium.org>
 *   Copyright (C) 2020 Andrew Lassalle <andrewlassalle@chromium.org>
 *
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <config.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-log-object.h"
#include "mm-utils.h"
#include "mm-netlink.h"

struct _MMNetlink {
    GObject parent;
    /* Netlink socket */
    GSocket *socket;
    GSource *source;
    /* Netlink state */
    guint       current_sequence_id;
    GHashTable *transactions;
};

struct _MMNetlinkClass {
    GObjectClass parent_class;
};

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMNetlink, mm_netlink, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))


/*****************************************************************************/
/*
 * Netlink message construction functions
 */

typedef GByteArray NetlinkMessage;

typedef struct {
    struct nlmsghdr  msghdr;
    struct ifinfomsg ifreq;
} NetlinkHeader;

static NetlinkHeader *
netlink_message_header (NetlinkMessage *msg)
{
    return (NetlinkHeader *) (msg->data);
}

static guint
get_pos_of_next_attr (NetlinkMessage *msg)
{
    return NLMSG_ALIGN (msg->len);
}

static void
append_netlink_attribute (NetlinkMessage *msg,
                          gushort         type,
                          gconstpointer   value,
                          gushort         len)
{
    guint          attr_len;
    guint          old_len;
    guint          next_attr_rel_pos;
    char          *next_attr_abs_pos;
    struct rtattr  new_attr;

    /* Expand the buffer to hold the new attribute */
    attr_len = RTA_ALIGN (RTA_LENGTH (len));
    old_len = msg->len;
    next_attr_rel_pos = get_pos_of_next_attr (msg);

    g_byte_array_set_size (msg, next_attr_rel_pos + attr_len);
    /* fill new bytes with zero, since some padding is added between attributes. */
    memset ((char *) msg->data + old_len, 0, msg->len - old_len);

    new_attr.rta_type = type;
    new_attr.rta_len = RTA_LENGTH (len);
    next_attr_abs_pos = (char *) msg->data + next_attr_rel_pos;
    memcpy (next_attr_abs_pos, &new_attr, sizeof (struct rtattr));

    if (value)
        memcpy (RTA_DATA (next_attr_abs_pos), value, len);

    /* Update the total netlink message length */
    netlink_message_header (msg)->msghdr.nlmsg_len = msg->len;
}

static void
append_netlink_attribute_uint32 (NetlinkMessage *msg,
                                 gushort         type,
                                 guint32         value)
{
    append_netlink_attribute (msg, type, &value, sizeof (value));
}

static NetlinkMessage *
netlink_message_new (guint   ifindex,
                     guint16 type)
{
    NetlinkMessage *msg;
    NetlinkHeader  *hdr;

    int size = sizeof (NetlinkHeader);

    msg = g_byte_array_new ();
    g_byte_array_set_size (msg, size);
    memset ((char *) msg->data, 0, size);

    hdr = netlink_message_header (msg);
    hdr->msghdr.nlmsg_len = msg->len;
    hdr->msghdr.nlmsg_type = type;
    hdr->msghdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    hdr->ifreq.ifi_family = AF_UNSPEC;
    hdr->ifreq.ifi_index = ifindex;

    return msg;
}

static NetlinkMessage *
netlink_message_new_setlink (guint    ifindex,
                             gboolean up,
                             guint    mtu)
{
    NetlinkMessage *msg;
    NetlinkHeader  *hdr;

    msg = netlink_message_new (ifindex, RTM_SETLINK);
    hdr = netlink_message_header (msg);

    hdr->ifreq.ifi_flags = up ? IFF_UP : 0;
    hdr->ifreq.ifi_change = IFF_UP;

    if (mtu)
        append_netlink_attribute_uint32 (msg, IFLA_MTU, mtu);

    return msg;
}

static void
netlink_message_free (NetlinkMessage *msg)
{
    g_byte_array_unref (msg);
}

/*****************************************************************************/
/* Netlink transactions */

typedef struct {
    MMNetlink *self;
    guint32    sequence_id;
    GSource   *timeout_source;
    GTask     *completion_task;
} Transaction;

static gboolean
transaction_timed_out (Transaction *tr)
{
    GTask *task;
    guint32 sequence_id;

    task = g_steal_pointer (&tr->completion_task);
    sequence_id = tr->sequence_id;

    g_hash_table_remove (tr->self->transactions,
                         GUINT_TO_POINTER (tr->sequence_id));

    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                             "Netlink message with sequence ID %u timed out",
                             sequence_id);

    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
transaction_complete_with_error (Transaction *tr,
                                 GError      *error)
{
    GTask *task;

    task = g_steal_pointer (&tr->completion_task);

    g_hash_table_remove (tr->self->transactions,
                         GUINT_TO_POINTER (tr->sequence_id));

    g_task_return_error (task, error);
    g_object_unref (task);
}

static void
transaction_complete (Transaction *tr,
                      gint         saved_errno)
{
    GTask *task;
    guint32 sequence_id;

    task = g_steal_pointer (&tr->completion_task);
    sequence_id = tr->sequence_id;

    g_hash_table_remove (tr->self->transactions,
                         GUINT_TO_POINTER (tr->sequence_id));

    if (!saved_errno) {
        g_task_return_boolean (task, TRUE);
    } else {
        g_task_return_new_error (task, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                                 "Netlink message with transaction %u failed",
                                 sequence_id);
    }

    g_object_unref (task);
}

static void
transaction_free (Transaction *tr)
{
    g_assert (tr->completion_task == NULL);
    g_source_destroy (tr->timeout_source);
    g_source_unref (tr->timeout_source);
    g_slice_free (Transaction, tr);
}

static Transaction *
transaction_new (MMNetlink      *self,
                 NetlinkMessage *msg,
                 guint           timeout,
                 GTask          *task)
{
    Transaction *tr;

    tr = g_slice_new0 (Transaction);
    tr->self = self;
    tr->sequence_id = ++self->current_sequence_id;
    netlink_message_header (msg)->msghdr.nlmsg_seq = tr->sequence_id;
    if (timeout) {
        tr->timeout_source = g_timeout_source_new_seconds (timeout);
        g_source_set_callback (tr->timeout_source,
                               (GSourceFunc) transaction_timed_out,
                               tr,
                               NULL);
        g_source_attach (tr->timeout_source,
                         g_main_context_get_thread_default ());
    }
    tr->completion_task = g_object_ref (task);

    g_hash_table_insert (self->transactions,
                         GUINT_TO_POINTER (tr->sequence_id),
                         tr);
    return tr;
}

/*****************************************************************************/

gboolean
mm_netlink_setlink_finish (MMNetlink     *self,
                           GAsyncResult  *res,
                           GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_netlink_setlink (MMNetlink           *self,
                    guint                ifindex,
                    gboolean             up,
                    guint                mtu,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask          *task;
    NetlinkMessage *msg;
    Transaction    *tr;
    gssize          bytes_sent;
    GError         *error = NULL;

    task = g_task_new (self, cancellable, callback, user_data);

    if (!self->socket) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "netlink support not available");
        g_object_unref (task);
        return;
    }

    msg = netlink_message_new_setlink (ifindex, up, mtu);

    /* The task ownership is transferred to the transaction. */
    tr = transaction_new (self, msg, 5, task);

    bytes_sent = g_socket_send (self->socket,
                                (const gchar *) msg->data,
                                msg->len,
                                cancellable,
                                &error);
    netlink_message_free (msg);

    if (bytes_sent < 0)
        transaction_complete_with_error (tr, error);

    g_object_unref (task);
}

/*****************************************************************************/

static gboolean
netlink_message_cb (GSocket      *socket,
                    GIOCondition  condition,
                    MMNetlink    *self)
{
    g_autoptr(GError) error = NULL;
    gchar             buf[512];
    gssize            bytes_received;
    guint             buffer_len;
    struct nlmsghdr  *hdr;

    if (condition & G_IO_HUP || condition & G_IO_ERR) {
        mm_obj_warn (self, "socket connection closed");
        return G_SOURCE_REMOVE;
    }

    bytes_received = g_socket_receive (socket, buf, sizeof (buf), NULL, &error);
    if (bytes_received < 0) {
        mm_obj_warn (self, "socket i/o failure: %s", error->message);
        return G_SOURCE_REMOVE;
    }

    buffer_len = (guint) bytes_received;
    for (hdr = (struct nlmsghdr *) buf; NLMSG_OK (hdr, buffer_len);
         NLMSG_NEXT (hdr, buffer_len)) {
        Transaction     *tr;
        struct nlmsgerr *err;

        if (hdr->nlmsg_type != NLMSG_ERROR)
            continue;

        tr = g_hash_table_lookup (self->transactions,
                                  GUINT_TO_POINTER (hdr->nlmsg_seq));
        if (!tr)
            continue;

        err = NLMSG_DATA (buf);
        transaction_complete (tr, err->error);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean
setup_netlink_socket (MMNetlink  *self,
                      GError    **error)
{
    gint socket_fd;

    socket_fd = socket (AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (socket_fd < 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Failed to create netlink socket");
        return FALSE;
    }

    self->socket = g_socket_new_from_fd (socket_fd, error);
    if (!self->socket) {
        close (socket_fd);
        return FALSE;
    }

    self->source = g_socket_create_source (self->socket,
                                           G_IO_IN | G_IO_ERR | G_IO_HUP,
                                           NULL);
    g_source_set_callback (self->source,
                           (GSourceFunc) netlink_message_cb,
                           self,
                           NULL);
    g_source_attach (self->source, NULL);

    return TRUE;
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("netlink");
}

/********************************************************************/

static void
mm_netlink_init (MMNetlink *self)
{
    g_autoptr(GError) error = NULL;

    if (!setup_netlink_socket (self, &error)) {
        mm_obj_warn (self, "couldn't setup netlink socket: %s", error->message);
        return;
    }

    self->current_sequence_id = 0;
    self->transactions = g_hash_table_new_full (g_direct_hash,
                                                g_direct_equal,
                                                NULL,
                                                (GDestroyNotify) transaction_free);
}

static void
dispose (GObject *object)
{
    MMNetlink *self = MM_NETLINK (object);

    g_assert (g_hash_table_size (self->transactions) == 0);

    g_clear_pointer (&self->transactions, g_hash_table_unref);
    if (self->source)
        g_source_destroy (self->source);
    g_clear_pointer (&self->source, g_source_unref);
    g_clear_object (&self->socket);

    G_OBJECT_CLASS (mm_netlink_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_netlink_class_init (MMNetlinkClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = dispose;
}

MM_DEFINE_SINGLETON_GETTER (MMNetlink, mm_netlink_get, MM_TYPE_NETLINK);

/* ---------------------------------------------------------------------------------------------------- */
