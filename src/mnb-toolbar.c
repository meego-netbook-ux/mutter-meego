/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* mnb-toolbar.c */
/*
 * Copyright (c) 2009, 2010 Intel Corp.
 *
 * Authors: Matthew Allum <matthew.allum@intel.com>
 *          Tomas Frydrych <tf@linux.intel.com>
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

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>
#include <gconf/gconf-client.h>
#include <moblin-panel/mpl-panel-common.h>
#include <display.h>
#include <keybindings.h>
#include <errors.h>

/*
 * Including mutter's errors.h defines the i18n macros, so undefine them before
 * including the glib i18n header.
 */
#undef _
#undef N_
#include <glib/gi18n.h>


#include "moblin-netbook.h"

#include "mnb-toolbar.h"
#include "mnb-panel-oop.h"
#include "mnb-toolbar-button.h"
#include "mnb-toolbar-applet.h"
#include "mnb-toolbar-icon.h"
#include "mnb-toolbar-clock.h"
#include "mnb-spinner.h"

/* For systray windows stuff */
#include <gdk/gdkx.h>

#include <clutter/x11/clutter-x11.h>

#define SLIDE_DURATION 150

#define KEY_DIR "/desktop/moblin/toolbar/panels"
#define KEY_ORDER KEY_DIR "/order"

#define BUTTON_SPACING 20
#define BUTTON_SPACING_INITIAL 13

/*
 * According to the latest MeeGo 1.0 designs, on the bigger screen the Toolbar
 * does not have any shadow on its sides, nor the rounded corners. This is done
 * by making the Toolbar wider than the screen by 2 * BIG_SCREEN_PAD and moving
 * it to the left by BIG_SCREEN_PAD. The BIG_SCREEN_BUTTON_SHIFT is the amount
 * by which the panel buttons have to be moved left so that the myzone button
 * fits into the middle of the space which is embossed into the Toolbar asset.
 */
#define BIG_SCREEN_PAD 10
#define BIG_SCREEN_BUTTON_SHIFT 4

#define MNB_TOOLBAR_MAX_APPLETS 4
#define TRAY_WIDTH (3 * TRAY_BUTTON_WIDTH + CLOCK_WIDTH + 4 * TRAY_PADDING + 4)
#define TRAY_PADDING   4

#define TOOLBAR_TRIGGER_THRESHOLD       1
#define TOOLBAR_TRIGGER_ADJUSTMENT      2
#define TOOLBAR_TRIGGER_THRESHOLD_TIMEOUT 500
#define TOOLBAR_LOWLIGHT_FADE_DURATION 300
#define TOOLBAR_AUTOSTART_DELAY 15
#define TOOLBAR_AUTOSTART_ATTEMPTS 10
#define TOOLBAR_WAITING_FOR_PANEL_TIMEOUT 1 /* in seconds */
#define TOOLBAR_PANEL_STUB_TIMEOUT 6        /* in seconds */
#define MOBLIN_BOOT_COUNT_KEY "/desktop/moblin/myzone/boot_count"

#define CLOSE_BUTTON_GUARD_WIDTH 35

#define MAX_PANELS(screen_width)                \
  ((screen_width - TRAY_WIDTH - TOOLBAR_X_PADDING - BUTTON_SPACING_INITIAL) / \
   (BUTTON_WIDTH + BUTTON_SPACING))

#if 0
/*
 * TODO
 * This is currently define in moblin-netbook.h, as it is needed by the
 * tray manager and MnbDropDown -- this should not be hardcoded, and we need
 * a way for the drop down to query it from the panel.
 */
#define TOOLBAR_HEIGHT 64
#endif

/*
 * The toolbar shadow extends by the TOOLBAR_SHADOW_EXTRA below the toolbar.
 * In addition, the lowlight actor is extended by the TOOLBAR_SHADOW_HEIGHT so
 * that it does not roll above the edge of the screen during the toolbar hide
 * animation.
 */
#define TOOLBAR_SHADOW_EXTRA  37
#define TOOLBAR_SHADOW_HEIGHT (TOOLBAR_HEIGHT + TOOLBAR_SHADOW_EXTRA)

G_DEFINE_TYPE (MnbToolbar, mnb_toolbar, MX_TYPE_FRAME)

#define MNB_TOOLBAR_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MNB_TYPE_TOOLBAR, MnbToolbarPrivate))

typedef struct _MnbToolbarPanel MnbToolbarPanel;

static void mnb_toolbar_constructed (GObject *self);
static void mnb_toolbar_real_hide (ClutterActor *actor);
static void mnb_toolbar_real_show (ClutterActor *actor);
static gboolean mnb_toolbar_stage_captured_cb (ClutterActor *stage,
                                               ClutterEvent *event,
                                               gpointer      data);
static gboolean mnb_toolbar_stage_input_cb (ClutterActor *stage,
                                            ClutterEvent *event,
                                            gpointer      data);
static void mnb_toolbar_stage_show_cb (ClutterActor *stage,
                                       MnbToolbar *toolbar);
static void mnb_toolbar_handle_dbus_name (MnbToolbar *, const gchar *);
static MnbPanel * mnb_toolbar_panel_name_to_panel (MnbToolbar  *toolbar,
                                                   const gchar *name);
static MnbToolbarPanel * mnb_toolbar_panel_name_to_panel_internal (MnbToolbar  *toolbar,
                                                                   const gchar *name);
static MnbToolbarPanel * mnb_toolbar_panel_to_toolbar_panel (MnbToolbar *toolbar,
                                                             MnbPanel *panel);
static void mnb_toolbar_activate_panel_internal (MnbToolbar *toolbar,
                                                 MnbToolbarPanel *tp,
                                                 MnbShowHideReason reason);
static void mnb_toolbar_setup_gconf (MnbToolbar *toolbar);
static gboolean mnb_toolbar_start_panel_service (MnbToolbar *toolbar,
                                                 MnbToolbarPanel *tp);
static void mnb_toolbar_workarea_changed_cb (MetaScreen *screen,
                                             MnbToolbar *toolbar);

enum {
  PROP_0,

  PROP_MUTTER_PLUGIN,
};

enum
{
  SHOW_COMPLETED,
  HIDE_BEGIN,
  HIDE_COMPLETED,

  LAST_SIGNAL
};

static guint toolbar_signals[LAST_SIGNAL] = { 0 };

typedef enum
{
  MNB_TOOLBAR_PANEL_NORMAL = 0,
  MNB_TOOLBAR_PANEL_APPLET,
  MNB_TOOLBAR_PANEL_CLOCK
} MnbToolbarPanelType;

struct _MnbToolbarPanel
{
  gchar      *name;
  gchar      *service;
  gchar      *button_stylesheet;
  gchar      *button_style;
  gchar      *tooltip;

  ClutterActor *button;
  MnbPanel   *panel;

  MnbToolbarPanelType type;

  gboolean    windowless : 1; /* button-only panel */
  gboolean    unloaded   : 1;
  gboolean    current    : 1;
  gboolean    pinged     : 1;
  gboolean    required   : 1;
  gboolean    failed     : 1;
};

static void
mnb_toolbar_panel_destroy (MnbToolbarPanel *tp)
{
  g_free (tp->name);
  g_free (tp->service);
  g_free (tp->button_stylesheet);
  g_free (tp->button_style);
  g_free (tp->tooltip);

  if (tp->button)
    g_critical (G_STRLOC ": button leaked");

  if (tp->panel)
    g_critical (G_STRLOC ": panel leaked");
}

struct _MnbToolbarPrivate
{
  MutterPlugin *plugin;

  ClutterActor *hbox; /* This is where all the contents are placed */
  ClutterActor *lowlight;
  ClutterActor *panel_stub;
  ClutterActor *spinner;
  ClutterActor *shadow;

  /*
   * TODO -- The proper clock is a toolbar button associated with a panel,
   * like any other button, and can be removed/added via the Toolbar capplet.
   * As such, we rely on a desktop file, and expect an associated dbus service,
   * etc. So, while we are waiting for the Date & Time panel to be ready for
   * release, we use a dummy clock object in its place; once the panel is ready
   * we should remove this.
   */
  ClutterActor *dummy_clock;

  GList        *panels;         /* Panels (the dropdowns) */

  guint         max_panels;
  guint         n_applets;

  MnbToolbarPanel *stubbed_panel; /* The panel for which we are showing stub */

  gboolean button_click      : 1; /* Used to differentiate programmatic and
                                   * manual button toggling.
                                   */
  gboolean no_autoloading    : 1;
  gboolean shown             : 1;
  gboolean shown_myzone      : 1;
  gboolean disabled          : 1;
  gboolean in_show_animation : 1; /* Animation tracking */
  gboolean in_hide_animation : 1;
  gboolean waiting_for_panel_show : 1; /* Set between button click and panel
                                        * show */
  gboolean waiting_for_panel_hide : 1;

  gboolean dont_autohide     : 1; /* Whether the panel should hide when the
                                   * pointer goes south
                                   */
  gboolean panel_input_only  : 1; /* Set when the region below panels should not
                                   * be included in the panel input region.
                                   */
  gboolean struts_set        : 1;
  gboolean have_clock        : 1;

  MnbShowHideReason reason_for_show; /* Reason for pending Toolbar show */
  MnbShowHideReason reason_for_hide; /* Reason for pending Toolbar hide */

  MnbInputRegion *trigger_region;  /* The show panel trigger region */
  MnbInputRegion *input_region;    /* The panel input region on the region
                                   * stack.
                                   */

  guint trigger_timeout_id;

  DBusGConnection *dbus_conn;
  DBusGProxy      *dbus_proxy;

  GSList          *pending_panels;
  MnbToolbarPanel *tp_to_activate;

  gint             old_screen_width;
  gint             old_screen_height;

  guint            waiting_for_panel_show_cb_id;
  guint            waiting_for_panel_hide_cb_id;
  guint            panel_stub_timeout_id;
  guint            trigger_cb_id;
};

static void
mnb_toolbar_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MnbToolbar *self = MNB_TOOLBAR (object);

  switch (property_id)
    {
    case PROP_MUTTER_PLUGIN:
      g_value_set_object (value, self->priv->plugin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mnb_toolbar_set_property (GObject *object, guint property_id,
                          const GValue *value, GParamSpec *pspec)
{
  MnbToolbar *self = MNB_TOOLBAR (object);

  switch (property_id)
    {
    case PROP_MUTTER_PLUGIN:
      self->priv->plugin = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mnb_toolbar_dispose (GObject *object)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (object)->priv;

  if (priv->dbus_conn)
    {
      g_object_unref (priv->dbus_conn);
      priv->dbus_conn = NULL;
    }

  if (priv->input_region)
    {
      mnb_input_manager_remove_region (priv->input_region);
      priv->input_region = NULL;
    }

  if (priv->trigger_region)
    {
      mnb_input_manager_remove_region (priv->trigger_region);
      priv->trigger_region = NULL;
    }

  G_OBJECT_CLASS (mnb_toolbar_parent_class)->dispose (object);
}

static void
mnb_toolbar_finalize (GObject *object)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (object)->priv;
  GSList            *l;

  l = priv->pending_panels;
  while (l)
    {
      gchar *n = l->data;
      g_free (n);

      l = l->next;
    }
  g_slist_free (priv->pending_panels);
  priv->pending_panels = NULL;

  G_OBJECT_CLASS (mnb_toolbar_parent_class)->finalize (object);
}

/*
 * show/hide machinery
 */
static void
mnb_toolbar_show_completed_cb (ClutterAnimation *animation, ClutterActor *actor)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (actor)->priv;
  GList             *l = priv->panels;

  if (!priv->in_hide_animation && CLUTTER_ACTOR_IS_VISIBLE (actor))
    {
      for (; l; l = l->next)
        {
          MnbToolbarPanel *panel = l->data;

          if (panel && panel->button)
            clutter_actor_set_reactive (CLUTTER_ACTOR (panel->button), TRUE);
        }

      clutter_actor_show (priv->shadow);

      g_signal_emit (actor, toolbar_signals[SHOW_COMPLETED], 0);
    }

  priv->in_show_animation = FALSE;

  if (!priv->waiting_for_panel_show)
    priv->reason_for_show = _MNB_SHOW_HIDE_UNSET;

  g_object_unref (actor);

  if (priv->tp_to_activate)
    {
      MnbToolbarPanel *tp = priv->tp_to_activate;

      priv->tp_to_activate = NULL;

      if (!mx_button_get_toggled (MX_BUTTON (tp->button)))
        mx_button_set_toggled (MX_BUTTON (tp->button), TRUE);
    }

}

static void
mnb_toolbar_show_lowlight (MnbToolbar *toolbar)
{
  ClutterActor *lowlight = toolbar->priv->lowlight;

  if (CLUTTER_ACTOR_IS_VISIBLE (lowlight))
    return;

  clutter_actor_set_opacity (lowlight, 0);
  clutter_actor_show (lowlight);

  clutter_actor_animate (CLUTTER_ACTOR(lowlight),
                         CLUTTER_EASE_IN_SINE,
                         TOOLBAR_LOWLIGHT_FADE_DURATION,
                         "opacity", 0x7f,
                         NULL);

}

static void
mnb_toolbar_hide_lowlight (MnbToolbar *toolbar)
{
  ClutterActor     *lowlight = toolbar->priv->lowlight;
  ClutterAnimation *anim;

  if (!CLUTTER_ACTOR_IS_VISIBLE (lowlight))
    return;

  anim = clutter_actor_animate (CLUTTER_ACTOR(lowlight),
                                CLUTTER_EASE_IN_SINE,
                                TOOLBAR_LOWLIGHT_FADE_DURATION,
                                "opacity", 0,
                                NULL);

  g_signal_connect_swapped (anim,
                            "completed",
                            G_CALLBACK (clutter_actor_hide),
                            lowlight);
}

void
mnb_toolbar_show (MnbToolbar *toolbar, MnbShowHideReason reason)
{
  MnbToolbarPrivate  *priv = MNB_TOOLBAR (toolbar)->priv;
  ClutterActor       *actor = (ClutterActor*)toolbar;
  ClutterAnimation   *animation;

  if (priv->in_show_animation)
    {
      g_signal_stop_emission_by_name (toolbar, "show");
      return;
    }

  priv->reason_for_show = reason;

  clutter_actor_show (actor);

  /* set initial width and height */
  clutter_actor_set_y (actor, -(clutter_actor_get_height (actor)));

  g_object_ref (actor);

  priv->in_show_animation = TRUE;

  /*
   * Start animation and wait for it to complete.
   */
  animation = clutter_actor_animate (actor,
                                     CLUTTER_LINEAR, 150, "y", 0.0, NULL);

  g_signal_connect (animation,
                    "completed",
                    G_CALLBACK (mnb_toolbar_show_completed_cb),
                    actor);
}

static void
mnb_toolbar_real_show (ClutterActor *actor)
{
  MnbToolbarPrivate  *priv = MNB_TOOLBAR (actor)->priv;
  GList              *l;

  if (priv->in_show_animation)
    {
      g_signal_stop_emission_by_name (actor, "show");
      return;
    }

  if (moblin_netbook_use_netbook_mode (priv->plugin))
    mnb_toolbar_show_lowlight (MNB_TOOLBAR (actor));

  /*
   * Show all of the buttons -- see comments in _hide_completed_cb() on why we
   * do this.
   */
  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *panel = l->data;

      if (panel && panel->button)
      {
        clutter_actor_show (CLUTTER_ACTOR (panel->button));
        clutter_actor_set_reactive (CLUTTER_ACTOR (panel->button), FALSE);
      }
    }

  /*
   * Call the parent show(); this must be done before we do anything else.
   */
  CLUTTER_ACTOR_CLASS (mnb_toolbar_parent_class)->show (actor);

  if (priv->input_region)
    mnb_input_manager_remove_region_without_update (priv->input_region);

  priv->input_region =
    mnb_input_manager_push_region (0, 0,
                                   priv->old_screen_width,
                                   TOOLBAR_HEIGHT + 10,
                                   FALSE, MNB_INPUT_LAYER_PANEL);

  moblin_netbook_stash_window_focus (priv->plugin, CurrentTime);
}

