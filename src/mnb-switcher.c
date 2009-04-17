/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Moblin Netbook
 * Copyright © 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "mnb-switcher.h"
#include "moblin-netbook.h"
#include "moblin-netbook-panel.h"
#include <display.h>
#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <nbtk/nbtk-tooltip.h>

#define HOVER_TIMEOUT  800

/*
 * MnbSwitcherApp
 *
 * A NbtkWidget subclass represening a single thumb in the switcher.
 */
#define MNB_TYPE_SWITCHER_APP                 (mnb_switcher_app_get_type ())
#define MNB_SWITCHER_APP(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), MNB_TYPE_SWITCHER_APP, MnbSwitcherApp))
#define MNB_IS_SWITCHER_APP(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MNB_TYPE_SWITCHER_APP))
#define MNB_SWITCHER_APP_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), MNB_TYPE_SWITCHER_APP, MnbSwitcherAppClass))
#define MNB_IS_SWITCHER_APP_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), MNB_TYPE_SWITCHER_APP))
#define MNB_SWITCHER_APP_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), MNB_TYPE_SWITCHER_APP, MnbSwitcherAppClass))

typedef struct _MnbSwitcherApp               MnbSwitcherApp;
typedef struct _MnbSwitcherAppPrivate        MnbSwitcherAppPrivate;
typedef struct _MnbSwitcherAppClass          MnbSwitcherAppClass;

struct _MnbSwitcherApp
{
  /*< private >*/
  NbtkBin parent_instance;

  MnbSwitcherAppPrivate *priv;
};

struct _MnbSwitcherAppClass
{
  /*< private >*/
  NbtkBinClass parent_class;
};

struct _MnbSwitcherAppPrivate
{
  MnbSwitcher  *switcher;
  MutterWindow *mw;
  guint         hover_timeout_id;
  ClutterActor *tooltip;
  guint         focus_id;
  guint         raised_id;
};

GType mnb_switcher_app_get_type (void);

G_DEFINE_TYPE (MnbSwitcherApp, mnb_switcher_app, NBTK_TYPE_BIN)

#define MNB_SWITCHER_APP_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MNB_TYPE_SWITCHER_APP,\
                                MnbSwitcherAppPrivate))

static void
mnb_switcher_app_dispose (GObject *object)
{
  MnbSwitcherAppPrivate *priv = MNB_SWITCHER_APP (object)->priv;
  MetaWindow            *meta_win;

  meta_win = mutter_window_get_meta_window (priv->mw);

  if (priv->hover_timeout_id)
    {
      g_source_remove (priv->hover_timeout_id);
      priv->hover_timeout_id = 0;
    }

  if (priv->focus_id)
    {
      g_signal_handler_disconnect (meta_win, priv->focus_id);
      priv->focus_id = 0;
    }

  if (priv->raised_id)
    {
      g_signal_handler_disconnect (meta_win, priv->raised_id);
      priv->raised_id = 0;
    }

  /*
   * Do not destroy the tooltip, this is happens automatically.
   */

  G_OBJECT_CLASS (mnb_switcher_app_parent_class)->dispose (object);
}

static void
mnb_switcher_app_class_init (MnbSwitcherAppClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = mnb_switcher_app_dispose;

  g_type_class_add_private (klass, sizeof (MnbSwitcherAppPrivate));
}

static void
mnb_switcher_app_init (MnbSwitcherApp *self)
{
  self->priv = MNB_SWITCHER_APP_GET_PRIVATE (self);
}

G_DEFINE_TYPE (MnbSwitcher, mnb_switcher, MNB_TYPE_DROP_DOWN)

#define MNB_SWITCHER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MNB_TYPE_SWITCHER, MnbSwitcherPrivate))

struct _MnbSwitcherPrivate {
  MutterPlugin *plugin;
  NbtkWidget   *table;
  NbtkWidget   *new_workspace;
  NbtkWidget   *new_label;
  NbtkTooltip  *active_tooltip;
  GList        *last_workspaces;

  ClutterActor *last_focused;
  MutterWindow *selected;
  GList        *tab_list;

  guint         show_completed_id;
  guint         hide_panel_cb_id;

  gboolean      dnd_in_progress : 1;
  gboolean      constructing    : 1;

  gint          active_ws;
};

struct input_data
{
  gint          index;
  MutterPlugin *plugin;
};

/*
 * Calback for clicks on a workspace in the switcher (switches to the
 * appropriate ws).
 */
static gboolean
workspace_input_cb (ClutterActor *clone, ClutterEvent *event, gpointer data)
{
  struct input_data *input_data = data;
  gint               indx       = input_data->index;
  MutterPlugin      *plugin     = input_data->plugin;
  MetaScreen        *screen     = mutter_plugin_get_screen (plugin);
  MetaWorkspace     *workspace;
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  guint32            timestamp = clutter_x11_get_current_event_time ();

  if (MNB_SWITCHER (priv->switcher)->priv->dnd_in_progress)
    return FALSE;

  workspace = meta_screen_get_workspace_by_index (screen, indx);

  if (!workspace)
    {
      g_warning ("No workspace specified, %s:%d\n", __FILE__, __LINE__);
      return FALSE;
    }

  clutter_actor_hide (priv->switcher);

  if (priv->in_alt_grab)
    {
      MetaDisplay *display = meta_screen_get_display (screen);

      /*
       * Make sure our stamp is recent enough.
       */
      timestamp = meta_display_get_current_time_roundtrip (display);

      meta_display_end_grab_op (display, timestamp);
      priv->in_alt_grab = FALSE;
    }

  meta_workspace_activate (workspace, timestamp);

  return FALSE;
}

static gboolean
workspace_switcher_clone_input_cb (ClutterActor *clone,
                                   ClutterEvent *event,
                                   gpointer      data)
{
  MnbSwitcherAppPrivate      *app_priv = MNB_SWITCHER_APP (clone)->priv;
  MutterWindow               *mw = app_priv->mw;
  MetaWindow                 *window;
  MetaWorkspace              *workspace;
  MetaWorkspace              *active_workspace;
  MetaScreen                 *screen;
  MnbSwitcher                *switcher = app_priv->switcher;
  MutterPlugin               *plugin = switcher->priv->plugin;
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  guint32                     timestamp;

  if (MNB_SWITCHER (priv->switcher)->priv->dnd_in_progress)
    return FALSE;

  window           = mutter_window_get_meta_window (mw);
  screen           = meta_window_get_screen (window);
  workspace        = meta_window_get_workspace (window);
  active_workspace = meta_screen_get_active_workspace (screen);
  timestamp        = clutter_x11_get_current_event_time ();

  clutter_actor_hide (CLUTTER_ACTOR (switcher));
  clutter_ungrab_pointer ();

  if (!active_workspace || (active_workspace == workspace))
    {
      meta_window_activate_with_workspace (window, timestamp, workspace);
    }
  else
    {
      meta_workspace_activate_with_focus (workspace, window, timestamp);
    }

  return FALSE;
}

