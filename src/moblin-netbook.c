/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
 *         Thomas Wood <thomas@linux.intel.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "moblin-netbook.h"
#include "moblin-netbook-chooser.h"
#include "mnb-drop-down.h"
#include "mnb-switcher.h"
#include "mnb-toolbar.h"
#include "effects/mnb-switch-zones-effect.h"

#include <glib/gi18n.h>

#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <gmodule.h>
#include <string.h>

#include <compositor-mutter.h>
#include <display.h>
#include <prefs.h>
#include <keybindings.h>

#define MINIMIZE_TIMEOUT            250
#define MAXIMIZE_TIMEOUT            250
#define MAP_TIMEOUT                 350
#define SWITCH_TIMEOUT              400
#define PANEL_SLIDE_THRESHOLD       1
#define PANEL_SLIDE_THRESHOLD_TIMEOUT 300
#define WS_SWITCHER_SLIDE_TIMEOUT   250
#define ACTOR_DATA_KEY "MCCP-moblin-netbook-actor-data"


/* callback data for when animations complete */
typedef struct
{
  ClutterActor *actor;
  MutterPlugin *plugin;
} EffectCompleteData;

static gboolean stage_input_cb (ClutterActor *stage, ClutterEvent *event,
                                gpointer data);
static gboolean stage_capture_cb (ClutterActor *stage, ClutterEvent *event,
                                  gpointer data);

static void setup_parallax_effect (MutterPlugin *plugin);

static void setup_focus_window (MutterPlugin *plugin);

static void
toolbar_trigger_region_set_height (MutterPlugin *plugin, gint height);

static GQuark actor_data_quark = 0;

static void     minimize   (MutterPlugin *plugin,
                            MutterWindow *actor);
static void     map        (MutterPlugin *plugin,
                            MutterWindow *actor);
static void     destroy    (MutterPlugin *plugin,
                            MutterWindow *actor);
static void     maximize   (MutterPlugin *plugin,
                            MutterWindow *actor,
                            gint x, gint y, gint width, gint height);
static void     unmaximize (MutterPlugin *plugin,
                            MutterWindow *actor,
                            gint x, gint y, gint width, gint height);

static void     kill_effect (MutterPlugin *plugin,
                             MutterWindow *actor, gulong event);

static const MutterPluginInfo * plugin_info (MutterPlugin *plugin);

static gboolean xevent_filter (MutterPlugin *plugin, XEvent *xev);

MUTTER_PLUGIN_DECLARE (MoblinNetbookPlugin, moblin_netbook_plugin);

static void moblin_netbook_input_region_apply (MutterPlugin *plugin);

static gboolean
on_lowlight_button_event (ClutterActor *actor,
                          ClutterEvent *event,
                          gpointer      user_data);


/*
 * Actor private data accessor
 */
static void
free_actor_private (gpointer data)
{
  if (G_LIKELY (data != NULL))
    g_slice_free (ActorPrivate, data);
}

ActorPrivate *
get_actor_private (MutterWindow *actor)
{
  ActorPrivate *priv = g_object_get_qdata (G_OBJECT (actor), actor_data_quark);

  if (G_UNLIKELY (actor_data_quark == 0))
    actor_data_quark = g_quark_from_static_string (ACTOR_DATA_KEY);

  if (G_UNLIKELY (!priv))
    {
      priv = g_slice_new0 (ActorPrivate);

      g_object_set_qdata_full (G_OBJECT (actor),
                               actor_data_quark, priv,
                               free_actor_private);
    }

  return priv;
}

static void
moblin_netbook_plugin_dispose (GObject *object)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (object)->priv;
  Display                    *xdpy;

  xdpy = mutter_plugin_get_xdisplay (MUTTER_PLUGIN (object));


  if (priv->current_input_region)
    {
      XFixesDestroyRegion (xdpy, priv->current_input_region);
      priv->current_input_region = None;
    }

  G_OBJECT_CLASS (moblin_netbook_plugin_parent_class)->dispose (object);
}

static void
moblin_netbook_plugin_finalize (GObject *object)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (object)->priv;

  G_OBJECT_CLASS (moblin_netbook_plugin_parent_class)->finalize (object);
}

static void
moblin_netbook_plugin_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
moblin_netbook_plugin_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/*
 * Metacity key handler for default Metacity bindings we want disabled.
 *
 * (This is necessary for keybidings that are related to the Alt+Tab shortcut.
 * In metacity these all use the src/ui/tabpopup.c object, which we have
 * disabled, so we need to take over all of those.)
 */
static void
metacity_nop_key_handler (MetaDisplay    *display,
                          MetaScreen     *screen,
                          MetaWindow     *window,
                          XEvent         *event,
                          MetaKeyBinding *binding,
                          gpointer        data)
{
}

static void
sync_notification_input_region_cb (ClutterActor        *notify_actor,
                                   MoblinNetbookPlugin *plugin)
{
  MoblinNetbookPluginPrivate *priv   = plugin->priv;

  if (priv->notification_input_region != NULL)
    {
      moblin_netbook_input_region_remove (MUTTER_PLUGIN(plugin),
                                          priv->notification_input_region);
      priv->notification_input_region = NULL;
    }

  if (CLUTTER_ACTOR_IS_VISIBLE (notify_actor))
    {
      gint x,y;
      guint width,height;

      clutter_actor_get_transformed_position (notify_actor, &x, &y);
      clutter_actor_get_transformed_size (notify_actor, &width, &height);

      if (width != 0 && height != 0)
        {
          priv->notification_input_region
            = moblin_netbook_input_region_push (MUTTER_PLUGIN(plugin),
                                                x, y, width, height);
        }
    }
}

static void
on_urgent_notifiy_visible_cb (ClutterActor    *notify_urgent,
                              GParamSpec      *pspec,
                              MutterPlugin *plugin)
{
  moblin_netbook_set_lowlight (plugin,
                               CLUTTER_ACTOR_IS_VISIBLE(notify_urgent));
}

static void
stage_show_cb (ClutterActor *stage, MutterPlugin *plugin)
{
  /*
   * Set up the stage even processing
   */
  toolbar_trigger_region_set_height (MUTTER_PLUGIN (plugin), 0);
}

