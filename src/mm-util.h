/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_UTIL_H
#define MM_UTIL_H

#include <glib.h>

typedef void (*MMUtilStripFn) (const char *str,
                               gpointer user_data);

/* Applies the regexp on string and calls the callback (if provided)
   with each match and user_data. After that, the matches are removed
   from the string.
*/
void mm_util_strip_string (GString *string,
                           GRegex *regex,
                           MMUtilStripFn callback,
                           gpointer user_data);

#endif /* MM_UTIL_H */
