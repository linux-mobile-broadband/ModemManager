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

#ifndef MM_TEST_UTILS_H
#define MM_TEST_UTILS_H

#include <glib.h>
#include <glib-object.h>

/*****************************************************************************/

/* cmp = TRUE (s1 contains s2) or FALSE (s1 does not contain s2) */
#define mm_assert_strstr(s1, cmp, s2) \
    G_STMT_START { \
        const char *__s1 = (s1), *__s2 = (s2); \
        if (strstr (__s1, __s2) == cmp) ; else \
            g_assertion_message_cmpstr (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                                        #s1 " %s " #s2, \
                                        __s1, \
                                        cmp ? "contains" : "does not contain", \
                                        __s2); \
    } G_STMT_END

/* Asserts that err's message contains s1 */
#define mm_assert_error_str(err, s1) \
    G_STMT_START { \
        if (!err || !err->message || !strstr (err->message, s1)) { \
            GString *gstring; \
            gstring = g_string_new ("assertion failed "); \
            if (err) \
                g_string_append_printf (gstring, "%s (%s, %d) did not contain '%s'", \
                    err->message, g_quark_to_string (err->domain), err->code, s1); \
            else \
                g_string_append_printf (gstring, "%s is NULL", #err); \
            g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, gstring->str); \
            g_string_free (gstring, TRUE); \
        } \
    } G_STMT_END

#endif /* MM_TEST_UTILS_H */