static void
mnb_toolbar_real_hide (ClutterActor *actor)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (actor)->priv;
  GList             *l;

  /* the hide animation has finished, so now really hide the actor */
  CLUTTER_ACTOR_CLASS (mnb_toolbar_parent_class)->hide (actor);

  /*
   * We need to explicitely hide all the individual buttons, otherwise the
   * button tooltips will stay on screen.
   */
  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *panel = l->data;

      if (panel && panel->button)
        {
          clutter_actor_hide (CLUTTER_ACTOR (panel->button));

          if (mx_button_get_toggled (MX_BUTTON (panel->button)))
            mx_button_set_toggled (MX_BUTTON (panel->button), FALSE);
        }
    }
}

static void
mnb_toolbar_hide_transition_completed_cb (ClutterAnimation *animation,
					  ClutterActor     *actor)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (actor)->priv;

  priv->in_hide_animation = FALSE;
  priv->dont_autohide = FALSE;
  priv->panel_input_only = FALSE;
  priv->reason_for_hide = _MNB_SHOW_HIDE_UNSET;

  moblin_netbook_unstash_window_focus (priv->plugin, CurrentTime);

  g_signal_emit (actor, toolbar_signals[HIDE_COMPLETED], 0);

  clutter_actor_hide (actor);

  g_object_unref (actor);
}

/*
 * Returns TRUE if windows other than docks and ORs are present.
 */
static gboolean
mnb_toolbar_check_for_windows (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv   = toolbar->priv;
  MetaScreen        *screen = mutter_plugin_get_screen (priv->plugin);
  GList             *l;
  gint               count = 0;
  gint               max_allowed = 0;

  if (mutter_plugin_debug_mode (priv->plugin))
    {
      /*
       * The don't hide panel/toolbar feature is an absolute PITA to debug
       * so if we are in debugging mode, increase the threshold to 1.
       */

      max_allowed = 1;
    }

  l = mutter_get_windows (screen);

  while (l)
    {
      MutterWindow       *m    = l->data;
      MetaCompWindowType  type = mutter_window_get_window_type (m);

      if (!(type == META_COMP_WINDOW_DOCK    ||
            type == META_COMP_WINDOW_DESKTOP ||
            mutter_window_is_override_redirect (m)))
        {
          if (++count > max_allowed)
            return TRUE;
        }

      l = l->next;
    }

  return FALSE;
}

void
mnb_toolbar_hide (MnbToolbar *toolbar, MnbShowHideReason reason)
{
  ClutterActor      *actor = CLUTTER_ACTOR (toolbar);
  MnbToolbarPrivate *priv = toolbar->priv;
  ClutterAnimation  *animation;
  gfloat             height;
  GList             *l;

  /*
   * Don't allow the toolbar to hide when not in netbook mode.
   */
  if (!moblin_netbook_use_netbook_mode (priv->plugin))
    return;

  if (priv->in_hide_animation)
    return;

  /*
   * If the reason for us hiding is of a lesser priority than the reason for
   * any pending show, we do not hide.
   */
  if (priv->reason_for_show > reason)
    {
      g_debug ("Not hiding Toolbar, reasons show %d, hide %d",
               priv->reason_for_show, reason);

        return;
    }

  priv->reason_for_hide = reason;

  clutter_actor_hide (priv->shadow);

  mnb_toolbar_hide_lowlight (MNB_TOOLBAR (actor));

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *panel = l->data;

      if (panel->button)
        clutter_actor_set_reactive (CLUTTER_ACTOR (panel->button), FALSE);
    }

  g_signal_emit (actor, toolbar_signals[HIDE_BEGIN], 0);

  if (priv->input_region)
    {
      mnb_input_manager_remove_region (priv->input_region);
      priv->input_region = NULL;
    }

  priv->in_hide_animation = TRUE;

  g_object_ref (actor);

  height = clutter_actor_get_height (actor);

  /*
   * Start animation and wait for it to complete.
   */
  animation = clutter_actor_animate (actor, CLUTTER_LINEAR, 150,
                                     "y", -height, NULL);

  g_signal_connect (animation,
                    "completed",
                    G_CALLBACK (mnb_toolbar_hide_transition_completed_cb),
                    actor);
}

static void
mnb_toolbar_allocate (ClutterActor          *actor,
                      const ClutterActorBox *box,
                      ClutterAllocationFlags flags)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (actor)->priv;
  ClutterActorClass *parent_class;

  /*
   * The show and hide animations trigger allocations with origin_changed
   * set to TRUE; if we call the parent class allocation in this case, it
   * will force relayout, which we do not want. Instead, we call directly the
   * ClutterActor implementation of allocate(); this ensures our actor box is
   * correct, which is all we call about during the animations.
   *
   * If the drop down is not visible, we just return; this insures that the
   * needs_allocation flag in ClutterActor remains set, and the actor will get
   * reallocated when we show it.
   */
  if (!CLUTTER_ACTOR_IS_VISIBLE (actor))
    return;

  if (priv->in_show_animation || priv->in_hide_animation)
    {
      ClutterActorClass  *actor_class;

      actor_class = g_type_class_peek (CLUTTER_TYPE_ACTOR);

      if (actor_class)
        actor_class->allocate (actor, box, flags);

      return;
    }

  parent_class = CLUTTER_ACTOR_CLASS (mnb_toolbar_parent_class);
  parent_class->allocate (actor, box, flags);
}

static gboolean
mnb_toolbar_dbus_show_toolbar (MnbToolbar *self, GError **error)
{
  mnb_toolbar_show (self, MNB_SHOW_HIDE_BY_DBUS);
  return TRUE;
}

static gboolean
mnb_toolbar_dbus_hide_toolbar (MnbToolbar *self, GError **error)
{
  mnb_toolbar_hide (self, MNB_SHOW_HIDE_BY_DBUS);
  return TRUE;
}

static gboolean
mnb_toolbar_dbus_show_panel (MnbToolbar *self, gchar *name, GError **error)
{
  MnbToolbarPanel *tp;

  tp = mnb_toolbar_panel_name_to_panel_internal (self, name);

  if (!tp)
    return FALSE;

  mnb_toolbar_activate_panel_internal (self, tp, MNB_SHOW_HIDE_BY_DBUS);

  return TRUE;
}

static gboolean
mnb_toolbar_dbus_hide_panel (MnbToolbar  *self,
                             gchar       *name,
                             gboolean     hide_toolbar,
                             GError     **error)
{
  MnbPanel *panel = mnb_toolbar_panel_name_to_panel (self, name);

  if (!panel)
    return FALSE;

  if (!mnb_panel_is_mapped (panel))
    {
      if (hide_toolbar && CLUTTER_ACTOR_IS_MAPPED (self))
        mnb_toolbar_hide (self, MNB_SHOW_HIDE_BY_DBUS);
    }
  else if (hide_toolbar)
    mnb_panel_hide_with_toolbar (panel, MNB_SHOW_HIDE_BY_DBUS);
  else
    mnb_panel_hide (panel);

  return TRUE;
}

#include "../src/mnb-toolbar-dbus-glue.h"

static gboolean
mnb_toolbar_button_press_event (ClutterActor         *actor,
                                ClutterButtonEvent   *event)
{
  /*
   * We never ever want press events propagated
   */
  return TRUE;
}

static void
mnb_toolbar_class_init (MnbToolbarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *clutter_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MnbToolbarPrivate));

  object_class->get_property = mnb_toolbar_get_property;
  object_class->set_property = mnb_toolbar_set_property;
  object_class->dispose = mnb_toolbar_dispose;
  object_class->finalize = mnb_toolbar_finalize;
  object_class->constructed = mnb_toolbar_constructed;

  clutter_class->show = mnb_toolbar_real_show;
  clutter_class->hide = mnb_toolbar_real_hide;
  clutter_class->allocate = mnb_toolbar_allocate;
  clutter_class->button_press_event = mnb_toolbar_button_press_event;

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass),
                                   &dbus_glib_mnb_toolbar_dbus_object_info);

  g_object_class_install_property (object_class,
                                   PROP_MUTTER_PLUGIN,
                                   g_param_spec_object ("mutter-plugin",
                                                      "Mutter Plugin",
                                                      "Mutter Plugin",
                                                      MUTTER_TYPE_PLUGIN,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT_ONLY));


  /*
   * MnbToolbar::show-completed, emitted when show animation completes.
   */
  toolbar_signals[SHOW_COMPLETED] =
    g_signal_new ("show-completed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MnbToolbarClass, show_completed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /*
   * MnbToolbar::hide-begin, emitted before hide animation is started.
   */
  toolbar_signals[HIDE_BEGIN] =
    g_signal_new ("hide-begin",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MnbToolbarClass, hide_begin),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /*
   * MnbToolbar::hide-completed, emitted when hide animation completes.
   */
  toolbar_signals[HIDE_COMPLETED] =
    g_signal_new ("hide-completed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MnbToolbarClass, hide_completed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

/*
 * We need a safety clearing mechanism for the waiting_for_panel flags (so that
 * if a panel fails to complete the show/hide process, we do not block the
 * various depenedent UI ops indefinitely).
 */
static gboolean
mnb_toolbar_waiting_for_panel_show_cb (gpointer data)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (data)->priv;

  priv->waiting_for_panel_show       = FALSE;
  priv->waiting_for_panel_show_cb_id = 0;
  priv->reason_for_show              = _MNB_SHOW_HIDE_UNSET;

  return FALSE;
}

static gboolean
mnb_toolbar_waiting_for_panel_hide_cb (gpointer data)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (data)->priv;

  priv->waiting_for_panel_hide       = FALSE;
  priv->waiting_for_panel_hide_cb_id = 0;

  return FALSE;
}

static void
mnb_toolbar_set_waiting_for_panel_show (MnbToolbar *toolbar,
                                        gboolean    whether,
                                        gboolean    with_timeout)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  /*
   * Remove any existing timeout (if whether is TRUE, we need to restart it)
   */
  if (priv->waiting_for_panel_show_cb_id)
    {
      g_source_remove (priv->waiting_for_panel_show_cb_id);
      priv->waiting_for_panel_show_cb_id = 0;
    }

  if (whether && with_timeout)
    priv->waiting_for_panel_show_cb_id =
      g_timeout_add_seconds (TOOLBAR_WAITING_FOR_PANEL_TIMEOUT,
                             mnb_toolbar_waiting_for_panel_show_cb, toolbar);

  priv->waiting_for_panel_show = whether;

  if (!whether)
    priv->reason_for_show = _MNB_SHOW_HIDE_UNSET;
}

static void
mnb_toolbar_set_waiting_for_panel_hide (MnbToolbar *toolbar, gboolean whether)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  /*
   * Remove any existing timeout (if whether is TRUE, we need to restart it)
   */
  if (priv->waiting_for_panel_hide_cb_id)
    {
      g_source_remove (priv->waiting_for_panel_hide_cb_id);
      priv->waiting_for_panel_hide_cb_id = 0;
    }

  if (whether)
    priv->waiting_for_panel_hide_cb_id =
      g_timeout_add_seconds (TOOLBAR_WAITING_FOR_PANEL_TIMEOUT,
                             mnb_toolbar_waiting_for_panel_hide_cb, toolbar);

  priv->waiting_for_panel_hide = whether;
}

static gboolean
mnb_toolbar_panel_stub_timeout_cb (gpointer data)
{
  MnbToolbar        *toolbar = data;
  MnbToolbarPrivate *priv    = toolbar->priv;
  MnbToolbarPanel   *stubbed = priv->stubbed_panel;
  GList             *l;

  mnb_toolbar_set_waiting_for_panel_show (toolbar, FALSE, FALSE);
  clutter_actor_hide (priv->panel_stub);
  mnb_spinner_stop ((MnbSpinner*)priv->spinner);

  priv->stubbed_panel = NULL;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (!tp || tp != stubbed)
        continue;

      if (mx_button_get_toggled (MX_BUTTON (tp->button)))
        {
          gchar *tooltip;

          tp->pinged = FALSE;
          tp->failed = TRUE;

          tooltip = g_strdup_printf (_("Sorry, %s is broken"),
                                     tp->tooltip);

          mx_widget_set_tooltip_text (MX_WIDGET (tp->button), tooltip);

          g_free (tooltip);

          mx_button_set_toggled (MX_BUTTON (tp->button), FALSE);
        }

      break;
    }

  return FALSE;
}

