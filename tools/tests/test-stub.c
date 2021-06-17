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
 * Copyright (C) 2020 Frederic Martinsons <frederic.martinsons@sigfox.com>
 */

#include <config.h>
#include <glib.h>
#include <gio/gio.h>
#include <libmm-glib.h>
#include <ModemManager-names.h>
#define MM_LOG_NO_OBJECT
#include <mm-log-test.h>

#define MM_TEST_IFACE_NAME "org.freedesktop.ModemManager1.LibmmGlibTest"

typedef struct {
    GMainLoop *loop;
    MMManager *mm_manager;
    GDBusConnection *gdbus_connection;
    GDBusProxy *mm_proxy;
    GDBusProxy *mm_modem_prop_proxy;
    GTestDBus *test_bus;
    gchar *modem_object_path;
    guint timeout_id;
    MMSim *sim;
    gboolean pin_error;
} TestData;

static void
setup (TestData **ptdata,
       gconstpointer data)
{
    GError *error = NULL;
    TestData *tdata = NULL;

    tdata = (TestData *)g_malloc0 (sizeof(TestData));
    *ptdata = tdata;

    tdata->loop = g_main_loop_new (NULL, FALSE);
    g_assert_nonnull (tdata->loop);

    tdata->test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_assert_nonnull (tdata->test_bus);

    g_test_dbus_add_service_dir (tdata->test_bus, TEST_SERVICES);
    g_test_dbus_up (tdata->test_bus);

    /* Grab a proxy to the fake NM service to trigger tests */
    tdata->mm_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                     NULL,
                                                     MM_DBUS_SERVICE,
                                                     MM_DBUS_PATH,
                                                     MM_TEST_IFACE_NAME,
                                                     NULL, &error);
    g_assert_no_error (error);

    tdata->gdbus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error (error);

    tdata->mm_manager = mm_manager_new_sync (tdata->gdbus_connection,
                                             G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                             NULL,
                                             &error);
    g_assert_no_error (error);
    g_assert_nonnull (tdata->mm_manager);
}

static void
teardown (TestData **ptdata,
          gconstpointer data)
{
    TestData *tdata = NULL;

    tdata = *ptdata;
    g_clear_object (&tdata->mm_modem_prop_proxy);
    g_clear_object (&tdata->mm_proxy);
    g_clear_object (&tdata->mm_manager);
    g_clear_object (&tdata->gdbus_connection);
    g_test_dbus_down (tdata->test_bus);
    g_clear_object (&tdata->test_bus);
    g_main_loop_unref (tdata->loop);
    g_free (tdata);
}

static gboolean
loop_timeout_cb (gpointer user_data)
{
    mm_err ("Timeout has elapsed");
    g_assert_not_reached ();
    return G_SOURCE_REMOVE;
}

static void
run_loop_for_ms (TestData *tdata,
                 guint32 timeout)
{
    mm_info ("Run loop for %u ms", timeout);
    tdata->timeout_id = g_timeout_add (timeout, loop_timeout_cb, tdata);
    g_main_loop_run (tdata->loop);
}

static void
stop_loop (TestData *tdata)
{
    if (tdata->timeout_id) {
        g_source_remove (tdata->timeout_id);
        tdata->timeout_id = 0;
    }
    mm_info ("Stop the loop");
    g_main_loop_quit (tdata->loop);
}

static void
add_modem_completion_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
    GError *error = NULL;
    GVariant *dbus_return = NULL;
    GVariant *obj_path_variant = NULL;
    TestData *tdata = NULL;

    tdata = (TestData*)user_data;

    mm_info ("AddModem DBus call completed");
    dbus_return = g_dbus_proxy_call_finish (tdata->mm_proxy, res, &error);
    g_assert_no_error (error);
    g_assert_nonnull (dbus_return);
    g_assert_cmpstr (g_variant_get_type_string (dbus_return), == , "(o)");

    obj_path_variant = g_variant_get_child_value (dbus_return, 0);
    tdata->modem_object_path = g_variant_dup_string (obj_path_variant, NULL);

    g_assert_null (tdata->mm_modem_prop_proxy);
    tdata->mm_modem_prop_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                                NULL,
                                                                MM_DBUS_SERVICE,
                                                                tdata->modem_object_path,
                                                                "org.freedesktop.DBus.Properties",
                                                                NULL, &error);

    g_assert_no_error (error);
    g_assert_nonnull (tdata->mm_modem_prop_proxy);

    g_variant_unref (dbus_return);
    g_variant_unref (obj_path_variant);

    stop_loop (tdata);
}

static gchar*
add_modem (TestData *tdata,
           gboolean add_sim,
           const gchar *iccid)
{
    g_dbus_proxy_call (tdata->mm_proxy,
                       "AddModem",
                       g_variant_new ("(bs)", add_sim, iccid),
                       G_DBUS_CALL_FLAGS_NO_AUTO_START,
                       1000,
                       NULL,
                       add_modem_completion_cb,
                       tdata);

    run_loop_for_ms (tdata, 1000);
    return tdata->modem_object_path;
}

