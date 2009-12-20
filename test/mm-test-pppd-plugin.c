/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 - 2009 Red Hat, Inc.
 */

#include <string.h>
#include <pppd/pppd.h>
#include <pppd/fsm.h>
#include <pppd/ipcp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>

int plugin_init (void);

char pppd_version[] = VERSION;
char *my_user = NULL;
char *my_pass = NULL;
char *my_file = NULL;

static void
mm_phasechange (void *data, int arg)
{
	const char *ppp_phase = NULL;

	switch (arg) {
	case PHASE_DEAD:
		ppp_phase = "dead";
		break;
	case PHASE_INITIALIZE:
		ppp_phase = "initialize";
		break;
	case PHASE_SERIALCONN:
		ppp_phase = "serial connection";
		break;
	case PHASE_DORMANT:
		ppp_phase = "dormant";
		break;
	case PHASE_ESTABLISH:
		ppp_phase = "establish";
		break;
	case PHASE_AUTHENTICATE:
		ppp_phase = "authenticate";
		break;
	case PHASE_CALLBACK:
		ppp_phase = "callback";
		break;
	case PHASE_NETWORK:
		ppp_phase = "network";
		break;
	case PHASE_RUNNING:
		ppp_phase = "running";
		break;
	case PHASE_TERMINATE:
		ppp_phase = "terminate";
		break;
	case PHASE_DISCONNECT:
		ppp_phase = "disconnect";
		break;
	case PHASE_HOLDOFF:
		ppp_phase = "holdoff";
		break;
	case PHASE_MASTER:
		ppp_phase = "master";
		break;
	default:
		ppp_phase = "unknown";
		break;
	}

	g_message ("mm-test-ppp-plugin: (%s): phase now '%s'", __func__, ppp_phase);
}

static void
append_ip4_addr (GString *str, const char *tag, guint32 addr)
{
	char buf[INET_ADDRSTRLEN + 2];
	struct in_addr tmp_addr = { .s_addr = addr };

	memset (buf, 0, sizeof (buf));

	if (inet_ntop (AF_INET, &tmp_addr, buf, sizeof (buf) - 1))
		g_string_append_printf (str, "%s %s\n", tag, buf);
}

static void
mm_ip_up (void *data, int arg)
{
	ipcp_options opts = ipcp_gotoptions[0];
	ipcp_options peer_opts = ipcp_hisoptions[0];
	guint32 pppd_made_up_address = htonl (0x0a404040 + ifunit);
	GString *contents;
	GError *err = NULL;
	gboolean success;

	g_message ("mm-test-ppp-plugin: (%s): ip-up event", __func__);

	if (!opts.ouraddr) {
		g_warning ("mm-test-ppp-plugin: (%s): didn't receive an internal IP from pppd!", __func__);
		mm_phasechange (NULL, PHASE_DEAD);
		return;
	}

	contents = g_string_sized_new (100);

	g_string_append_printf (contents, "iface %s\n", ifname);

	append_ip4_addr (contents, "addr", opts.ouraddr);

	/* Prefer the peer options remote address first, _unless_ pppd made the
	 * address up, at which point prefer the local options remote address,
	 * and if that's not right, use the made-up address as a last resort.
	 */
	if (peer_opts.hisaddr && (peer_opts.hisaddr != pppd_made_up_address))
		append_ip4_addr (contents, "gateway", peer_opts.hisaddr);
	else if (opts.hisaddr)
		append_ip4_addr (contents, "gateway", opts.hisaddr);
	else if (peer_opts.hisaddr == pppd_made_up_address) {
		/* As a last resort, use the made-up address */
		append_ip4_addr (contents, "gateway", peer_opts.hisaddr);
	}

	if (opts.dnsaddr[0] || opts.dnsaddr[1]) {
		if (opts.dnsaddr[0])
			append_ip4_addr (contents, "dns1", opts.dnsaddr[0]);
		if (opts.dnsaddr[1])
			append_ip4_addr (contents, "dns2", opts.dnsaddr[1]);
	}

	if (opts.winsaddr[0] || opts.winsaddr[1]) {
		if (opts.winsaddr[0])
			append_ip4_addr (contents, "wins1", opts.winsaddr[0]);
		if (opts.winsaddr[1])
			append_ip4_addr (contents, "wins2", opts.winsaddr[1]);
	}

	g_string_append (contents, "DONE\n");

	success = g_file_set_contents (my_file, contents->str, -1, &err);
	if (success)
		g_message ("nm-ppp-plugin: (%s): saved IP4 config to %s", __func__, my_file);
	else {
		g_message ("nm-ppp-plugin: (%s): error saving IP4 config to %s: (%d) %s",
		           __func__, my_file, err->code, err->message);
		g_clear_error (&err);
	}

	g_string_free (contents, TRUE);
}

static int
get_chap_check()
{
	return 1;
}

static int
get_pap_check()
{
	return 1;
}

static int
get_credentials (char *username, char *password)
{
	size_t len;

	if (username && !password) {
		/* pppd is checking pap support; return 1 for supported */
		return 1;
	}

	g_message ("nm-ppp-plugin: (%s): sending credentials (%s / %s)",
	           __func__,
	           my_user ? my_user : "",
	           my_pass ? my_pass : "");

	if (my_user) {
		len = strlen (my_user) + 1;
		len = len < MAXNAMELEN ? len : MAXNAMELEN;

		strncpy (username, my_user, len);
		username[len - 1] = '\0';
	}

	if (my_pass) {
		len = strlen (my_pass) + 1;
		len = len < MAXSECRETLEN ? len : MAXSECRETLEN;

		strncpy (password, my_pass, len);
		password[len - 1] = '\0';
	}

	return 1;
}

static void
mm_exit_notify (void *data, int arg)
{
	g_message ("mm-test-ppp-plugin: (%s): cleaning up", __func__);

	g_free (my_user);
	my_user = NULL;
	g_free (my_pass);
	my_pass = NULL;
	g_free (my_file);
	my_file = NULL;
}

int
plugin_init (void)
{
	char **args;

	g_message ("mm-test-ppp-plugin: (%s): initializing", __func__);

	/* mm-test passes the file + username + password in as the 'ipparam' arg
	 * to pppd.
	 */
	args = g_strsplit (ipparam, "+", 0);
	if (!args || g_strv_length (args) != 3) {
		g_message ("mm-test-ppp-plugin: (%s): ipparam arguments error ('%s')",
		           __func__, ipparam);
		return -1;
	}

	my_user = (args[0] && strlen (args[0])) ? g_strdup (args[0]) : NULL;
	my_pass = (args[1] && strlen (args[1])) ? g_strdup (args[1]) : NULL;
	my_file = (args[2] && strlen (args[2])) ? g_strdup (args[2]) : NULL;

	g_strfreev (args);

	if (!my_file) {
		g_message ("mm-test-ppp-plugin: (%s): missing IP config file",
		           __func__);
		return -1;
	}

	chap_passwd_hook = get_credentials;
	chap_check_hook = get_chap_check;
	pap_passwd_hook = get_credentials;
	pap_check_hook = get_pap_check;

	add_notifier (&phasechange, mm_phasechange, NULL);
	add_notifier (&ip_up_notifier, mm_ip_up, NULL);
	add_notifier (&exitnotify, mm_exit_notify, NULL);

	return 0;
}