static void
mnb_toolbar_show_pending_panel (MnbToolbar *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  gint               screen_width, screen_height;

  mutter_plugin_query_screen_size (priv->plugin,
                                   &screen_width, &screen_height);

  clutter_actor_set_size (priv->panel_stub, screen_width, screen_height / 3);

  /*
   * Set the waiting_for_panel_show flag, but without the timeout (since we
   * have a stub timeout of our own, which needs to be considerably longer).
   */
  mnb_toolbar_set_waiting_for_panel_show (toolbar, TRUE, FALSE);
  clutter_actor_set_opacity (priv->panel_stub, 0xff);
  clutter_actor_show (priv->panel_stub);
  clutter_actor_raise_top (priv->panel_stub);
  mnb_spinner_start ((MnbSpinner*)priv->spinner);
  priv->stubbed_panel = tp;

  if (priv->panel_stub_timeout_id)
    {
      g_source_remove (priv->panel_stub_timeout_id);
    }

  priv->panel_stub_timeout_id =
    g_timeout_add_seconds (TOOLBAR_PANEL_STUB_TIMEOUT,
                           mnb_toolbar_panel_stub_timeout_cb,
                           toolbar);

  tp->pinged = TRUE;
  mnb_toolbar_start_panel_service (toolbar, tp);
}

static gboolean
mnb_toolbar_button_button_release_cb (MnbToolbarButton   *button,
                                      ClutterButtonEvent *event,
                                      MnbToolbar         *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  priv->button_click = TRUE;

  return FALSE;
}

/*
 * Toolbar button click handler.
 *
 * If the new button stage is 'checked' we show the asociated panel and hide
 * all others; in the oposite case, we hide the associated panel.
 */
static void
mnb_toolbar_button_toggled_cb (MxButton *button,
                               GParamSpec *pspec,
                               MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv    = toolbar->priv;
  gboolean           checked;
  GList             *l;
  gboolean           button_click;

  static gboolean    recursion = FALSE;

  if (recursion)
    return;

  recursion = TRUE;

  button_click = priv->button_click;
  priv->button_click = FALSE;

  checked = mx_button_get_toggled (button);

  /*
   * If the user clicked on a button to show panel, but currently focused
   * window is system-modal, ignore the click.
   */
  if (button_click && checked)
    {
      MutterPlugin               *plugin = priv->plugin;
      MoblinNetbookPluginPrivate *ppriv  = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
      MetaWindow                 *focus  = ppriv->last_focused;

      if (focus && meta_window_is_modal (focus))
        {
          MetaWindow *p = meta_window_find_root_ancestor (focus);

          if (p == focus)
            {
              mx_button_set_toggled (button, FALSE);
              recursion = FALSE;
              return;
            }
        }
    }

  /*
   * If the user clicked on a button to hide a panel, and there are no
   * meaningful windows present, ignore the click.
   *
   * We differentiate here between a human click and a programmatic toggle,
   * otherwise the button is left in wrong state, for example, when launching
   * the first application.
   */
  if (button_click && !checked && !mnb_toolbar_check_for_windows (toolbar))
    {
      mx_button_set_toggled (button, TRUE);
      recursion = FALSE;
      return;
    }

  /*
   * Clear the autohiding flag -- if the user is clicking on the panel buttons
   * then we are back to normal mode.
   */
  priv->dont_autohide = FALSE;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (!tp)
        continue;

      if (tp->button != (ClutterActor*)button)
      {
        if (tp->button && mx_button_get_toggled (MX_BUTTON (tp->button)))
          mx_button_set_toggled (MX_BUTTON (tp->button), FALSE);

        if (tp->panel)
          {
            if (mnb_panel_is_mapped (tp->panel))
              {
                mnb_panel_hide (tp->panel);
              }
          }

        /*
         * Ensure the pinged flag is cleared (the user seems to have clicked
         * some other buttons since they clicked on this one)
         */
        tp->pinged = FALSE;
      }
    else
      {
        /*
         * When showing/hiding this panels, set the waiting_for_panel flag; this
         * serves two purposes:
         *
         *   a) forces LEAVE events to be ignored until the panel is shown
         *      (see bug 3531).
         *
         *   b) Prevents race conditions when the user starts clicking fast at
         *      the button (see bug 5020)
         */

        if (tp->panel)
          {
            if (checked && !mnb_panel_is_mapped (tp->panel))
              {
                mnb_toolbar_set_waiting_for_panel_show (toolbar, TRUE, TRUE);
                mnb_panel_show (tp->panel);

                if (priv->panel_stub_timeout_id)
                  {
                    g_source_remove (priv->panel_stub_timeout_id);
                    priv->panel_stub_timeout_id = 0;
                    clutter_actor_hide (priv->panel_stub);
                    mnb_spinner_stop ((MnbSpinner*)priv->spinner);
                    priv->stubbed_panel = NULL;
                  }
              }
            else if (!checked && mnb_panel_is_mapped (tp->panel))
              {
                mnb_toolbar_set_waiting_for_panel_hide (toolbar, TRUE);
                mnb_panel_hide (tp->panel);
              }
          }
        else
          {
            if (checked)
              {
                if (!tp->failed)
                  mnb_toolbar_show_pending_panel (toolbar, tp);
                else
                  {
                    g_warning ("Panel %s preiviously failed to load, ingoring",
                               tp->name);

                    mx_button_set_toggled (MX_BUTTON (tp->button), FALSE);
                  }
              }
            else
              {
                /*
                 * We are waiting for the panel to load; just ignore the click.
                 * This is not very nice to the user maybe, but simple and
                 * clean.
                 */
                if (button_click && !tp->failed)
                  mx_button_set_toggled (MX_BUTTON (tp->button), TRUE);
              }
          }
      }
    }

  recursion = FALSE;
}

static MnbToolbarPanel *
mnb_toolbar_panel_to_toolbar_panel (MnbToolbar *toolbar, MnbPanel *panel)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;

  g_return_val_if_fail (panel, NULL);

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->panel == panel)
        return tp;
    }

  return NULL;
}

MnbToolbarPanel *
mnb_toolbar_panel_name_to_panel_internal (MnbToolbar  *toolbar,
                                          const gchar *name)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l = priv->panels;

  g_return_val_if_fail (name, NULL);

  for (; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->name)
        if (!strcmp (name, tp->name))
          return tp;
    }

  return NULL;
}

MnbToolbarPanel *
mnb_toolbar_panel_service_to_panel_internal (MnbToolbar  *toolbar,
                                             const gchar *service)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l = priv->panels;

  g_return_val_if_fail (service, NULL);

  for (; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->service)
        {
          if (!strcmp (service, tp->service))
            return tp;
        }
    }

  return NULL;
}

MnbPanel *
mnb_toolbar_panel_name_to_panel (MnbToolbar *toolbar, const gchar *name)
{
  MnbToolbarPanel *tp;

  tp = mnb_toolbar_panel_name_to_panel_internal (toolbar, name);

  if (!tp)
    return NULL;

  return tp->panel;
}

/*
 * Helper function to manage lowlight stacking when showing a panel.
 */
static void
mnb_toolbar_raise_lowlight_for_panel (MnbToolbar *toolbar, MnbPanel *panel)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  if (CLUTTER_IS_ACTOR (panel))
    {
      clutter_actor_raise_top (priv->lowlight);
      clutter_actor_raise_top (priv->shadow);
    }
  else if (MNB_IS_PANEL_OOP (panel))
    {
      MnbPanelOop  *opanel = (MnbPanelOop*) panel;
      ClutterActor *actor;

      actor = (ClutterActor*) mnb_panel_oop_get_mutter_window (opanel);

      if (CLUTTER_ACTOR_IS_VISIBLE (priv->panel_stub))
        clutter_actor_raise (priv->panel_stub, actor);

      clutter_actor_lower (priv->shadow, actor);

      /*
       * If the panel is not in a modal state, we lower the lowlight just below
       * the panel shadow.
       *
       * If the panel is in modal state, we raise the lowlight above the panel
       * actor itself.
       */
      if (!mnb_panel_is_modal (panel))
        clutter_actor_lower (priv->lowlight, priv->shadow);
      else
        clutter_actor_raise (priv->lowlight, actor);
    }
}

static void
mnb_toolbar_panel_show_begin_cb (MnbPanel *panel, MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  if (CLUTTER_ACTOR_IS_VISIBLE (priv->panel_stub))
    {
      guint  w, h;
      gfloat wf, hf;

      mnb_panel_get_size (panel, &w, &h);

      wf = w;
      hf = h;

      clutter_actor_animate (priv->panel_stub, CLUTTER_EASE_IN_SINE,
                             SLIDE_DURATION,
                             "opacity", 0,
                             "width", wf,
                             "height", hf,
                             NULL);
    }

  mnb_toolbar_raise_lowlight_for_panel (toolbar, panel);
}

#if 0
/*
 * Use for any built-in panels, which require the input region to cover the
 * whole panel area.
 */
static void
mnb_toolbar_dropdown_show_completed_full_cb (MnbPanel   *panel,
                                             MnbToolbar *toolbar)
{
  g_assert (CLUTTER_IS_ACTOR (panel));

  mnb_input_manager_push_actor (CLUTTER_ACTOR (panel), MNB_INPUT_LAYER_PANEL);

  mnb_toolbar_set_waiting_for_panel_show (toolbar, FALSE, FALSE);
}
#endif

static void
mnb_toolbar_dropdown_show_completed_partial_cb (MnbPanel    *panel,
                                                MnbToolbar  *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MutterWindow      *mcw;

  g_assert (MNB_IS_PANEL_OOP (panel));

  mcw = mnb_panel_oop_get_mutter_window ((MnbPanelOop*)panel);
  mnb_panel_oop_set_delayed_show ((MnbPanelOop*)panel, FALSE);

  if (!mcw)
    g_warning ("Completed show on panel with no window ?!");
  else
    mnb_input_manager_push_oop_panel (mcw);

  clutter_actor_hide (priv->panel_stub);
  mnb_spinner_stop ((MnbSpinner*)priv->spinner);
  priv->stubbed_panel = NULL;

  if (!moblin_netbook_use_netbook_mode (priv->plugin))
    mnb_toolbar_show_lowlight (toolbar);

  mnb_toolbar_raise_lowlight_for_panel (toolbar, panel);
  mnb_toolbar_set_waiting_for_panel_show (toolbar, FALSE, FALSE);
}

static void
mnb_toolbar_dropdown_hide_completed_cb (MnbPanel *panel, MnbToolbar  *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MutterPlugin      *plugin = priv->plugin;
  MnbPanel          *active;

  moblin_netbook_stash_window_focus (plugin, CurrentTime);

  if (!priv->waiting_for_panel_show &&
      !moblin_netbook_use_netbook_mode (priv->plugin) &&
      (!(active = mnb_toolbar_get_active_panel (toolbar)) ||
       active == panel))
    {
      mnb_toolbar_hide_lowlight (toolbar);
    }

  priv->panel_input_only = FALSE;
  mnb_toolbar_set_waiting_for_panel_hide (toolbar, FALSE);
}

static gint
mnb_toolbar_get_panel_index (MnbToolbar *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;
  gint               index;

  if (!tp)
    return -1;

  for (l = priv->panels, index = 0; l; l = l->next)
    {
      MnbToolbarPanel *t = l->data;

      if (t->type == MNB_TOOLBAR_PANEL_APPLET ||
          t->type == MNB_TOOLBAR_PANEL_CLOCK)
        continue;

      if (l->data == tp)
        {
          return index;
        }

      ++index;
    }

  return -1;
}

static gint
mnb_toolbar_get_applet_index (MnbToolbar *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;
  gint               index;

  if (!tp)
    return -1;

  for (l = priv->panels, index = 0; l; l = l->next)
    {
      MnbToolbarPanel *t = l->data;

      if (t->type != MNB_TOOLBAR_PANEL_APPLET &&
          t->type != MNB_TOOLBAR_PANEL_CLOCK)
        continue;

      if (l->data == tp)
        {
          return index;
        }

      ++index;
    }

  return -1;
}

static gint
mnb_toolbar_get_clock_position_in_tray (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;
  gint               index;

  for (l = priv->panels, index = 0; l; l = l->next)
    {
      MnbToolbarPanel *t = l->data;

      if (t->type == MNB_TOOLBAR_PANEL_NORMAL)
        continue;

      if (t->type == MNB_TOOLBAR_PANEL_CLOCK)
        return index;

      ++index;
    }

  return -1;
}

#if 0
static void
mnb_toolbar_append_panel_builtin_internal (MnbToolbar      *toolbar,
                                           MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MutterPlugin      *plugin = priv->plugin;
  MnbPanel          *panel = NULL;
  gint               screen_width, screen_height;

  if (!tp)
    return;

  if (tp->panel)
    {
      /*
       * BTW -- this code should not be reached; we should have exited
       * already in the button test.
       */
      g_warning ("The Spaces Zone cannot be replaced\n");
      return;
    }

  mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

  {
#if 0
      MetaScreen  *screen  = mutter_plugin_get_screen (plugin);
      MetaDisplay *display = meta_screen_get_display (screen);

      panel = tp->panel = MNB_PANEL (mnb_switcher_new (plugin));

      g_signal_connect (panel, "show-completed",
                        G_CALLBACK(mnb_toolbar_dropdown_show_completed_full_cb),
                        toolbar);
      g_signal_connect (panel, "show-begin",
                        G_CALLBACK(mnb_toolbar_panel_show_begin_cb),
                        toolbar);

      g_signal_connect (display, "notify::focus-window",
                        G_CALLBACK (mnb_switcher_focus_window_cb),
                        panel);
#endif
    }

  if (!panel)
    {
      g_warning ("Builtin panel %s is not available", tp->name);
      return;
    }

  g_signal_connect (panel, "hide-completed",
                    G_CALLBACK(mnb_toolbar_dropdown_hide_completed_cb),
                    toolbar);

  /* This is safe, because the Switcher is an actor */
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->hbox),
                               CLUTTER_ACTOR (panel));
  clutter_actor_set_width (CLUTTER_ACTOR (panel),
                           screen_width);

  if (tp->button)
    mnb_panel_set_button (panel, MX_BUTTON (tp->button));
  mnb_panel_set_position (panel, 0, TOOLBAR_HEIGHT);
}
#endif

static void
mnb_toolbar_panel_request_button_style_cb (MnbPanel    *panel,
                                           const gchar *style_id,
                                           MnbToolbar  *toolbar)
{
  MnbToolbarPanel *tp;

  tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

  if (!tp || !tp->button)
    return;

  clutter_actor_set_name (CLUTTER_ACTOR (tp->button), style_id);
}

