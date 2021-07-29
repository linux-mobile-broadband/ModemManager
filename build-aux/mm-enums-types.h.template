/*** BEGIN file-header ***/

#include <glib-object.h>

G_BEGIN_DECLS
/*** END file-header ***/

/*** BEGIN file-production ***/

/* enumerations from "@filename@" */
/*** END file-production ***/

/*** BEGIN value-header ***/
GType @enum_name@_get_type (void) G_GNUC_CONST;
#define @ENUMPREFIX@_TYPE_@ENUMSHORT@ (@enum_name@_get_type ())

/* Define type-specific symbols */
#undef __MM_IS_ENUM__
#undef __MM_IS_FLAGS__
#define __MM_IS_@TYPE@__

#if defined __MM_IS_ENUM__
const gchar *@enum_name@_get_string (@EnumName@ val);
#endif

#if defined __MM_IS_FLAGS__
gchar *@enum_name@_build_string_from_mask (@EnumName@ mask);
#endif

/*** END value-header ***/

/*** BEGIN file-tail ***/
G_END_DECLS

/*** END file-tail ***/