static void
moblin_netbook_plugin_constructed (GObject *object)
{
  MoblinNetbookPlugin        *plugin = MOBLIN_NETBOOK_PLUGIN (object);
  MoblinNetbookPluginPrivate *priv   = plugin->priv;

  guint minimize_timeout          = MINIMIZE_TIMEOUT;
  guint maximize_timeout          = MAXIMIZE_TIMEOUT;
  guint map_timeout               = MAP_TIMEOUT;
  guint switch_timeout            = SWITCH_TIMEOUT;

  ClutterActor  *overlay;
  ClutterActor  *toolbar;
  ClutterActor  *lowlight;
  gint           screen_width, screen_height;
  XRectangle     rect[1];
  XserverRegion  region;
  Display       *xdpy = mutter_plugin_get_xdisplay (MUTTER_PLUGIN (plugin));
  ClutterColor   low_clr = { 0, 0, 0, 0x7f };
  GError        *err = NULL;
  MetaScreen    *screen;
  Window         root_xwin;
  MoblinNetbookNotifyStore *notify_store;

  /* tweak with env var as then possible to develop in desktop env. */
  if (!g_getenv("MUTTER_DISABLE_WS_CLAMP"))
    meta_prefs_set_num_workspaces (1);

  nbtk_style_load_from_file (nbtk_style_get_default (),
                             PLUGIN_PKGDATADIR "/theme/mutter-moblin.css",
                             &err);
  if (err)
    {
      g_warning ("%s", err->message);
      g_error_free (err);
    }

  mutter_plugin_query_screen_size (MUTTER_PLUGIN (plugin),
                                   &screen_width, &screen_height);

  if (mutter_plugin_debug_mode (MUTTER_PLUGIN (plugin)))
    {
      g_debug ("%s: Entering debug mode.", priv->info.name);
      /*
       * Double the effect duration to make them easier to observe.
       */
      minimize_timeout          *= 2;
      maximize_timeout          *= 2;
      map_timeout               *= 2;
      switch_timeout            *= 2;
    }

  overlay = mutter_plugin_get_overlay_group (MUTTER_PLUGIN (plugin));

  lowlight = clutter_rectangle_new_with_color (&low_clr);
  priv->lowlight = lowlight;
  clutter_actor_set_size (lowlight, screen_width, screen_height);
  clutter_actor_set_reactive (lowlight, TRUE);

  g_signal_connect (priv->lowlight, "captured-event",
                    G_CALLBACK (on_lowlight_button_event),
                    NULL);
  /*
   * This also creates the launcher.
   */
  toolbar = priv->toolbar =
    CLUTTER_ACTOR (mnb_toolbar_new (MUTTER_PLUGIN (plugin)));

#if 1
  /*
   * TODO this needs to be hooked into the dbus API exposed by the out of
   * process applets, once we have them.
   */
  mnb_toolbar_append_panel (MNB_TOOLBAR (toolbar), "m-zone",
                            "m_zone", NULL, None);

  mnb_toolbar_append_panel (MNB_TOOLBAR (toolbar), "spaces-zone",
                            "zones", NULL, None);

  mnb_toolbar_append_panel (MNB_TOOLBAR (toolbar), "status-zone",
                            "status", NULL, None);

  mnb_toolbar_append_panel (MNB_TOOLBAR (toolbar), "applications-zone",
                            "applications", NULL, None);

  mnb_toolbar_append_panel (MNB_TOOLBAR (toolbar), "pasteboard-zone",
                            "pastboard", NULL, None);

#endif

  clutter_container_add (CLUTTER_CONTAINER (overlay), lowlight, toolbar, NULL);
  clutter_actor_hide (lowlight);

  /*
   * Hook into "show" signal on stage, to set up input regions.
   * (We cannot set up the stage here, because the overlay window, etc.,
   * is not in place until the stage is shown.)
   */
  g_signal_connect (mutter_plugin_get_stage (MUTTER_PLUGIN (plugin)),
                    "show", G_CALLBACK (stage_show_cb), plugin);

  /*
   * Hook to the captured signal, so we get to see all events before our
   * children and do not interfere with their event processing.
   */
  g_signal_connect (mutter_plugin_get_stage (MUTTER_PLUGIN (plugin)),
                    "captured-event", G_CALLBACK (stage_capture_cb),
                    plugin);

  g_signal_connect (mutter_plugin_get_stage (MUTTER_PLUGIN (plugin)),
                    "button-press-event", G_CALLBACK (stage_input_cb),
                    plugin);
  g_signal_connect (mutter_plugin_get_stage (MUTTER_PLUGIN (plugin)),
                    "key-press-event", G_CALLBACK (stage_input_cb),
                    plugin);

  clutter_set_motion_events_enabled (TRUE);

  setup_parallax_effect (MUTTER_PLUGIN (plugin));

  setup_focus_window (MUTTER_PLUGIN (plugin));

  moblin_netbook_sn_setup (MUTTER_PLUGIN (plugin));

  /* Notifications */
  notify_store = moblin_netbook_notify_store_new ();

  priv->notification_cluster = mnb_notification_cluster_new ();

  mnb_notification_cluster_set_store
                    (MNB_NOTIFICATION_CLUSTER(priv->notification_cluster),
                     notify_store);

  clutter_container_add (CLUTTER_CONTAINER (overlay),
                         priv->notification_cluster, NULL);

  clutter_actor_set_anchor_point_from_gravity (priv->notification_cluster,
                                               CLUTTER_GRAVITY_SOUTH_EAST);

  clutter_actor_set_position (priv->notification_cluster,
                              screen_width,
                              screen_height);

  g_signal_connect (priv->notification_cluster,
                    "sync-input-region",
                    G_CALLBACK (sync_notification_input_region_cb),
                    MUTTER_PLUGIN (plugin));


  priv->notification_urgent = mnb_notification_urgent_new ();

  clutter_actor_set_anchor_point_from_gravity (priv->notification_urgent,
                                               CLUTTER_GRAVITY_CENTER);

  clutter_actor_set_position (priv->notification_urgent,
                              screen_width/2,
                              screen_height/2);

  clutter_container_add (CLUTTER_CONTAINER (overlay),
                         priv->notification_urgent, NULL);

  mnb_notification_urgent_set_store
                        (MNB_NOTIFICATION_URGENT(priv->notification_urgent),
                         notify_store);

  g_signal_connect (priv->notification_urgent,
                    "sync-input-region",
                    G_CALLBACK (sync_notification_input_region_cb),
                    MUTTER_PLUGIN (plugin));

  clutter_actor_hide (CLUTTER_ACTOR(priv->notification_urgent));

  g_signal_connect (priv->notification_urgent,
                    "notify::visible",
                    G_CALLBACK (on_urgent_notifiy_visible_cb),
                    MUTTER_PLUGIN (plugin));


  /* Keys */

  meta_prefs_override_no_tab_popup (TRUE);

  /*
   * Install NOP handler for shortcuts that are related to Alt+Tab.
   */
  meta_keybindings_set_custom_handler ("switch_group",
                                       metacity_nop_key_handler,
                                       plugin, NULL);
  meta_keybindings_set_custom_handler ("switch_group_backward",
                                       metacity_nop_key_handler,
                                       plugin, NULL);
  meta_keybindings_set_custom_handler ("switch_group_backward",
                                       metacity_nop_key_handler,
                                       plugin, NULL);
}

