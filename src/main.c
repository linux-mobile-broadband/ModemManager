/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <libhal.h>
#include "mm-manager.h"
#include "mm-options.h"

#define HAL_DBUS_SERVICE "org.freedesktop.Hal"

static void
mm_signal_handler (int signo)
{
    if (signo == SIGUSR1)
        mm_options_set_debug (!mm_options_debug ());
}

static void
setup_signals (void)
{
    struct sigaction action;
    sigset_t mask;

    sigemptyset (&mask);
    action.sa_handler = mm_signal_handler;
    action.sa_mask = mask;
    action.sa_flags = 0;
    sigaction (SIGUSR1,  &action, NULL);
}

static void
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer ignored)
{
    int syslog_priority;    

    switch (log_level) {
    case G_LOG_LEVEL_ERROR:
        syslog_priority = LOG_CRIT;
        break;

    case G_LOG_LEVEL_CRITICAL:
        syslog_priority = LOG_ERR;
        break;

    case G_LOG_LEVEL_WARNING:
        syslog_priority = LOG_WARNING;
        break;

    case G_LOG_LEVEL_MESSAGE:
        syslog_priority = LOG_NOTICE;
        break;

    case G_LOG_LEVEL_DEBUG:
        syslog_priority = LOG_DEBUG;
        break;

    case G_LOG_LEVEL_INFO:
    default:
        syslog_priority = LOG_INFO;
        break;
    }

    syslog (syslog_priority, "%s", message);
}


static void
logging_setup (void)
{
    openlog (G_LOG_DOMAIN, LOG_CONS, LOG_DAEMON);
    g_log_set_handler (G_LOG_DOMAIN, 
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);
}

static void
logging_shutdown (void)
{
    closelog ();
}

static void
destroy_cb (DBusGProxy *proxy, gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *) user_data;

    g_message ("disconnected from the system bus, exiting.");
    g_main_loop_quit (loop);
}

static DBusGProxy *
create_dbus_proxy (DBusGConnection *bus)
{
    DBusGProxy *proxy;
    GError *err = NULL;
    int request_name_result;

    proxy = dbus_g_proxy_new_for_name (bus,
                                       "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus");

    if (!dbus_g_proxy_call (proxy, "RequestName", &err,
                            G_TYPE_STRING, MM_DBUS_SERVICE,
                            G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &request_name_result,
                            G_TYPE_INVALID)) {
        g_warning ("Could not acquire the %s service.\n"
                   "  Message: '%s'", MM_DBUS_SERVICE, err->message);

        g_error_free (err);
        g_object_unref (proxy);
        proxy = NULL;
    } else if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        g_warning ("Could not acquire the " MM_DBUS_SERVICE
                   " service as it is already taken. Return: %d",
                   request_name_result);

        g_object_unref (proxy);
        proxy = NULL;
    } else {
        dbus_g_proxy_add_signal (proxy, "NameOwnerChanged",
                                 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                 G_TYPE_INVALID);
    }

    return proxy;
}

static void
hal_init (MMManager *manager)
{
    LibHalContext *hal_ctx;
    DBusError dbus_error;

    hal_ctx = libhal_ctx_new ();
	if (!hal_ctx) {
		g_warning ("Could not get connection to the HAL service.");
    }

	libhal_ctx_set_dbus_connection (hal_ctx, dbus_g_connection_get_connection (mm_manager_get_bus (manager)));

	dbus_error_init (&dbus_error);
	if (!libhal_ctx_init (hal_ctx, &dbus_error)) {
		g_warning ("libhal_ctx_init() failed: %s", dbus_error.message);
        dbus_error_free (&dbus_error);
    }

    mm_manager_set_hal_ctx (manager, hal_ctx);
}

static void
hal_deinit (MMManager *manager)
{
    LibHalContext *hal_ctx;

    hal_ctx = mm_manager_get_hal_ctx (manager);
    if (hal_ctx) {
        libhal_ctx_shutdown (hal_ctx, NULL);
        libhal_ctx_free (hal_ctx);
        mm_manager_set_hal_ctx (manager, NULL);
    }
}

static gboolean
hal_on_bus (DBusGProxy *proxy)
{
    GError *err = NULL;
    gboolean has_owner = FALSE;

    if (!dbus_g_proxy_call (proxy,
                            "NameHasOwner", &err,
                            G_TYPE_STRING, HAL_DBUS_SERVICE,
                            G_TYPE_INVALID,
                            G_TYPE_BOOLEAN, &has_owner,
                            G_TYPE_INVALID)) {
        g_warning ("Error on NameHasOwner DBUS call: %s", err->message);
        g_error_free (err);
    }

    return has_owner;
}

static void
name_owner_changed (DBusGProxy *proxy,
                    const char *name,
                    const char *old_owner,
                    const char *new_owner,
                    gpointer user_data)
{
    MMManager *manager;
    gboolean old_owner_good;
    gboolean new_owner_good;

    /* Only care about signals from HAL */
    if (strcmp (name, HAL_DBUS_SERVICE))
        return;

    manager = MM_MANAGER (user_data);
    old_owner_good = (old_owner && (strlen (old_owner) > 0));
    new_owner_good = (new_owner && (strlen (new_owner) > 0));

	if (!old_owner_good && new_owner_good) {
		g_message ("HAL appeared");
		hal_init (manager);
	} else if (old_owner_good && !new_owner_good) {
		/* HAL went away. Bad HAL. */
		g_message ("HAL disappeared");
		hal_deinit (manager);
	}
}

int
main (int argc, char *argv[])
{
    DBusGConnection *bus;
    DBusGProxy *proxy;
    GMainLoop *loop;
    MMManager *manager;
    GError *err = NULL;

    mm_options_parse (argc, argv);
    g_type_init ();

    setup_signals ();

    if (!mm_options_debug ())
        logging_setup ();

    bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
    if (!bus) {
        g_warning ("Could not get the system bus. Make sure "
                   "the message bus daemon is running! Message: %s",
                   err->message);
        g_error_free (err);
        return -1;
    }

    proxy = create_dbus_proxy (bus);
    if (!proxy)
        return -1;

    manager = mm_manager_new (bus);

    dbus_g_proxy_connect_signal (proxy,
                                 "NameOwnerChanged",
                                 G_CALLBACK (name_owner_changed),
                                 manager, NULL);

    if (hal_on_bus (proxy))
        hal_init (manager);

    loop = g_main_loop_new (NULL, FALSE);
    g_signal_connect (proxy, "destroy", G_CALLBACK (destroy_cb), loop);

    g_main_loop_run (loop);

    hal_deinit (manager);
    g_object_unref (manager);
    g_object_unref (proxy);
    dbus_g_connection_unref (bus);    

    logging_shutdown ();

    return 0;
}
