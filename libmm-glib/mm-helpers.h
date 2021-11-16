/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2011-2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef _MM_HELPERS_H_
#define _MM_HELPERS_H_

/******************************************************************************/

#define RETURN_NON_EMPTY_CONSTANT_STRING(input) do {    \
        const gchar *str;                               \
                                                        \
        str = (input);                                  \
        if (str && str[0])                              \
            return str;                                 \
    } while (0);                                        \
    return NULL

#define RETURN_NON_EMPTY_STRING(input) do {             \
        gchar *str;                                     \
                                                        \
        str = (input);                                  \
        if (str && str[0])                              \
            return str;                                 \
        g_free (str);                                   \
    } while (0);                                        \
    return NULL


/******************************************************************************/
/* These are helper macros to work with properties that are being monitored
 * internally by the proxy objects. This internal monitoring is used to allow
 * maintaining 'custom' types associated to complex DBus properties like
 * dictionaries.
 *
 * Basic ARRAY and OBJECT type support is given.
 */

#define PROPERTY_COMMON_DECLARE(property_name)      \
    guint         property_name##_id;               \
    gboolean      property_name##_refresh_required;

#define PROPERTY_DECLARE(property_name,PropertyType) \
    PropertyType *property_name;                     \
    PROPERTY_COMMON_DECLARE (property_name)

#define PROPERTY_ARRAY_DECLARE(property_name)             PROPERTY_DECLARE (property_name, GArray)
#define PROPERTY_OBJECT_DECLARE(property_name,ObjectType) PROPERTY_DECLARE (property_name, ObjectType)
#define PROPERTY_ERROR_DECLARE(property_name)             PROPERTY_DECLARE (property_name, GError)

