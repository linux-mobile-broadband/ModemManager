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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-call.h"
#include "mm-context.h"
#include "mm-call-list.h"
#include "mm-iface-modem-voice.h"
#include "fake-modem.h"
#include "fake-call.h"
#include "mm-log.h"
#include "mm-test-utils.h"

/****************************************************************/
/* Make the linker happy */

#if defined WITH_QMI

typedef struct MMBroadbandModemQmi MMBroadbandModemQmi;
GType mm_broadband_modem_qmi_get_type (void);
MMPortQmi *mm_broadband_modem_qmi_peek_port_qmi (MMBroadbandModemQmi *self);

GType
mm_broadband_modem_qmi_get_type (void)
{
    return G_TYPE_INVALID;
}

MMPortQmi *
mm_broadband_modem_qmi_peek_port_qmi (MMBroadbandModemQmi *self)
{
    return NULL;
}

#endif /* WITH_QMI */

#if defined WITH_MBIM

typedef struct MMBroadbandModemMbim MMBroadbandModemMbim;
GType mm_broadband_modem_mbim_get_type (void);
MMPortMbim *mm_broadband_modem_mbim_peek_port_mbim (MMBroadbandModemMbim *self);

GType
mm_broadband_modem_mbim_get_type (void)
{
    return G_TYPE_INVALID;
}

MMPortMbim *
mm_broadband_modem_mbim_peek_port_mbim (MMBroadbandModemMbim *self)
{
    return NULL;
}

#endif /* WITH_MBIM */

/****************************************************************/

typedef struct {
    const gchar *desc;

    const gchar *start_error_msg;
    const gchar *accept_error_msg;
    const gchar *deflect_error_msg;
    const gchar *hangup_error_msg;

    const gchar *number;

    /* DTMF */
    const gchar *dtmf_error_msg;
    const gchar *dtmf_stop_error_msg;
    const guint  dtmf_accept_len; /* how many chars modem can accept at a time */
    const guint  dtmf_tone_duration;
    const gchar *dtmf;
    const guint  dtmf_min_duration;
} Testcase;

typedef struct {
    GTestDBus *dbus;
    GDBusConnection *connection;
    GMainLoop *loop;
    guint name_id;

    MmGdbusModemVoice *voice_proxy;
    MMFakeModem *modem;

    MmGdbusCall *call_proxy;
    MMFakeCall  *call;

    GError *error;

    const Testcase *tc;
} TestFixture;

/****************************************************************/

static MMFakeCall *
get_call (TestFixture *tf)
{
    MMCallList  *list;
    MMFakeCall  *call;
    const gchar *call_path;

    list = mm_fake_modem_get_call_list (tf->modem);
    g_assert (list);

    g_assert (tf->call_proxy);
    call_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (tf->call_proxy));
    call = (MMFakeCall *) mm_call_list_get_call (list, call_path);
    g_assert (call);
    return call;
}

/****************************************************************/

static void
dtmf_send_ready (MmGdbusCall *call,
                 GAsyncResult *res,
                 TestFixture *tf)
{
    if (!mm_gdbus_call_call_send_dtmf_finish (call, res, &tf->error))
        g_assert_true (tf->error);
    g_main_loop_quit (tf->loop);
}

static void
dtmf_call_start_ready (MmGdbusCall *call,
                       GAsyncResult *res,
                       TestFixture *tf)
{
    gboolean          success;
    g_autoptr(GError) error = NULL;
    const MMCallInfo        cinfo = {
        .index = 1,
        .number = (gchar *) tf->tc->number,
        .direction = MM_CALL_DIRECTION_OUTGOING,
        .state = MM_CALL_STATE_ACTIVE,
    };

    success = mm_gdbus_call_call_start_finish (call, res, &error);
    g_assert_no_error (error);
    g_assert_true (success);

    /* Set the call active */
    mm_iface_modem_voice_report_call (MM_IFACE_MODEM_VOICE (tf->modem), &cinfo);
    g_main_loop_quit (tf->loop);
}