static void
emit_state_changed_completion_cb (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
    GError *error = NULL;
    GVariant *dbus_return = NULL;
    TestData *tdata = NULL;

    tdata = (TestData*)user_data;

    mm_info ("EmitStateChanged DBus call completed");
    dbus_return = g_dbus_proxy_call_finish (tdata->mm_proxy, res, &error);
    g_assert_no_error (error);
    g_assert_nonnull (dbus_return);
    g_variant_unref (dbus_return);

    stop_loop (tdata);
}

static void
set_modem_state (TestData *tdata,
                 MMModemState state,
                 MMModemStateFailedReason reason)
{
    GError *error = NULL;
    GVariant *ret = NULL;
    GVariant *old_state_variant = NULL;
    gint old_state = 0;

    g_assert_nonnull (tdata->mm_modem_prop_proxy);

    /* Get current state */
    ret = g_dbus_proxy_call_sync (tdata->mm_modem_prop_proxy,
                                  "org.freedesktop.DBus.Properties.Get",
                                  g_variant_new ("(ss)",
                                                 MM_DBUS_INTERFACE_MODEM,
                                                 MM_MODEM_PROPERTY_STATE),
                                  G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  -1,
                                  NULL,
                                  &error);
    g_assert_no_error (error);
    g_variant_get (ret, "(v)", &old_state_variant);
    old_state = g_variant_get_int32 (old_state_variant);
    g_variant_unref (ret);
    g_variant_unref (old_state_variant);

    ret = g_dbus_proxy_call_sync (tdata->mm_modem_prop_proxy,
                                  "org.freedesktop.DBus.Properties.Set",
                                  g_variant_new ("(ssv)",
                                                 MM_DBUS_INTERFACE_MODEM,
                                                 MM_MODEM_PROPERTY_STATE,
                                                 g_variant_new_int32 (state)),
                                  G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  -1,
                                  NULL,
                                  &error);
    g_assert_no_error (error);
    g_variant_unref (ret);

    ret = g_dbus_proxy_call_sync (tdata->mm_modem_prop_proxy,
                                  "org.freedesktop.DBus.Properties.Set",
                                  g_variant_new ("(ssv)",
                                                 MM_DBUS_INTERFACE_MODEM,
                                                 MM_MODEM_PROPERTY_STATEFAILEDREASON,
                                                 g_variant_new_uint32 (reason)),
                                  G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  -1,
                                  NULL,
                                  &error);

    g_assert_no_error (error);
    g_variant_unref (ret);

    /* Emit state change signal */
    g_dbus_proxy_call (tdata->mm_proxy,
                       "EmitStateChanged",
                       g_variant_new ("(iiu)", old_state, state, reason),
                       G_DBUS_CALL_FLAGS_NO_AUTO_START,
                       1000,
                       NULL,
                       emit_state_changed_completion_cb,
                       tdata);

    run_loop_for_ms (tdata, 1000);
}

static void
set_modem_unlock_completion_cb (GObject *source_object,
                                GAsyncResult *res,
                                gpointer user_data)
{
    GError *error = NULL;
    GVariant *dbus_return = NULL;
    TestData *tdata = NULL;

    tdata = (TestData*)user_data;

    mm_info ("org.freedesktop.DBus.Properties.Set DBus call completed");
    dbus_return = g_dbus_proxy_call_finish (tdata->mm_modem_prop_proxy, res, &error);
    g_assert_no_error (error);
    g_assert_nonnull (dbus_return);
    g_variant_unref (dbus_return);

    stop_loop (tdata);
}

static void
set_modem_unlock (TestData *tdata,
                  MMModemLock lock_state)
{
    g_assert_nonnull (tdata->mm_modem_prop_proxy);

    g_dbus_proxy_call (tdata->mm_modem_prop_proxy,
                       "org.freedesktop.DBus.Properties.Set",
                       g_variant_new ("(ssv)",
                                      MM_DBUS_INTERFACE_MODEM,
                                      MM_MODEM_PROPERTY_UNLOCKREQUIRED,
                                      g_variant_new_uint32 (lock_state)),
                       G_DBUS_CALL_FLAGS_NO_AUTO_START,
                       1000,
                       NULL,
                       set_modem_unlock_completion_cb,
                       tdata);

    run_loop_for_ms (tdata, 1000);
}

static void
set_modem_equipment_error (TestData *tdata,
                           MMMobileEquipmentError equipmentError,
                           gboolean clear)
{
    GError *error = NULL;
    GVariant *ret = NULL;

    ret = g_dbus_proxy_call_sync (tdata->mm_proxy,
                                  "SetMobileEquipmentError",
                                  g_variant_new ("(ub)", equipmentError, clear),
                                  G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  3000,
                                  NULL,
                                  &error);
    g_assert_no_error (error);
    g_variant_unref (ret);
}