static void
moblin_netbook_plugin_class_init (MoblinNetbookPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  MutterPluginClass *plugin_class  = MUTTER_PLUGIN_CLASS (klass);

  gobject_class->finalize        = moblin_netbook_plugin_finalize;
  gobject_class->dispose         = moblin_netbook_plugin_dispose;
  gobject_class->constructed     = moblin_netbook_plugin_constructed;
  gobject_class->set_property    = moblin_netbook_plugin_set_property;
  gobject_class->get_property    = moblin_netbook_plugin_get_property;

  plugin_class->map              = map;
  plugin_class->minimize         = minimize;
  plugin_class->maximize         = maximize;
  plugin_class->unmaximize       = unmaximize;
  plugin_class->destroy          = destroy;
  plugin_class->switch_workspace = mnb_switch_zones_effect;
  plugin_class->kill_effect      = kill_effect;
  plugin_class->plugin_info      = plugin_info;
  plugin_class->xevent_filter    = xevent_filter;

  g_type_class_add_private (gobject_class, sizeof (MoblinNetbookPluginPrivate));
}

static void
moblin_netbook_plugin_init (MoblinNetbookPlugin *self)
{
  MoblinNetbookPluginPrivate *priv;

  self->priv = priv = MOBLIN_NETBOOK_PLUGIN_GET_PRIVATE (self);

  priv->info.name        = _("Moblin Netbook Effects");
  priv->info.version     = "0.1";
  priv->info.author      = "Intel Corp.";
  priv->info.license     = "GPL";
  priv->info.description = _("Effects for Moblin Netbooks");
}

static void
on_desktop_pre_paint (ClutterActor *actor, gpointer data)
{
  MoblinNetbookPlugin *plugin = data;
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  ClutterColor       col = { 0xff, 0xff, 0xff, 0xff };
  CoglHandle         cogl_texture;
  float              t_w, t_h;
  guint              tex_width, tex_height;
  guint              w, h;

  clutter_actor_get_size (priv->parallax_tex, &w, &h);

  cogl_translate (-(gint)w/4, 0 , 0);

  cogl_texture
       = clutter_texture_get_cogl_texture (CLUTTER_TEXTURE(priv->parallax_tex));

  if (cogl_texture == COGL_INVALID_HANDLE)
    return;

  col.alpha = clutter_actor_get_paint_opacity (actor);
  cogl_set_source_color4ub (col.red,
                            col.green,
                            col.blue,
                            col.alpha);

  tex_width = cogl_texture_get_width (cogl_texture);
  tex_height = cogl_texture_get_height (cogl_texture);

  t_w = (float) w / tex_width;
  t_h = (float) h / tex_height;

  /* Parent paint translated us into position */
  cogl_set_source_texture (cogl_texture);
  cogl_rectangle_with_texture_coords (0, 0,
                                      w, h,
                                      0, 0,
                                      t_w, t_h);

  g_signal_stop_emission_by_name (actor, "paint");
}

struct parallax_data
{
  gint direction;
  MoblinNetbookPlugin *plugin;
};

/*
 * Minimize effect completion callback; this function restores actor state, and
 * calls the manager callback function.
 */
static void
on_minimize_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect; must hide it first to ensure
   * that the restoration will not be visible.
   */
  MutterPlugin *plugin = data->plugin;
  ActorPrivate *apriv;
  MutterWindow *mcw = MUTTER_WINDOW (data->actor);

  apriv = get_actor_private (mcw);
  apriv->tml_minimize = NULL;

  clutter_actor_hide (data->actor);

  clutter_actor_set_scale (data->actor, 1.0, 1.0);
  clutter_actor_move_anchor_point_from_gravity (data->actor,
                                                CLUTTER_GRAVITY_NORTH_WEST);

  /* Now notify the manager that we are done with this effect */
  mutter_plugin_effect_completed (plugin, mcw,
                                      MUTTER_PLUGIN_MINIMIZE);
}

/*
 * Simple minimize handler: it applies a scale effect (which must be reversed on
 * completion).
 */
static void
minimize (MutterPlugin * plugin, MutterWindow *mcw)

{
  MetaCompWindowType type;
  ClutterActor      *actor  = CLUTTER_ACTOR (mcw);

  type = mutter_window_get_window_type (mcw);

  if (type == META_COMP_WINDOW_NORMAL)
    {
      ActorPrivate *apriv = get_actor_private (mcw);
      ClutterAnimation *animation;
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);

      apriv->is_minimized = TRUE;

      clutter_actor_move_anchor_point_from_gravity (actor,
                                                    CLUTTER_GRAVITY_CENTER);

      animation = clutter_actor_animate (actor,
                                         CLUTTER_EASE_IN_SINE,
                                         MINIMIZE_TIMEOUT,
                                         "scale-x", 0.0,
                                         "scale-y", 0.0,
                                         NULL);

      data->actor = actor;
      data->plugin = plugin;

      g_signal_connect (clutter_animation_get_timeline (animation),
                        "completed",
                        G_CALLBACK (on_minimize_effect_complete),
                        data);
    }
  else
    mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MINIMIZE);
}

/*
 * Minimize effect completion callback; this function restores actor state, and
 * calls the manager callback function.
 */
static void
on_maximize_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect.
   */
  MutterPlugin *plugin = data->plugin;
  MutterWindow *mcw    = MUTTER_WINDOW (data->actor);
  ActorPrivate *apriv  = get_actor_private (mcw);

  apriv->tml_maximize = NULL;

  clutter_actor_set_scale (data->actor, 1.0, 1.0);
  clutter_actor_move_anchor_point_from_gravity (data->actor,
                                                CLUTTER_GRAVITY_NORTH_WEST);

  /* Now notify the manager that we are done with this effect */
  mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAXIMIZE);

  g_free (data);
}