static void
mnb_toolbar_panel_request_button_state_cb (MnbPanel       *panel,
                                           MnbButtonState  state,
                                           MnbToolbar     *toolbar)
{
  ClutterActor    *actor;
  MnbToolbarPanel *tp;

  tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

  if (!tp || !tp->button)
    return;

  actor = CLUTTER_ACTOR (tp->button);

  if (CLUTTER_ACTOR_IS_MAPPED (actor) && (state & MNB_BUTTON_HIDDEN))
    clutter_actor_hide (actor);
  else if (!CLUTTER_ACTOR_IS_MAPPED (actor) && !(state & MNB_BUTTON_HIDDEN))
    clutter_actor_show (actor);

  if ((state & MNB_BUTTON_INSENSITIVE))
    g_warning (G_STRLOC " Insensitive state is not yet implemented.");
}

static void
mnb_toolbar_panel_notify_modal_cb (MnbPanel   *panel,
                                   GParamSpec *pspec,
                                   MnbToolbar *toolbar)
{
  MnbToolbarPanel *tp;

  tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

  if (!tp || !tp->panel || !mnb_panel_is_mapped (tp->panel))
    return;

  mnb_toolbar_raise_lowlight_for_panel (toolbar, panel);
}

static void
mnb_toolbar_panel_request_tooltip_cb (MnbPanel    *panel,
                                      const gchar *tooltip,
                                      MnbToolbar  *toolbar)
{
  MnbToolbarPanel *tp;

  tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

  if (!tp || !tp->button)
    return;

  if (tp->button && tp->type != MNB_TOOLBAR_PANEL_CLOCK)
    mx_widget_set_tooltip_text (MX_WIDGET (tp->button), tooltip);
}

/*
 * Removes the panel from the pending_panels list
 */
static void
mnb_toolbar_remove_panel_from_pending (MnbToolbar *toolbar, MnbPanelOop *panel)
{
  MnbToolbarPrivate *priv      = toolbar->priv;
  const gchar       *dbus_name = mnb_panel_oop_get_dbus_name (panel);

  if (dbus_name)
    {
      GSList *l = priv->pending_panels;

      while (l)
        {
          gchar *n = l->data;

          if (!strcmp (n, dbus_name))
            {
              g_free (n);
              priv->pending_panels =
                g_slist_delete_link (priv->pending_panels, l);
              break;
            }

          l = l->next;
        }
    }
}

/*
 * Removes the panel from the toolbar, avoiding any recursion
 * due to "destroy" signal handler.
 *
 * The panel_destroyed parameter should be set to TRUE if the panel is known
 * to be in the destroy sequence.
 */
static void
mnb_toolbar_dispose_of_button (MnbToolbar      *toolbar,
                               MnbToolbarPanel *tp)
{
  ClutterActor *button;

  if (!tp)
    return;

  button = tp->button;

  if (!button)
    return;

  tp->button = NULL;

  g_signal_handlers_disconnect_matched (button,
                                        G_SIGNAL_MATCH_DATA,
                                        0, 0, NULL, NULL,
                                        toolbar);

  clutter_actor_destroy (button);
}

/*
 * Removes the panel from the toolbar, avoiding any recursion
 * due to "destroy" signal handler.
 *
 * The panel_destroyed parameter should be set to TRUE if the panel is known
 * to be in the destroy sequence.
 */
static void
mnb_toolbar_dispose_of_panel (MnbToolbar      *toolbar,
                              MnbToolbarPanel *tp,
                              gboolean         panel_destroyed)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MnbPanel          *panel;

  if (!tp)
    return;

  panel  = tp->panel;

  if (!tp->panel)
    return;

  tp->panel  = NULL;

  g_signal_handlers_disconnect_matched (panel,
                                        G_SIGNAL_MATCH_DATA,
                                        0, 0, NULL, NULL,
                                        toolbar);

  if (MNB_IS_PANEL_OOP (panel))
    mnb_toolbar_remove_panel_from_pending (toolbar, (MnbPanelOop*)panel);

  if (!panel_destroyed && CLUTTER_IS_ACTOR (panel))
    clutter_container_remove_actor (CLUTTER_CONTAINER (priv->hbox),
                                        CLUTTER_ACTOR (panel));
}

#if 0
static void
mnb_toolbar_update_dropdown_input_region (MnbToolbar  *toolbar,
                                          MnbPanel    *panel)
{
  MnbToolbarPrivate *priv;
  MutterPlugin      *plugin;
  gfloat             x, y,w, h;
  MnbInputRegion    *region;

  /*
   * If this panel is visible, we need to update the input region to match
   * the new geometry.
   */
  if (!mnb_panel_is_mapped (panel))
    return;

  priv   = toolbar->priv;
  plugin = priv->plugin;

  mnb_drop_down_get_footer_geometry (panel, &x, &y, &w, &h);

  region = mnb_panel_get_input_region (panel);

  if (region)
    mnb_input_manager_remove_region_without_update (region);

  if (priv->panel_input_only)
    region = mnb_input_manager_push_region ((gint)x, TOOLBAR_HEIGHT + (gint)y,
                                            (guint)w, (guint)h,
                                            FALSE, MNB_INPUT_LAYER_PANEL);
  else
    region = mnb_input_manager_push_region ((gint)x, TOOLBAR_HEIGHT + (gint)y,
                                            (guint)w,
                                            priv->old_screen_height -
                                            (TOOLBAR_HEIGHT+(gint)y),
                                            FALSE, MNB_INPUT_LAYER_PANEL);

  mnb_panel_set_input_region (panel, region);
}
#endif

static void
mnb_toolbar_panel_died_cb (MnbPanel *panel, MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MnbToolbarPanel   *tp;

  tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

  if (!tp)
    return;

  mnb_toolbar_dispose_of_panel (toolbar, tp, FALSE);

  /*
   * If the panel went away because we unloaded it, we are done.
   */
  if (tp->unloaded)
    {
      mnb_toolbar_dispose_of_button (toolbar, tp);
      priv->panels = g_list_remove (priv->panels, tp);
      mnb_toolbar_panel_destroy (tp);
      return;
    }

  /*
   * Try to restart the service
   */
  if (!toolbar->priv->no_autoloading && tp->service)
    {
      mnb_toolbar_handle_dbus_name (toolbar, tp->service);
    }
}

static void
mnb_toolbar_panel_ready_cb (MnbPanel *panel, MnbToolbar *toolbar)
{
  if (MNB_IS_PANEL (panel))
    {
      MnbToolbarPrivate *priv   = toolbar->priv;
      ClutterActor      *button;
      const gchar       *tooltip;
      const gchar       *style_id;
      const gchar       *stylesheet;
      MnbToolbarPanel   *tp;

      tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

      if (!tp)
        return;

      button = tp->button;

      tooltip    = mnb_panel_get_tooltip (panel);
      stylesheet = mnb_panel_get_stylesheet (panel);
      style_id   = mnb_panel_get_button_style (panel);

      if (button)
        {
          gchar *button_style = NULL;

          if (stylesheet && *stylesheet &&
              (!tp->button_stylesheet ||
               strcmp (stylesheet, tp->button_stylesheet)))
            {
              GError  *error = NULL;

              if (!mx_style_load_from_file (mx_style_get_default (),
                                            stylesheet, &error))
                {
                  if (error)
                    g_warning ("Unable to load stylesheet %s: %s",
                               stylesheet, error->message);

                  g_clear_error (&error);
                }
              else
                {
                  g_free (tp->button_stylesheet);
                  tp->button_stylesheet = g_strdup (stylesheet);
                }
            }

          if (!style_id || !*style_id)
            {
              const gchar *name = mnb_panel_get_name (panel);

              if (tp->button_style)
                style_id = tp->button_style;
              else
                button_style = g_strdup_printf ("%s-button", name);
            }

          if (tp->type != MNB_TOOLBAR_PANEL_CLOCK)
            mx_widget_set_tooltip_text (MX_WIDGET (button), tooltip);

          clutter_actor_set_name (CLUTTER_ACTOR (button),
                                  button_style ? button_style : style_id);

          g_free (button_style);
        }

      if (tp->pinged)
        {
          tp->pinged = FALSE;

          if (MNB_IS_PANEL_OOP (panel))
            mnb_panel_oop_set_delayed_show ((MnbPanelOop*)panel, TRUE);

          if (priv->panel_stub_timeout_id)
            {
              g_source_remove (priv->panel_stub_timeout_id);
              priv->panel_stub_timeout_id = 0;
            }

          mnb_panel_show (panel);
        }
      else if (!priv->shown_myzone && priv->shown)
        {
          if (tp->name && !strcmp (tp->name, "moblin-panel-myzone"))
            {
              mnb_panel_show (panel);
              priv->shown_myzone = TRUE;
            }
        }
    }
}

static void
mnb_toolbar_panel_destroy_cb (MnbPanel *panel, MnbToolbar *toolbar)
{
  MnbToolbarPanel *tp;

  if (!MNB_IS_PANEL_OOP (panel))
    {
      g_warning ("Unhandled panel type: %s", G_OBJECT_TYPE_NAME (panel));
      return;
    }

  tp = mnb_toolbar_panel_to_toolbar_panel (toolbar, panel);

  if (!tp)
    {
      /*
       * This is the case when the panel initialization failed, and the panel
       * was never added to the Toolbar. We need to release any floating
       * reference.
       */
      if (MNB_IS_PANEL_OOP (panel))
        mnb_toolbar_remove_panel_from_pending (toolbar, (MnbPanelOop*)panel);

      if (g_object_is_floating (panel))
        {
          g_object_ref_sink (panel);
          g_object_unref (panel);
        }

      return;
    }

  mnb_toolbar_dispose_of_panel (toolbar, tp, TRUE);
}

static void
mnb_toolbar_ensure_applet_position (MnbToolbar *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv   = toolbar->priv;
  MutterPlugin      *plugin = priv->plugin;
  gint               screen_width, screen_height;
  ClutterActor      *button;
  gint               index;

  if (!tp || !tp->button)
    return;

  button = tp->button;

  mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

  index = mnb_toolbar_get_applet_index (toolbar, tp);

  if (index < MNB_TOOLBAR_MAX_APPLETS)
    {
      gint x, y, clock_index;
      gint width = screen_width;

      if (!moblin_netbook_use_netbook_mode (plugin))
        width += BIG_SCREEN_PAD;

      clock_index = mnb_toolbar_get_clock_position_in_tray (toolbar);

      y = TOOLBAR_HEIGHT - TRAY_BUTTON_HEIGHT;
      x = width - (index + 1) * (TRAY_BUTTON_WIDTH+TRAY_PADDING) - 4;

      if (clock_index >= 0 && index >= clock_index)
        x -= (CLOCK_WIDTH - TRAY_BUTTON_WIDTH);

      if (tp->type != MNB_TOOLBAR_PANEL_CLOCK)
        {
          mnb_toolbar_button_set_reactive_area (MNB_TOOLBAR_BUTTON (button),
                                                0, -y,
                                                TRAY_BUTTON_WIDTH,
                                                TOOLBAR_HEIGHT);
        }

      clutter_actor_set_position (CLUTTER_ACTOR (button), (gfloat)x, (gfloat)y);

      if (!clutter_actor_get_parent (button))
        clutter_container_add_actor (CLUTTER_CONTAINER (priv->hbox),
                                     button);
    }
}

static void
mnb_toolbar_ensure_button_position (MnbToolbar *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv   = toolbar->priv;
  MutterPlugin      *plugin = priv->plugin;
  gint               screen_width, screen_height;
  ClutterActor      *button;

  if (!tp || !tp->button)
    return;

  button = tp->button;

  mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

  /*
   * The button size and positioning depends on whether this is a regular
   * zone button, but one of the applet buttons.
   */
  if (tp->type == MNB_TOOLBAR_PANEL_NORMAL)
    {
      gint index = mnb_toolbar_get_panel_index (toolbar, tp);

      if (index < priv->max_panels)
        {
          gfloat spacing = BUTTON_SPACING_INITIAL +
            (BUTTON_WIDTH * index);

          if (index > 0)
            spacing += BUTTON_SPACING_INITIAL + BUTTON_SPACING / 2;

          if (index > 1)
            spacing += (index - 1) * BUTTON_SPACING;

          if (!moblin_netbook_use_netbook_mode (plugin))
            spacing += BIG_SCREEN_BUTTON_SHIFT;

          clutter_actor_set_position (CLUTTER_ACTOR (button), spacing,
                                      TOOLBAR_HEIGHT - BUTTON_HEIGHT);

          mnb_toolbar_button_set_reactive_area (MNB_TOOLBAR_BUTTON (button),
                                                0,
                                                -(TOOLBAR_HEIGHT-BUTTON_HEIGHT),
                                                BUTTON_WIDTH,
                                                TOOLBAR_HEIGHT);

          if (!clutter_actor_get_parent (button))
            clutter_container_add_actor (CLUTTER_CONTAINER (priv->hbox),
                                         button);
        }
    }
  else
    mnb_toolbar_ensure_applet_position (toolbar, tp);
}

static void
mnb_toolbar_windowless_button_toggled_cb (MxButton *button,
                                          GParamSpec *pspec,
                                          MnbToolbar *toolbar)
{
  /*
   * Windowless buttons cannot be checked.
   */
  if (mx_button_get_toggled (button))
    mx_button_set_toggled (button, FALSE);
}

/*
 * Appends a panel
 */