static void
dnd_begin_cb (NbtkWidget   *table,
	      ClutterActor *dragged,
	      ClutterActor *icon,
	      gint          x,
	      gint          y,
	      gpointer      data)
{
  MnbSwitcherPrivate    *priv         = MNB_SWITCHER (data)->priv;
  MnbSwitcherAppPrivate *dragged_priv = MNB_SWITCHER_APP (dragged)->priv;

  priv->dnd_in_progress = TRUE;

  if (dragged_priv->hover_timeout_id)
    {
      g_source_remove (dragged_priv->hover_timeout_id);
      dragged_priv->hover_timeout_id = 0;
    }

  if (CLUTTER_ACTOR_IS_VISIBLE (dragged_priv->tooltip))
    {
      nbtk_tooltip_hide (NBTK_TOOLTIP (dragged_priv->tooltip));

      if (priv->active_tooltip == (NbtkTooltip*)dragged_priv->tooltip)
        priv->active_tooltip = NULL;
    }

  clutter_actor_set_opacity (dragged, 0x4f);

  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_workspace),
                          "switcher-workspace-new-active");
  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_label),
                          "workspace-title-new-active");
}

static void
dnd_end_cb (NbtkWidget   *table,
	    ClutterActor *dragged,
	    ClutterActor *icon,
	    gint          x,
	    gint          y,
	    gpointer      data)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (data)->priv;

  priv->dnd_in_progress = FALSE;

  clutter_actor_set_rotation (icon, CLUTTER_Y_AXIS, 0.0, 0, 0, 0);
  clutter_actor_set_opacity (dragged, 0xff);

  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_workspace), "");
  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_label), "");
}

static void
dnd_motion_cb (NbtkWidget   *table,
               ClutterActor *dragged,
               ClutterActor *icon,
               gint          x,
               gint          y,
               gpointer      data)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (data)->priv;
  gint                screen_width, screen_height;
  gdouble             dx, f;

  mutter_plugin_query_screen_size (priv->plugin, &screen_width, &screen_height);

  dx = (gdouble)(x - screen_width/2);

  f = 30. * dx / (gdouble)screen_width;

  clutter_actor_set_rotation (icon, CLUTTER_Y_AXIS, f,
                              0, clutter_actor_get_width (icon)/2, 0);
}

static gint
tablist_sort_func (gconstpointer a, gconstpointer b)
{
  ClutterActor          *clone1 = CLUTTER_ACTOR (a);
  ClutterActor          *clone2 = CLUTTER_ACTOR (b);
  ClutterActor          *parent1 = clutter_actor_get_parent (clone1);
  ClutterActor          *parent2 = clutter_actor_get_parent (clone2);
  ClutterActor          *gparent1 = clutter_actor_get_parent (parent1);
  ClutterActor          *gparent2 = clutter_actor_get_parent (parent2);
  gint                   pcol1, pcol2;

  if (parent1 == parent2)
    {
      /*
       * The simple case of both clones on the same workspace.
       */
      gint row1, row2, col1, col2;

      clutter_container_child_get (CLUTTER_CONTAINER (parent1), clone1,
                                   "row", &row1, "column", &col1, NULL);
      clutter_container_child_get (CLUTTER_CONTAINER (parent1), clone2,
                                   "row", &row2, "column", &col2, NULL);

      if (row1 < row2)
        return -1;

      if (row1 > row2)
        return 1;

      if (col1 < col2)
        return -1;

      if (col1 > col2)
        return 1;

      return 0;
    }

  clutter_container_child_get (CLUTTER_CONTAINER (gparent1), parent1,
                               "column", &pcol1, NULL);
  clutter_container_child_get (CLUTTER_CONTAINER (gparent2), parent2,
                               "column", &pcol2, NULL);

  if (pcol1 < pcol2)
    return -1;

  if (pcol1 > pcol2)
    return 1;

  return 0;
}

static void
dnd_dropped_cb (NbtkWidget   *table,
		ClutterActor *dragged,
		ClutterActor *icon,
		gint          x,
		gint          y,
		gpointer      data)
{
  MnbSwitcher           *switcher = MNB_SWITCHER (data);
  MnbSwitcherPrivate    *priv = switcher->priv;
  MnbSwitcherAppPrivate *dragged_priv = MNB_SWITCHER_APP (dragged)->priv;
  ClutterChildMeta      *meta;
  ClutterActor          *parent;
  ClutterActor          *table_actor = CLUTTER_ACTOR (table);
  MetaWindow            *meta_win;
  gint                   col;
  guint32                timestamp;

  if (!(meta_win = mutter_window_get_meta_window (dragged_priv->mw)))
    {
      g_warning ("No MutterWindow associated with this item.");
      return;
    }

  parent = clutter_actor_get_parent (table_actor);

  g_assert (NBTK_IS_TABLE (parent));

  meta = clutter_container_get_child_meta (CLUTTER_CONTAINER (parent),
					   table_actor);

  g_object_get (meta, "column", &col, NULL);

  if (priv->tab_list)
    {
      priv->tab_list = g_list_sort (priv->tab_list, tablist_sort_func);
    }

  timestamp = clutter_x11_get_current_event_time ();
  meta_window_change_workspace_by_index (meta_win, col, TRUE, timestamp);
}

static NbtkTable *
mnb_switcher_append_workspace (MnbSwitcher *switcher);