static void
test_modem_interface (TestData **ptdata,
                      gconstpointer data)
{
    TestData *tdata = NULL;
    GDBusObject *modem_object = NULL;
    MMModem *mm_modem = NULL;
    g_autofree gchar *modem_path = NULL;

    tdata = *ptdata;
    /* Add a modem object (with no sim attached) */
    modem_path = add_modem (tdata, FALSE, "");
    modem_object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (tdata->mm_manager), modem_path);
    g_assert_nonnull (modem_object);

    mm_modem = mm_object_get_modem (MM_OBJECT (modem_object));
    g_clear_object (&modem_object);

    /* Check the modem states */
    g_assert_cmpuint (mm_modem_get_state (mm_modem), ==, MM_MODEM_STATE_UNKNOWN);
    g_assert_cmpuint (mm_modem_get_state_failed_reason (mm_modem), ==, MM_MODEM_STATE_FAILED_REASON_UNKNOWN);

    /* Set new state and check that it is propagated */
    set_modem_state (tdata, MM_MODEM_STATE_REGISTERED, MM_MODEM_STATE_FAILED_REASON_NONE);

    g_assert_cmpuint (mm_modem_get_state (mm_modem), ==, MM_MODEM_STATE_REGISTERED);
    g_assert_cmpuint (mm_modem_get_state_failed_reason (mm_modem), ==, MM_MODEM_STATE_FAILED_REASON_NONE);

    g_clear_object (&mm_modem);
}

static void
mm_sim_send_pin_completion_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
    GError *error = NULL;
    gboolean ret = FALSE;
    TestData *tdata = NULL;

    tdata = (TestData*)user_data;

    mm_info("SendPin DBus method call completed");
    ret = mm_sim_send_pin_finish (tdata->sim, res, &error);
    if (tdata->pin_error) {
        g_assert_nonnull (error);
        g_assert_false (ret);
        g_clear_error (&error);
    } else {
        g_assert_no_error (error);
        g_assert_true (ret);
    }
    stop_loop (tdata);
}

static void
test_sim_interface (TestData **ptdata,
                    gconstpointer data)
{
    TestData *tdata = NULL;
    GDBusObject *modem_object = NULL;
    MMModem *mm_modem = NULL;
    GError *error = NULL;
    g_autofree gchar *modem_path = NULL;

    tdata = *ptdata;
    /* Add a modem with a sim object */
    modem_path = add_modem (tdata, TRUE, "89330122503000800750");
    modem_object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (tdata->mm_manager), modem_path);
    g_assert_nonnull (modem_object);
    mm_modem = mm_object_get_modem (MM_OBJECT (modem_object));
    g_clear_object (&modem_object);

    g_assert_cmpuint (mm_modem_get_unlock_required (mm_modem), ==, MM_MODEM_LOCK_NONE);
    /* Lock the modem */
    set_modem_unlock (tdata, MM_MODEM_LOCK_SIM_PIN);
    g_assert_cmpuint (mm_modem_get_unlock_required (mm_modem), ==, MM_MODEM_LOCK_SIM_PIN);

    tdata->sim = mm_modem_get_sim_sync (mm_modem, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (tdata->sim);
    g_assert_cmpstr (mm_sim_get_identifier(tdata->sim), ==, "89330122503000800750");

    /* Send a pin code */
    tdata->pin_error = FALSE;
    mm_sim_send_pin (tdata->sim, "1234", NULL, mm_sim_send_pin_completion_cb, tdata);
    run_loop_for_ms (tdata, 1000);

    /* Check that the modem has been unlocked */
    g_assert_cmpuint (mm_modem_get_unlock_required (mm_modem), ==, MM_MODEM_LOCK_NONE);

    /* Re lock it */
    set_modem_unlock (tdata, MM_MODEM_LOCK_SIM_PIN);
    g_assert_cmpuint (mm_modem_get_unlock_required (mm_modem), ==, MM_MODEM_LOCK_SIM_PIN);

    /* Set an error that will simulate wrong pin code */
    set_modem_equipment_error (tdata, MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD, FALSE);

    tdata->pin_error = TRUE;
    mm_sim_send_pin (tdata->sim, "0000", NULL, mm_sim_send_pin_completion_cb, tdata);
    run_loop_for_ms (tdata, 1000);

    g_assert_cmpuint (mm_modem_get_unlock_required (mm_modem), ==, MM_MODEM_LOCK_SIM_PIN);

    /* Clear the error and retry the pin code */
    set_modem_equipment_error (tdata, 0, TRUE);
    tdata->pin_error = FALSE;
    mm_sim_send_pin (tdata->sim, "1234", NULL, mm_sim_send_pin_completion_cb, tdata);
    run_loop_for_ms (tdata, 1000);

    g_assert_cmpuint (mm_modem_get_unlock_required (mm_modem), ==, MM_MODEM_LOCK_NONE);

    g_clear_object (&tdata->sim);
    g_clear_object (&mm_modem);
}

int main (int argc,
          char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add ("/MM/stub/modem/interface",
                TestData *, NULL, setup,
                test_modem_interface,
                teardown);
    g_test_add ("/MM/stub/sim/interface",
                TestData *, NULL, setup,
                test_sim_interface,
                teardown);
    return g_test_run ();
}