static void
mnb_toolbar_append_button (MnbToolbar  *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv   = toolbar->priv;
  ClutterActor      *button;
  gchar             *button_style = NULL;
  const gchar       *name;
  const gchar       *tooltip;
  const gchar       *stylesheet = NULL;
  const gchar       *style_id = NULL;
  gint               index;

  if (!tp)
    return;

  /*
   * Check that we have space to show this panel.
   */
  if (tp->type == MNB_TOOLBAR_PANEL_APPLET ||
      tp->type == MNB_TOOLBAR_PANEL_CLOCK)
    {
      index = mnb_toolbar_get_applet_index (toolbar, tp);

      if (index >= MNB_TOOLBAR_MAX_APPLETS)
        {
          g_warning ("Button for applet %s could not be appended "
                     "(not enough space; applet index %d, max applets %d)",
                     tp->name, index, MNB_TOOLBAR_MAX_APPLETS);

          return;
        }
    }
  else
    {
      index = mnb_toolbar_get_panel_index (toolbar, tp);

      if (index >= toolbar->priv->max_panels)
        {
          g_warning ("Button for panel %s could not be appended "
                     "(not enough space; panel index %d, max panels %d)",
                     tp->name, index, toolbar->priv->max_panels);

          return;
        }
    }

  name       = tp->name;
  tooltip    = tp->tooltip;
  stylesheet = tp->button_stylesheet;
  style_id   = tp->button_style;

  if (!style_id || !*style_id)
    {
      if (tp->button_style)
        style_id = tp->button_style;
      else if (tp->type != MNB_TOOLBAR_PANEL_CLOCK)
        button_style = g_strdup_printf ("%s-button", name);
    }

  if (tp->button)
    clutter_actor_destroy (CLUTTER_ACTOR (tp->button));

  if (tp->type == MNB_TOOLBAR_PANEL_CLOCK)
    {
      button = tp->button = mnb_toolbar_clock_new ();
      toolbar->priv->have_clock = TRUE;

      if (priv->dummy_clock)
        {
          clutter_actor_destroy (priv->dummy_clock);
          priv->dummy_clock = NULL;
        }
    }
  else if (tp->windowless)
    button = tp->button = mnb_toolbar_icon_new ();
  else if (tp->type == MNB_TOOLBAR_PANEL_APPLET)
    button = tp->button = mnb_toolbar_applet_new ();
  else
    button = tp->button = mnb_toolbar_button_new ();

  if (stylesheet && *stylesheet)
    {
      if (stylesheet && *stylesheet)
        {
          GError  *error = NULL;

          if (!mx_style_load_from_file (mx_style_get_default (),
                                        stylesheet, &error))
            {
              if (error)
                g_warning ("Unable to load stylesheet %s: %s",
                           stylesheet, error->message);

              g_clear_error (&error);

              g_free (tp->button_stylesheet);
              tp->button_stylesheet = NULL;
            }
        }
    }

  mx_button_set_is_toggle (MX_BUTTON (button), TRUE);

  if (tp->type != MNB_TOOLBAR_PANEL_CLOCK)
    mx_widget_set_tooltip_text (MX_WIDGET (button), tooltip);

  clutter_actor_set_name (button, button_style ? button_style : style_id);

  g_free (button_style);

  mnb_toolbar_ensure_button_position (toolbar, tp);

  if (!tp->windowless)
    {
      g_signal_connect (button, "notify::toggled",
                        G_CALLBACK (mnb_toolbar_button_toggled_cb),
                        toolbar);

      g_signal_connect (button, "button-release-event",
                        G_CALLBACK (mnb_toolbar_button_button_release_cb),
                        toolbar);
    }
  else
    g_signal_connect (button, "notify::toggled",
                      G_CALLBACK (mnb_toolbar_windowless_button_toggled_cb),
                      toolbar);
}

/*
 * Appends a panel
 */
static void
mnb_toolbar_append_panel (MnbToolbar  *toolbar, MnbPanel *panel)
{
  const gchar       *name;
  const gchar       *service = NULL;
  MnbToolbarPanel   *tp;

  if (MNB_IS_PANEL (panel))
    {
      name = mnb_panel_get_name (panel);

      /*
       * Remove this panel from the pending list.
       */
      if (MNB_IS_PANEL_OOP (panel))
        {
          service = mnb_panel_oop_get_dbus_name ((MnbPanelOop*)panel);
          mnb_toolbar_remove_panel_from_pending (toolbar, (MnbPanelOop*)panel);
        }
    }
   else
    {
      g_warning ("Unhandled panel type: %s", G_OBJECT_TYPE_NAME (panel));
      return;
    }

  tp = mnb_toolbar_panel_name_to_panel_internal (toolbar, name);

  if (!tp)
    {
      if (service)
          tp = mnb_toolbar_panel_service_to_panel_internal (toolbar, service);

      if (!tp)
        {
          g_warning (G_STRLOC ": Unknown panel %s", name);
          return;
        }
    }

  if (panel == tp->panel)
    return;

  /*
   * Disconnect this function from the "ready" signal. Instead, we connect a
   * handler later on that updates things if this signal is issued again.
   */
  g_signal_handlers_disconnect_by_func (panel,
                                        mnb_toolbar_append_panel, toolbar);

  /*
   * If the respective slot is already occupied, remove the old objects.
   */
  mnb_toolbar_dispose_of_panel (toolbar, tp, FALSE);

  g_signal_connect (panel, "show-completed",
                    G_CALLBACK(mnb_toolbar_dropdown_show_completed_partial_cb),
                    toolbar);
  g_signal_connect (panel, "show-begin",
                    G_CALLBACK(mnb_toolbar_panel_show_begin_cb),
                    toolbar);

  g_signal_connect (panel, "hide-completed",
                    G_CALLBACK (mnb_toolbar_dropdown_hide_completed_cb), toolbar);

  g_signal_connect (panel, "request-button-style",
                    G_CALLBACK (mnb_toolbar_panel_request_button_style_cb),
                    toolbar);

  g_signal_connect (panel, "request-button-state",
                    G_CALLBACK (mnb_toolbar_panel_request_button_state_cb),
                    toolbar);

  g_signal_connect (panel, "request-tooltip",
                    G_CALLBACK (mnb_toolbar_panel_request_tooltip_cb),
                    toolbar);

  g_signal_connect (panel, "notify::modal",
                    G_CALLBACK (mnb_toolbar_panel_notify_modal_cb),
                    toolbar);

  g_signal_connect (panel, "remote-process-died",
                    G_CALLBACK (mnb_toolbar_panel_died_cb), toolbar);

  tp->panel = panel;

  if (tp->button)
    mnb_panel_set_button (panel, MX_BUTTON (tp->button));

  if (mnb_panel_oop_is_ready (MNB_PANEL_OOP (panel)))
    mnb_toolbar_panel_ready_cb (panel, toolbar);
  else
    g_signal_connect (panel, "ready",
                      G_CALLBACK (mnb_toolbar_panel_ready_cb), toolbar);
}

static void
mnb_toolbar_init (MnbToolbar *self)
{
  MnbToolbarPrivate *priv;
  guint32            flags = moblin_netbook_get_compositor_option_flags ();

  priv = self->priv = MNB_TOOLBAR_GET_PRIVATE (self);

  if (flags & MNB_OPTION_DISABLE_PANEL_RESTART)
    priv->no_autoloading = TRUE;
}

static DBusGConnection *
mnb_toolbar_connect_to_dbus (MnbToolbar *self)
{
  MnbToolbarPrivate *priv = self->priv;
  DBusGConnection   *conn;
  DBusGProxy        *proxy;
  GError            *error = NULL;
  guint              status;

  conn = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

  if (!conn)
    {
      g_warning ("Cannot connect to DBus: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  proxy = dbus_g_proxy_new_for_name (conn,
                                     DBUS_SERVICE_DBUS,
                                     DBUS_PATH_DBUS,
                                     DBUS_INTERFACE_DBUS);

  if (!proxy)
    {
      g_object_unref (conn);
      return NULL;
    }

  if (!org_freedesktop_DBus_request_name (proxy,
                                          MPL_TOOLBAR_DBUS_NAME,
                                          DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                          &status, &error))
    {
      if (error)
        {
          g_warning ("%s: %s", __FUNCTION__, error->message);
          g_error_free (error);
        }
      else
        {
          g_warning ("%s: Unknown error", __FUNCTION__);
        }

      g_object_unref (conn);
      conn = NULL;
    }

  priv->dbus_proxy = proxy;

  dbus_g_proxy_add_signal (proxy, "NameOwnerChanged",
                           G_TYPE_STRING,
                           G_TYPE_STRING,
                           G_TYPE_STRING,
                           G_TYPE_INVALID);

  return conn;
}

/*
 * Creates OOP panel for the panel service as it appears on the bus
 */
static void
mnb_toolbar_handle_dbus_name (MnbToolbar *toolbar, const gchar *name)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MnbPanelOop       *panel;

  panel = mnb_panel_oop_new (name,
                             TOOLBAR_X_PADDING,
                             TOOLBAR_HEIGHT + 4,
                             priv->old_screen_width - TOOLBAR_X_PADDING * 2,
                             priv->old_screen_height - TOOLBAR_HEIGHT - 8);

  if (panel)
    {
      g_signal_connect (panel, "destroy",
                        G_CALLBACK (mnb_toolbar_panel_destroy_cb), toolbar);

      if (mnb_panel_oop_is_ready (panel))
        {
          mnb_toolbar_append_panel (toolbar, (MnbPanel*)panel);
        }
      else
        {
          priv->pending_panels =
            g_slist_prepend (priv->pending_panels, g_strdup (name));
          g_signal_connect_swapped (panel, "ready",
                                    G_CALLBACK (mnb_toolbar_append_panel),
                                    toolbar);
        }
    }
}

static void
mnb_toolbar_noc_cb (DBusGProxy  *proxy,
                    const gchar *name,
                    const gchar *old_owner,
                    const gchar *new_owner,
                    MnbToolbar  *toolbar)
{
  MnbToolbarPrivate *priv;
  GSList            *l;

  /*
   * Unfortunately, we get this for all name owner changes on the bus, so
   * return early.
   */
  if (!name || strncmp (name, MPL_PANEL_DBUS_NAME_PREFIX,
                        strlen (MPL_PANEL_DBUS_NAME_PREFIX)))
    return;

  priv = MNB_TOOLBAR (toolbar)->priv;

  if (!new_owner || !*new_owner)
    {
      /*
       * This is the case where a panel gone away; we can ignore it here,
       * as this gets handled nicely elsewhere.
       */
      return;
    }

  l = priv->pending_panels;
  while (l)
    {
      const gchar *my_name = l->data;

      if (!strcmp (my_name, name))
        {
          /* We are already handling this one */
          return;
        }

      l = l->next;
    }

  mnb_toolbar_handle_dbus_name (toolbar, name);
}

/*
 * Start panel service for the given panel, if necessary
 *
 * Returns FALSE if the panel is already present.
 */
static gboolean
mnb_toolbar_start_panel_service (MnbToolbar *toolbar, MnbToolbarPanel *tp)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  if (tp->panel)
    return FALSE;

  if (!tp->service)
    {
      g_warning ("Panel %s does not provide service", tp->name);
      return FALSE;
    }

  mnb_toolbar_ping_panel_oop (priv->dbus_conn, tp->service);

  return TRUE;
}

#if 0
static gboolean
mnb_toolbar_autostart_panels_cb (gpointer toolbar)
{
  static gint count = 0;

  MnbToolbarPrivate  *priv = MNB_TOOLBAR (toolbar)->priv;
  gboolean            missing = FALSE;
  GList              *l;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (!tp || tp->unloaded)
        continue;

      if (!tp->panel && tp->service)
        {
          missing = mnb_toolbar_start_panel_service (toolbar, tp);

          if (count > TOOLBAR_AUTOSTART_ATTEMPTS)
            {
              g_warning ("Panel %s is still not running after %d "
                         "attempts to start it, last attempt.",
                         tp->name, count);
            }
        }
    }

  if (!missing || count > TOOLBAR_AUTOSTART_ATTEMPTS)
    return FALSE;

  count++;

  return TRUE;
}
#endif

static void
mnb_toolbar_dbus_list_names_cb (DBusGProxy  *proxy,
                                char       **names,
                                GError      *error,
                                gpointer     data)
{
  MnbToolbar         *toolbar = MNB_TOOLBAR (data);
  MnbToolbarPrivate  *priv    = toolbar->priv;

  if (!priv->dbus_conn || !priv->dbus_proxy)
    {
      g_warning ("DBus connection not available, cannot start panels !!!");
      return;
    }

  /*
   * Insert panels for any services already running.
   */
  if (!error)
    {
      gchar **p = names;
      while (*p)
        {
          if (!strncmp (*p, MPL_PANEL_DBUS_NAME_PREFIX,
                        strlen (MPL_PANEL_DBUS_NAME_PREFIX)))
            {
              gboolean  has_owner = FALSE;

              if (org_freedesktop_DBus_name_has_owner (priv->dbus_proxy,
                                                       *p, &has_owner, NULL) &&
                  has_owner)
                {
                  MnbToolbarPanel *tp;

                  tp = mnb_toolbar_panel_service_to_panel_internal (toolbar,*p);

                  if (tp)
                    mnb_toolbar_handle_dbus_name (toolbar, *p);
                }
            }

          p++;
        }
    }
  else
    {
      g_warning (G_STRLOC " Initial panel setup failed: %s",
                 error->message);
      g_error_free (error);
    }

  if (names)
    dbus_free_string_array (names);

  dbus_g_proxy_connect_signal (priv->dbus_proxy, "NameOwnerChanged",
                               G_CALLBACK (mnb_toolbar_noc_cb),
                               toolbar, NULL);
}


/*
 * Create panels for any of our services that are already up.
 */
static void
mnb_toolbar_dbus_setup_panels (MnbToolbar *toolbar)
{
  MnbToolbarPrivate  *priv = toolbar->priv;

  if (!priv->dbus_conn || !priv->dbus_proxy)
    {
      g_warning ("DBus connection not available, cannot start panels !!!");
      return;
    }

  /*
   * Insert panels for any services already running. Like everything else,
   * do this asynchronously to avoid blocking the WM.
   */
  org_freedesktop_DBus_list_names_async (priv->dbus_proxy,
                                         mnb_toolbar_dbus_list_names_cb,
                                         toolbar);
}

/*
 * If the compositor restacks, and we are showing an OOP panel, we need to
 * lower the shadow below the panel.
 */
static void
mnb_toolbar_screen_restacked_cb (MetaScreen *screen, MnbToolbar *toolbar)
{
  MnbPanel *panel;

  panel = mnb_toolbar_get_active_panel (toolbar);

  if (!panel || !MNB_IS_PANEL_OOP (panel))
    return;

  mnb_toolbar_raise_lowlight_for_panel (toolbar, panel);
}

static void
mnb_toolbar_setup_panels (MnbToolbar *toolbar)
{
  /*
   * Set up panels from gconf watch for changes
   */
  mnb_toolbar_setup_gconf (toolbar);

  mnb_toolbar_dbus_setup_panels (toolbar);
}