static void
dnd_new_dropped_cb (NbtkWidget   *table,
                    ClutterActor *dragged,
                    ClutterActor *icon,
                    gint          x,
                    gint          y,
                    gpointer      data)
{
  MnbSwitcher           *switcher = MNB_SWITCHER (data);
  MnbSwitcherPrivate    *priv = switcher->priv;
  MnbSwitcherAppPrivate *dragged_priv = MNB_SWITCHER_APP (dragged)->priv;
  ClutterChildMeta      *meta, *d_meta;
  ClutterActor          *parent;
  ClutterActor          *table_actor = CLUTTER_ACTOR (table);
  MetaWindow            *meta_win;
  gint                   col;
  NbtkTable             *new_ws;
  gboolean               keep_ratio = FALSE;
  guint32                timestamp;

  if (!(meta_win = mutter_window_get_meta_window (dragged_priv->mw)))
    {
      g_warning ("No MutterWindow associated with this item.");
      return;
    }

  parent = clutter_actor_get_parent (table_actor);

  g_assert (NBTK_IS_TABLE (parent));

  meta = clutter_container_get_child_meta (CLUTTER_CONTAINER (parent),
					   table_actor);
  d_meta = clutter_container_get_child_meta (CLUTTER_CONTAINER (table),
                                             dragged);

  g_object_get (meta, "column", &col, NULL);
  g_object_get (d_meta, "keep-aspect-ratio", &keep_ratio, NULL);

  new_ws = mnb_switcher_append_workspace (switcher);

  g_object_ref (dragged);
  clutter_container_remove_actor (CLUTTER_CONTAINER (table), dragged);
  nbtk_table_add_actor (new_ws, dragged, 1, 0);

  clutter_container_child_set (CLUTTER_CONTAINER (new_ws), dragged,
			       "keep-aspect-ratio", keep_ratio, NULL);

  g_object_unref (dragged);

  if (priv->tab_list)
    {
      priv->tab_list = g_list_sort (priv->tab_list, tablist_sort_func);
    }

  timestamp = clutter_x11_get_current_event_time ();
  meta_window_change_workspace_by_index (meta_win, col, TRUE, timestamp);
}

static gboolean
clone_hover_timeout_cb (gpointer data)
{
  MnbSwitcherAppPrivate *app_priv = MNB_SWITCHER_APP (data)->priv;
  MnbSwitcherPrivate    *priv     = app_priv->switcher->priv;

  if (!priv->dnd_in_progress)
    {
      if (priv->active_tooltip)
        nbtk_tooltip_hide (priv->active_tooltip);

      priv->active_tooltip = NBTK_TOOLTIP (app_priv->tooltip);
      nbtk_tooltip_show (priv->active_tooltip);
    }

  app_priv->hover_timeout_id = 0;

  return FALSE;
}

static gboolean
clone_enter_event_cb (ClutterActor *actor,
		      ClutterCrossingEvent *event,
		      gpointer data)
{
  MnbSwitcherAppPrivate *child_priv = MNB_SWITCHER_APP (actor)->priv;
  MnbSwitcherPrivate    *priv       = child_priv->switcher->priv;

  if (!priv->dnd_in_progress)
    child_priv->hover_timeout_id = g_timeout_add (HOVER_TIMEOUT,
						  clone_hover_timeout_cb,
						  actor);

  return FALSE;
}

static gboolean
clone_leave_event_cb (ClutterActor *actor,
		      ClutterCrossingEvent *event,
		      gpointer data)
{
  MnbSwitcherAppPrivate *child_priv = MNB_SWITCHER_APP (actor)->priv;
  MnbSwitcherPrivate    *priv       = child_priv->switcher->priv;

  if (child_priv->hover_timeout_id)
    {
      g_source_remove (child_priv->hover_timeout_id);
      child_priv->hover_timeout_id = 0;
    }

  if (CLUTTER_ACTOR_IS_VISIBLE (child_priv->tooltip))
    {
      nbtk_tooltip_hide (NBTK_TOOLTIP (child_priv->tooltip));

      if (priv->active_tooltip == (NbtkTooltip*)child_priv->tooltip)
        priv->active_tooltip = NULL;
    }

  return FALSE;
}

static ClutterActor *
table_find_child (ClutterContainer *table, gint row, gint col)
{
  ClutterActor *child = NULL;
  GList        *l, *kids;

  kids = l = clutter_container_get_children (CLUTTER_CONTAINER (table));

  while (l)
    {
      ClutterActor *a = l->data;
      gint r, c;

      clutter_container_child_get (table, a, "row", &r, "column", &c, NULL);

      if ((r == row) && (c == col))
        {
          child = a;
          break;
        }

      l = l->next;
    }

  g_list_free (kids);

  return child;
}

static void
dnd_enter_cb (NbtkWidget   *table,
              ClutterActor *dragged,
              ClutterActor *icon,
              gint          x,
              gint          y,
              gpointer      data)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (data)->priv;
  ClutterActor *label;
  gint          col;

  clutter_container_child_get (CLUTTER_CONTAINER (priv->table),
                               CLUTTER_ACTOR (table),
                               "column", &col, NULL);

  label = table_find_child (CLUTTER_CONTAINER (priv->table), 0, col);

  clutter_actor_set_name (CLUTTER_ACTOR (table), "switcher-workspace-new-over");

  if (label)
    clutter_actor_set_name (label, "workspace-title-new-over");
}

static void
dnd_leave_cb (NbtkWidget   *table,
              ClutterActor *dragged,
              ClutterActor *icon,
              gint          x,
              gint          y,
              gpointer      data)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (data)->priv;
  ClutterActor *label;
  gint          col;

  clutter_container_child_get (CLUTTER_CONTAINER (priv->table),
                               CLUTTER_ACTOR (table), "column", &col, NULL);

  label = table_find_child (CLUTTER_CONTAINER (priv->table), 0, col);

  if (priv->active_ws == col)
    {
      clutter_actor_set_name (CLUTTER_ACTOR (table),
                              "switcher-workspace-active");

      if (label)
        clutter_actor_set_name (label, "workspace-title-active");
    }
  else
    {
      clutter_actor_set_name (CLUTTER_ACTOR (table), "");

      if (label)
        clutter_actor_set_name (label, "");
    }
}