static void
dtmf_proxy_ready (gpointer unused,
                  GAsyncResult *res,
                  TestFixture *tf)
{
    g_autoptr(GError)     error = NULL;
    g_autoptr(MMCallList) list = NULL;

    tf->call_proxy = mm_gdbus_call_proxy_new_for_bus_finish (res, &error);
    g_assert_no_error (error);
    g_assert (tf->call_proxy);
    g_main_loop_quit (tf->loop);
}

static void
dtmf_create_call_ready (MMIfaceModemVoice *self,
                        GAsyncResult *res,
                        TestFixture  *tf)
{
    g_autoptr(GError)  error = NULL;
    g_autofree gchar  *call_path = NULL;
    gboolean           success;

    success = mm_gdbus_modem_voice_call_create_call_finish (MM_GDBUS_MODEM_VOICE (self),
                                                            &call_path,
                                                            res,
                                                            &error);
    g_assert_true (success);
    g_assert_no_error (error);

    /* Create our call proxy */
    mm_gdbus_call_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     "org.freedesktop.ModemManager1",
                                     call_path,
                                     NULL,
                                     (GAsyncReadyCallback) dtmf_proxy_ready,
                                     tf);
}

static void
test_dtmf (TestFixture *tf, const Testcase *tc, gboolean test_stop)
{
    g_autoptr(GError)            error = NULL;
    g_autoptr(GVariant)          dictionary = NULL;
    g_autoptr(MMCallProperties)  call_props = NULL;
    MMFakeCall                  *call;
    guint                        dtmf_len_no_pause = 0;
    const gchar                 *p;
    gint64                       call_start_time;

    tf->tc = tc;

    call_props = mm_call_properties_new ();
    mm_call_properties_set_number (call_props, tf->tc->number);
    mm_call_properties_set_dtmf_tone_duration (call_props, tf->tc->dtmf_tone_duration);
    dictionary = mm_call_properties_get_dictionary (call_props);

    /* Create the voice call we'll use for DTMF */
    mm_gdbus_modem_voice_call_create_call (tf->voice_proxy,
                                           dictionary,
                                           NULL,
                                           (GAsyncReadyCallback)dtmf_create_call_ready,
                                           tf);
    g_main_loop_run (tf->loop);

    /* start the voice call */
    mm_gdbus_call_call_start (tf->call_proxy,
                              NULL,
                              (GAsyncReadyCallback) dtmf_call_start_ready,
                              tf);
    g_main_loop_run (tf->loop);

    /* Get the call and copy expectations into it */
    call = get_call (tf);
    call->priv->dtmf_accept_len = tf->tc->dtmf_accept_len;
    call->priv->start_error_msg = tf->tc->start_error_msg;
    call->priv->accept_error_msg = tf->tc->accept_error_msg;
    call->priv->deflect_error_msg = tf->tc->deflect_error_msg;
    call->priv->hangup_error_msg = tf->tc->hangup_error_msg;
    call->priv->dtmf_error_msg = tf->tc->dtmf_error_msg;
    call->priv->dtmf_stop_error_msg = tf->tc->dtmf_stop_error_msg;

    mm_fake_call_enable_dtmf_stop (call, test_stop);

g_message ("####### about to run %s", tf->tc->desc);
    /* Run the test */
    call_start_time = g_get_real_time ();
    mm_gdbus_call_call_send_dtmf (tf->call_proxy,
                                  tf->tc->dtmf,
                                  NULL,
                                  (GAsyncReadyCallback) dtmf_send_ready,
                                  tf);
    g_main_loop_run (tf->loop);

    /* Validate results */
    if (tf->tc->start_error_msg)
        mm_assert_error_str (tf->error, tf->tc->start_error_msg);
    else if (tf->tc->accept_error_msg)
        mm_assert_error_str (tf->error, tf->tc->accept_error_msg);
    else if (tf->tc->deflect_error_msg)
        mm_assert_error_str (tf->error, tf->tc->deflect_error_msg);
    else if (tf->tc->hangup_error_msg)
        mm_assert_error_str (tf->error, tf->tc->hangup_error_msg);
    else if (tf->tc->dtmf_error_msg)
        mm_assert_error_str (tf->error, tf->tc->dtmf_error_msg);
    else if (test_stop && tf->tc->dtmf_stop_error_msg)
        mm_assert_error_str (tf->error, tf->tc->dtmf_stop_error_msg);
    else {
        p = tf->tc->dtmf;
        while (*p++) {
            if (*p != MM_CALL_DTMF_PAUSE_CHAR)
                dtmf_len_no_pause++;
        }
        g_assert_cmpint (strlen (call->priv->dtmf_sent->str), ==, dtmf_len_no_pause);

        if (tf->tc->dtmf_min_duration) {
            g_assert_cmpint (call_start_time + (tf->tc->dtmf_min_duration * G_USEC_PER_SEC), <=, g_get_real_time ());
        }
    }
    if (test_stop && !tf->tc->dtmf_error_msg)
        g_assert_true (call->priv->dtmf_stop_called);
}

