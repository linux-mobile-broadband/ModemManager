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

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/error-helpers/connection-error",       test_error_helpers_connection_error);
    g_test_add_func ("/MM/error-helpers/mobile-equipment-error", test_error_helpers_mobile_equipment_error);
    g_test_add_func ("/MM/error-helpers/message-error",          test_error_helpers_message_error);

    return g_test_run ();
}