static NbtkWidget *
make_workspace_content (MnbSwitcher *switcher, gboolean active, gint col)
{
  MnbSwitcherPrivate *priv = switcher->priv;
  NbtkWidget         *table = priv->table;
  NbtkWidget         *new_ws;
  struct input_data  *input_data = g_new (struct input_data, 1);

  input_data = g_new (struct input_data, 1);
  input_data->index = col;
  input_data->plugin = priv->plugin;

  new_ws = nbtk_table_new ();
  nbtk_table_set_row_spacing (NBTK_TABLE (new_ws), 6);
  nbtk_table_set_col_spacing (NBTK_TABLE (new_ws), 6);

  nbtk_widget_set_style_class_name (new_ws, "switcher-workspace");

  if (active)
    clutter_actor_set_name (CLUTTER_ACTOR (new_ws),
                            "switcher-workspace-active");

  nbtk_widget_set_dnd_threshold (new_ws, 5);

  g_signal_connect (new_ws, "dnd-begin",
                    G_CALLBACK (dnd_begin_cb), switcher);

  g_signal_connect (new_ws, "dnd-end",
                    G_CALLBACK (dnd_end_cb), switcher);

  g_signal_connect (new_ws, "dnd-motion",
                    G_CALLBACK (dnd_motion_cb), switcher);

  g_signal_connect (new_ws, "dnd-dropped",
                    G_CALLBACK (dnd_dropped_cb), switcher);

  g_signal_connect (new_ws, "dnd-enter",
                    G_CALLBACK (dnd_enter_cb), switcher);

  g_signal_connect (new_ws, "dnd-leave",
                    G_CALLBACK (dnd_leave_cb), switcher);

  nbtk_table_add_widget (NBTK_TABLE (table), new_ws, 1, col);

  /* switch workspace when the workspace is selected */
  g_signal_connect_data (new_ws, "button-press-event",
                         G_CALLBACK (workspace_input_cb), input_data,
                         (GClosureNotify)g_free, 0);

  return new_ws;
}

static NbtkWidget *
make_workspace_label (MnbSwitcher *switcher, gboolean active, gint col)
{
  MnbSwitcherPrivate *priv = switcher->priv;
  NbtkWidget         *table = priv->table;
  ClutterActor       *ws_label;
  NbtkWidget         *label;
  gchar              *s;
  struct input_data  *input_data = g_new (struct input_data, 1);

  input_data->index = col;
  input_data->plugin = priv->plugin;

  s = g_strdup_printf ("%d", col + 1);

  ws_label = nbtk_bin_new ();
  label = nbtk_label_new (s);

  nbtk_widget_set_style_class_name (label, "workspace-title-label");

  nbtk_bin_set_child (NBTK_BIN (ws_label), CLUTTER_ACTOR (label));
  nbtk_bin_set_alignment (NBTK_BIN (ws_label),
                          NBTK_ALIGN_CENTER, NBTK_ALIGN_CENTER);

  if (active)
    clutter_actor_set_name (CLUTTER_ACTOR (ws_label), "workspace-title-active");

  nbtk_widget_set_style_class_name (NBTK_WIDGET (ws_label), "workspace-title");

  clutter_actor_set_reactive (CLUTTER_ACTOR (ws_label), TRUE);

  g_signal_connect_data (ws_label, "button-press-event",
                         G_CALLBACK (workspace_input_cb), input_data,
                         (GClosureNotify) g_free, 0);

  nbtk_table_add_widget (NBTK_TABLE (table), NBTK_WIDGET (ws_label), 0, col);
  clutter_container_child_set (CLUTTER_CONTAINER (table),
                               CLUTTER_ACTOR (ws_label),
                               "y-expand", FALSE, NULL);

  return NBTK_WIDGET (ws_label);
}

struct ws_remove_data
{
  MnbSwitcher *switcher;
  gint         col;
  GList       *remove;
};

static void
table_foreach_remove_ws (ClutterActor *child, gpointer data)
{
  struct ws_remove_data *remove_data = data;
  MnbSwitcher           *switcher    = remove_data->switcher;
  NbtkWidget            *table       = switcher->priv->table;
  ClutterChildMeta      *meta;
  gint                   row, col;

  meta = clutter_container_get_child_meta (CLUTTER_CONTAINER (table), child);

  g_assert (meta);
  g_object_get (meta, "row", &row, "column", &col, NULL);

  /*
   * Children below the column we are removing are unaffected.
   */
  if (col < remove_data->col)
    return;

  /*
   * We cannot remove the actors in the foreach function, as that potentially
   * affects a list in which the container holds the data (e.g., NbtkTable).
   * We schedule it for removal, and then remove all once we are finished with
   * the foreach.
   */
  if (col == remove_data->col)
    {
      remove_data->remove = g_list_prepend (remove_data->remove, child);
    }
  else
    {
      /*
       * For some reason changing the colum clears the y-expand property :-(
       * Need to preserve it on the first row.
       */
      if (!row)
        {
          gchar *s;

          clutter_container_child_set (CLUTTER_CONTAINER (table), child,
                                       "column", col - 1,
                                       "y-expand", FALSE, NULL);

          s = g_strdup_printf ("%d", col);

          nbtk_label_set_text (NBTK_LABEL (child), s);
          g_free (s);
        }
      else
        clutter_container_child_set (CLUTTER_CONTAINER (table), child,
                                     "column", col - 1, NULL);
    }
}

static void
screen_n_workspaces_notify (MetaScreen *screen,
                            GParamSpec *pspec,
                            gpointer    data)
{
  MnbSwitcher *switcher = MNB_SWITCHER (data);
  gint         n_c_workspaces;
  GList       *c_workspaces;
  GList       *o_workspaces;
  gint         n_o_workspaces;
  gboolean    *map;
  gint         i;
  GList       *k;
  struct ws_remove_data remove_data;

  if (!CLUTTER_ACTOR_IS_VISIBLE (CLUTTER_ACTOR (switcher)))
    return;

  n_c_workspaces = meta_screen_get_n_workspaces (screen);
  c_workspaces   = meta_screen_get_workspaces (screen);
  o_workspaces   = switcher->priv->last_workspaces;
  n_o_workspaces = g_list_length (o_workspaces);

  if (n_c_workspaces < 8)
      nbtk_widget_set_dnd_threshold (switcher->priv->new_workspace, 5);
  else
      nbtk_widget_set_dnd_threshold (switcher->priv->new_workspace, 0);

  if (n_o_workspaces < n_c_workspaces)
    {
      /*
       * The relayout is handled in the dnd_dropped callback.
       */
      g_list_free (switcher->priv->last_workspaces);
      switcher->priv->last_workspaces = g_list_copy (c_workspaces);
      return;
    }

  remove_data.switcher = switcher;
  remove_data.remove = NULL;

  map = g_slice_alloc0 (sizeof (gboolean) * n_o_workspaces);

  k = c_workspaces;

  while (k)
    {
      MetaWorkspace *w = k->data;
      GList         *l = o_workspaces;

      i = 0;

      while (l)
        {
          MetaWorkspace *w2 = l->data;

          if (w == w2)
            {
              map[i] = TRUE;
              break;
            }

          ++i;
          l = l->next;
        }

      k = k->next;
    }

  for (i = 0; i < n_o_workspaces; ++i)
    {
      if (!map[i])
        {
          GList *l;
          ClutterContainer *table = CLUTTER_CONTAINER (switcher->priv->table);

          remove_data.col = i;
          clutter_container_foreach (table,
                                     (ClutterCallback) table_foreach_remove_ws,
                                     &remove_data);

          l = remove_data.remove;
          while (l)
            {
              ClutterActor *child = l->data;

              clutter_container_remove_actor (table, child);

              l = l->next;
            }

          g_list_free (remove_data.remove);
        }
    }

  g_list_free (switcher->priv->last_workspaces);
  switcher->priv->last_workspaces = g_list_copy (c_workspaces);
}

