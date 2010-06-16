/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2010 Intel Corp.
 *
 * Authors: Tomas Frydrych <tf@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _MNB_SYSTRAY_H
#define _MNB_SYSTRAY_H

#include <mx/mx.h>

G_BEGIN_DECLS

#define MNB_TYPE_SYSTRAY                                                \
   (mnb_systray_get_type())
#define MNB_SYSTRAY(obj)                                                \
   (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                  \
                                MNB_TYPE_SYSTRAY,                       \
                                MnbSystray))
#define MNB_SYSTRAY_CLASS(klass)                                        \
   (G_TYPE_CHECK_CLASS_CAST ((klass),                                   \
                             MNB_TYPE_SYSTRAY,                          \
                             MnbSystrayClass))
#define MNB_IS_SYSTRAY(obj)                                             \
   (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                  \
                                MNB_TYPE_SYSTRAY))
#define MNB_IS_SYSTRAY_CLASS(klass)                                     \
   (G_TYPE_CHECK_CLASS_TYPE ((klass),                                   \
                             MNB_TYPE_SYSTRAY))
#define MNB_SYSTRAY_GET_CLASS(obj)                                      \
   (G_TYPE_INSTANCE_GET_CLASS ((obj),                                   \
                               MNB_TYPE_SYSTRAY,                        \
                               MnbSystrayClass))

typedef struct _MnbSystray        MnbSystray;
typedef struct _MnbSystrayClass   MnbSystrayClass;
typedef struct _MnbSystrayPrivate MnbSystrayPrivate;

struct _MnbSystrayClass
{
  MxBoxLayoutClass parent_class;
};

struct _MnbSystray
{
  MxBoxLayout parent;

  /*<private>*/
  MnbSystrayPrivate *priv;
};

GType mnb_systray_get_type (void) G_GNUC_CONST;

ClutterActor *mnb_systray_new (void);

G_END_DECLS

#endif /* _MNB_SYSTRAY_H */
