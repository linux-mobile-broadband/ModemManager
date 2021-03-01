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
 * Singleton support imported from NetworkManager.
 *     (C) Copyright 2014 Red Hat, Inc.
 *
 * GPtrArray lookup with GEqualFunc imported from GLib 2.48
 */

#include "mm-utils.h"

#if !GLIB_CHECK_VERSION(2,54,0)

gboolean
mm_ptr_array_find_with_equal_func (GPtrArray     *haystack,
				   gconstpointer  needle,
				   GEqualFunc     equal_func,
				   guint         *index_)
{
  guint i;

  g_return_val_if_fail (haystack != NULL, FALSE);

  if (equal_func == NULL)
      equal_func = g_direct_equal;

  for (i = 0; i < haystack->len; i++) {
      if (equal_func (g_ptr_array_index (haystack, i), needle)) {
          if (index_ != NULL)
              *index_ = i;
          return TRUE;
      }
  }

  return FALSE;
}

#endif