/*
 * The Nature of Maximize operation is such that it is difficult to do a visual
 * effect that would work well. Scaling, the obvious effect, does not work that
 * well, because at the end of the effect we end up with window content bigger
 * and differently laid out than in the real window; this is a proof concept.
 *
 * (Something like a sound would be more appropriate.)
 */
static void
maximize (MutterPlugin *plugin, MutterWindow *mcw,
          gint end_x, gint end_y, gint end_width, gint end_height)
{
  ClutterActor               *actor = CLUTTER_ACTOR (mcw);
  MetaCompWindowType          type;

  gdouble  scale_x  = 1.0;
  gdouble  scale_y  = 1.0;
  gint     anchor_x = 0;
  gint     anchor_y = 0;

  type = mutter_window_get_window_type (mcw);

  if (type == META_COMP_WINDOW_NORMAL)
    {
      ActorPrivate *apriv = get_actor_private (mcw);
      ClutterAnimation *animation;
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      guint width, height;
      gint  x, y;

      apriv->is_maximized = TRUE;

      clutter_actor_get_size (actor, &width, &height);
      clutter_actor_get_position (actor, &x, &y);

      /*
       * Work out the scale and anchor point so that the window is expanding
       * smoothly into the target size.
       */
      scale_x = (gdouble)end_width / (gdouble) width;
      scale_y = (gdouble)end_height / (gdouble) height;

      anchor_x = (gdouble)(x - end_x)*(gdouble)width /
        ((gdouble)(end_width - width));
      anchor_y = (gdouble)(y - end_y)*(gdouble)height /
        ((gdouble)(end_height - height));

      clutter_actor_move_anchor_point (actor, anchor_x, anchor_y);

      animation = clutter_actor_animate (actor,
                                         CLUTTER_EASE_IN_SINE,
                                         MAXIMIZE_TIMEOUT,
                                         "scale-x", scale_x,
                                         "scale-y", scale_y,
                                         NULL);

      data->actor = actor;
      data->plugin = plugin;

      g_signal_connect (clutter_animation_get_timeline (animation),
                        "completed",
                        G_CALLBACK (on_maximize_effect_complete),
                        data);
      return;
    }

  mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAXIMIZE);
}

/*
 * See comments on the maximize() function.
 *
 * (Just a skeleton code.)
 */
static void
unmaximize (MutterPlugin *plugin, MutterWindow *mcw,
            gint end_x, gint end_y, gint end_width, gint end_height)
{
  MetaCompWindowType  type;

  type = mutter_window_get_window_type (mcw);

  if (type == META_COMP_WINDOW_NORMAL)
    {
      ActorPrivate *apriv = get_actor_private (mcw);

      apriv->is_maximized = FALSE;
    }

  /* Do this conditionally, if the effect requires completion callback. */
  mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_UNMAXIMIZE);
}

static void
on_map_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect.
   */
  MutterPlugin *plugin = data->plugin;
  MutterWindow *mcw    = MUTTER_WINDOW (data->actor);
  ActorPrivate *apriv  = get_actor_private (mcw);

  apriv->tml_map = NULL;

  clutter_actor_move_anchor_point_from_gravity (data->actor,
                                                CLUTTER_GRAVITY_NORTH_WEST);

  g_free (data);

  /* Now notify the manager that we are done with this effect */
  mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAP);
}

static void
check_for_empty_workspace (MutterPlugin *plugin,
                           gint workspace, MetaWindow *ignore)
{
  MetaScreen *screen = mutter_plugin_get_screen (plugin);
  gboolean    workspace_empty = TRUE;
  GList      *l;

  l = mutter_get_windows (screen);
  while (l)
    {
      MutterWindow *m = l->data;
      MetaWindow   *mw = mutter_window_get_meta_window (m);

      if (mw != ignore)
        {
          gint w = mutter_window_get_workspace (m);

          if (w == workspace)
            {
              workspace_empty = FALSE;
              break;
            }
        }

      l = l->next;
    }

  if (workspace_empty)
    {
      MetaWorkspace *mws;
      guint32        timestamp;

      timestamp = clutter_x11_get_current_event_time ();

      mws = meta_screen_get_workspace_by_index (screen, workspace);

      meta_screen_remove_workspace (screen, mws, timestamp);
    }
}

/*
 * Protype; don't want to add this the public includes in metacity,
 * should be able to get rid of this call eventually.
 */
void meta_window_calc_showing (MetaWindow  *window);

static void
meta_window_workspace_changed_cb (MetaWindow *mw,
                                  gint        old_workspace,
                                  gpointer    data)
{
  MutterPlugin *plugin = MUTTER_PLUGIN (data);

  /*
   * Flush any pending changes to the visibility of the window.
   * (bug 1008 suggests that the removal of an empty workspace is sometimes
   * causing a race condition on calculating the window visibility, along the
   * lines of the window changing status
   *
   *  visible -> hidden -> visible
   *
   * As this is queued up, by the time the status is calculated this might
   * appear as the visibility has not changed, but in fact somewhere along the
   * line it the window has already been pushed down the stack.
   *
   * Needs further investigation; this is an attempt to work around the problem
   * by flushing the state in the intermediate stage for the alpha2 release.
   */
  meta_window_calc_showing (mw);

  check_for_empty_workspace (plugin, old_workspace, mw);
}

static void
meta_window_fullcreen_notify_cb (GObject    *object,
                                 GParamSpec *spec,
                                 gpointer    data)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (data)->priv;
  gboolean                    fullscreen;

  g_object_get (object, "fullscreen", &fullscreen, NULL);

  if (fullscreen)
    {
      priv->fullscreen_apps++;
    }
  else
    {
      priv->fullscreen_apps--;

      if (priv->fullscreen_apps < 0)
        {
          g_warning ("Error in fullscreen accounting, fixing up.");
          priv->fullscreen_apps = 0;
        }
    }
}

/*
 * Simple map handler: it applies a scale effect which must be reversed on
 * completion).
 */
