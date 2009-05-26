/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2009 Intel Corp.
 *
 * Author: Robert Staudinger <robertx.staudinger@intel.com>
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

#include "mnb-launcher-grid.h"

G_DEFINE_TYPE (MnbLauncherGrid, mnb_launcher_grid, NBTK_TYPE_GRID)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MNB_TYPE_LAUNCHER_GRID, MnbLauncherGridPrivate))

typedef struct _MnbLauncherGridPrivate MnbLauncherGridPrivate;

struct _MnbLauncherGridPrivate
{
  int placeholder;
};

static void
mnb_launcher_grid_class_init (MnbLauncherGridClass *klass)
{
  g_type_class_add_private (klass, sizeof (MnbLauncherGridPrivate));
}

static void
mnb_launcher_grid_init (MnbLauncherGrid *self)
{}

NbtkWidget *
mnb_launcher_grid_new (void)
{
  return g_object_new (MNB_TYPE_LAUNCHER_GRID, NULL);
}

typedef struct
{
  const gchar *pseudo_class;
  NbtkWidget  *widget;
} find_widget_by_pseudo_class_data;

static void
_find_widget_by_pseudo_class_cb (ClutterActor                      *actor,
                                 find_widget_by_pseudo_class_data  *data)
{
  const gchar *pseudo_class;

  if (!CLUTTER_ACTOR_IS_VISIBLE (actor))
    return;

  if (!NBTK_IS_STYLABLE (actor))
    return;

  pseudo_class = nbtk_stylable_get_pseudo_class (NBTK_STYLABLE (actor));
  if (0 == g_strcmp0 (data->pseudo_class, pseudo_class))
    data->widget = NBTK_WIDGET (actor);
}

NbtkWidget *
mnb_launcher_grid_find_widget_by_pseudo_class (MnbLauncherGrid  *grid,
                                               const gchar      *pseudo_class)
{
  find_widget_by_pseudo_class_data data;

  data.pseudo_class = pseudo_class;
  data.widget = NULL;

  clutter_container_foreach (CLUTTER_CONTAINER (grid),
                             (ClutterCallback) _find_widget_by_pseudo_class_cb,
                             &data);

  return data.widget;
}

typedef struct {
  ClutterUnit  x;
  ClutterUnit  y;
  NbtkWidget  *widget;
} find_widget_by_point_data_t;

static void
_find_widget_by_point_cb (ClutterActor                *actor,
                          find_widget_by_point_data_t *data)
{
  ClutterUnit left = clutter_actor_get_xu (actor);
  ClutterUnit top = clutter_actor_get_yu (actor);
  ClutterUnit right = left + clutter_actor_get_widthu (actor);
  ClutterUnit bottom = top + clutter_actor_get_heightu (actor);

  if (CLUTTER_ACTOR_IS_VISIBLE (actor) &&
      left <= data->x &&
      top <= data->y &&
      right >= data->x &&
      bottom >= data->y)
    {
      data->widget = NBTK_WIDGET (actor);
    }
}

NbtkWidget *
mnb_launcher_grid_find_widget_by_point (MnbLauncherGrid *self,
                                        ClutterUnit      x,
                                        ClutterUnit      y)
{
  find_widget_by_point_data_t data;

  data.x = x;
  data.y = y;
  data.widget = NULL;

  clutter_container_foreach (CLUTTER_CONTAINER (self),
                             (ClutterCallback) _find_widget_by_point_cb,
                             &data);

  return data.widget;
}

NbtkWidget *
mnb_launcher_grid_keynav_up (MnbLauncherGrid *self)
{
  NbtkWidget  *old, *new;
  ClutterUnit  x, y;

  old = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (old == NULL)
    return NULL;

  x = clutter_actor_get_xu (CLUTTER_ACTOR (old)) +
      clutter_actor_get_widthu (CLUTTER_ACTOR (old)) / 2;

  y = clutter_actor_get_yu (CLUTTER_ACTOR (old)) -
      nbtk_grid_get_row_gap (NBTK_GRID (self)) -
      clutter_actor_get_heightu (CLUTTER_ACTOR (old)) / 2;

  new = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (new)
    {
      nbtk_widget_set_style_pseudo_class (old, NULL);
      nbtk_widget_set_style_pseudo_class (new, "hover");
      return new;
    }

  return NULL;
}

static NbtkWidget *
mnb_launcher_grid_keynav_right (MnbLauncherGrid *self)
{
  NbtkWidget  *old, *new;
  ClutterUnit  x, y;

  old = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (old == NULL)
    return NULL;

  x = clutter_actor_get_xu (CLUTTER_ACTOR (old)) +
      nbtk_grid_get_column_gap (NBTK_GRID (self)) +
      clutter_actor_get_widthu (CLUTTER_ACTOR (old)) * 1.5;

  y = clutter_actor_get_yu (CLUTTER_ACTOR (old)) +
      clutter_actor_get_heightu (CLUTTER_ACTOR (old)) / 2;

  new = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (new)
    {
      nbtk_widget_set_style_pseudo_class (old, NULL);
      nbtk_widget_set_style_pseudo_class (new, "hover");
      return new;
    }

  return NULL;
}

