/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-kernel-device-generic-rules.h"

static void
udev_rule_match_clear (MMUdevRuleMatch *rule_match)
{
    g_free (rule_match->parameter);
    g_free (rule_match->value);
}

static void
udev_rule_clear (MMUdevRule *rule)
{
    switch (rule->result.type) {
    case MM_UDEV_RULE_RESULT_TYPE_PROPERTY:
        g_free (rule->result.content.property.name);
        g_free (rule->result.content.property.value);
        break;
    case MM_UDEV_RULE_RESULT_TYPE_GOTO_TAG:
    case MM_UDEV_RULE_RESULT_TYPE_LABEL:
        g_free (rule->result.content.tag);
        break;
    case MM_UDEV_RULE_RESULT_TYPE_GOTO_INDEX:
    case MM_UDEV_RULE_RESULT_TYPE_UNKNOWN:
    default:
        break;
    }

    if (rule->conditions)
        g_array_unref (rule->conditions);
}

static gboolean
split_item (const gchar  *item,
            gchar       **out_left,
            gchar       **out_operator,
            gchar       **out_right,
            GError      **error)
{
    const gchar *aux;
    gchar       *left = NULL;
    gchar       *operator = NULL;
    gchar       *right = NULL;
    GError      *inner_error = NULL;

    g_assert (item && out_left && out_operator && out_right);

    /* Get left/operator/right */
    if (((aux = strstr (item, "==")) != NULL) || ((aux = strstr (item, "!=")) != NULL)) {
        operator = g_strndup (aux, 2);
        right    = g_strdup (aux + 2);
    } else if ((aux = strstr (item, "=")) != NULL) {
        operator = g_strndup (aux, 1);
        right    = g_strdup (aux + 1);
    } else {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Invalid rule item, missing operator: '%s'", item);
        goto out;
    }

    left = g_strndup (item, (aux - item));
    g_strstrip (left);
    if (!left[0]) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Invalid rule item, missing left field: '%s'", item);
        goto out;
    }

    g_strdelimit (right, "\"", ' ');
    g_strstrip (right);
    if (!right[0]) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Invalid rule item, missing right field: '%s'", item);
        goto out;
    }

out:
    if (inner_error) {
        g_free (left);
        g_free (operator);
        g_free (right);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *out_left     = left;
    *out_operator = operator;
    *out_right    = right;
    return TRUE;
}