static gboolean
mnb_toolbar_lowlight_button_press_cb (ClutterActor *lowlight,
                                      ClutterEvent *event,
                                      MnbToolbar   *toolbar)
{
  if (CLUTTER_ACTOR_IS_MAPPED (toolbar))
    {
      MnbPanel *panel = mnb_toolbar_get_active_panel (toolbar);

      if (!panel || !MNB_IS_PANEL_OOP (panel))
        return FALSE;

      /*
       * If there are no singificant windows, we do not hide the toolbar (we
       * do not hide the panel either, the user must explicitely switch to a
       * different panel).
       */
      if (mnb_toolbar_check_for_windows (toolbar))
        mnb_panel_hide_with_toolbar (panel, MNB_SHOW_HIDE_BY_MOUSE);

      return TRUE;
    }

  return FALSE;
}

static void
mnb_toolbar_set_struts (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv           = toolbar->priv;
  MutterPlugin      *plugin         = priv->plugin;
  gboolean           netbook_mode   = moblin_netbook_use_netbook_mode (plugin);
  MoblinNetbookPluginPrivate *ppriv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  MetaScreen        *screen         = mutter_plugin_get_screen (plugin);
  MetaDisplay       *display        = meta_screen_get_display (screen);
  Display           *xdpy           = meta_display_get_xdisplay (display);
  Window             xwin           = ppriv->focus_xwin;
  Atom               strut_atom     = meta_display_get_atom (display,
                                                      META_ATOM__NET_WM_STRUT);

  /*
   * When not in netbook mode, we need to reserve space for the toolbar.
   */
  if (!netbook_mode && !priv->struts_set)
    {
      gint32 strut[4] = {0,};

      strut[2] = TOOLBAR_HEIGHT;

      meta_error_trap_push (display);

      XChangeProperty (xdpy, xwin,
                       strut_atom,
                       XA_CARDINAL, 32, PropModeReplace,
                       (unsigned char *) &strut, 4);

      meta_error_trap_pop (display, FALSE);

      priv->struts_set = TRUE;
    }
  else if (netbook_mode && priv->struts_set)
    {
      meta_error_trap_push (display);

      XDeleteProperty (xdpy, xwin, strut_atom);

      meta_error_trap_pop (display, FALSE);

      priv->struts_set = FALSE;
    }
}

static void
mnb_toolbar_constructed (GObject *self)
{
  MnbToolbarPrivate *priv = MNB_TOOLBAR (self)->priv;
  MutterPlugin      *plugin = priv->plugin;
  ClutterActor      *actor = CLUTTER_ACTOR (self);
  ClutterActor      *hbox;
  ClutterActor      *lowlight, *panel_stub;
  ClutterActor      *shadow;
  ClutterTexture    *sh_texture;
  gint               screen_width, screen_height;
  ClutterColor       low_clr = { 0, 0, 0, 0x7f };
  DBusGConnection   *conn;
  MetaScreen        *screen = mutter_plugin_get_screen (plugin);
  ClutterActor      *wgroup = mutter_get_window_group_for_screen (screen);
  gboolean           netbook_mode = moblin_netbook_use_netbook_mode (plugin);

  /*
   * Make sure our parent gets chance to do what it needs to.
   */
  if (G_OBJECT_CLASS (mnb_toolbar_parent_class)->constructed)
    G_OBJECT_CLASS (mnb_toolbar_parent_class)->constructed (self);

  if ((conn = mnb_toolbar_connect_to_dbus (MNB_TOOLBAR (self))))
    {
      priv->dbus_conn = conn;
      dbus_g_connection_register_g_object (conn, MPL_TOOLBAR_DBUS_PATH, self);
    }
  else
    {
      g_warning (G_STRLOC " DBus connection not available !!!");
    }

  mutter_plugin_query_screen_size (plugin,
                                   &priv->old_screen_width,
                                   &priv->old_screen_height);

  clutter_actor_set_reactive (actor, TRUE);

  hbox = priv->hbox = clutter_group_new ();

  g_object_set (self,
                "show-on-set-parent", FALSE,
                NULL);

  {
    MetaWorkspace *workspace = meta_screen_get_active_workspace (screen);

    mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

    if (workspace)
      {
        MetaRectangle  r;

        meta_workspace_get_work_area_all_monitors (workspace, &r);

        screen_height = r.y + r.height;
      }
  }

  priv->old_screen_width  = screen_width;
  priv->old_screen_height = screen_height;

  priv->max_panels = MAX_PANELS (screen_width);

  g_debug ("Consructing, netbook_mode %d", netbook_mode);

  if (!netbook_mode)
    {
      clutter_actor_set_x (actor, -BIG_SCREEN_PAD);
      clutter_actor_set_size (actor,
                              screen_width + 2 * BIG_SCREEN_PAD,
                              TOOLBAR_HEIGHT);
    }
  else
    clutter_actor_set_size (actor, screen_width, TOOLBAR_HEIGHT);

  lowlight = clutter_rectangle_new_with_color (&low_clr);

  clutter_actor_set_size (lowlight, screen_width, screen_height);
  clutter_container_add_actor (CLUTTER_CONTAINER (wgroup), lowlight);
  clutter_actor_hide (lowlight);
  clutter_actor_set_reactive (lowlight, TRUE);

  g_signal_connect (lowlight, "button-press-event",
                    G_CALLBACK (mnb_toolbar_lowlight_button_press_cb),
                    self);

  priv->lowlight = lowlight;

  {
    ClutterActor *spinner = priv->spinner = mnb_spinner_new ();

    panel_stub = (ClutterActor*)mx_frame_new ();
    mx_bin_set_child (MX_BIN (panel_stub), spinner);

    clutter_actor_set_size (panel_stub,
                            screen_width, screen_height / 3);
    clutter_actor_set_position (panel_stub, 0, TOOLBAR_HEIGHT);
    clutter_actor_set_name (panel_stub, "panel-stub");
    clutter_container_add_actor (CLUTTER_CONTAINER (wgroup), panel_stub);
    clutter_actor_hide (panel_stub);
    mnb_spinner_stop ((MnbSpinner*)spinner);
    priv->panel_stub = panel_stub;
  }

  /*
   * The shadow needs to go into the window group, like the lowlight.
   */
  sh_texture = mx_texture_cache_get_texture (mx_texture_cache_get_default (),
                                             THEMEDIR
                                             "/panel/panel-shadow.png");
  if (sh_texture)
    {
      shadow = mx_texture_frame_new (sh_texture,
                                     0,   /* top */
                                     200, /* right */
                                     0,   /* bottom */
                                     200  /* left */);
      clutter_actor_set_size (shadow, screen_width, TOOLBAR_SHADOW_EXTRA);
      clutter_actor_set_y (shadow, TOOLBAR_HEIGHT);
      clutter_container_add_actor (CLUTTER_CONTAINER (wgroup), shadow);
      clutter_actor_hide (shadow);
      priv->shadow = shadow;
    }

  mx_bin_set_alignment (MX_BIN (self), MX_ALIGN_START, MX_ALIGN_START);
  mx_bin_set_child (MX_BIN (self), hbox);

  /*
   * In netbook mode, we need hook to the captured signal to show/hide the
   * toolbar as required.
   */
  if (netbook_mode)
    priv->trigger_cb_id =
      g_signal_connect (mutter_plugin_get_stage (MUTTER_PLUGIN (plugin)),
                        "captured-event",
                        G_CALLBACK (mnb_toolbar_stage_captured_cb),
                        self);

  g_signal_connect (mutter_plugin_get_stage (plugin),
                    "button-press-event",
                    G_CALLBACK (mnb_toolbar_stage_input_cb),
                    self);

  mnb_toolbar_set_struts (MNB_TOOLBAR (self));

  /*
   * Hook into "show" signal on stage, to set up input regions.
   * (We cannot set up the stage here, because the overlay window, etc.,
   * is not in place until the stage is shown.)
   */
  g_signal_connect (mutter_plugin_get_stage (MUTTER_PLUGIN (plugin)),
                    "show", G_CALLBACK (mnb_toolbar_stage_show_cb),
                    self);

  mnb_toolbar_setup_panels (MNB_TOOLBAR (self));

  g_signal_connect (screen, "restacked",
                    G_CALLBACK (mnb_toolbar_screen_restacked_cb),
                    self);
}

ClutterActor*
mnb_toolbar_new (MutterPlugin *plugin)
{
  return g_object_new (MNB_TYPE_TOOLBAR,
                       "mutter-plugin", plugin, NULL);
}

static void
mnb_toolbar_activate_panel_internal (MnbToolbar        *toolbar,
                                     MnbToolbarPanel   *tp,
                                     MnbShowHideReason  reason)
{
  MnbToolbarPrivate *priv  = toolbar->priv;
  GList             *l;
  MnbPanel          *panel = tp->panel;

  g_return_if_fail (tp);

  if (panel && mnb_panel_is_mapped (panel))
    return;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *t = l->data;

      if (!t)
        continue;

      if (t->panel)
        {
          if (t != tp)
            {
              if (mnb_panel_is_mapped (t->panel))
                {
                  mnb_toolbar_set_waiting_for_panel_hide (toolbar, TRUE);
                  mnb_panel_hide (t->panel);
                }
            }
          else
            {
              mnb_toolbar_set_waiting_for_panel_show (toolbar, TRUE, TRUE);
              mnb_panel_show (t->panel);
            }
        }
      else if (t == tp && tp->button)
        {
          /*
           * Supposed to start panel that is not yet running.
           *
           * Toggle the Toolbar button.
           */
          if (!CLUTTER_ACTOR_IS_VISIBLE (toolbar))
            {
              priv->tp_to_activate = tp;
              mnb_toolbar_show (toolbar, reason);
            }
          else if (!mx_button_get_toggled (MX_BUTTON (tp->button)))
            {
              /*
               * We are not calling mnb_toolbar_show() since it is already
               * visible, but we need to set the reason for the show
               * to prevent the toolbar from hiding.
               */
              priv->reason_for_show = reason;
              mx_button_set_toggled (MX_BUTTON (tp->button), TRUE);
            }
        }
    }
}

void
mnb_toolbar_activate_panel (MnbToolbar        *toolbar,
                            const gchar       *panel_name,
                            MnbShowHideReason  reason)
{
  MnbToolbarPanel *tp = mnb_toolbar_panel_name_to_panel_internal (toolbar,
                                                                  panel_name);

  if (tp)
    mnb_toolbar_activate_panel_internal (toolbar, tp, reason);
}

void
mnb_toolbar_deactivate_panel (MnbToolbar *toolbar, const gchar *panel_name)
{
  MnbPanel        *panel;
  MnbToolbarPanel *tp = mnb_toolbar_panel_name_to_panel_internal (toolbar,
                                                                  panel_name);

  if (!tp || !tp->panel)
    return;

  panel = tp->panel;

  if (!mnb_panel_is_mapped (panel))
    return;

  mnb_panel_hide (panel);
}

void
mnb_toolbar_unload_panel (MnbToolbar *toolbar, const gchar *panel_name)
{
  MnbPanel        *panel;
  MnbToolbarPanel *tp = mnb_toolbar_panel_name_to_panel_internal (toolbar,
                                                                  panel_name);

  if (!tp || !tp->panel)
    return;

  panel = tp->panel;

  if (!MNB_IS_PANEL_OOP (panel))
    {
      g_warning ("Panel %s cannot be unloaded (only OOP panels can be).",
                 panel_name);
      return;
    }

  tp->unloaded = TRUE;

  mnb_panel_oop_unload ((MnbPanelOop*)panel);
}

void
mnb_toolbar_load_panel (MnbToolbar *toolbar, const gchar *panel_name)
{
  MnbToolbarPrivate *priv  = toolbar->priv;
  MnbToolbarPanel   *tp;
  gchar             *dbus_name;

  tp = mnb_toolbar_panel_name_to_panel_internal (toolbar, panel_name);

  if (!tp || !tp->panel)
    return;

  tp->unloaded = FALSE;

  dbus_name = g_strconcat (MPL_PANEL_DBUS_NAME_PREFIX, panel_name, NULL);

  mnb_toolbar_ping_panel_oop (priv->dbus_conn, dbus_name);

  g_free (dbus_name);
}

/* returns NULL if no panel active */
const gchar *
mnb_toolbar_get_active_panel_name (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv  = toolbar->priv;
  GList             *l;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->panel && mnb_panel_is_mapped (tp->panel))
        return tp->name;
    }

  return NULL;
}

/* Are we animating in or out */
gboolean
mnb_toolbar_in_transition (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  return (priv->in_show_animation || priv->in_hide_animation);
}

/*
 * Sets a flag indicating whether the toolbar should hide when the pointer is
 * outside of the toolbar input zone -- this is the normal behaviour, but
 * needs to be disabled, for example, when the toolbar is opened using the
 * kbd shortcut.
 *
 * This flag gets cleared automatically when the panel is hidden.
 */
void
mnb_toolbar_set_dont_autohide (MnbToolbar *toolbar, gboolean dont)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  priv->dont_autohide = dont;
}

/*
 * Machinery for showing and hiding the panel in response to pointer position.
 */

/*
 * The timeout callback that shows the panel if the pointer stayed long enough
 * in the trigger region.
 */
static gboolean
mnb_toolbar_trigger_timeout_cb (gpointer data)
{
  MnbToolbar *toolbar = MNB_TOOLBAR (data);

  mnb_toolbar_show (toolbar, MNB_SHOW_HIDE_BY_MOUSE);
  toolbar->priv->trigger_timeout_id = 0;

  return FALSE;
}

/*
 * Changes the size of the trigger region (we increase the size of the trigger
 * region while wating for the trigger timeout to reduce effects of jitter).
 */
static void
mnb_toolbar_trigger_region_set_height (MnbToolbar *toolbar, gint height)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MutterPlugin      *plugin = priv->plugin;
  gint               screen_width, screen_height;

  mutter_plugin_query_screen_size (plugin, &screen_width, &screen_height);

  if (priv->trigger_region != NULL)
    mnb_input_manager_remove_region (priv->trigger_region);

  priv->trigger_region
    = mnb_input_manager_push_region (0,
                                     0,
                                     screen_width,
                                     TOOLBAR_TRIGGER_THRESHOLD + height,
                                     FALSE, MNB_INPUT_LAYER_PANEL);
}

/*
 * Returns TRUE if one of the out of process panels is showing; used to
 * block Toolbar closing on stage leave event.
 *
 * (NB: returns FALSE for panels that are not of MnbPanel type!)
 */
