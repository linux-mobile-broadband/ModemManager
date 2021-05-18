/*** BEGIN file-header ***/

/*** END file-header ***/

/*** BEGIN file-production ***/
/* enumerations from "@filename@" */
/*** END file-production ***/

/*** BEGIN value-header ***/
static const G@Type@Value @enum_name@_values[] = {
/*** END value-header ***/
/*** BEGIN value-production ***/
    { @VALUENAME@, "@VALUENAME@", "@valuenick@" },
/*** END value-production ***/
/*** BEGIN value-tail ***/
    { 0, NULL, NULL }
};

/* Define type-specific symbols */
#undef __MM_IS_ENUM__
#undef __MM_IS_FLAGS__
#define __MM_IS_@TYPE@__

GType
@enum_name@_get_type (void)
{
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_@type@_register_static (g_intern_static_string ("@EnumName@"),
                                      @enum_name@_values);
        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}

/**
 * @enum_name@_get_string:
 * @val: a @EnumName@.
 *
 * Gets the nickname string for the #@EnumName@ specified at @val.
 *
 * Returns: (transfer none): a string with the nickname, or %NULL if not found. Do not free the returned value.
 */
#if defined __MM_IS_ENUM__
const gchar *
@enum_name@_get_string (@EnumName@ val)
{
    guint i;

    for (i = 0; @enum_name@_values[i].value_nick; i++) {
        if ((gint)val == @enum_name@_values[i].value)
            return @enum_name@_values[i].value_nick;
    }

    return NULL;
}
#endif /* __MM_IS_ENUM_ */

/**
 * @enum_name@_build_string_from_mask:
 * @mask: bitmask of @EnumName@ values.
 *
 * Builds a string containing a comma-separated list of nicknames for
 * each #@EnumName@ in @mask.
 *
 * Returns: (transfer full): a string with the list of nicknames, or %NULL if none given. The returned value should be freed with g_free().
 */
#if defined __MM_IS_FLAGS__
gchar *
@enum_name@_build_string_from_mask (@EnumName@ mask)
{
    guint i;
    gboolean first = TRUE;
    GString *str = NULL;

    for (i = 0; @enum_name@_values[i].value_nick; i++) {
        /* We also look for exact matches */
        if (mask == @enum_name@_values[i].value) {
            if (str)
                g_string_free (str, TRUE);
            return g_strdup (@enum_name@_values[i].value_nick);
        }

        /* Build list with single-bit masks */
        if (mask & @enum_name@_values[i].value) {
            guint c;
            gulong number = @enum_name@_values[i].value;

            for (c = 0; number; c++)
                number &= number - 1;

            if (c == 1) {
                if (!str)
                    str = g_string_new ("");
                g_string_append_printf (str, "%s%s",
                                        first ? "" : ", ",
                                        @enum_name@_values[i].value_nick);
                if (first)
                    first = FALSE;
            }
        }
    }

    return (str ? g_string_free (str, FALSE) : NULL);
}
#endif /* __MM_IS_FLAGS__ */

/*** END value-tail ***/

/*** BEGIN file-tail ***/
/*** END file-tail ***/
