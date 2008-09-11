/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <syslog.h>
#include <dbus/dbus-glib.h>
#include "mm-manager.h"
#include "mm-options.h"

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

static gboolean
dbus_init (GMainLoop *loop)
{
    DBusGConnection *connection;
    DBusGProxy *proxy;
    GError *err = NULL;
    int request_name_result;

    connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
    if (!connection) {
        g_warning ("Could not get the system bus. Make sure "
		           "the message bus daemon is running! Message: %s",
		           err->message);
		g_error_free (err);
		return FALSE;
	}

    proxy = dbus_g_proxy_new_for_name (connection,
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
        goto err;
	}

	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_warning ("Could not acquire the NetworkManagerSystemSettings service "
		           "as it is already taken. Return: %d",
		           request_name_result);
        goto err;
	}

    g_signal_connect (proxy, "destroy", G_CALLBACK (destroy_cb), loop);

    return TRUE;

 err:
    dbus_g_connection_unref (connection);
    g_object_unref (proxy);

    return FALSE;
}

int
main (int argc, char *argv[])
{
    GMainLoop *loop;
    MMManager *manager;

    mm_options_parse (argc, argv);
    g_type_init ();

	if (!mm_options_debug ())
		logging_setup ();

    loop = g_main_loop_new (NULL, FALSE);

    if (!dbus_init (loop))
        return -1;

    manager = mm_manager_new ();

    g_main_loop_run (loop);

    g_object_unref (manager);
    logging_shutdown ();

    return 0;
}