static void
map (MutterPlugin *plugin, MutterWindow *mcw)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  ClutterActor               *actor = CLUTTER_ACTOR (mcw);
  MetaCompWindowType          type;

  type = mutter_window_get_window_type (mcw);

  if (type == META_COMP_WINDOW_DESKTOP && priv->parallax_tex != NULL )
    {
      gint screen_width, screen_height;

      /*
       * FIXME -- the way it currently works means we still have a fullscreen
       * GLX texture in place which serves no purpose. We should make this work
       * without needing the desktop window. The parallax texture could simply
       * be placed directly on stage, underneath the Mutter windows group.
       */
      mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

      clutter_actor_set_size (priv->parallax_tex,
                              screen_width * 8,
                              screen_height);

      clutter_actor_set_parent (priv->parallax_tex, actor);

      g_signal_connect (actor,
                        "paint", G_CALLBACK (on_desktop_pre_paint),
                        plugin);

      mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAP);
      return;
    }

  /*
   * The OR test must come first, since GTK_WINDOW_POPUP type windows are
   * both override redirect, but also have a _NET_WM_WINDOW_TYPE set to NORMAL
   */
  if (mutter_window_is_override_redirect (mcw))
    {
      Window      xwin = mutter_window_get_x_window (mcw);
      MnbToolbar *toolbar = MNB_TOOLBAR (priv->toolbar);

      if (mnb_toolbar_is_tray_config_window (toolbar, xwin))
        mnb_toolbar_append_tray_window (toolbar, mcw);
      else
        mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAP);

    }


  /*
   * Anything that might be associated with startup notification needs to be
   * handled here; if this list grows, we should just split it further.
   */
  else if (type == META_COMP_WINDOW_NORMAL ||
           type == META_COMP_WINDOW_SPLASHSCREEN ||
           type == META_COMP_WINDOW_DIALOG)
    {
      ClutterAnimation   *animation;
      EffectCompleteData *data  = g_new0 (EffectCompleteData, 1);
      ActorPrivate       *apriv = get_actor_private (mcw);
      MetaWindow         *mw    = mutter_window_get_meta_window (mcw);

      if (mw)
        {
          gboolean    fullscreen;
          const char *sn_id = meta_window_get_startup_id (mw);

          if (!moblin_netbook_sn_should_map (plugin, mcw, sn_id))
            return;

          g_object_get (mw, "fullscreen", &fullscreen, NULL);

          if (fullscreen)
            priv->fullscreen_apps++;

          g_signal_connect (mw, "notify::fullscreen",
                            G_CALLBACK (meta_window_fullcreen_notify_cb),
                            plugin);
        }

      /*
       * Anything that we do not animated exits at this point.
       */
      if (type == META_COMP_WINDOW_DIALOG)
        {
          mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAP);
          return;
        }

      clutter_actor_move_anchor_point_from_gravity (actor,
                                                    CLUTTER_GRAVITY_CENTER);

      clutter_actor_set_scale (actor, 0.0, 0.0);
      clutter_actor_show (actor);

      animation = clutter_actor_animate (actor, CLUTTER_EASE_OUT_ELASTIC,
                                         MAP_TIMEOUT,
                                         "scale-x", 1.0,
                                         "scale-y", 1.0,
                                         NULL);
      data->plugin = plugin;
      data->actor = actor;
      apriv->tml_map = clutter_animation_get_timeline (animation);

      g_signal_connect (apriv->tml_map,
                        "completed",
                        G_CALLBACK (on_map_effect_complete),
                        data);

      apriv->is_minimized = FALSE;

      g_signal_connect (mw, "workspace-changed",
                        G_CALLBACK (meta_window_workspace_changed_cb),
                        plugin);

      if (type == META_COMP_WINDOW_NORMAL)
        {
          g_signal_connect (mw, "focus",
                            G_CALLBACK (mnb_switcher_meta_window_focus_cb),
                            mnb_toolbar_get_switcher (MNB_TOOLBAR (
                                                              priv->toolbar)));

          g_object_weak_ref (G_OBJECT (mw),
                             mnb_switcher_meta_window_weak_ref_cb,
                             mnb_toolbar_get_switcher (MNB_TOOLBAR (
                                                              priv->toolbar)));
        }
    }
  else
    mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_MAP);

}

static void
destroy (MutterPlugin *plugin, MutterWindow *mcw)
{
  MetaCompWindowType          type;
  gint                        workspace;
  MetaWindow                 *meta_win;

  type      = mutter_window_get_window_type (mcw);
  workspace = mutter_window_get_workspace (mcw);
  meta_win  = mutter_window_get_meta_window (mcw);

  if (type == META_COMP_WINDOW_NORMAL)
    {
      MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
      gboolean                    fullscreen;

      g_object_get (meta_win, "fullscreen", &fullscreen, NULL);

      if (fullscreen)
        {
          priv->fullscreen_apps--;

          if (priv->fullscreen_apps < 0)
            {
              g_warning ("Error in fullscreen accounting, fixing up.");
              priv->fullscreen_apps = 0;
            }
        }

      /*
       * Disconnect the fullscreen notification handler; strictly speaking
       * this should not be necessary, as the MetaWindow should be going away,
       * but take no chances.
       */
      g_signal_handlers_disconnect_by_func (meta_win,
                                            meta_window_fullcreen_notify_cb,
                                            plugin);

    }

  mutter_plugin_effect_completed (plugin, mcw, MUTTER_PLUGIN_DESTROY);

  /*
   * Do not destroy workspace if the closing window is a splash screen.
   * (Sometimes the splash gets destroyed before the application window
   * maps, e.g., Gimp.)
   */
  if (type != META_COMP_WINDOW_SPLASHSCREEN)
    check_for_empty_workspace (plugin, workspace, meta_win);
}

static void
last_focus_weak_notify_cb (gpointer data, GObject *meta_win)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (data)->priv;

  if ((MetaWindow*)meta_win == priv->last_focused)
    {
      priv->last_focused = NULL;

      /* FIXME */
      g_warning ("just lost the last focused window during grab!\n");
    }
}

void
moblin_netbook_unfocus_stage (MutterPlugin *plugin, guint32 timestamp)
{
  MoblinNetbookPluginPrivate *priv    = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MetaScreen                 *screen  = mutter_plugin_get_screen (plugin);
  MetaDisplay                *display = meta_screen_get_display (screen);
  MetaWindow                 *focus;

  if (timestamp == CurrentTime)
    timestamp = clutter_x11_get_current_event_time ();

  /*
   * Work out what we should focus next.
   *
   * First, we tray to get the window from metacity tablist, if that fails
   * fall back on the cached last_focused window.
   */
  focus = meta_display_get_tab_current (display,
                                        META_TAB_LIST_NORMAL,
                                        screen,
                                        NULL);


  if (!focus)
    focus = priv->last_focused;

  if (priv->last_focused)
    {
      g_object_weak_unref (G_OBJECT (priv->last_focused),
                           last_focus_weak_notify_cb, plugin);

      priv->last_focused = NULL;
    }

  if (focus)
    meta_display_set_input_focus_window (display, focus, FALSE, timestamp);
}

