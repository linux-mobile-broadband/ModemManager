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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    MM_UDEV_RULE_MATCH_TYPE_UNKNOWN,
    MM_UDEV_RULE_MATCH_TYPE_EQUAL,
    MM_UDEV_RULE_MATCH_TYPE_NOT_EQUAL,
} MMUdevRuleMatchType;

typedef struct {
    MMUdevRuleMatchType  type;
    gchar               *parameter;
    gchar               *value;
} MMUdevRuleMatch;

typedef enum {
    MM_UDEV_RULE_RESULT_TYPE_UNKNOWN,
    MM_UDEV_RULE_RESULT_TYPE_PROPERTY,
    MM_UDEV_RULE_RESULT_TYPE_LABEL,
    MM_UDEV_RULE_RESULT_TYPE_GOTO_INDEX,
    MM_UDEV_RULE_RESULT_TYPE_GOTO_TAG, /* internal use only */
} MMUdevRuleResultType;

typedef struct {
    gchar *name;
    gchar *value;
} MMUdevRuleResultProperty;

typedef struct {
    MMUdevRuleResultType type;
    union {
        MMUdevRuleResultProperty  property;
        gchar                    *tag;
        guint                     index;
    } content;
} MMUdevRuleResult;

typedef struct {
    GArray           *conditions;
    MMUdevRuleResult  result;
} MMUdevRule;

GArray *mm_kernel_device_generic_rules_load (const gchar  *rules_dir,
                                             GError      **error);

G_END_DECLS
