/*** BEGIN file-header ***/
/*** END file-header ***/

/*** BEGIN file-production ***/
/* enumerations from "@filename@" */
/*** END file-production ***/

/*** BEGIN value-header ***/

/* @enum_name@_quark() implemented in mm-errors-quarks.c */

GType
@enum_name@_get_type (void)
{
  static gsize g_define_type_id_initialized = 0;

  if (g_once_init_enter (&g_define_type_id_initialized))
    {
      static const G@Type@Value values[] = {
/*** END value-header ***/

/*** BEGIN value-production ***/
        { @VALUENAME@, "@VALUENAME@", "@valuenick@" },
/*** END value-production ***/

/*** BEGIN value-tail ***/
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_@type@_register_static (g_intern_static_string ("@EnumName@"), values);
      g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

  return g_define_type_id_initialized;
}

/*** END value-tail ***/

/*** BEGIN file-tail ***/
/*** END file-tail ***/