void
moblin_netbook_focus_stage (MutterPlugin *plugin, guint32 timestamp)
{
  MoblinNetbookPluginPrivate *priv    = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MetaScreen                 *screen  = mutter_plugin_get_screen (plugin);
  MetaDisplay                *display = meta_screen_get_display (screen);
  Display                    *xdpy    = mutter_plugin_get_xdisplay (plugin);

  if (timestamp == CurrentTime)
    timestamp = clutter_x11_get_current_event_time ();


  /*
   * Map the input blocker window so keystrokes, etc., are not reaching apps.
   */
  if (priv->last_focused)
    g_object_weak_unref (G_OBJECT (priv->last_focused),
                         last_focus_weak_notify_cb, plugin);

  priv->last_focused = meta_display_get_focus_window (display);

  if (priv->last_focused)
    g_object_weak_ref (G_OBJECT (priv->last_focused),
                       last_focus_weak_notify_cb, plugin);

  XSetInputFocus (xdpy,
                  priv->focus_xwin,
                  RevertToPointerRoot,
                  timestamp);
}

static gboolean
xevent_filter (MutterPlugin *plugin, XEvent *xev)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MnbToolbar                 *toolbar = MNB_TOOLBAR (priv->toolbar);
  NbtkWidget                 *switcher;

  switcher = mnb_toolbar_get_switcher (toolbar);

  if (switcher && mnb_switcher_handle_xevent (MNB_SWITCHER (switcher), xev))
    return TRUE;

  if (xev->type == KeyPress &&
      XKeycodeToKeysym (xev->xkey.display, xev->xkey.keycode, 0) ==
                                                    MOBLIN_PANEL_SHORTCUT_KEY)
    {
      if (!CLUTTER_ACTOR_IS_VISIBLE (priv->toolbar))
        {
          /*
           * Set the dont_autohide flag on the toolbar; this stops the panel
           * hiding due to mouse pointer movement until the pointer re-enters
           * the panel (i.e., if the toolbar opens, but the pointer is outside
           * of it, we do not want the toolbar to hide as soon as the user
           * moves the pointer).
           */
          mnb_toolbar_set_dont_autohide (MNB_TOOLBAR (priv->toolbar), TRUE);
          clutter_actor_show (priv->toolbar);
        }
      else
        {
          clutter_actor_hide (priv->toolbar);
        }

      return TRUE;
    }

  sn_display_process_event (priv->sn_display, xev);

  if (xev->type == KeyPress || xev->type == KeyRelease)
    {
      MetaScreen   *screen  = mutter_plugin_get_screen (plugin);
      ClutterActor *stage   = mutter_get_stage_for_screen (screen);
      Window        xwin;

      /*
       * We only get key events on the no-focus window, but for clutter we
       * need to pretend they come from the stage window.
       */
      xwin = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));

      xev->xany.window = xwin;
    }

  return (clutter_x11_handle_event (xev) != CLUTTER_X11_FILTER_CONTINUE);
}

static void
kill_effect (MutterPlugin *plugin, MutterWindow *mcw, gulong event)
{
  ActorPrivate *apriv;

  if (event & MUTTER_PLUGIN_SWITCH_WORKSPACE)
    {
      /*
       * We never kill the zone switching effect; since the effect does not
       * use the MutterWindows directly, it does not screw up the layout.
       */
      return;
    }

  apriv = get_actor_private (mcw);

  if ((event & MUTTER_PLUGIN_MINIMIZE) && apriv->tml_minimize)
    {
      clutter_timeline_stop (apriv->tml_minimize);
      g_signal_emit_by_name (apriv->tml_minimize, "completed", NULL);
    }

  if ((event & MUTTER_PLUGIN_MAXIMIZE) && apriv->tml_maximize)
    {
      clutter_timeline_stop (apriv->tml_maximize);
      g_signal_emit_by_name (apriv->tml_maximize, "completed", NULL);
    }

  if ((event & MUTTER_PLUGIN_MAP) && apriv->tml_map)
    {
      clutter_timeline_stop (apriv->tml_map);

      /*
       * Force emission of the "completed" signal so that the necessary
       * cleanup is done (we cannot readily supply the data necessary to
       * call our callback directly).
       */
      g_signal_emit_by_name (apriv->tml_map, "completed");
    }
}

static void
toolbar_trigger_region_set_height (MutterPlugin *plugin, gint height)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  gint screen_width, screen_height;

  mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

  if (priv->toolbar_trigger_region != NULL)
    moblin_netbook_input_region_remove (plugin, priv->toolbar_trigger_region);

  priv->toolbar_trigger_region
    = moblin_netbook_input_region_push (plugin,
                                        0,
                                        0,
                                        screen_width,
                                        PANEL_SLIDE_THRESHOLD + height);
}

static gboolean
panel_slide_timeout_cb (gpointer data)
{
  MutterPlugin  *plugin = data;
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;

  /*
   * Reset the trigger region to its normal side; this is needed whether we
   * end up showing the panel, or not.
   */
  toolbar_trigger_region_set_height (MUTTER_PLUGIN (plugin), 0);

  if (priv->last_y < PANEL_SLIDE_THRESHOLD)
    {
      clutter_actor_show (priv->toolbar);
    }

  priv->panel_slide_timeout_id = 0;

  return FALSE;
}