static gboolean
load_rule_result (MMUdevRuleResult  *rule_result,
                  const gchar       *item,
                  GError           **error)
{
    gchar  *left;
    gchar  *operator;
    gchar  *right;
    GError *inner_error = NULL;
    gsize   left_len;

    if (!split_item (item, &left, &operator, &right, error))
        return FALSE;

    if (!g_str_equal (operator, "=")) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Invalid rule result operator: '%s'", item);
        goto out;
    }

    if (g_str_equal (left, "LABEL")) {
        rule_result->type = MM_UDEV_RULE_RESULT_TYPE_LABEL;
        rule_result->content.tag = right;
        right = NULL;
        goto out;
    }

    if (g_str_equal (left, "GOTO")) {
        rule_result->type = MM_UDEV_RULE_RESULT_TYPE_GOTO_TAG;
        rule_result->content.tag = right;
        right = NULL;
        goto out;
    }

    left_len = strlen (left);
    if (g_str_has_prefix (left, "ENV{") && left[left_len - 1] == '}') {
        rule_result->type = MM_UDEV_RULE_RESULT_TYPE_PROPERTY;
        rule_result->content.property.name = g_strndup (left + 4, left_len - 5);
        rule_result->content.property.value = right;
        right = NULL;
        goto out;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Invalid rule result parameter: '%s'", item);

out:
    g_free (left);
    g_free (operator);
    g_free (right);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

static gboolean
load_rule_match (MMUdevRuleMatch  *rule_match,
                 const gchar      *item,
                 GError          **error)
{
    gchar  *left;
    gchar  *operator;
    gchar  *right;

    if (!split_item (item, &left, &operator, &right, error))
        return FALSE;

    if (g_str_equal (operator, "=="))
        rule_match->type = MM_UDEV_RULE_MATCH_TYPE_EQUAL;
    else if (g_str_equal (operator, "!="))
        rule_match->type = MM_UDEV_RULE_MATCH_TYPE_NOT_EQUAL;
    else {
        g_free (left);
        g_free (operator);
        g_free (right);
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Invalid rule match, wrong match type: '%s'", item);

        return FALSE;
    }

    g_free (operator);
    rule_match->parameter = left;
    rule_match->value     = right;
    return TRUE;
}

static gboolean
load_rule_from_line (MMUdevRule   *rule,
                     const gchar  *line,
                     GError      **error)
{
    gchar  **split;
    guint    n_items;
    GError  *inner_error = NULL;

    split = g_strsplit (line, ",", -1);
    n_items = g_strv_length (split);

    /* Conditions */
    if (n_items > 1) {
        guint i;

        rule->conditions = g_array_sized_new (FALSE, FALSE, sizeof (MMUdevRuleMatch), n_items - 1);
        g_array_set_clear_func (rule->conditions, (GDestroyNotify) udev_rule_match_clear);

        /* All items except for the last one are conditions */
        for (i = 0; !inner_error && i < (n_items - 1); i++) {
            MMUdevRuleMatch rule_match = { 0 };

            /* If condition correctly preloaded, add it to the rule */
            if (!load_rule_match (&rule_match, split[i], &inner_error))
                goto out;
            g_assert (rule_match.type != MM_UDEV_RULE_MATCH_TYPE_UNKNOWN);
            g_assert (rule_match.parameter);
            g_assert (rule_match.value);
            g_array_append_val (rule->conditions, rule_match);
        }
    }

    /* Last item, the result */
    if (!load_rule_result (&rule->result, split[n_items - 1], &inner_error))
        goto out;

    g_assert ((rule->result.type == MM_UDEV_RULE_RESULT_TYPE_GOTO_TAG && rule->result.content.tag) ||
              (rule->result.type == MM_UDEV_RULE_RESULT_TYPE_LABEL && rule->result.content.tag) ||
              (rule->result.type == MM_UDEV_RULE_RESULT_TYPE_PROPERTY && rule->result.content.property.name && rule->result.content.property.value));

out:
    g_strfreev (split);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean
process_goto_tags (GArray  *rules,
                   guint    first_rule_index,
                   GError **error)
{
    guint i;

    for (i = first_rule_index; i < rules->len; i++) {
        MMUdevRule *rule;

        rule = &g_array_index (rules, MMUdevRule, i);
        if (rule->result.type == MM_UDEV_RULE_RESULT_TYPE_GOTO_TAG) {
            guint j;
            guint label_index = 0;

            for (j = i + 1; j < rules->len; j++) {
                MMUdevRule *walker;

                walker = &g_array_index (rules, MMUdevRule, j);
                if (walker->result.type == MM_UDEV_RULE_RESULT_TYPE_LABEL &&
                    g_str_equal (rule->result.content.tag, walker->result.content.tag)) {

                    if (label_index) {
                        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "More than one label '%s' found", rule->result.content.tag);
                        return FALSE;
                    }

                    label_index = j;
                }
            }

            if (!label_index) {
                g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find label '%s'", rule->result.content.tag);
                return FALSE;
            }

            rule->result.type = MM_UDEV_RULE_RESULT_TYPE_GOTO_INDEX;
            g_free (rule->result.content.tag);
            rule->result.content.index = label_index;
        }
    }

    return TRUE;
}

static gboolean
load_rules_from_file (GArray       *rules,
                      const gchar  *path,
                      GError      **error)
{
    GFile            *file;
    GFileInputStream *fistream;
    GDataInputStream *distream = NULL;
    GError           *inner_error = NULL;
    gchar            *line;
    guint             first_rule_index;

    first_rule_index = rules->len;

    file = g_file_new_for_path (path);
    fistream = g_file_read (file, NULL, &inner_error);
    if (!fistream)
        goto out;

    distream = g_data_input_stream_new (G_INPUT_STREAM (fistream));

    while (((line = g_data_input_stream_read_line_utf8 (distream, NULL, NULL, &inner_error)) != NULL) && !inner_error) {
        const gchar *aux;

        aux = line;
        while (*aux == ' ')
            aux++;
        if (*aux != '#' && *aux != '\0') {
            MMUdevRule rule = { 0 };

            if (load_rule_from_line (&rule, aux, &inner_error))
                g_array_append_val (rules, rule);
            else
                udev_rule_clear (&rule);
        }
        g_free (line);
    }

out:

    if (distream)
        g_object_unref (distream);
    if (fistream)
        g_object_unref (fistream);
    g_object_unref (file);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (first_rule_index < rules->len && !process_goto_tags (rules, first_rule_index, error))
        return FALSE;

    return TRUE;
}

static GList *
list_rule_files (const gchar *rules_dir_path)
{
    static const gchar *expected_rules_prefix[] = { "77-mm-", "78-mm-", "79-mm-", "80-mm-" };
    GFile              *udevrulesdir;
    GFileEnumerator    *enumerator;
    GList              *children = NULL;

    udevrulesdir = g_file_new_for_path (rules_dir_path);
    enumerator   = g_file_enumerate_children (udevrulesdir,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL,
                                              NULL);
    if (enumerator) {
        GFileInfo *info;

        /* If we get any kind of error, assume we need to stop enumerating */
        while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
            guint i;

            for (i = 0; i < G_N_ELEMENTS (expected_rules_prefix); i++) {
                if (g_str_has_prefix (g_file_info_get_name (info), expected_rules_prefix[i])) {
                    children = g_list_prepend (children, g_build_path (G_DIR_SEPARATOR_S, rules_dir_path, g_file_info_get_name (info), NULL));
                    break;
                }
            }
            g_object_unref (info);
        }
        g_object_unref (enumerator);
    }
    g_object_unref (udevrulesdir);

    return g_list_sort (children, (GCompareFunc) g_strcmp0);
}

GArray *
mm_kernel_device_generic_rules_load (const gchar  *rules_dir,
                                     GError      **error)
{
    GList  *rule_files, *l;
    GArray *rules;
    GError *inner_error = NULL;

    rules = g_array_new (FALSE, FALSE, sizeof (MMUdevRule));
    g_array_set_clear_func (rules, (GDestroyNotify) udev_rule_clear);

    /* List rule files in rules dir */
    rule_files = list_rule_files (rules_dir);
    if (!rule_files) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No rule files found in '%s'", rules_dir);
        goto out;
    }

    /* Iterate over rule files */
    for (l = rule_files; l; l = g_list_next (l)) {
        if (!load_rules_from_file (rules, (const gchar *)(l->data), &inner_error))
            goto out;
    }

    /* Fail if no rules were loaded */
    if (rules->len == 0) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "No rules loaded");
        goto out;
    }

out:
    if (rule_files)
        g_list_free_full (rule_files, g_free);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_array_unref (rules);
        return NULL;
    }

    return rules;
}