static void
test_dtmf_nostop (TestFixture *tf, gconstpointer user_data)
{
    test_dtmf (tf, (const Testcase *) user_data, FALSE);
}

static void
test_dtmf_need_stop (TestFixture *tf, gconstpointer user_data)
{
    test_dtmf (tf, (const Testcase *) user_data, TRUE);
}

/************************************************************/

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 TestFixture     *tf)
{
    tf->connection = connection;
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar *name,
                  TestFixture *tf)
{
    g_main_loop_quit (tf->loop);
}

static void
voice_init_ready (MMIfaceModemVoice *_self,
                  GAsyncResult      *result,
                  TestFixture       *tf)
{
    g_autoptr(GError)  error = NULL;
    gboolean           success;

    success = mm_iface_modem_voice_initialize_finish (_self, result, &error);
    g_assert_no_error (error);
    g_assert (success);
    g_main_loop_quit (tf->loop);
}

static void
voice_proxy_ready (gpointer unused,
                   GAsyncResult *res,
                   TestFixture *tf)
{
    g_autoptr(GError) error = NULL;

    tf->voice_proxy = mm_gdbus_modem_voice_proxy_new_for_bus_finish (res, &error);
    g_assert_no_error (error);
    g_assert (tf->voice_proxy);
    g_main_loop_quit (tf->loop);
}

static void
test_fixture_setup (TestFixture *tf, gconstpointer unused)
{
    g_autoptr(GError) error = NULL;
    gboolean          success;

    success = mm_log_setup (mm_context_get_log_level (),
                            mm_context_get_log_file (),
                            mm_context_get_log_journal (),
                            mm_context_get_log_timestamps (),
                            mm_context_get_log_relative_timestamps (),
                            mm_context_get_log_personal_info (),
                            &error);
    g_assert_no_error (error);
    g_assert (success);

    tf->loop = g_main_loop_new (NULL, FALSE);

    /* Create the global dbus-daemon for this test suite */
    tf->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_assert (tf->dbus);

    /* Add the private directory with our in-tree service files,
     * TEST_SERVICES is defined by the build system to point
     * to the right directory. */
    g_test_dbus_add_service_dir (tf->dbus, TEST_SERVICES);

    /* Start the private DBus daemon */
    g_test_dbus_up (tf->dbus);

    /* Acquire name, don't allow replacement */
    tf->name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                  MM_DBUS_SERVICE,
                                  G_BUS_NAME_OWNER_FLAGS_NONE,
                                  (GBusAcquiredCallback) on_bus_acquired,
                                  (GBusNameAcquiredCallback) name_acquired_cb,
                                  NULL,
                                  tf,
                                  NULL);
    /* Wait for name acquired */
    g_main_loop_run (tf->loop);

    /* Create and export the server-side modem voice interface */
    g_assert (tf->connection);
    tf->modem = mm_fake_modem_new (tf->connection);
    mm_iface_modem_voice_initialize (MM_IFACE_MODEM_VOICE (tf->modem),
                                     NULL,
                                     (GAsyncReadyCallback) voice_init_ready,
                                     tf);
    g_main_loop_run (tf->loop);

    if (!mm_fake_modem_export_interfaces (tf->modem, &error))
        g_assert_no_error (error);

    /* Create client-side modem proxy */
    mm_gdbus_modem_voice_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.freedesktop.ModemManager1",
                                            mm_fake_modem_get_path (tf->modem),
                                            NULL,
                                            (GAsyncReadyCallback) voice_proxy_ready,
                                            tf);
    g_main_loop_run (tf->loop);
}