static void
dnd_new_enter_cb (NbtkWidget   *table,
                  ClutterActor *dragged,
                  ClutterActor *icon,
                  gint          x,
                  gint          y,
                  gpointer      data)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (data)->priv;

  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_workspace),
                          "switcher-workspace-new-over");
  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_label),
                          "workspace-title-new-over");
}

static void
dnd_new_leave_cb (NbtkWidget   *table,
                  ClutterActor *dragged,
                  ClutterActor *icon,
                  gint          x,
                  gint          y,
                  gpointer      data)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (data)->priv;

  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_workspace), "");
  clutter_actor_set_name (CLUTTER_ACTOR (priv->new_label), "");
}

static void
meta_window_focus_cb (MetaWindow *mw, gpointer data)
{
  MnbSwitcherAppPrivate *child_priv = MNB_SWITCHER_APP (data)->priv;
  MnbSwitcher           *switcher = child_priv->switcher;
  MnbSwitcherPrivate    *priv = switcher->priv;

  if (priv->constructing || priv->last_focused == data)
    return;

  if (priv->last_focused)
    clutter_actor_set_name (priv->last_focused, "");

  clutter_actor_set_name (CLUTTER_ACTOR (data), "switcher-application-active");
  priv->last_focused = data;
  priv->selected = child_priv->mw;
}

static void mnb_switcher_clone_weak_notify (gpointer data, GObject *object);

struct origin_data
{
  ClutterActor *clone;
  MutterWindow *mw;
  MnbSwitcher  *switcher;
};

static void
mnb_switcher_origin_weak_notify (gpointer data, GObject *obj)
{
  struct origin_data *origin_data = data;
  ClutterActor       *clone = origin_data->clone;
  MnbSwitcherPrivate *priv  = origin_data->switcher->priv;

  if (priv->tab_list)
    {
      priv->tab_list = g_list_remove (priv->tab_list, clone);
    }

  /*
   * The original MutterWindow destroyed; remove the weak reference the
   * we added to the clone referencing the original window, then
   * destroy the clone.
   */
  g_object_weak_unref (G_OBJECT (clone), mnb_switcher_clone_weak_notify, data);
  clutter_actor_destroy (clone);

  g_free (data);
}

static void
mnb_switcher_clone_weak_notify (gpointer data, GObject *obj)
{
  struct origin_data *origin_data = data;
  GObject            *origin = G_OBJECT (origin_data->mw);

  /*
   * Clone destroyed -- this function gets only called whent the clone
   * is destroyed while the original MutterWindow still exists, so remove
   * the weak reference we added on the origin for sake of the clone.
   */
  if (origin_data->switcher->priv->last_focused == (ClutterActor*)obj)
    origin_data->switcher->priv->last_focused = NULL;

  g_object_weak_unref (origin, mnb_switcher_origin_weak_notify, data);
}

static void
on_show_completed_cb (ClutterActor *self, gpointer data)
{
  MnbSwitcherPrivate    *priv     = MNB_SWITCHER (self)->priv;
  MnbSwitcherAppPrivate *app_priv = MNB_SWITCHER_APP (data)->priv;

  if (priv->active_tooltip)
    {
      nbtk_tooltip_hide (priv->active_tooltip);
      priv->active_tooltip = NULL;
    }

  if (app_priv->tooltip)
    {
      ClutterActorBox box;

      priv->active_tooltip = NBTK_TOOLTIP (app_priv->tooltip);

      /*
       * Make sure there is no pending allocation (without this the initial
       * tooltip is place incorrectly).
       */
      clutter_actor_get_allocation_box (self, &box);

      nbtk_tooltip_show (priv->active_tooltip);
    }
}