static gboolean
stage_capture_cb (ClutterActor *stage, ClutterEvent *event, gpointer data)
{
  MoblinNetbookPlugin *plugin = data;

  if (event->type == CLUTTER_MOTION)
    {
      gint                        event_y, event_x;
      MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;

      event_x = ((ClutterMotionEvent*)event)->x;
      event_y = ((ClutterMotionEvent*)event)->y;

      priv->last_y = event_y;

#if 0
      if (priv->panel_out_in_progress || priv->panel_back_in_progress)
        return FALSE;
#endif

      if (priv->panel_disabled)
        return FALSE;

      if (CLUTTER_ACTOR_IS_VISIBLE (priv->toolbar))
        {
          /*
           * FIXME -- we should use the height of the panel background here;
           *          when we refactor the panel code, we should expose that
           *          value as a property on the object.
           */
          guint height = 64; // clutter_actor_get_height (priv->panel_shadow);

          if (event_y > (gint)height)
            {
              /*
               * FIXME -- we have to override the Toolbar show() and hide()
               * methods to deal with when panels are visible.
               */
              clutter_actor_hide (priv->toolbar);
            }
        }
      else if (event_y < PANEL_SLIDE_THRESHOLD)
        {
          /*
           * Panel does not get shown in response to mouse events if a
           * fullscreen app is present.
           *
           * NB: Panel can still be shown using the kbd shortcut; however,
           *     as the Panel steels kbd focus, this results in the fullscreen
           *     application being un-fullscreened and also possibly ending up
           *     lower down in the window stack.
           */
          if (priv->fullscreen_apps)
            return FALSE;

          if (!priv->panel_slide_timeout_id &&
              !CLUTTER_ACTOR_IS_VISIBLE (priv->toolbar))
            {
              /* Increase sensitivity */
              toolbar_trigger_region_set_height (MUTTER_PLUGIN (plugin), 5);

              priv->panel_slide_timeout_id =
                g_timeout_add (PANEL_SLIDE_THRESHOLD_TIMEOUT,
                               panel_slide_timeout_cb, plugin);
            }
        }
    }

  return FALSE;
}

/*
 * Handles input events on stage.
 *
 */
static gboolean
stage_input_cb (ClutterActor *stage, ClutterEvent *event, gpointer data)
{
  MutterPlugin *plugin = data;
#if 0
  if (event->type == CLUTTER_BUTTON_PRESS)
    {
      gint           event_y, event_x;
      MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;

      event_x = ((ClutterButtonEvent*)event)->x;
      event_y = ((ClutterButtonEvent*)event)->y;

      priv->last_y = event_y;

      if (priv->panel_out_in_progress || priv->panel_back_in_progress)
        return FALSE;

      if (CLUTTER_ACTOR_IS_VISIBLE (priv->switcher))
        clutter_actor_hide (priv->switcher);

      if (priv->launcher)
        clutter_actor_hide (priv->launcher);

      if (priv->mzone_grid)
        clutter_actor_hide (priv->mzone_grid);

      shell_tray_manager_close_all_config_windows (priv->tray_manager);

      if (CLUTTER_ACTOR_IS_VISIBLE (priv->panel))
        {
          guint height = 64; // clutter_actor_get_height (priv->panel_shadow);

          if (event_y > (gint)height)
            {
              /* MA */ // hide_panel (plugin);
            }
        }
    }
#endif
  return FALSE;
}

static void
setup_focus_window (MutterPlugin *plugin)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  Window                      xwin;
  XSetWindowAttributes        attr;
  Display                    *xdpy    = mutter_plugin_get_xdisplay (plugin);
  MetaScreen                 *screen  = mutter_plugin_get_screen (plugin);
  MetaDisplay                *display = meta_screen_get_display (screen);
  Atom                        type_atom;

  type_atom = meta_display_get_atom (display,
                                     META_ATOM__NET_WM_WINDOW_TYPE_DOCK);

  attr.event_mask        = KeyPressMask | KeyReleaseMask;
  attr.override_redirect = True;

  xwin = XCreateWindow (xdpy,
                        RootWindow (xdpy,
                                    meta_screen_get_screen_number (screen)),
                        -100, -100, 1, 1, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWEventMask | CWOverrideRedirect, &attr);

  XChangeProperty (xdpy, xwin,
                   meta_display_get_atom (display,
                                          META_ATOM__NET_WM_WINDOW_TYPE),
                   XA_ATOM, 32, PropModeReplace,
                   (unsigned char *) &type_atom,
                   1);

  XMapWindow (xdpy, xwin);

  priv->focus_xwin = xwin;
}

static void
setup_parallax_effect (MutterPlugin *plugin)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MetaScreen                 *screen = mutter_plugin_get_screen (plugin);
  MetaDisplay                *display = meta_screen_get_display (screen);
  Display                    *xdpy = mutter_plugin_get_xdisplay (plugin);
  gboolean                    have_desktop = FALSE;
  gint                        screen_width, screen_height;
  Window                     *children, *l;
  guint                       n_children;
  Window                      root_win;
  Window                      parent_win;
  Status                      status;
  Atom                        desktop_atom;

  mutter_plugin_query_screen_size (MUTTER_PLUGIN (plugin),
                                   &screen_width, &screen_height);

  /*
   * The do_init() method is called before the Manager starts managing
   * pre-existing windows, so we cannot query the windows via the normal
   * Mutter API, but have to use X lib to traverse the root window list.
   *
   * (We could probably add manage_all_windows() virtual to the plugin API
   * to split the initialization into two stages, but it is probably not worth
   * the hassle.)
   */
  root_win = RootWindow (xdpy, meta_screen_get_screen_number (screen));

  status = XQueryTree (xdpy, root_win, &root_win, &parent_win, &children,
                       &n_children);

  desktop_atom = meta_display_get_atom (display,
                                        META_ATOM__NET_WM_WINDOW_TYPE_DESKTOP);

  if (status)
    {
      guint i;
      Atom  type_atom;

      type_atom = meta_display_get_atom (display,
                                         META_ATOM__NET_WM_WINDOW_TYPE);

      for (l = children, i = 0; i < n_children; ++l, ++i)
        {
          unsigned long  n_items, ret_bytes;
          Atom           ret_type;
          int            ret_format;
          Atom          *type;

          XGetWindowProperty (xdpy, *l, type_atom, 0, 8192, False,
                              XA_ATOM, &ret_type, &ret_format,
                              &n_items, &ret_bytes, (unsigned char**)&type);

          if (type)
            {
              if (*type == desktop_atom)
                have_desktop = TRUE;

              XFree (type);
            }

          if (have_desktop)
            break;
        }

      XFree (children);
    }

  if (!have_desktop)
    {
      /*
       * Create a dummy desktop window.
       */
      Window               dwin;
      XSetWindowAttributes attr;

      attr.event_mask = ExposureMask;

      dwin = XCreateWindow (xdpy,
                            RootWindow (xdpy,
                                        meta_screen_get_screen_number (screen)),
                            0, 0, 1, 1, 0,
                            CopyFromParent, InputOnly, CopyFromParent,
                            CWEventMask, &attr);

      XChangeProperty (xdpy, dwin,
                       meta_display_get_atom (display,
                                              META_ATOM__NET_WM_WINDOW_TYPE),
                       XA_ATOM, 32, PropModeReplace,
                       (unsigned char *) &desktop_atom,
                       1);

      XMapWindow (xdpy, dwin);
    }

  /* FIXME: pull image from theme, css ? */
  priv->parallax_tex = clutter_texture_new_from_file
                        (PLUGIN_PKGDATADIR "/theme/panel/background-tile.png",
                         NULL);

  if (priv->parallax_tex == NULL)
    {
      g_warning ("Failed to load '"
                 PLUGIN_PKGDATADIR
                 "/theme/panel/background-tile.png', No tiled desktop image");
    }
  else
    {
      ClutterActor *bg_clone;
      ClutterActor *stage = mutter_get_stage_for_screen (screen);

      g_object_set (priv->parallax_tex,
                    "repeat-x", TRUE,
                    "repeat-y", TRUE,
                    NULL);

      bg_clone = clutter_clone_new (priv->parallax_tex);
      clutter_actor_set_size (bg_clone, screen_width, screen_height);
      clutter_container_add_actor (CLUTTER_CONTAINER (stage), bg_clone);
      clutter_actor_lower_bottom (bg_clone);
    }

}

