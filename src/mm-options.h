/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_OPTIONS_H
#define MM_OPTIONS_H

void     mm_options_parse     (int argc, char *argv[]);
void     mm_options_set_debug (gboolean enabled);
gboolean mm_options_debug     (void);

#endif /* MM_OPTIONS_H */