static gboolean
mnb_toolbar_panels_showing (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;

  if (priv->waiting_for_panel_hide)
    return FALSE;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp    = l->data;

      if (!tp || !tp->panel)
        continue;

      if (mnb_panel_is_mapped (tp->panel))
        return TRUE;
    }

  return FALSE;
}

/*
 * Callback for ClutterStage::captured-event singal.
 *
 * Processes CLUTTER_ENTER and CLUTTER_LEAVE events and shows/hides the
 * panel as required.
 */
static gboolean
mnb_toolbar_stage_captured_cb (ClutterActor *stage,
                               ClutterEvent *event,
                               gpointer      data)
{
  MnbToolbar        *toolbar = MNB_TOOLBAR (data);
  MnbToolbarPrivate *priv    = toolbar->priv;
  gboolean           show_toolbar;

  /*
   * Shortcircuit what we can:
   *
   * a) toolbar is disabled (e.g., in lowlight),
   * b) the event is something other than enter/leave
   * c) we got an enter event on something other than stage,
   * d) we got a leave event bug are showing panels, or waiting for panel to
   *    show
   * e) we are already animating.
   *
   * Split into multiple statments for readability.
   */

  if (priv->disabled)
    {
      /* g_debug (G_STRLOC " leaving early"); */
      return FALSE;
    }

  if (!(event->type == CLUTTER_ENTER || event->type == CLUTTER_LEAVE))
    {
      /* g_debug (G_STRLOC " leaving early"); */
      return FALSE;
    }

  if ((event->type == CLUTTER_ENTER) &&
      (event->crossing.x > priv->old_screen_width - CLOSE_BUTTON_GUARD_WIDTH))
    {
      /* g_debug (G_STRLOC " leaving early"); */
      return FALSE;
    }

  if ((event->type == CLUTTER_ENTER) && (event->crossing.source != stage))
    {
      /* g_debug (G_STRLOC " leaving early"); */
      return FALSE;
    }

  if ((event->type == CLUTTER_ENTER) &&
      event->crossing.y > TOOLBAR_TRIGGER_THRESHOLD+TOOLBAR_TRIGGER_ADJUSTMENT)
    {
      /* g_debug (G_STRLOC " leaving early"); */
      return FALSE;
    }

  if ((event->type == CLUTTER_LEAVE) &&
      (priv->waiting_for_panel_show ||
       priv->dont_autohide ||
       mnb_toolbar_panels_showing (toolbar) ||
       !mnb_toolbar_check_for_windows (toolbar)))
    {
      /* g_debug (G_STRLOC " leaving early (waiting %d, dont_autohide %d)", */
      /*          priv->waiting_for_panel, priv->dont_autohide); */
      return FALSE;
    }

#if 0
  if (mnb_toolbar_in_transition (toolbar))
    {
      /* g_debug (G_STRLOC " leaving early"); */
      return FALSE;
    }
#endif

  /*
   * This is when we want to show the toolbar:
   *
   *  a) we got an enter event on stage,
   *
   *    OR
   *
   *  b) we got a leave event on stage at the very top of the screen (when the
   *     pointer is at the position that coresponds to the top of the window,
   *     it is considered to have left the window; when the user slides pointer
   *     to the top, we get an enter event immediately followed by a leave
   *     event).
   *
   *  In all cases, only if the toolbar is not already visible.
   */
  show_toolbar  = (event->type == CLUTTER_ENTER);
  show_toolbar |= ((event->type == CLUTTER_LEAVE) && (event->crossing.y == 0));
  show_toolbar &= !CLUTTER_ACTOR_IS_MAPPED (toolbar);

  if (show_toolbar)
    {
      /*
       * If any fullscreen apps are present, then bail out.
       */
      if (moblin_netbook_fullscreen_apps_present (priv->plugin))
            return FALSE;

      /*
       * Only do this once; if the timeout is already installed, we wait
       * (see bug 3949)
       */
      if (!priv->trigger_timeout_id)
        {
          /*
           * Increase sensitivity -- increasing size of the trigger zone while
           * the timeout reduces the effect of a shaking hand.
           */
          mnb_toolbar_trigger_region_set_height (toolbar,
                                                 TOOLBAR_TRIGGER_ADJUSTMENT);

          priv->trigger_timeout_id =
            g_timeout_add (TOOLBAR_TRIGGER_THRESHOLD_TIMEOUT,
                           mnb_toolbar_trigger_timeout_cb, toolbar);
        }
    }
  else if (event->type == CLUTTER_LEAVE)
    {
      /*
       * The most reliable way of detecting that the pointer is leaving the
       * stage is from the related actor -- no related == pointer gone
       * elsewhere.
       */
      if (event->crossing.related != NULL)
        return FALSE;

      if (priv->trigger_timeout_id)
        {
          /*
           * Pointer left us before the required timeout triggered; clean up.
           */
          mnb_toolbar_trigger_region_set_height (toolbar, 0);
          g_source_remove (priv->trigger_timeout_id);
          priv->trigger_timeout_id = 0;
        }
      else if (CLUTTER_ACTOR_IS_MAPPED (toolbar) &&
               !priv->waiting_for_panel_hide)
        {
          mnb_toolbar_trigger_region_set_height (toolbar, 0);
          mnb_toolbar_hide (toolbar, MNB_SHOW_HIDE_BY_MOUSE);
        }
    }

  return FALSE;
}

/*
 * Handles ButtonPress events on stage.
 *
 * Used to hide the toolbar if the user clicks directly on stage.
 */
static gboolean
mnb_toolbar_stage_input_cb (ClutterActor *stage,
                            ClutterEvent *event,
                            gpointer      data)
{
  MnbToolbar *toolbar = MNB_TOOLBAR (data);

  if (event->type == CLUTTER_BUTTON_PRESS)
    {
      if (mnb_toolbar_in_transition (toolbar))
        return FALSE;

      if (!CLUTTER_ACTOR_IS_MAPPED (toolbar))
        return FALSE;

      /*
       * If there are no singificant windows, we do not hide the toolbar (we
       * do not hide the panel either, the user must explicitely switch to a
       * different panel).
       */
      if (mnb_toolbar_check_for_windows (toolbar))
        mnb_toolbar_hide (toolbar, MNB_SHOW_HIDE_BY_MOUSE);
    }

  return FALSE;
}

static void
mnb_toolbar_ensure_size_for_screen (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv      = toolbar->priv;
  MetaScreen        *screen    = mutter_plugin_get_screen (priv->plugin);
  MetaWorkspace     *workspace = meta_screen_get_active_workspace (screen);
  gint               screen_width, screen_height;
  GList             *l;
  gboolean           netbook_mode;
  gint               width = priv->old_screen_width;

  netbook_mode = moblin_netbook_use_netbook_mode (priv->plugin);

  mutter_plugin_query_screen_size (priv->plugin, &screen_width, &screen_height);

  if (workspace)
    {
      MetaRectangle  r;

      meta_workspace_get_work_area_all_monitors (workspace, &r);

      screen_height = r.y + r.height;
    }

  if (priv->old_screen_width  == screen_width &&
      priv->old_screen_height == screen_height)
    {
      return;
    }

  /*
   * Ensure that the handler for showing Toolbar in small-screen mode is
   * (dis)connected as appropriate.
   */
  if (netbook_mode && !priv->trigger_cb_id)
    {
      priv->trigger_cb_id =
        g_signal_connect (mutter_plugin_get_stage (priv->plugin),
                          "captured-event",
                          G_CALLBACK (mnb_toolbar_stage_captured_cb),
                          toolbar);
    }
  else if (!netbook_mode && priv->trigger_cb_id)
    {
      g_signal_handler_disconnect (mutter_plugin_get_stage (priv->plugin),
                                   priv->trigger_cb_id);
      priv->trigger_cb_id = 0;
    }

  /*
   * NB: This might trigger another change to the workspace area, but that
   *     one will become a no-op.
   */
  mnb_toolbar_set_struts (toolbar);

  /*
   * First, handle only stuff that depends only on width changes.
   */
  if (priv->old_screen_width != screen_width)
    {
      MutterPlugin               *plugin = priv->plugin;
      MoblinNetbookPluginPrivate *ppriv = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;

      width = screen_width;

      priv->max_panels = MAX_PANELS (screen_width);

      /*
       * First of all, sort out the size of the Toolbar assets to match the
       * new screen.
       */
      if (!netbook_mode)
        {
          g_debug ("Moving Toolbar to %d for bigger screen", BIG_SCREEN_PAD);

          width += BIG_SCREEN_PAD * 2;
          clutter_actor_set_x ((ClutterActor*)toolbar, -BIG_SCREEN_PAD);
        }
      else
        clutter_actor_set_x ((ClutterActor*) toolbar, 0);

      clutter_actor_set_width ((ClutterActor*)toolbar, width);
      clutter_actor_set_width (priv->shadow, screen_width);

      /*
       * Now re-read the gconf key holding the panel order; this ensures that
       * all the panels that can be loaded are. This will also take care of
       * the panel positions.
       */
      gconf_client_notify (ppriv->gconf_client, KEY_ORDER);
    }

  clutter_actor_set_size (priv->lowlight, screen_width, screen_height);

  for (l = priv->panels; l; l = l->next)
  {
    MnbToolbarPanel *tp = l->data;

    if (!tp || !tp->panel)
      continue;

    /*
     * The panel size is the overall size of the panel actor; the height of the
     * actor includes the shadow, so we need to add the extra bit by which the
     * shadow protrudes below the actor.
     */
    mnb_panel_set_size (tp->panel,
                        screen_width - TOOLBAR_X_PADDING * 2,
                        screen_height - TOOLBAR_HEIGHT - 8);
  }

  if (priv->input_region)
    {
      mnb_input_manager_remove_region_without_update (priv->input_region);

      priv->input_region =
        mnb_input_manager_push_region (0, 0,
                                       screen_width,
                                       TOOLBAR_HEIGHT + 10,
                                       FALSE, MNB_INPUT_LAYER_PANEL);
    }

  priv->old_screen_width  = screen_width;
  priv->old_screen_height = screen_height;
}

static void
mnb_toolbar_workarea_changed_cb (MetaScreen *screen, MnbToolbar *toolbar)
{
  mnb_toolbar_ensure_size_for_screen (toolbar);
}

static void
mnb_toolbar_stage_allocation_cb (ClutterActor *stage,
                                 GParamSpec   *pspec,
                                 MnbToolbar   *toolbar)
{
  mnb_toolbar_ensure_size_for_screen (toolbar);

  /*
   * This is a one-off to deal with the inital stage allocation; screen size
   * changes are handled by the workarea_changed_cb.
   */
  g_signal_handlers_disconnect_by_func (stage,
                                        mnb_toolbar_stage_allocation_cb,
                                        toolbar);
}

static void
mnb_toolbar_alt_f2_key_handler (MetaDisplay    *display,
                                MetaScreen     *screen,
                                MetaWindow     *window,
                                XEvent         *event,
                                MetaKeyBinding *binding,
                                gpointer        data)
{
  MnbToolbar *toolbar = MNB_TOOLBAR (data);
  MoblinNetbookPluginPrivate *ppriv =
    MOBLIN_NETBOOK_PLUGIN (toolbar->priv->plugin)->priv;
  MnbToolbarPanel *tp;

  if (CLUTTER_ACTOR_IS_MAPPED (ppriv->notification_urgent))
    return;

  tp = mnb_toolbar_panel_name_to_panel_internal (toolbar,
                                                 "moblin-panel-applications");

  if (tp)
    mnb_toolbar_activate_panel_internal (toolbar, tp, MNB_SHOW_HIDE_BY_KEY);
}

/*
 * Callback for ClutterStage::show() signal.
 *
 * Carries out set up that can only be done once the stage is shown and the
 * associated X resources are in place.
 */
static void
mnb_toolbar_stage_show_cb (ClutterActor *stage, MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  MutterPlugin      *plugin = priv->plugin;
  XWindowAttributes  attr;
  long               event_mask;
  Window             xwin;
  MetaScreen        *screen;
  Display           *xdpy;
  ClutterStage      *stg;
  MnbPanel          *myzone;

  xdpy   = mutter_plugin_get_xdisplay (plugin);
  stg    = CLUTTER_STAGE (mutter_plugin_get_stage (plugin));
  screen = mutter_plugin_get_screen (plugin);

  /*
   * Set up the stage input region
   */
  mnb_toolbar_trigger_region_set_height (toolbar, 0);

  /*
   * Make sure we are getting enter and leave events for stage (set up both
   * stage and overlay windows).
   */
  xwin       = clutter_x11_get_stage_window (stg);
  event_mask = EnterWindowMask | LeaveWindowMask;

  if (XGetWindowAttributes (xdpy, xwin, &attr))
    {
      event_mask |= attr.your_event_mask;
    }

  XSelectInput (xdpy, xwin, event_mask);

  xwin = mutter_get_overlay_window (screen);
  event_mask = EnterWindowMask | LeaveWindowMask;

  if (XGetWindowAttributes (xdpy, xwin, &attr))
    {
      event_mask |= attr.your_event_mask;
    }

  XSelectInput (xdpy, xwin, event_mask);

  priv->shown = TRUE;

  /*
   * Show Myzone
   */
  if (!priv->shown_myzone &&
      (myzone = mnb_toolbar_panel_name_to_panel (toolbar, MPL_PANEL_MYZONE)))
    {
      /*
       * We can only do this if there are no modal windows showing otherwise
       * the zone will cover up the modal window; the user can then open another
       * panel, such as the applications, launch a new application, at which
       * point we switch to the new zone with the new application, but the modal
       * window on the original zone will still have focus, and the user will
       * have no idea why her kbd is not working. (Other than on startup this
       * should not be an issue, since will automatically hide the Shell when
       * the modal window pops up.
       */
      if (!moblin_netbook_modal_windows_present (plugin, -1))
        {
          priv->shown_myzone = TRUE;
          mnb_panel_show (myzone);
        }
    }

#if 0
  g_timeout_add_seconds (TOOLBAR_AUTOSTART_DELAY,
                         mnb_toolbar_autostart_panels_cb,
                         toolbar);
#endif

  g_signal_connect (stage, "notify::allocation",
                    G_CALLBACK (mnb_toolbar_stage_allocation_cb),
                    toolbar);

  /*
   * We need to be called last, after the handler in moblin-netbook.c has
   * fixed up the screen mode to either small-screen or bigger-screen.
   */
  g_signal_connect_after (screen,
                          "workareas-changed",
                          G_CALLBACK (mnb_toolbar_workarea_changed_cb),
                          toolbar);

  meta_keybindings_set_custom_handler ("panel_run_dialog",
                                       mnb_toolbar_alt_f2_key_handler,
                                       toolbar, NULL);
}

