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

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include "mm-error-helpers.h"
#include "mm-log.h"

#if defined WITH_QMI
# include <libqmi-glib.h>
#endif

#if defined WITH_MBIM
# include <libmbim-glib.h>
#endif

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

static void
normalize_and_assert_mm_error (gboolean     expect_fallback,
                               GQuark       domain,
                               int          code,
                               const gchar *description)
{
    g_autoptr(GError) input_error = NULL;
    g_autoptr(GError) normalized_error = NULL;

    g_debug ("Checking normalized error: %s", description);

    input_error = g_error_new_literal (domain, code, description);
    normalized_error = mm_normalize_error (input_error);

    g_assert (normalized_error);
    g_assert ((normalized_error->domain == MM_CORE_ERROR) ||
              (normalized_error->domain == MM_MOBILE_EQUIPMENT_ERROR) ||
              (normalized_error->domain == MM_CONNECTION_ERROR) ||
              (normalized_error->domain == MM_SERIAL_ERROR) ||
              (normalized_error->domain == MM_MESSAGE_ERROR) ||
              (normalized_error->domain == MM_CDMA_ACTIVATION_ERROR) ||
              g_error_matches (normalized_error, G_IO_ERROR, G_IO_ERROR_CANCELLED));

    g_assert (expect_fallback == g_error_matches (normalized_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED));
}

static void
test_error_helpers_normalize (void)
{
    normalize_and_assert_mm_error (TRUE,  G_IO_ERROR, G_IO_ERROR_FAILED,    "GIO error");
    normalize_and_assert_mm_error (FALSE, G_IO_ERROR, G_IO_ERROR_CANCELLED, "GIO error cancelled");

    normalize_and_assert_mm_error (TRUE, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Known core error"); /* This is exactly the fallback */
    normalize_and_assert_mm_error (TRUE, MM_CORE_ERROR, 123456,               "Unknown core error");

    normalize_and_assert_mm_error (FALSE, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK, "Known ME error");
    normalize_and_assert_mm_error (TRUE,  MM_MOBILE_EQUIPMENT_ERROR, 123456789,                            "Unknown ME error");

    normalize_and_assert_mm_error (FALSE, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER, "Known connection error");
    normalize_and_assert_mm_error (TRUE,  MM_CONNECTION_ERROR, 123456,                         "Unknown connection error");

    normalize_and_assert_mm_error (FALSE, MM_SERIAL_ERROR, MM_SERIAL_ERROR_OPEN_FAILED, "Known serial error");
    normalize_and_assert_mm_error (TRUE,  MM_SERIAL_ERROR, 123456,                      "Unknown serial error");

    normalize_and_assert_mm_error (FALSE, MM_MESSAGE_ERROR, MM_MESSAGE_ERROR_ME_FAILURE, "Known message error");
    normalize_and_assert_mm_error (TRUE,  MM_MESSAGE_ERROR, 123456,                      "Unknown message error");

    normalize_and_assert_mm_error (FALSE, MM_CDMA_ACTIVATION_ERROR, MM_CDMA_ACTIVATION_ERROR_ROAMING, "Known CDMA activation error");
    normalize_and_assert_mm_error (TRUE,  MM_CDMA_ACTIVATION_ERROR, 123456,                           "Unknown CDMA activation error");

#if defined WITH_QMI
    normalize_and_assert_mm_error (FALSE, QMI_CORE_ERROR, QMI_CORE_ERROR_TIMEOUT, "Known QMI core error");
    normalize_and_assert_mm_error (TRUE,  QMI_CORE_ERROR, 123456,                 "Unknown QMI core error");

    normalize_and_assert_mm_error (FALSE, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_ABORTED, "Known QMI protocol error");
    normalize_and_assert_mm_error (TRUE,  QMI_PROTOCOL_ERROR, 123456,                     "Unknown QMI protocol error");
#endif

#if defined WITH_MBIM
    normalize_and_assert_mm_error (FALSE, MBIM_CORE_ERROR, MBIM_CORE_ERROR_ABORTED, "Known MBIM core error");
    normalize_and_assert_mm_error (TRUE,  MBIM_CORE_ERROR, 123456,                  "Unknown MBIM core error");

    normalize_and_assert_mm_error (FALSE, MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_CANCEL, "Known MBIM protocol error");
    normalize_and_assert_mm_error (TRUE,  MBIM_PROTOCOL_ERROR, 123456,                     "Unknown MBIM protocol error");

    normalize_and_assert_mm_error (FALSE, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_BUSY, "Known MBIM status error");
    normalize_and_assert_mm_error (TRUE,  MBIM_STATUS_ERROR, 123456,                 "Unknown MBIM status error");
#endif
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

    g_test_add_func ("/MM/error-helpers/normalize", test_error_helpers_normalize);

    return g_test_run ();
}