static void
mnb_switcher_show (ClutterActor *self)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (self)->priv;
  MetaScreen   *screen = mutter_plugin_get_screen (priv->plugin);
  MetaDisplay  *display = meta_screen_get_display (screen);
  gint          ws_count, active_ws;
  gint          i, screen_width, screen_height;
  NbtkWidget   *table;
  GList        *window_list, *l;
  NbtkWidget  **spaces;
  GList        *workspaces = meta_screen_get_workspaces (screen);
  MetaWindow   *current_focus = NULL;
  ClutterActor *current_focus_clone = NULL;
  ClutterActor *top_most_clone = NULL;
  MutterWindow *top_most_mw = NULL;
  gboolean      found_focus = FALSE;

  struct win_location
  {
    gint  row;
    gint  col;
    guint height;
  } *win_locs;

  priv->constructing = TRUE;

  current_focus = meta_display_get_focus_window (display);

  mutter_plugin_query_screen_size (priv->plugin, &screen_width, &screen_height);

  priv->last_workspaces = g_list_copy (workspaces);

  if (priv->tab_list)
    {
      g_list_free (priv->tab_list);
      priv->tab_list = NULL;
    }

  /* create the contents */

  table = nbtk_table_new ();
  priv->table = table;
  nbtk_table_set_row_spacing (NBTK_TABLE (table), 4);
  nbtk_table_set_col_spacing (NBTK_TABLE (table), 7);

  clutter_actor_set_name (CLUTTER_ACTOR (table), "switcher-table");

  ws_count = meta_screen_get_n_workspaces (screen);
  priv->active_ws = active_ws = meta_screen_get_active_workspace_index (screen);

  /* loop through all the workspaces, adding a label for each */
  for (i = 0; i < ws_count; i++)
    {
      gboolean active = FALSE;

      if (i == active_ws)
        active = TRUE;

      make_workspace_label (MNB_SWITCHER (self), active, i);
    }

  /* iterate through the windows, adding them to the correct workspace */

  win_locs    = g_slice_alloc0 (sizeof (struct win_location) * ws_count);
  spaces      = g_slice_alloc0 (sizeof (NbtkWidget*) * ws_count);
  window_list = mutter_plugin_get_windows (priv->plugin);

  for (l = window_list; l; l = g_list_next (l))
    {
      MutterWindow          *mw = l->data;
      ClutterActor          *texture, *c_tx, *clone;
      gint                   ws_indx;
      MetaCompWindowType     type;
      guint                  w, h;
      guint                  clone_h;
      struct origin_data    *origin_data;
      MetaWindow            *meta_win = mutter_window_get_meta_window (mw);
      gchar                 *title;
      MnbSwitcherAppPrivate *app_priv;

      ws_indx = mutter_window_get_workspace (mw);
      type = mutter_window_get_window_type (mw);
      /*
       * Only show regular windows that are not sticky (getting stacking order
       * right for sticky windows would be really hard, and since they appear
       * on each workspace, they do not help in identifying which workspace
       * it is).
       *
       * We show dialogs transient to root, as these can be top level
       * application windows.
       */
      if (ws_indx < 0                             ||
          mutter_window_is_override_redirect (mw) ||
          (type != META_COMP_WINDOW_NORMAL  &&
           type != META_COMP_WINDOW_DIALOG))
        {
          continue;
        }

      if (type ==  META_COMP_WINDOW_DIALOG)
        {
          MetaWindow *parent = meta_window_find_root_ancestor (meta_win);

          if (parent != meta_win)
            continue;
        }

      /* create the table for this workspace if we don't already have one */
      if (!spaces[ws_indx])
        {
          gboolean active = FALSE;

          if (ws_indx == active_ws)
            active = TRUE;

          spaces[ws_indx] =
            make_workspace_content (MNB_SWITCHER (self), active, ws_indx);
        }

      texture = mutter_window_get_texture (mw);
      c_tx    = clutter_clone_new (texture);
      clone   = g_object_new (MNB_TYPE_SWITCHER_APP, NULL);
      nbtk_widget_set_style_class_name (NBTK_WIDGET (clone),
                                        "switcher-application");

      /*
       * If the window has focus, apply the active style.
       */
      if (!found_focus &&
          (current_focus == meta_win ||
           (current_focus &&
            meta_window_is_ancestor_of_transient (meta_win, current_focus))))
        {
          found_focus = TRUE;

          clutter_actor_set_name (clone, "switcher-application-active");

          priv->last_focused = clone;
          priv->selected = mw;

          current_focus_clone = clone;
        }

      /*
       * Find the topmost window on the current workspace. We will used this
       * in case no window currently has focus.
       */
      if (active_ws == ws_indx)
        {
          top_most_clone = clone;
          top_most_mw = mw;
        }

      clutter_container_add_actor (CLUTTER_CONTAINER (clone), c_tx);

      clutter_actor_set_reactive (clone, TRUE);

      origin_data = g_new0 (struct origin_data, 1);
      origin_data->clone = clone;
      origin_data->mw = mw;
      origin_data->switcher = MNB_SWITCHER (self);
      priv->tab_list = g_list_prepend (priv->tab_list, clone);

      g_object_weak_ref (G_OBJECT (mw),
                         mnb_switcher_origin_weak_notify, origin_data);
      g_object_weak_ref (G_OBJECT (clone),
                         mnb_switcher_clone_weak_notify, origin_data);

      g_object_get (meta_win, "title", &title, NULL);

      app_priv = MNB_SWITCHER_APP (clone)->priv;
      app_priv->switcher = MNB_SWITCHER (self);
      app_priv->mw       = mw;
      app_priv->tooltip  = g_object_new (NBTK_TYPE_TOOLTIP,
                                         "widget", clone,
                                         "label", title,
                                         NULL);
      g_free (title);

      g_signal_connect (clone, "enter-event",
                        G_CALLBACK (clone_enter_event_cb), NULL);
      g_signal_connect (clone, "leave-event",
                        G_CALLBACK (clone_leave_event_cb), NULL);

      app_priv->focus_id =
        g_signal_connect (meta_win, "focus",
                          G_CALLBACK (meta_window_focus_cb), clone);
      app_priv->raised_id =
        g_signal_connect (meta_win, "raised",
                          G_CALLBACK (meta_window_focus_cb), clone);

      clutter_actor_get_size (clone, &h, &w);

      clone_h = (guint)((double)h/(gdouble)w * 80.0);
      clutter_actor_set_size (clone, clone_h, 80);

      /*
       * FIXME -- this depends on the styling, should not be hardcoded.
       */
      win_locs[ws_indx].height += (clone_h + 10);

      if (win_locs[ws_indx].height >= screen_height - 100 )
        {
          win_locs[ws_indx].col++;
          win_locs[ws_indx].row = 0;
          win_locs[ws_indx].height = clone_h + 10;
        }

      nbtk_table_add_actor (NBTK_TABLE (spaces[ws_indx]), clone,
                            win_locs[ws_indx].row++, win_locs[ws_indx].col);

      clutter_container_child_set (CLUTTER_CONTAINER (spaces[ws_indx]), clone,
                                   "keep-aspect-ratio", TRUE, NULL);

      g_signal_connect (clone, "button-release-event",
                        G_CALLBACK (workspace_switcher_clone_input_cb),
			NULL);
    }

  /*
   * If no window is currenlty focused, then try to focus the topmost window.
   */
  if (!current_focus && top_most_clone)
    {
      MetaWindow    *meta_win = mutter_window_get_meta_window (top_most_mw);
      MetaWorkspace *workspace;
      guint32        timestamp;

      clutter_actor_set_name (top_most_clone, "switcher-application-active");

      priv->last_focused = top_most_clone;
      priv->selected = top_most_mw;

      timestamp = clutter_x11_get_current_event_time ();
      workspace = meta_window_get_workspace (meta_win);

      current_focus_clone = top_most_clone;

      meta_window_activate_with_workspace (meta_win, timestamp, workspace);
    }

  /* add an "empty" message for empty workspaces */
  for (i = 0; i < ws_count; i++)
    {
      if (!spaces[i])
        {
          NbtkWidget *label;

          label = nbtk_label_new ("No applications on this zone");

          nbtk_table_add_widget (NBTK_TABLE (table), label, 1, i);
        }
    }

  /*
   * Now create the new workspace column.
   */
  {
    NbtkWidget *new_ws = nbtk_table_new ();
    NbtkWidget *label;

    label = NBTK_WIDGET (nbtk_bin_new ());
    nbtk_table_add_widget (NBTK_TABLE (table), label, 0, ws_count);
    nbtk_widget_set_style_class_name (label, "workspace-title-new");
    clutter_container_child_set (CLUTTER_CONTAINER (table),
                                 CLUTTER_ACTOR (label),
                                 "y-expand", FALSE, NULL);

    nbtk_table_set_row_spacing (NBTK_TABLE (new_ws), 6);
    nbtk_table_set_col_spacing (NBTK_TABLE (new_ws), 6);
    nbtk_widget_set_style_class_name (NBTK_WIDGET (new_ws),
                                      "switcher-workspace-new");

    if (ws_count < 8)
      nbtk_widget_set_dnd_threshold (new_ws, 5);

    g_signal_connect (new_ws, "dnd-begin",
                      G_CALLBACK (dnd_begin_cb), self);

    g_signal_connect (new_ws, "dnd-end",
                      G_CALLBACK (dnd_end_cb), self);

    g_signal_connect (new_ws, "dnd-dropped",
                      G_CALLBACK (dnd_new_dropped_cb), self);

    g_signal_connect (new_ws, "dnd-enter",
                      G_CALLBACK (dnd_new_enter_cb), self);

    g_signal_connect (new_ws, "dnd-leave",
                      G_CALLBACK (dnd_new_leave_cb), self);

    priv->new_workspace = new_ws;
    priv->new_label = label;

    nbtk_table_add_widget (NBTK_TABLE (table), new_ws, 1, ws_count);
  }

  g_slice_free1 (sizeof (NbtkWidget*) * ws_count, spaces);
  g_slice_free1 (sizeof (struct win_location) * ws_count, win_locs);

  priv->tab_list = g_list_sort (priv->tab_list, tablist_sort_func);

  mnb_drop_down_set_child (MNB_DROP_DOWN (self),
                           CLUTTER_ACTOR (table));

  priv->constructing = FALSE;

  /*
   * We connect to the show-completed signal, and if there is something focused
   * in the switcher (should be most of the time), we try to pop up the
   * tooltip from the callback.
   */
  if (priv->show_completed_id)
    {
      g_signal_handler_disconnect (self, priv->show_completed_id);
    }

  if (current_focus_clone)
    priv->show_completed_id =
      g_signal_connect (self, "show-completed",
                        G_CALLBACK (on_show_completed_cb),
                        current_focus_clone);

  CLUTTER_ACTOR_CLASS (mnb_switcher_parent_class)->show (self);
}

