/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2016 Kinvolk GmbH
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define EOS_DECLARE_REFCOUNTED(ModuleObjName, module_obj_name, MODULE, OBJ_NAME) \
  G_DECLARE_FINAL_TYPE (ModuleObjName, module_obj_name, MODULE, OBJ_NAME, GObject)

#define EOS_DEFINE_REFCOUNTED(TYPE_NAME, TypeName, type_name, dispose_impl_func, finalize_impl_func) \
  G_DEFINE_TYPE (TypeName, type_name, G_TYPE_OBJECT)                    \
                                                                        \
  static void                                                           \
  type_name##_real_dispose (GObject *object)                            \
  {                                                                     \
    typedef void (*Func)(TypeName *obj);                                \
                                                                        \
    TypeName *self = TYPE_NAME (object);                                \
    Func f = dispose_impl_func;                                         \
                                                                        \
    if (f != NULL)                                                      \
      f (self);                                                         \
    G_OBJECT_CLASS (type_name##_parent_class)->dispose (object);        \
  }                                                                     \
                                                                        \
  static void                                                           \
  type_name##_real_finalize (GObject *object)                           \
  {                                                                     \
    typedef void (*Func)(TypeName *obj);                                \
                                                                        \
    TypeName *self = TYPE_NAME (object);                                \
    Func f = finalize_impl_func;                                        \
                                                                        \
    if (f != NULL)                                                      \
      f (self);                                                         \
    G_OBJECT_CLASS (type_name##_parent_class)->finalize (object);       \
  }                                                                     \
                                                                        \
  static void                                                           \
  type_name##_class_init (TypeName##Class *self_class)                  \
  {                                                                     \
    GObjectClass *object_class = G_OBJECT_CLASS (self_class);           \
                                                                        \
    object_class->dispose = type_name##_real_dispose;                   \
    object_class->finalize = type_name##_real_finalize;                 \
  }                                                                     \
                                                                        \
  static void                                                           \
  type_name##_init (TypeName *self)                                     \
  {}

G_END_DECLS