/*
 * Sets the disabled flag which indicates that the toolbar should not be
 * shown.
 *
 * Unlike the dont_autohide flag, this setting remains in force until
 * explicitely cleared.
 */
void
mnb_toolbar_set_disabled (MnbToolbar *toolbar, gboolean disabled)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  priv->disabled = disabled;
}

MnbPanel *
mnb_toolbar_find_panel_for_xid (MnbToolbar *toolbar, guint xid)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;
      MnbPanel        *panel;

      if (!tp || !tp->panel || !MNB_IS_PANEL_OOP (tp->panel))
        continue;

      panel = tp->panel;

      if (xid == mnb_panel_oop_get_xid ((MnbPanelOop*)panel))
        {
          return panel;
        }
    }

  return NULL;
}

/*
 * Returns the active panel, or NULL, if no panel is active.
 */
MnbPanel *
mnb_toolbar_get_active_panel (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;
  GList             *l;

  if (!CLUTTER_ACTOR_IS_MAPPED (toolbar))
    return NULL;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->panel && mnb_panel_is_mapped (tp->panel))
        return tp->panel;
    }

  return NULL;
}

gboolean
mnb_toolbar_is_waiting_for_panel (MnbToolbar *toolbar)
{
  MnbToolbarPrivate *priv = toolbar->priv;

  return (priv->waiting_for_panel_show || priv->waiting_for_panel_hide);
}

void
mnb_toolbar_foreach_panel (MnbToolbar        *toolbar,
                           MnbToolbarCallback callback,
                           gpointer           data)
{
  MnbToolbarPrivate *priv  = toolbar->priv;
  GList             *l;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->panel)
        callback (tp->panel, data);
    }
}

gboolean
mnb_toolbar_owns_window (MnbToolbar *toolbar, MutterWindow *mcw)
{
  MnbToolbarPrivate *priv  = toolbar->priv;
  GList             *l;

  if (!mcw)
    return FALSE;

  for (l = priv->panels; l; l = l->next)
    {
      MnbToolbarPanel *tp = l->data;

      if (tp && tp->panel && MNB_IS_PANEL_OOP (tp->panel))
        if (mnb_panel_oop_owns_window ((MnbPanelOop*)tp->panel, mcw))
          return TRUE;
    }

  return FALSE;
}

/*
 * Creates MnbToolbarPanel object from a desktop file.
 *
 * desktop is the desktop file name without the .desktop prefix, which will be
 * loaded from standard location.
 */
static MnbToolbarPanel *
mnb_toolbar_make_panel_from_desktop (MnbToolbar *toolbar, const gchar *desktop)
{
  gchar           *path;
  GKeyFile        *kfile;
  GError          *error = NULL;
  MnbToolbarPanel *tp = NULL;

  path = g_strconcat (PANELSDIR, "/", desktop, ".desktop", NULL);

  kfile = g_key_file_new ();

  if (!g_key_file_load_from_file (kfile, path, G_KEY_FILE_NONE, &error))
    {
      g_warning ("Failed to load %s: %s", path, error->message);
      g_clear_error (&error);
    }
  else
    {
      gchar    *s;
      gboolean  b;
      gboolean  builtin = FALSE;

      tp = g_new0 (MnbToolbarPanel, 1);

      tp->name = g_strdup (desktop);

      b = g_key_file_get_boolean (kfile,
                                  G_KEY_FILE_DESKTOP_GROUP,
                                  "X-Moblin-Panel-Optional",
                                  &error);

      /*
       * If the key does not exist (i.e., there is an error), the panel is
       * not required
       */
      if (!error)
        tp->required = !b;
      else
        g_clear_error (&error);

      b = g_key_file_get_boolean (kfile,
                                  G_KEY_FILE_DESKTOP_GROUP,
                                  "X-Moblin-Panel-Windowless",
                                  &error);

      /*
       * If the key does not exist (i.e., there is an error), the panel is
       * not windowless
       */
      if (!error)
        tp->windowless = b;
      else
        g_clear_error (&error);

      s = g_key_file_get_locale_string (kfile,
                                        G_KEY_FILE_DESKTOP_GROUP,
                                        G_KEY_FILE_DESKTOP_KEY_NAME,
                                        NULL,
                                        NULL);

      tp->tooltip = s;

      s = g_key_file_get_string (kfile,
                                 G_KEY_FILE_DESKTOP_GROUP,
                                 "X-Moblin-Panel-Button-Style",
                                        NULL);

      if (!s)
        {
          /*
           * FIXME -- temporary hack
           */
          if (!strcmp (desktop, "moblin-panel-myzone"))
            tp->button_style = g_strdup ("myzone-button");
          else
            tp->button_style = g_strdup_printf ("%s-button", desktop);
        }
      else
        tp->button_style = s;

      s = g_key_file_get_string (kfile,
                                 G_KEY_FILE_DESKTOP_GROUP,
                                 "X-Moblin-Panel-Type",
                                 NULL);

      if (s)
        {
          if (!strcmp (s, "applet"))
            tp->type = MNB_TOOLBAR_PANEL_APPLET;
          else if (!strcmp (s, "clock"))
            tp->type = MNB_TOOLBAR_PANEL_CLOCK;

          g_free (s);
        }

      if (!builtin)
        {
          s = g_key_file_get_string (kfile,
                                     G_KEY_FILE_DESKTOP_GROUP,
                                     "X-Moblin-Panel-Stylesheet",
                                     NULL);

          tp->button_stylesheet = s;

          s = g_key_file_get_string (kfile,
                                     G_KEY_FILE_DESKTOP_GROUP,
                                     "X-Moblin-Service",
                                     NULL);

          tp->service = s;
        }

      /*
       * Sanity check
       */
      if (!builtin && !tp->service)
        {
          g_warning ("Panel desktop file %s did not contain dbus service",
                     desktop);

          mnb_toolbar_panel_destroy (tp);
          tp = NULL;
        }
    }

  g_key_file_free (kfile);

  return tp;
}

static gint
mnb_toolbar_find_panel_by_name (const MnbToolbarPanel *tp, const gchar *name)
{
  if (!tp || !tp->name)
    return -1;

  return strcmp (tp->name, name);
}

/*
 * Sorts out panels to match the new order, removing / adding any panels
 * in the process.
 *
 * the strings parameter identifies the list type; if TRUE, the list data are
 * strings, if FALSE they are GConfValues holding a string.
 */
static void
mnb_toolbar_fixup_panels (MnbToolbar *toolbar, GSList *order, gboolean strings)
{
  MnbToolbarPrivate *priv  = toolbar->priv;
  GSList            *l;
  GList             *panels = priv->panels;
  gint               n_panels = 0;

  for (l = order; l; l = l->next)
    {
      const gchar     *name;
      GList           *k;
      MnbToolbarPanel *tp;

      if (!strings)
        {
          GConfValue *value = l->data;

          name = gconf_value_get_string (value);
        }
      else
        name = l->data;

      k = g_list_find_custom (panels, name,
                              (GCompareFunc) mnb_toolbar_find_panel_by_name);

      if (!k)
        tp = mnb_toolbar_make_panel_from_desktop (toolbar, name);
      else
        tp = k->data;

      if (!tp)
        continue;

      tp->current = TRUE;

      /*
       * Move to the start of the list.
       */
      if (k)
        panels = g_list_delete_link (panels, k);

      panels = g_list_prepend (panels, tp);
    }

  priv->panels = panels;

  /*
   * Count how many panels we are trying to put on the toolbar.
   */
  while (panels)
    {
      MnbToolbarPanel *tp = panels->data;

      if (tp->type == MNB_TOOLBAR_PANEL_NORMAL && (tp->current || tp->required))
        ++n_panels;

      panels = panels->next;
    }

  panels = priv->panels;

  /*
   * Destroy any panels that are no longer current, or panels that do not
   * fit on the Toolbar, making sure we keep any required panels.
   */
  while (panels)
    {
      MnbToolbarPanel *tp = panels->data;
      MnbPanel        *panel;
      gboolean         regular;

      if (!tp)
        {
          panels = panels->next;
          continue;
        }

      /*
       * if the panel is either not a regular panel, or the current number of
       * panels we are trying to put on the toolbar is below the max thershold,
       * move on.
       */
      if ((tp->current || tp->required) &&
          (n_panels <= priv->max_panels ||
           tp->type != MNB_TOOLBAR_PANEL_NORMAL))
        {
          panels = panels->next;
          continue;
        }

      /*
       * If this is a regular panel that is required and we are over the max
       * threshold, we need to find some other panel to remove in its stead.
       */
      if (tp->type == MNB_TOOLBAR_PANEL_NORMAL &&
          tp->required && n_panels > priv->max_panels)
        {
          /*
           * Find some other panel to remove instead ... the panel list is
           * at this point in reverse order, so we look for the panel forward.
           */
          GList *k = panels->next;

          while (k)
            {
              MnbToolbarPanel *t = k->data;

              if (t->type == MNB_TOOLBAR_PANEL_NORMAL &&
                  !t->required && t->current)
                {
                  tp = t;
                  break;
                }

              k = k->next;
            }
        }

      panel = tp->panel;

      regular = (tp->type == MNB_TOOLBAR_PANEL_NORMAL);

      mnb_toolbar_dispose_of_button (toolbar, tp);
      mnb_toolbar_dispose_of_panel  (toolbar, tp, FALSE);

      priv->panels = g_list_remove (priv->panels, tp);
      mnb_toolbar_panel_destroy (tp);

      if (MNB_IS_PANEL_OOP (panel))
        mnb_panel_oop_unload ((MnbPanelOop*)panel);

      /*
       * Start from the beginning.
       */
      panels = priv->panels;

      if (regular)
        --n_panels;
    }

  panels = priv->panels;

  /*
   * Now move any required panels that were not on the list to the start.
   */
  while (panels)
    {
      MnbToolbarPanel *tp   = panels->data;
      GList           *next = panels->next;

      if (!tp->current)
        {
          tp->current = TRUE;

          priv->panels = g_list_remove (priv->panels, tp);
          priv->panels = g_list_prepend (priv->panels, tp);
        }

      panels = next;
    }

  priv->panels = g_list_reverse (priv->panels);

  /*
   * Now fix up the button positions; also clear the current flag on the
   * remaining panels
   */
  for (panels = priv->panels; panels; panels = panels->next)
    {
      MnbToolbarPanel *tp = panels->data;

      if (!tp)
        continue;

      if (tp->current && !tp->button)
        mnb_toolbar_append_button (toolbar, tp);

      tp->current = FALSE;

      mnb_toolbar_ensure_button_position (toolbar, tp);
    }
}

static void
mnb_toolbar_panel_gconf_key_changed_cb (GConfClient *client,
                                        guint        cnxn_id,
                                        GConfEntry  *entry,
                                        gpointer     data)
{
  MnbToolbar  *toolbar = MNB_TOOLBAR (data);
  GConfValue  *value;
  const gchar *key;

  key = gconf_entry_get_key (entry);

  if (!key)
    {
      g_warning (G_STRLOC ": no key!");
      return;
    }

  value = gconf_entry_get_value (entry);

  if (!value)
    {
      g_warning (G_STRLOC ": no value!");
      return;
    }

  if (!strcmp (key, KEY_ORDER))
    {
      GSList *order;

      if (value->type != GCONF_VALUE_LIST)
        {
          g_warning (G_STRLOC ": %s does not contain a list!", KEY_ORDER);
          return;
        }

      if (gconf_value_get_list_type (value) != GCONF_VALUE_STRING)
        {
          g_warning (G_STRLOC ": %s list does not contain strings!", KEY_ORDER);
          return;
        }

      order = gconf_value_get_list (value);

      mnb_toolbar_fixup_panels (toolbar, order, FALSE);
      return;
    }

  g_warning (G_STRLOC ": Unknown key %s", key);
}

static void
mnb_toolbar_load_gconf_settings (MnbToolbar *toolbar)
{
  MnbToolbarPrivate          *priv   = toolbar->priv;
  MutterPlugin               *plugin = priv->plugin;
  MoblinNetbookPluginPrivate *ppriv  = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  GConfClient                *client = ppriv->gconf_client;
  GSList                     *order;
  gint                        i;
  const gchar                *required[4] = {"moblin-panel-myzone",
                                             "moblin-panel-applications",
                                             "moblin-panel-zones",
                                             "carrick"};

  order = gconf_client_get_list (client, KEY_ORDER, GCONF_VALUE_STRING, NULL);

  /*
   * Ensure that the required panels are in the list; if not, we insert them
   * at the end.
   */
  for (i = 0; i < G_N_ELEMENTS (required); ++i)
    {
      GSList *l = g_slist_find_custom (order,
                                       required[i],
                                       (GCompareFunc)g_strcmp0);

      if (!l)
        {
          order = g_slist_append (order, g_strdup (required[i]));
        }
    }

  mnb_toolbar_fixup_panels (toolbar, order, TRUE);

  /*
   * Clean up
   */
  g_slist_foreach (order, (GFunc)g_free, NULL);
  g_slist_free (order);
}

static void
mnb_toolbar_setup_gconf (MnbToolbar *toolbar)
{
  MnbToolbarPrivate          *priv   = toolbar->priv;
  MutterPlugin               *plugin = priv->plugin;
  MoblinNetbookPluginPrivate *ppriv  = MOBLIN_NETBOOK_PLUGIN (plugin)->priv;
  GConfClient                *client = ppriv->gconf_client;
  GError                     *error  = NULL;

  gconf_client_add_dir (client,
                        KEY_DIR,
                        GCONF_CLIENT_PRELOAD_NONE,
                        &error);

  if (error)
    {
      g_warning (G_STRLOC ": Error when adding directory for notification: %s",
                 error->message);
      g_clear_error (&error);
    }

  gconf_client_notify_add (client,
                           KEY_DIR,
                           mnb_toolbar_panel_gconf_key_changed_cb,
                           toolbar,
                           NULL,
                           &error);

  mnb_toolbar_load_gconf_settings (toolbar);
}