static void
test_fixture_cleanup (TestFixture *tf, gconstpointer unused)
{
    g_bus_unown_name (tf->name_id);
    mm_iface_modem_voice_shutdown (MM_IFACE_MODEM_VOICE (tf->modem));

    g_clear_error (&tf->error);

    g_clear_object (&tf->call_proxy);
    g_clear_object (&tf->call);
    g_clear_object (&tf->voice_proxy);
    /* Run dispose to break a ref cycle in case there's still a FakeCall hanging around */
    g_object_run_dispose (G_OBJECT (tf->modem));
    g_clear_object (&tf->modem);
    g_dbus_connection_close_sync (tf->connection, NULL, NULL);
    g_test_dbus_stop (tf->dbus);
    g_test_dbus_down (tf->dbus);
    g_clear_object (&tf->dbus);
    g_main_loop_unref (tf->loop);
    mm_log_shutdown ();
}

/****************************************************************/

static const Testcase tests[] = {
    {
        .desc = "/MM/Call/DTMF/send-one-accept-len",
        .number = "911",
        .dtmf_accept_len = 1,
        .dtmf_tone_duration = 300,
        .dtmf = "987654321",
    },
    {
        .desc = "/MM/Call/DTMF/send-single-tone",
        .number = "911",
        .dtmf_accept_len = 3,
        .dtmf_tone_duration = 300,
        .dtmf = "9",
    },
    {
        .desc = "/MM/Call/DTMF/send-multi-tone",
        .number = "911",
        .dtmf_accept_len = 3,
        .dtmf_tone_duration = 300,
        .dtmf = "123",
    },
    {
        .desc = "/MM/Call/DTMF/send-pause",
        .number = "911",
        .dtmf_accept_len = 10,
        .dtmf_tone_duration = 300,
        .dtmf = "123,,4",
        .dtmf_min_duration = 4,
    },
    /* Error testing */
    {
        .desc = "/MM/Call/DTMF/send-error",
        .number = "911",
        .dtmf_accept_len = 1,
        .dtmf = "123",
        .dtmf_error_msg = "send failure",
    },
    {
        .desc = "/MM/Call/DTMF/stop-error",
        .number = "911",
        .dtmf_accept_len = 1,
        .dtmf = "123",
        .dtmf_stop_error_msg = "stop failure",
    },
};



#define TCASE(n, d, f)                 \
    g_test_add (n,                     \
                TestFixture,           \
                d,                     \
                test_fixture_setup,    \
                f,                     \
                test_fixture_cleanup); \

#define TCASE_DTMF_STOP(n, d, f) \
    { \
        g_autofree gchar *desc = g_strdup_printf ("%s-stop", n); \
        TCASE(desc, d, f); \
    }

int main (int argc, char **argv)
{
    const gchar *test_args[] = { argv[0], "--test-session" };
    guint i;

    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);
    mm_context_init (G_N_ELEMENTS (test_args), (gchar **) test_args);

    for (i = 0; i < G_N_ELEMENTS (tests); i++) {
        TCASE(tests[i].desc, &tests[i], test_dtmf_nostop);
        /* Test everything again for paired start/stop (eg QMI) */
        TCASE_DTMF_STOP(tests[i].desc, &tests[i], test_dtmf_need_stop);
    }

    return g_test_run ();
}