NbtkWidget *
mnb_launcher_grid_keynav_down (MnbLauncherGrid *self)
{
  NbtkWidget  *old, *new;
  ClutterUnit  x, y;

  old = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (old == NULL)
    return NULL;

  x = clutter_actor_get_xu (CLUTTER_ACTOR (old)) +
      clutter_actor_get_widthu (CLUTTER_ACTOR (old)) / 2;

  y = clutter_actor_get_yu (CLUTTER_ACTOR (old)) +
      nbtk_grid_get_row_gap (NBTK_GRID (self)) +
      clutter_actor_get_heightu (CLUTTER_ACTOR (old)) * 1.5;

  new = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (new)
    {
      nbtk_widget_set_style_pseudo_class (old, NULL);
      nbtk_widget_set_style_pseudo_class (new, "hover");
      return new;
    }

  return NULL;
}

static NbtkWidget *
mnb_launcher_grid_keynav_left (MnbLauncherGrid *self)
{
  NbtkWidget  *old, *new;
  ClutterUnit  x, y;

  old = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (old == NULL)
    return NULL;

  x = clutter_actor_get_xu (CLUTTER_ACTOR (old)) -
      nbtk_grid_get_column_gap (NBTK_GRID (self)) -
      clutter_actor_get_widthu (CLUTTER_ACTOR (old)) / 2;

  y = clutter_actor_get_yu (CLUTTER_ACTOR (old)) +
      clutter_actor_get_heightu (CLUTTER_ACTOR (old)) / 2;

  new = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (new)
    {
      nbtk_widget_set_style_pseudo_class (old, NULL);
      nbtk_widget_set_style_pseudo_class (new, "hover");
      return new;
    }

  return NULL;
}

static NbtkWidget *
mnb_launcher_grid_keynav_wrap_up (MnbLauncherGrid *self)
{
  NbtkWidget  *old, *new;
  NbtkPadding  padding;
  ClutterUnit  x, y;

  old = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (old == NULL)
    return NULL;

  nbtk_widget_get_padding (NBTK_WIDGET (self), &padding);

  x = clutter_actor_get_widthu (CLUTTER_ACTOR (self)) -
      padding.right -
      clutter_actor_get_widthu (CLUTTER_ACTOR (old)) / 2;

  y = clutter_actor_get_yu (CLUTTER_ACTOR (old)) -
      nbtk_grid_get_row_gap (NBTK_GRID (self)) -
      clutter_actor_get_heightu (CLUTTER_ACTOR (old)) / 2;

  new = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (new)
    {
      nbtk_widget_set_style_pseudo_class (old, NULL);
      nbtk_widget_set_style_pseudo_class (new, "hover");
      return new;
    }

  return NULL;
}

static NbtkWidget *
mnb_launcher_grid_keynav_wrap_down (MnbLauncherGrid *self)
{
  NbtkWidget  *old, *new;
  NbtkPadding  padding;
  ClutterUnit  x, y;

  old = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (old == NULL)
    return NULL;

  nbtk_widget_get_padding (NBTK_WIDGET (self), &padding);

  x = padding.left +
      clutter_actor_get_widthu (CLUTTER_ACTOR (old)) / 2;

  y = clutter_actor_get_yu (CLUTTER_ACTOR (old)) +
      nbtk_grid_get_row_gap (NBTK_GRID (self)) +
      clutter_actor_get_heightu (CLUTTER_ACTOR (old)) * 1.5;

  new = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (new)
    {
      nbtk_widget_set_style_pseudo_class (old, NULL);
      nbtk_widget_set_style_pseudo_class (new, "hover");
      return new;
    }

  return NULL;
}

NbtkWidget *
mnb_launcher_grid_keynav_first (MnbLauncherGrid *self)
{
  NbtkWidget  *widget;
  NbtkPadding  padding;
  ClutterUnit  x, y;

  nbtk_widget_get_padding (NBTK_WIDGET (self), &padding);

  x = padding.left + 1;
  y = padding.top + 1;

  widget = mnb_launcher_grid_find_widget_by_point (self, x, y);
  if (widget)
    {
      nbtk_widget_set_style_pseudo_class (widget, "hover");
      return widget;
    }

  return NULL;
}

void
mnb_launcher_grid_keynav_out (MnbLauncherGrid *self)
{
  NbtkWidget  *widget;

  widget = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
  if (widget)
    nbtk_widget_set_style_pseudo_class (widget, NULL);
}

NbtkWidget *
mnb_launcher_grid_keynav (MnbLauncherGrid *self,
                          guint            keyval)
{
  NbtkWidget *widget;

  widget = NULL;
  switch (keyval)
    {
      case CLUTTER_Return:
        widget = mnb_launcher_grid_find_widget_by_pseudo_class (self, "hover");
        break;
      case CLUTTER_Left:
        widget = mnb_launcher_grid_keynav_left (self);
        if (!widget)
          widget = mnb_launcher_grid_keynav_wrap_up (self);
        break;
      case CLUTTER_Up:
        widget = mnb_launcher_grid_keynav_up (self);
        break;
      case CLUTTER_Right:
        widget = mnb_launcher_grid_keynav_right (self);
        if (!widget)
          widget = mnb_launcher_grid_keynav_wrap_down (self);
        break;
      case CLUTTER_Down:
        widget = mnb_launcher_grid_keynav_down (self);
        break;
    }

  return widget;
}