/*
 * Core of the plugin init function, called for initial initialization and
 * by the reload() function. Returns TRUE on success.
 */
static const MutterPluginInfo *
plugin_info (MutterPlugin *plugin)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;

  return &priv->info;
}



void
moblin_netbook_stash_window_focus (MutterPlugin *plugin, guint32 timestamp)
{
  MoblinNetbookPluginPrivate *priv    = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MetaScreen                 *screen  = mutter_plugin_get_screen (plugin);
  MetaDisplay                *display = meta_screen_get_display (screen);
  Display                    *xdpy    = mutter_plugin_get_xdisplay (plugin);

  if (timestamp == CurrentTime)
    timestamp = clutter_x11_get_current_event_time ();

  if (priv->last_focused)
    g_object_weak_unref (G_OBJECT (priv->last_focused),
                         last_focus_weak_notify_cb, plugin);

  priv->last_focused = meta_display_get_focus_window (display);

  if (priv->last_focused)
    g_object_weak_ref (G_OBJECT (priv->last_focused),
                       last_focus_weak_notify_cb, plugin);

  XSetInputFocus (xdpy,
                  priv->focus_xwin,
                  RevertToPointerRoot,
                  timestamp);
}

void
moblin_netbook_unstash_window_focus (MutterPlugin *plugin, guint32 timestamp)
{
  MoblinNetbookPluginPrivate *priv    = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MetaScreen                 *screen  = mutter_plugin_get_screen (plugin);
  MetaDisplay                *display = meta_screen_get_display (screen);
  MetaWindow                 *focus;


  if (timestamp == CurrentTime)
    timestamp = clutter_x11_get_current_event_time ();

  /*
   * Work out what we should focus next.
   *
   * First, we tray to get the window from metacity tablist, if that fails
   * fall back on the cached last_focused window.
   */
  focus = meta_display_get_tab_current (display,
                                        META_TAB_LIST_NORMAL,
                                        screen,
                                        NULL);

  if (!focus)
    focus = priv->last_focused;

  if (priv->last_focused)
    {
      g_object_weak_unref (G_OBJECT (priv->last_focused),
                           last_focus_weak_notify_cb, plugin);

      priv->last_focused = NULL;
    }

  if (focus)
    meta_display_set_input_focus_window (display, focus, FALSE, timestamp);
}

struct MnbInputRegion
{
  XserverRegion region;
};

MnbInputRegion
moblin_netbook_input_region_push (MutterPlugin *plugin,
                                  gint          x,
                                  gint          y,
                                  guint         width,
                                  guint         height)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MnbInputRegion mir = g_slice_alloc (sizeof (struct MnbInputRegion));
  XRectangle     rect;
  Display       *xdpy = mutter_plugin_get_xdisplay (plugin);

  rect.x       = x;
  rect.y       = y;
  rect.width   = width;
  rect.height  = height;

  mir->region  = XFixesCreateRegion (xdpy, &rect, 1);

  priv->input_region_stack = g_list_append (priv->input_region_stack, mir);

  moblin_netbook_input_region_apply (plugin);

  return mir;
}

void
moblin_netbook_input_region_remove (MutterPlugin *plugin, MnbInputRegion mir)
{
  moblin_netbook_input_region_remove_without_update (plugin, mir);
  moblin_netbook_input_region_apply (plugin);
}

void
moblin_netbook_input_region_remove_without_update (MutterPlugin  *plugin,
                                                   MnbInputRegion mir)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  Display *xdpy = mutter_plugin_get_xdisplay (plugin);

  if (mir->region)
    XFixesDestroyRegion (xdpy, mir->region);

  priv->input_region_stack = g_list_remove (priv->input_region_stack, mir);

  g_slice_free (struct MnbInputRegion, mir);
}

static void
moblin_netbook_input_region_apply (MutterPlugin *plugin)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  Display *xdpy = mutter_plugin_get_xdisplay (plugin);
  GList *l = priv->input_region_stack;
  XserverRegion result;

  if (priv->current_input_region)
    XFixesDestroyRegion (xdpy, priv->current_input_region);

  result = priv->current_input_region = XFixesCreateRegion (xdpy, NULL, 0);

  while (l)
    {
      MnbInputRegion mir = l->data;

      if (mir->region)
        XFixesUnionRegion (xdpy, result, result, mir->region);

      l = l->next;
    }

  mutter_plugin_set_stage_input_region (plugin, result);
}

static gboolean
on_lowlight_button_event (ClutterActor *actor,
                          ClutterEvent *event,
                          gpointer      user_data)
{
  return TRUE;                  /* Simply block events being handled */
}

void
moblin_netbook_set_lowlight (MutterPlugin *plugin, gboolean on)
{
  MoblinNetbookPluginPrivate *priv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  static MnbInputRegion input_region;
  static gboolean active = FALSE;

  if (on && !active)
    {
      gint screen_width, screen_height;

      mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

      input_region
        = moblin_netbook_input_region_push (plugin,
                                            0, 0, screen_width, screen_height);

      clutter_actor_show (priv->lowlight);
      priv->panel_disabled = active = TRUE;
    }
  else
    {
      if (active)
        {
          clutter_actor_hide (priv->lowlight);
          moblin_netbook_input_region_remove (plugin, input_region);
          priv->panel_disabled = active = FALSE;
        }
    }
}

