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

#include <display.h>
#include <errors.h>
#include <X11/Xatom.h>

#include "../meego-netbook.h"
#include "../mnb-input-manager.h"

#include "mnb-systray.h"
#include "shell-tray-manager.h"

static void mnb_systray_dispose (GObject *object);
static void mnb_systray_finalize (GObject *object);
static void mnb_systray_constructed (GObject *object);

G_DEFINE_TYPE (MnbSystray, mnb_systray, MX_TYPE_BOX_LAYOUT);

#define MNB_SYSTRAY_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), MNB_TYPE_SYSTRAY, MnbSystrayPrivate))

struct _MnbSystrayPrivate
{
  ShellTrayManager *manager;

  guint disposed   : 1;
  guint struts_set : 1;
};

enum
{
  N_SIGNALS,
};

enum
{
  PROP_0,
};

/* static guint signals[N_SIGNALS] = {0}; */

static void
mnb_systray_allocate (ClutterActor          *actor,
                      const ClutterActorBox *box,
                      ClutterAllocationFlags flags)
{
  MutterPlugin      *plugin = meego_netbook_get_plugin_singleton ();
  ClutterActorClass *klass = CLUTTER_ACTOR_CLASS (mnb_systray_parent_class);

  klass->allocate (actor, box, flags);

  if (CLUTTER_ACTOR_IS_MAPPED (actor))
    {
      gint bottom = box->y2 - box->y1;

      meego_netbook_set_struts (plugin, -1, -1, -1, bottom);
    }
}

static void
mnb_systray_show (ClutterActor *actor)
{
  MutterPlugin      *plugin = meego_netbook_get_plugin_singleton ();
  ClutterActorClass *klass  = CLUTTER_ACTOR_CLASS (mnb_systray_parent_class);
  gint               bottom = clutter_actor_get_height (actor);

  klass->show (actor);

  meego_netbook_set_struts (plugin, -1, -1, -1, bottom);
}

static void
mnb_systray_hide (ClutterActor *actor)
{
  MutterPlugin      *plugin = meego_netbook_get_plugin_singleton ();
  ClutterActorClass *klass = CLUTTER_ACTOR_CLASS (mnb_systray_parent_class);

  klass->hide (actor);

  meego_netbook_set_struts (plugin, -1, -1, -1, 0);
}

static void
mnb_systray_class_init (MnbSystrayClass *klass)
{
  GObjectClass      *object_class = (GObjectClass *)klass;
  ClutterActorClass *actor_class  = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MnbSystrayPrivate));

  object_class->dispose      = mnb_systray_dispose;
  object_class->finalize     = mnb_systray_finalize;
  object_class->constructed  = mnb_systray_constructed;

  actor_class->allocate      = mnb_systray_allocate;
  actor_class->show          = mnb_systray_show;
  actor_class->hide          = mnb_systray_hide;
}

static void
mnb_systray_icon_added (ShellTrayManager *manager,
                        ClutterActor     *icon,
                        const gchar      *wm_class,
                        MnbSystray       *tray)
{
  clutter_container_add_actor (CLUTTER_CONTAINER (tray), icon);
}

static void
mnb_systray_icon_removed (ShellTrayManager *manager,
                          ClutterActor     *icon,
                          MnbSystray       *tray)
{
  clutter_container_remove_actor (CLUTTER_CONTAINER (tray), icon);
}

static void
mnb_systray_constructed (GObject *object)
{
  MnbSystray        *self   = (MnbSystray*) object;
  MnbSystrayPrivate *priv   = self->priv;
  MutterPlugin      *plugin = meego_netbook_get_plugin_singleton ();
  MetaScreen        *screen = mutter_plugin_get_screen (plugin);
  ClutterActor      *stage  = mutter_get_stage_for_screen (screen);

  if (G_OBJECT_CLASS (mnb_systray_parent_class)->constructed)
    G_OBJECT_CLASS (mnb_systray_parent_class)->constructed (object);

  mnb_input_manager_push_actor (CLUTTER_ACTOR (object), MNB_INPUT_LAYER_TOP);

  g_signal_connect (priv->manager, "tray-icon-added",
                    G_CALLBACK (mnb_systray_icon_added),
                    self);
  g_signal_connect (priv->manager, "tray-icon-removed",
                    G_CALLBACK (mnb_systray_icon_removed),
                    self);

  shell_tray_manager_manage_stage (priv->manager, CLUTTER_STAGE (stage));
}

static void
mnb_systray_init (MnbSystray *self)
{
  MnbSystrayPrivate *priv;

  priv = self->priv = MNB_SYSTRAY_GET_PRIVATE (self);

  priv->manager = shell_tray_manager_new ();
}

static void
mnb_systray_dispose (GObject *object)
{
  MnbSystray        *self = (MnbSystray*) object;
  MnbSystrayPrivate *priv = self->priv;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  g_object_unref (priv->manager);
  priv->manager = NULL;

  G_OBJECT_CLASS (mnb_systray_parent_class)->dispose (object);
}

static void
mnb_systray_finalize (GObject *object)
{
  G_OBJECT_CLASS (mnb_systray_parent_class)->finalize (object);
}

ClutterActor *
mnb_systray_new (void)
{
  return g_object_new (MNB_TYPE_SYSTRAY, NULL);
}

