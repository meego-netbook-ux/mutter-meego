/*
 * Copyright © 2009, 2010, Intel Corporation.
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
 *
 * Authors: Thomas Wood <thomas.wood@intel.com>
 */

#include "sw-overview.h"
#include "sw-zone.h"

G_DEFINE_TYPE (SwOverview, sw_overview, MX_TYPE_BOX_LAYOUT)

#define OVERVIEW_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SW_TYPE_OVERVIEW, SwOverviewPrivate))

enum
{
  PROP_N_ZONES = 1
};


struct _SwOverviewPrivate
{
  gint n_zones;

  ClutterActor *dummy;
  gulong dummy_added_handler;
};


static void
sw_overview_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SwOverviewPrivate *priv = SW_OVERVIEW (object)->priv;

  switch (property_id)
    {
  case PROP_N_ZONES:
    g_value_set_int (value, priv->n_zones);
    break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
sw_overview_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{

  switch (property_id)
    {
  case PROP_N_ZONES:
    break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
sw_overview_dispose (GObject *object)
{
  G_OBJECT_CLASS (sw_overview_parent_class)->dispose (object);
}

static void actor_added_to_dummy (ClutterContainer *zone, ClutterActor *actor, SwOverview *view);

static void sw_overview_renumber_zones (SwOverview *view);

static void
sw_overview_finalize (GObject *object)
{
  G_OBJECT_CLASS (sw_overview_parent_class)->finalize (object);
}

static void
sw_overview_add_dummy (SwOverview *view)
{
  SwOverviewPrivate *priv = view->priv;

  priv->dummy = sw_zone_new ();
  sw_zone_set_dummy (SW_ZONE (priv->dummy), TRUE);
  priv->dummy_added_handler = g_signal_connect (priv->dummy, "actor-added",
                                                G_CALLBACK (actor_added_to_dummy),
                                                view);

  clutter_container_add_actor (CLUTTER_CONTAINER (view), priv->dummy);
}

static void
actor_added_to_dummy (ClutterContainer *zone,
                      ClutterActor     *actor,
                      SwOverview       *view)
{
  SwOverviewPrivate *priv = view->priv;

  /* disconnect this handler since it is now a real zone */
  g_signal_handler_disconnect (zone, priv->dummy_added_handler);
  sw_zone_set_dummy (SW_ZONE (zone), FALSE);
  clutter_container_child_set (CLUTTER_CONTAINER (view),
                               CLUTTER_ACTOR (zone), "expand", TRUE, NULL);

  priv->dummy = NULL;


  sw_overview_renumber_zones (view);
}

static void
sw_overview_renumber_zones (SwOverview   *view)
{
  GList *children, *l;
  gint count;

  children = clutter_container_get_children (CLUTTER_CONTAINER (view));

  count = 0;
  for (l = children; l; l = g_list_next (l))
    {
      if (!sw_zone_get_dummy (SW_ZONE (l->data)))
        sw_zone_set_number (SW_ZONE (l->data), ++count);
    }

  /* add the new dummy */
  if (count < 8 && !view->priv->dummy)
    sw_overview_add_dummy (view);
}

static void
sw_overview_class_init (SwOverviewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SwOverviewPrivate));

  object_class->get_property = sw_overview_get_property;
  object_class->set_property = sw_overview_set_property;
  object_class->dispose = sw_overview_dispose;
  object_class->finalize = sw_overview_finalize;
}

void
sw_overview_add_zone (SwOverview *self)
{
  ClutterActor *zone;
  SwOverviewPrivate *priv = self->priv;

  if (priv->dummy)
    clutter_container_remove_actor (CLUTTER_CONTAINER (self), priv->dummy);
  priv->dummy = NULL;

  zone = sw_zone_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (self), zone);
  clutter_container_child_set (CLUTTER_CONTAINER (self),
                               zone, "expand", TRUE, NULL);

  sw_overview_renumber_zones (self);
}

static void
sw_overview_init (SwOverview *self)
{
  self->priv = OVERVIEW_PRIVATE (self);

  mx_box_layout_set_spacing (MX_BOX_LAYOUT (self), 1);

  mx_box_layout_set_enable_animations (MX_BOX_LAYOUT (self), TRUE);

  g_signal_connect (self, "actor-removed",
                    G_CALLBACK (sw_overview_renumber_zones), NULL);
}

ClutterActor *
sw_overview_new (void)
{
  return g_object_new (SW_TYPE_OVERVIEW, NULL);
}


void
window_drag_begin (SwOverview *overview)
{
  GList *children, *l;

  children = clutter_container_get_children (CLUTTER_CONTAINER (overview));

  for (l = children; l; l = g_list_next (l))
    {
      sw_zone_set_drag_in_progress (SW_ZONE (l->data), TRUE);
    }

  g_list_free (children);
}

void
window_drag_end (SwOverview *overview)
{
  GList *children, *l;

  children = clutter_container_get_children (CLUTTER_CONTAINER (overview));

  for (l = children; l; l = g_list_next (l))
    {
      sw_zone_set_drag_in_progress (SW_ZONE (l->data), FALSE);
    }

  g_list_free (children);
}



void
sw_overview_add_window (SwOverview *overview,
                        SwWindow   *window,
                        gint        index)
{
  GList *children;
  ClutterContainer *zone;

  children = clutter_container_get_children (CLUTTER_CONTAINER (overview));

  zone = g_list_nth_data (children, index);

  if (!zone)
    {
      zone = CLUTTER_CONTAINER (children->data);

      if (!zone)
        return;
    }

  clutter_container_add_actor (zone, CLUTTER_ACTOR (window));


  g_signal_connect_swapped (window, "drag-begin",
                            G_CALLBACK (window_drag_begin), overview);
  g_signal_connect_swapped (window, "drag-end",
                            G_CALLBACK (window_drag_end), overview);

  g_list_free (children);
}

void
sw_overview_remove_window (SwOverview *overview,
                           gulong      xid)
{
  GList *children, *l;

  children = clutter_container_get_children (CLUTTER_CONTAINER (overview));

  for (l = children; l; l = g_list_next (l))
    {
      sw_zone_remove_window (SW_ZONE (l->data), xid);
    }

  g_list_free (children);
}

void
sw_overview_set_focused_zone (SwOverview *overview,
                              gint        index)
{
  GList *children, *l;
  guint i;

  children = clutter_container_get_children (CLUTTER_CONTAINER (overview));

  i = 0;
  for (l = children; l; l = g_list_next (l))
    {
      sw_zone_set_focused (SW_ZONE (l->data), (i == index));
      i++;
    }

  g_list_free (children);
}

void
sw_overview_set_focused_window (SwOverview *overview,
                                gulong      xid)
{
  GList *children, *l;
  guint i;

  children = clutter_container_get_children (CLUTTER_CONTAINER (overview));

  i = 0;
  for (l = children; l; l = g_list_next (l))
    {
      sw_zone_set_focused_window (SW_ZONE (l->data), xid);
    }

  g_list_free (children);
}