#define PROPERTY_INITIALIZE(property_name,signal_name)          \
    self->priv->property_name##_refresh_required = TRUE;        \
    self->priv->property_name##_id =                            \
        g_signal_connect (self,                                 \
                          "notify::" signal_name,               \
                          G_CALLBACK (property_name##_updated), \
                          NULL);

#define PROPERTY_ARRAY_FINALIZE(property_name)                    \
     g_clear_pointer (&self->priv->property_name, g_array_unref);

#define PROPERTY_OBJECT_FINALIZE(property_name) \
     g_clear_object (&self->priv->property_name);

#define PROPERTY_ERROR_FINALIZE(property_name) \
     g_clear_error (&self->priv->property_name);


/* This helper macro uses a GMutexLocker to lock the context
 * in which the macro is defined (so it must always be defined at the
 * start of the context). It also will run a given refresh method if
 * a specific input flag is set.
 */
#define PROPERTY_LOCK_AND_REFRESH(property_name)              \
    g_autoptr(GMutexLocker) locker = NULL;                    \
                                                              \
    locker = g_mutex_locker_new (&self->priv->mutex);         \
    if (self->priv->property_name##_refresh_required) {       \
        property_name##_refresh (self);                       \
        self->priv->property_name##_refresh_required = FALSE; \
    }

/* This helper defines the property refresh method, and can be used for simple
 * one-to-one property vs array transformations */
#define PROPERTY_ARRAY_DEFINE_REFRESH(property_name,Type,type,TYPE,variant_to_garray) \
    static void                                                                       \
    property_name##_refresh (MM##Type *self)                                          \
    {                                                                                 \
        g_autoptr(GVariant) variant = NULL;                                           \
                                                                                      \
        g_clear_pointer (&self->priv->property_name, g_array_unref);                  \
                                                                                      \
        variant = mm_gdbus_##type##_dup_##property_name (MM_GDBUS_##TYPE (self));     \
        if (!variant)                                                                 \
            return;                                                                   \
                                                                                      \
        self->priv->property_name = variant_to_garray (variant);                      \
    }

/* This helper defines the property refresh method, and can be used for simple
 * one-to-one property vs object transformations */
#define PROPERTY_OBJECT_DEFINE_REFRESH(property_name,Type,type,TYPE,variant_to_object) \
    static void                                                                        \
    property_name##_refresh (MM##Type *self)                                           \
    {                                                                                  \
        g_autoptr(GVariant) variant = NULL;                                            \
                                                                                       \
        g_clear_object (&self->priv->property_name);                                   \
                                                                                       \
        variant = mm_gdbus_##type##_dup_##property_name (MM_GDBUS_##TYPE (self));      \
        if (!variant)                                                                  \
            return;                                                                    \
                                                                                       \
        self->priv->property_name = variant_to_object (variant);                       \
    }

#define PROPERTY_OBJECT_DEFINE_REFRESH_FAILABLE(property_name,Type,type,TYPE,variant_to_object) \
    static void                                                                                 \
    property_name##_refresh (MM##Type *self)                                                    \
    {                                                                                           \
        g_autoptr(GVariant) variant = NULL;                                                     \
        g_autoptr(GError)   inner_error = NULL;                                                 \
                                                                                                \
        g_clear_object (&self->priv->property_name);                                            \
                                                                                                \
        variant = mm_gdbus_##type##_dup_##property_name (MM_GDBUS_##TYPE (self));               \
        if (!variant)                                                                           \
            return;                                                                             \
                                                                                                \
        self->priv->property_name = variant_to_object (variant, &inner_error);                  \
        if (inner_error)                                                                        \
            g_warning ("Invalid object variant reported: %s", inner_error->message);            \
    }

/* This helper defines the property refresh method, and can be used for simple
 * one-to-one property vs GError transformations */
#define PROPERTY_ERROR_DEFINE_REFRESH_FAILABLE(property_name,Type,type,TYPE,variant_to_error) \
    static void                                                                               \
    property_name##_refresh (MM##Type *self)                                                  \
    {                                                                                         \
        g_autoptr(GVariant) variant = NULL;                                                   \
        g_autoptr(GError)   inner_error = NULL;                                               \
                                                                                              \
        g_clear_error (&self->priv->property_name);                                           \
                                                                                              \
        variant = mm_gdbus_##type##_dup_##property_name (MM_GDBUS_##TYPE (self));             \
        if (!variant)                                                                         \
            return;                                                                           \
                                                                                              \
        self->priv->property_name = variant_to_error (variant, &inner_error);                 \
        if (inner_error)                                                                      \
            g_warning ("Invalid error variant reported: %s", inner_error->message);           \
    }

/* This helper defines the common generic property updated callback */
#define PROPERTY_DEFINE_UPDATED(property_name,Type)          \
    static void                                              \
    property_name##_updated (MM##Type *self)                 \
    {                                                        \
        g_autoptr(GMutexLocker) locker = NULL;               \
                                                             \
        locker = g_mutex_locker_new (&self->priv->mutex);    \
        self->priv->property_name##_refresh_required = TRUE; \
    }

/* Getter implementation for arrays of complex types that need
 * deep copy. */
#define PROPERTY_ARRAY_DEFINE_GET_DEEP(property_name,Type,type,TYPE,ArrayItemType,garray_to_array) \
    gboolean                                                                                       \
    mm_##type##_get_##property_name (MM##Type       *self,                                         \
                                     ArrayItemType **out,                                          \
                                     guint          *n_out)                                        \
    {                                                                                              \
        g_return_val_if_fail (MM_IS_##TYPE (self), FALSE);                                         \
        g_return_val_if_fail (out != NULL, FALSE);                                                 \
        g_return_val_if_fail (n_out != NULL, FALSE);                                               \
                                                                                                   \
        {                                                                                          \
            PROPERTY_LOCK_AND_REFRESH (property_name)                                              \
            return garray_to_array (self->priv->property_name, out, n_out);                        \
        }                                                                                          \
    }

/* Getter implementation for arrays of simple types */
#define PROPERTY_ARRAY_DEFINE_GET(property_name,Type,type,TYPE,ArrayItemType)                       \
    gboolean                                                                                        \
    mm_##type##_get_##property_name (MM##Type       *self,                                          \
                                     ArrayItemType **out,                                           \
                                     guint          *n_out)                                         \
    {                                                                                               \
        g_return_val_if_fail (MM_IS_##TYPE (self), FALSE);                                          \
        g_return_val_if_fail (out != NULL, FALSE);                                                  \
        g_return_val_if_fail (n_out != NULL, FALSE);                                                \
                                                                                                    \
        {                                                                                           \
            PROPERTY_LOCK_AND_REFRESH (property_name)                                               \
            if (!self->priv->property_name)                                                         \
                return FALSE;                                                                       \
                                                                                                    \
            *out = NULL;                                                                            \
            *n_out = self->priv->property_name->len;                                                \
            if (self->priv->property_name->len > 0)                                                 \
                *out = g_memdup (self->priv->property_name->data,                                   \
                                 (guint)(sizeof (ArrayItemType) * self->priv->property_name->len)); \
            return TRUE;                                                                            \
        }                                                                                           \
    }

/* Peeker implementation for arrays of any type */
#define PROPERTY_ARRAY_DEFINE_PEEK(property_name,Type,type,TYPE,ArrayItemType) \
    gboolean                                                                   \
    mm_##type##_peek_##property_name (MM##Type             *self,              \
                                      const ArrayItemType **out,               \
                                      guint                *n_out)             \
    {                                                                          \
        g_return_val_if_fail (MM_IS_##TYPE (self), FALSE);                     \
        g_return_val_if_fail (out != NULL, FALSE);                             \
        g_return_val_if_fail (n_out != NULL, FALSE);                           \
                                                                               \
        {                                                                      \
            PROPERTY_LOCK_AND_REFRESH (property_name)                          \
                                                                               \
            if (!self->priv->property_name)                                    \
                return FALSE;                                                  \
                                                                               \
            *n_out = self->priv->property_name->len;                           \
            *out = (ArrayItemType *)self->priv->property_name->data;           \
            return TRUE;                                                       \
        }                                                                      \
    }

/* Get implementations for object properties */
#define PROPERTY_OBJECT_DEFINE_GET(property_name,object_name,Type,type,TYPE,ObjectType) \
    ObjectType *                                                                        \
    mm_##type##_get_##object_name (MM##Type *self)                                      \
    {                                                                                   \
        g_return_val_if_fail (MM_IS_##TYPE (self), NULL);                               \
        {                                                                               \
            PROPERTY_LOCK_AND_REFRESH (property_name)                                   \
            return (self->priv->object_name ?                                           \
                    g_object_ref (self->priv->object_name) :                            \
                    NULL);                                                              \
        }                                                                               \
    }

/* Peek implementations for object properties */
#define PROPERTY_OBJECT_DEFINE_PEEK(property_name,object_name,Type,type,TYPE,ObjectType) \
    ObjectType *                                                                         \
    mm_##type##_peek_##object_name (MM##Type *self)                                      \
    {                                                                                    \
        g_return_val_if_fail (MM_IS_##TYPE (self), NULL);                                \
        {                                                                                \
            PROPERTY_LOCK_AND_REFRESH (property_name)                                    \
            return self->priv->object_name;                                              \
        }                                                                                \
    }

/* Get implementations for error properties */
#define PROPERTY_ERROR_DEFINE_GET(property_name,Type,type,TYPE) \
    GError *                                                    \
    mm_##type##_get_##property_name (MM##Type *self)            \
    {                                                           \
        g_return_val_if_fail (MM_IS_##TYPE (self), NULL);       \
        {                                                       \
            PROPERTY_LOCK_AND_REFRESH (property_name)           \
            return (self->priv->property_name ?                 \
                    g_error_copy (self->priv->property_name) :  \
                    NULL);                                      \
        }                                                       \
    }

/* Peek implementations for error properties */
#define PROPERTY_ERROR_DEFINE_PEEK(property_name,Type,type,TYPE) \
    GError *                                                     \
    mm_##type##_peek_##property_name (MM##Type *self)            \
    {                                                            \
        g_return_val_if_fail (MM_IS_##TYPE (self), NULL);        \
        {                                                        \
            PROPERTY_LOCK_AND_REFRESH (property_name)            \
            return self->priv->property_name;                    \
        }                                                        \
    }

#define PROPERTY_ARRAY_DEFINE(property_name,Type,type,TYPE,ArrayItemType,variant_to_garray) \
    PROPERTY_ARRAY_DEFINE_REFRESH (property_name, Type, type, TYPE, variant_to_garray)      \
    PROPERTY_DEFINE_UPDATED       (property_name, Type)                                     \
    PROPERTY_ARRAY_DEFINE_GET     (property_name, Type, type, TYPE, ArrayItemType)          \
    PROPERTY_ARRAY_DEFINE_PEEK    (property_name, Type, type, TYPE, ArrayItemType)

#define PROPERTY_ARRAY_DEFINE_DEEP(property_name,Type,type,TYPE,ArrayItemType,variant_to_garray,garray_to_array) \
    PROPERTY_ARRAY_DEFINE_REFRESH  (property_name, Type, type, TYPE, variant_to_garray)                          \
    PROPERTY_DEFINE_UPDATED        (property_name, Type)                                                         \
    PROPERTY_ARRAY_DEFINE_GET_DEEP (property_name, Type, type, TYPE, ArrayItemType, garray_to_array)             \
    PROPERTY_ARRAY_DEFINE_PEEK     (property_name, Type, type, TYPE, ArrayItemType)

#define PROPERTY_OBJECT_DEFINE(property_name,Type,type,TYPE,ObjectType,variant_to_object)       \
    PROPERTY_OBJECT_DEFINE_REFRESH (property_name, Type, type, TYPE, variant_to_object)         \
    PROPERTY_DEFINE_UPDATED        (property_name, Type)                                        \
    PROPERTY_OBJECT_DEFINE_GET     (property_name, property_name, Type, type, TYPE, ObjectType) \
    PROPERTY_OBJECT_DEFINE_PEEK    (property_name, property_name, Type, type, TYPE, ObjectType)

#define PROPERTY_OBJECT_DEFINE_FAILABLE(property_name,Type,type,TYPE,ObjectType,variant_to_object)       \
    PROPERTY_OBJECT_DEFINE_REFRESH_FAILABLE (property_name, Type, type, TYPE, variant_to_object)         \
    PROPERTY_DEFINE_UPDATED                 (property_name, Type)                                        \
    PROPERTY_OBJECT_DEFINE_GET              (property_name, property_name, Type, type, TYPE, ObjectType) \
    PROPERTY_OBJECT_DEFINE_PEEK             (property_name, property_name, Type, type, TYPE, ObjectType)

#define PROPERTY_ERROR_DEFINE_FAILABLE(property_name,Type,type,TYPE,variant_to_error)          \
    PROPERTY_ERROR_DEFINE_REFRESH_FAILABLE (property_name, Type, type, TYPE, variant_to_error) \
    PROPERTY_DEFINE_UPDATED                (property_name, Type)                               \
    PROPERTY_ERROR_DEFINE_GET              (property_name, Type, type, TYPE)                   \
    PROPERTY_ERROR_DEFINE_PEEK             (property_name, Type, type, TYPE)

#endif /* _MM_HELPERS_H_ */