static NbtkTable *
mnb_switcher_append_workspace (MnbSwitcher *switcher)
{
  MnbSwitcherPrivate *priv = switcher->priv;
  NbtkWidget         *table = priv->table;
  NbtkWidget         *last_ws = priv->new_workspace;
  NbtkWidget         *last_label = priv->new_label;
  NbtkTable          *new_ws;
  gint                col;
  ClutterChildMeta   *meta;

  meta = clutter_container_get_child_meta (CLUTTER_CONTAINER (table),
                                           CLUTTER_ACTOR (last_ws));

  g_object_get (meta, "column", &col, NULL);

  clutter_container_child_set (CLUTTER_CONTAINER (table),
                               CLUTTER_ACTOR (last_ws),
                               "column", col + 1, NULL);

  clutter_container_child_set (CLUTTER_CONTAINER (table),
                               CLUTTER_ACTOR (last_label),
                               "column", col + 1,
                               "y-expand", FALSE, NULL);

  /*
   * Insert new workspace label and content pane where the new workspace
   * area was.
   */
  make_workspace_label   (switcher, FALSE, col);
  new_ws = NBTK_TABLE (make_workspace_content (switcher, FALSE, col));

  return new_ws;
}

static void
mnb_switcher_hide (ClutterActor *self)
{
  MnbSwitcherPrivate *priv = MNB_SWITCHER (self)->priv;

  if (priv->show_completed_id)
    {
      g_signal_handler_disconnect (self, priv->show_completed_id);
      priv->show_completed_id = 0;
    }

  CLUTTER_ACTOR_CLASS (mnb_switcher_parent_class)->hide (self);
}

static void
mnb_switcher_finalize (GObject *object)
{
  MnbSwitcher *switcher = MNB_SWITCHER (object);

  g_list_free (switcher->priv->last_workspaces);

  G_OBJECT_CLASS (mnb_switcher_parent_class)->finalize (object);
}

static void
mnb_switcher_class_init (MnbSwitcherClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  object_class->finalize = mnb_switcher_finalize;

  actor_class->show = mnb_switcher_show;
  actor_class->hide = mnb_switcher_hide;

  g_type_class_add_private (klass, sizeof (MnbSwitcherPrivate));
}

static void
on_switcher_hide_completed_cb (ClutterActor *self, gpointer data)
{
  MnbSwitcherPrivate *priv;

  g_return_if_fail (MNB_IS_SWITCHER (self));

  priv = MNB_SWITCHER (self)->priv;

  if (priv->tab_list)
    {
      g_list_free (priv->tab_list);
      priv->tab_list = NULL;
    }

  mnb_drop_down_set_child (MNB_DROP_DOWN (self), NULL);
  priv->table = NULL;
  priv->last_focused = NULL;
  priv->selected = NULL;
}

static void
mnb_switcher_init (MnbSwitcher *self)
{
  self->priv = MNB_SWITCHER_GET_PRIVATE (self);

  g_signal_connect (self, "hide-completed",
                    G_CALLBACK (on_switcher_hide_completed_cb), NULL);
}

NbtkWidget*
mnb_switcher_new (MutterPlugin *plugin)
{
  MnbSwitcher *switcher;
  MetaScreen  *screen;

  g_return_val_if_fail (MUTTER_PLUGIN (plugin), NULL);

  switcher = g_object_new (MNB_TYPE_SWITCHER, NULL);
  switcher->priv->plugin = plugin;

  screen = mutter_plugin_get_screen (plugin);

  g_signal_connect (screen, "notify::n-workspaces",
                    G_CALLBACK (screen_n_workspaces_notify), switcher);

  return NBTK_WIDGET (switcher);
}


