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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include "mm-error-helpers.h"
#include "mm-log.h"

#define TEST_ERROR_HELPER(ERROR_CAPS,ERROR_SMALL,ERROR_CAMEL)               \
  static void                                                               \
  test_error_helpers_## ERROR_SMALL (void)                                  \
  {                                                                         \
      GError     *error;                                                    \
      gint       i;                                                        \
      GEnumClass *enum_class;                                               \
                                                                            \
      enum_class = g_type_class_ref (MM_TYPE_ ## ERROR_CAPS);               \
      for (i = enum_class->minimum; i <= enum_class->maximum; i++) {        \
          GEnumValue *enum_value;                                           \
                                                                            \
          enum_value = g_enum_get_value (enum_class, i);                    \
          if (enum_value) {                                                 \
              error = mm_## ERROR_SMALL ## _for_code ((MM##ERROR_CAMEL)i, NULL); \
              g_assert_error (error, MM_ ## ERROR_CAPS, i);                 \
              g_error_free (error);                                         \
          }                                                                 \
      }                                                                     \
      g_type_class_unref (enum_class);                                      \
  }

TEST_ERROR_HELPER (CONNECTION_ERROR,       connection_error,       ConnectionError)
TEST_ERROR_HELPER (MOBILE_EQUIPMENT_ERROR, mobile_equipment_error, MobileEquipmentError)
TEST_ERROR_HELPER (MESSAGE_ERROR,          message_error,          MessageError)

/*****************************************************************************/

static void
test_error_helpers_mobile_equipment_error_for_code (void)
{
    GError *error = NULL;

    /* first */
    error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE, NULL);
    g_assert_error (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE);
    g_clear_error (&error);

    /* last */
    error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED, NULL);
    g_assert_error (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED);
    g_clear_error (&error);

    /* other > 255 */
    error = mm_mobile_equipment_error_for_code (256, NULL);
    g_assert_error (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN);
    g_clear_error (&error);
}

static void
test_error_helpers_message_error_for_code (void)
{
    GError *error = NULL;

    /* first */
    error = mm_message_error_for_code (MM_MESSAGE_ERROR_ME_FAILURE, NULL);
    g_assert_error (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_ME_FAILURE);
    g_clear_error (&error);

    /* last */
    error = mm_message_error_for_code (MM_MESSAGE_ERROR_UNKNOWN, NULL);
    g_assert_error (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_UNKNOWN);
    g_clear_error (&error);

    /* other < 300 */
    error = mm_message_error_for_code (299, NULL);
    g_assert_error (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_UNKNOWN);
    g_clear_error (&error);

    /* other > 500 */
    error = mm_message_error_for_code (501, NULL);
    g_assert_error (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_UNKNOWN);
    g_clear_error (&error);
}

/*****************************************************************************/

static void
test_error_helpers_mobile_equipment_error_for_string (void)
{
    GError *error = NULL;

    error = mm_mobile_equipment_error_for_string ("operationnotallowed", NULL);
    g_assert_error (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED);
    g_clear_error (&error);

    error = mm_mobile_equipment_error_for_string ("arratsaldeon", NULL);
    g_assert_error (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN);
    g_clear_error (&error);
}

static void
test_error_helpers_message_error_for_string (void)
{
    GError *error = NULL;

    error = mm_message_error_for_string ("operationnotallowed", NULL);
    g_assert_error (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_NOT_ALLOWED);
    g_clear_error (&error);

    error = mm_message_error_for_string ("arratsaldeon", NULL);
    g_assert_error (error, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_UNKNOWN);
    g_clear_error (&error);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/error-helpers/connection-error",       test_error_helpers_connection_error);
    g_test_add_func ("/MM/error-helpers/mobile-equipment-error", test_error_helpers_mobile_equipment_error);
    g_test_add_func ("/MM/error-helpers/message-error",          test_error_helpers_message_error);

    g_test_add_func ("/MM/error-helpers/mobile-equipment-error/for-code",   test_error_helpers_mobile_equipment_error_for_code);
    g_test_add_func ("/MM/error-helpers/message-error/for-code",            test_error_helpers_message_error_for_code);

    g_test_add_func ("/MM/error-helpers/mobile-equipment-error/for-string", test_error_helpers_mobile_equipment_error_for_string);
    g_test_add_func ("/MM/error-helpers/message-error/for-string",          test_error_helpers_message_error_for_string);

    return g_test_run ();
}
