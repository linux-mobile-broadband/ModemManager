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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <mm-log-test.h>
#include <mm-kernel-device-generic-rules.h>

#define PROGRAM_NAME    "mmrules"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Context */
static gchar    *path;
static gboolean  verbose_flag;
static gboolean  version_flag;

static GOptionEntry main_entries[] = {
    { "path", 'p', 0, G_OPTION_ARG_FILENAME, &path,
      "Specify path to udev rules directory",
      "[PATH]"
    },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose_flag,
      "Run action with verbose logs",
      NULL
    },
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag,
      "Print version",
      NULL
    },
    { NULL }
};

static void
print_version_and_exit (void)
{
    g_print ("\n"
             PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2015) Aleksander Morgado\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}

static void
print_rule (MMUdevRule *rule)
{
    /* Process conditions */
    if (rule->conditions) {
        guint i;

        for (i = 0; i < rule->conditions->len; i++) {
            MMUdevRuleMatch *rule_match;

            rule_match = &g_array_index (rule->conditions, MMUdevRuleMatch, i);
            switch (rule_match->type) {
            case MM_UDEV_RULE_MATCH_TYPE_EQUAL:
                g_print ("  [condition %u] %s == %s\n",
                           i, rule_match->parameter, rule_match->value);
                break;
            case MM_UDEV_RULE_MATCH_TYPE_NOT_EQUAL:
                g_print ("  [condition %u] %s != %s\n",
                         i, rule_match->parameter, rule_match->value);
                break;
            case MM_UDEV_RULE_MATCH_TYPE_UNKNOWN:
            default:
                g_assert_not_reached ();
            }
        }
    }

    /* Process result */
    switch (rule->result.type) {
    case MM_UDEV_RULE_RESULT_TYPE_LABEL:
        g_print ("  [result] label %s\n", rule->result.content.tag);
        break;
    case MM_UDEV_RULE_RESULT_TYPE_GOTO_INDEX:
        g_print ("  [result] jump to rule %u\n", rule->result.content.index);
        break;
    case MM_UDEV_RULE_RESULT_TYPE_PROPERTY:
        g_print ("  [result] set property %s = %s\n",
                 rule->result.content.property.name, rule->result.content.property.value);
        break;
    case MM_UDEV_RULE_RESULT_TYPE_GOTO_TAG:
    case MM_UDEV_RULE_RESULT_TYPE_UNKNOWN:
    default:
        g_assert_not_reached ();
    }
}

int main (int argc, char **argv)
{
    GOptionContext *context;
    GArray         *rules;
    guint           i;
    GError         *error = NULL;

    setlocale (LC_ALL, "");

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- ModemManager udev rules testing");
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    /* No device path given? */
    if (!path) {
        g_printerr ("error: no path specified\n");
        exit (EXIT_FAILURE);
    }

    /* Load rules from directory */
    rules = mm_kernel_device_generic_rules_load (path, &error);
    if (!rules) {
        g_printerr ("error: couldn't load rules: %s", error->message);
        exit (EXIT_FAILURE);
    }

    /* Print loaded rules */
    for (i = 0; i < rules->len; i++) {
        g_print ("-----------------------------------------\n");
        g_print ("rule [%u]:\n", i);
        print_rule (&g_array_index (rules, MMUdevRule, i));
    }

    g_array_unref (rules);

    return EXIT_SUCCESS;
}