static void
select_inner_foreach_cb (ClutterActor *child, gpointer data)
{
  MnbSwitcherAppPrivate *app_priv;
  MetaWindow            *meta_win = data;
  MetaWindow            *my_win;
  MnbSwitcherPrivate    *priv;

  /*
   * Skip anything that is not a MnbSwitcherApp
   */
  if (!MNB_IS_SWITCHER_APP (child))
    return;

  app_priv = MNB_SWITCHER_APP (child)->priv;
  priv     = app_priv->switcher->priv;

  my_win = mutter_window_get_meta_window (app_priv->mw);

  if (meta_win == my_win)
    {
      clutter_actor_set_name (child, "switcher-application-active");

      app_priv->switcher->priv->selected = app_priv->mw;

      if (app_priv->tooltip)
        {
          if (priv->active_tooltip)
            nbtk_tooltip_hide (priv->active_tooltip);

          priv->active_tooltip = NBTK_TOOLTIP (app_priv->tooltip);
        }
    }
  else
    {
      clutter_actor_set_name (child, "");

      if (app_priv->tooltip)
        {
          if (priv->active_tooltip == (NbtkTooltip*)app_priv->tooltip)
            priv->active_tooltip = NULL;

          nbtk_tooltip_hide (NBTK_TOOLTIP (app_priv->tooltip));
        }
    }
}

static void
select_outer_foreach_cb (ClutterActor *child, gpointer data)
{
  gint          row;
  ClutterActor *parent;
  MetaWindow   *meta_win = data;

  parent = clutter_actor_get_parent (child);

  clutter_container_child_get (CLUTTER_CONTAINER (parent), child,
                               "row", &row, NULL);

  /* Skip the header row */
  if (!row)
    return;

  if (!NBTK_IS_TABLE (child))
    return;

  clutter_container_foreach (CLUTTER_CONTAINER (child),
                             select_inner_foreach_cb,
                             meta_win);
}

void
mnb_switcher_select_window (MnbSwitcher *switcher, MetaWindow *meta_win)
{
  MnbSwitcherPrivate *priv = switcher->priv;
  ClutterActorBox     box;

  if (!priv->table)
    return;

  clutter_container_foreach (CLUTTER_CONTAINER (priv->table),
                             select_outer_foreach_cb, meta_win);

  if (priv->active_tooltip)
    {
      /*
       * The above changes styling of the contents and hence leaves the actor in
       * unallocated state -- this brute forces allocation, which is necessary
       * before we can show the tooltip.
       */
      clutter_actor_get_allocation_box (CLUTTER_ACTOR (priv->table), &box);

      nbtk_tooltip_show (priv->active_tooltip);
    }
}

void
mnb_switcher_activate_selection (MnbSwitcher *switcher, gboolean close,
                                 guint timestamp)
{
  MnbSwitcherPrivate *priv = switcher->priv;

  MetaWindow                 *window;
  MetaWorkspace              *workspace;
  MetaWorkspace              *active_workspace;
  MetaScreen                 *screen;
  MutterPlugin               *plugin;

  if (!priv->selected)
    return;

  plugin           = switcher->priv->plugin;
  window           = mutter_window_get_meta_window (priv->selected);
  screen           = meta_window_get_screen (window);
  workspace        = meta_window_get_workspace (window);
  active_workspace = meta_screen_get_active_workspace (screen);

  if (close)
    mnb_switcher_hide_with_panel (switcher);

  if (!active_workspace || (active_workspace == workspace))
    {
      meta_window_activate_with_workspace (window, timestamp, workspace);
    }
  else
    {
      meta_workspace_activate_with_focus (workspace, window, timestamp);
    }
}

MetaWindow *
mnb_switcher_get_selection (MnbSwitcher *switcher)
{
  MnbSwitcherPrivate *priv = switcher->priv;

  if (!priv->selected)
    return NULL;

  return mutter_window_get_meta_window (priv->selected);
}

static gint
tablist_find_func (gconstpointer a, gconstpointer b)
{
  ClutterActor          *clone    = CLUTTER_ACTOR (a);
  MetaWindow            *meta_win = META_WINDOW (b);
  MetaWindow            *my_win;
  MnbSwitcherAppPrivate *app_priv = MNB_SWITCHER_APP (clone)->priv;

  my_win = mutter_window_get_meta_window (app_priv->mw);

  if (my_win == meta_win)
    return 0;

  return 1;
}

/*
 * Return the next window that Alt+Tab should advance to.
 *
 * The current parameter indicates where we should start from; if NULL, start
 * from the beginning of our Alt+Tab cycle list.
 *
 * FIXME -- this just a stub using the complete list of mutter windows.
 */
MetaWindow *
mnb_switcher_get_next_window (MnbSwitcher *switcher,
                              MetaWindow  *current,
                              gboolean     backward)
{
  MnbSwitcherPrivate    *priv = switcher->priv;
  GList                 *l;
  ClutterActor          *next = NULL;
  MnbSwitcherAppPrivate *next_priv;

  if (!current)
    {
      if (!priv->selected)
        return NULL;

      current = mutter_window_get_meta_window (priv->selected);
    }

  if (!priv->tab_list)
    {
      g_warning ("No tablist in existence!\n");

      return NULL;
    }

  l = g_list_find_custom (priv->tab_list, current, tablist_find_func);

  if (!backward)
    {
      if (!l || !l->next)
        next = priv->tab_list->data;
      else
        next = l->next->data;
    }
  else
    {
      if (!l || !l->prev)
        next = g_list_last (priv->tab_list)->data;
      else
        next = l->prev->data;
    }

  next_priv = MNB_SWITCHER_APP (next)->priv;

  return mutter_window_get_meta_window (next_priv->mw);
}

static void
hide_panel_on_hide_completed_cb (MnbSwitcher *switcher, gpointer data)
{
  MnbSwitcherPrivate *priv = switcher->priv;
  MutterPlugin       *plugin = priv->plugin;

  g_signal_handler_disconnect (switcher, priv->hide_panel_cb_id);
  priv->hide_panel_cb_id = 0;

  hide_panel (plugin);
}

/*
 * Hides both switcher and panel, in a sequnce so so as to preserve the
 * hide animations.
 */
void
mnb_switcher_hide_with_panel (MnbSwitcher *switcher)
{
  MnbSwitcherPrivate *priv = switcher->priv;

  if (priv->hide_panel_cb_id)
    return;

  priv->hide_panel_cb_id =
    g_signal_connect (switcher, "hide-completed",
                      G_CALLBACK (hide_panel_on_hide_completed_cb), NULL);

  clutter_actor_hide (CLUTTER_ACTOR (switcher));
}
