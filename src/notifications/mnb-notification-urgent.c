/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008, 2010 Intel Corp.
 *
 * Author: Matthew Allum <mallum@linux.intel.com>
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

#include "../moblin-netbook.h"
#include "mnb-notification-urgent.h"
#include "mnb-notification.h"
#include "moblin-netbook-notify-store.h"
#include "math.h"

G_DEFINE_TYPE (MnbNotificationUrgent,   \
               mnb_notification_urgent, \
               MX_TYPE_WIDGET)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
   MNB_TYPE_NOTIFICATION_URGENT,    \
   MnbNotificationUrgentPrivate))

#define URGENT_WIDTH 400
#define FADE_DURATION 300

static void last_focused_weak_notify (gpointer data, GObject *object);

struct _MnbNotificationUrgentPrivate {
  ClutterGroup *notifiers;
  ClutterActor   *active;
  gint          n_notifiers;
  ClutterActor *last_focused;
};

enum
{
  SYNC_INPUT_REGION,
  LAST_SIGNAL
};

static guint urgent_signals[LAST_SIGNAL] = { 0 };

static void
mnb_notification_urgent_get_property (GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
mnb_notification_urgent_set_property (GObject *object, guint property_id,
                                       const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
mnb_notification_urgent_dispose (GObject *object)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (object)->priv;

  if (priv->last_focused)
    {
      g_object_weak_unref (G_OBJECT (priv->last_focused),
                           last_focused_weak_notify,
                           object);
      priv->last_focused = NULL;
    }

  G_OBJECT_CLASS (mnb_notification_urgent_parent_class)->dispose (object);
}

static void
mnb_notification_urgent_finalize (GObject *object)
{
  G_OBJECT_CLASS (mnb_notification_urgent_parent_class)->finalize (object);
}

static void
mnb_notification_urgent_paint (ClutterActor *actor)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (actor)->priv;

  if (priv->notifiers && CLUTTER_ACTOR_IS_MAPPED (priv->notifiers))
      clutter_actor_paint (CLUTTER_ACTOR (priv->notifiers));
}

static void
mnb_notification_urgent_map (ClutterActor *actor)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (actor)->priv;

  CLUTTER_ACTOR_CLASS (mnb_notification_urgent_parent_class)->map (actor);

  if (priv->notifiers)
      clutter_actor_map (CLUTTER_ACTOR (priv->notifiers));
}

static void
mnb_notification_urgent_unmap (ClutterActor *actor)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (actor)->priv;

  CLUTTER_ACTOR_CLASS (mnb_notification_urgent_parent_class)->unmap (actor);

  if (priv->notifiers)
      clutter_actor_unmap (CLUTTER_ACTOR (priv->notifiers));
}

static void
mnb_notification_urgent_get_preferred_width (ClutterActor *actor,
                                             gfloat        for_height,
                                             gfloat       *min_width,
                                             gfloat       *natural_width)
{
  *min_width = URGENT_WIDTH;
  *natural_width = URGENT_WIDTH;
}

static void
mnb_notification_urgent_get_preferred_height (ClutterActor *actor,
                                              gfloat        for_width,
                                              gfloat       *min_height,
                                              gfloat       *natural_height)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (actor)->priv;

  *min_height = 0;
  *natural_height = 0;

  if (priv->notifiers)
    {
      gfloat m_height, p_height;

      clutter_actor_get_preferred_height (CLUTTER_ACTOR (priv->notifiers),
                                          URGENT_WIDTH, &m_height, &p_height);

      *min_height += m_height;
      *natural_height += p_height;
    }
}


static void
mnb_notification_urgent_allocate (ClutterActor          *actor,
                                  const ClutterActorBox *box,
                                  ClutterAllocationFlags flags)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (actor)->priv;
  ClutterActorClass *klass;

  klass = CLUTTER_ACTOR_CLASS (mnb_notification_urgent_parent_class);

  klass->allocate (actor, box, flags);

  if (priv->notifiers)
    {
      gfloat m_height, p_height;
      ClutterActorBox notifier_box = { 0, };

      clutter_actor_get_preferred_height (CLUTTER_ACTOR (priv->notifiers),
                                          URGENT_WIDTH, &m_height, &p_height);

      notifier_box.x2 = URGENT_WIDTH;
      notifier_box.y2 = floor(p_height);

      clutter_actor_allocate (CLUTTER_ACTOR(priv->notifiers),
                              &notifier_box, flags);
    }
}

static void
mnb_notification_urgent_pick (ClutterActor       *actor,
                               const ClutterColor *color)
{
  CLUTTER_ACTOR_CLASS (mnb_notification_urgent_parent_class)->pick (actor,
                                                                     color);
  mnb_notification_urgent_paint (actor);
}

static void
dismiss_all_foreach (ClutterActor *notifier)
{
  g_signal_emit_by_name (notifier, "closed", 0);
}

static gboolean
mnb_notification_urgent_key_press_event (ClutterActor    *actor,
                                         ClutterKeyEvent *event)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (actor)->priv;

  switch (event->keyval)
    {
    case CLUTTER_Escape:
      clutter_container_foreach (CLUTTER_CONTAINER(priv->notifiers),
                                 (ClutterCallback)dismiss_all_foreach,
                                 NULL);
      break;
    default:
      {
        GList       *notifiers;
        GList       *last;
        KeySym       keysym = event->keyval;
        const gchar *action;

        notifiers =
          clutter_container_get_children (CLUTTER_CONTAINER(priv->notifiers));

        last = g_list_last (notifiers);

        if (last &&
            (action = mnb_notification_find_action_for_keysym (last->data,
                                                               keysym)))
          {
            g_signal_emit_by_name (last->data, "action", action);
          }

        g_list_free (notifiers);
      }
      break;
    }

  return TRUE;
}

static void
mnb_notification_urgent_class_init (MnbNotificationUrgentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *clutter_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MnbNotificationUrgentPrivate));

  object_class->get_property = mnb_notification_urgent_get_property;
  object_class->set_property = mnb_notification_urgent_set_property;
  object_class->dispose = mnb_notification_urgent_dispose;
  object_class->finalize = mnb_notification_urgent_finalize;

  clutter_class->allocate = mnb_notification_urgent_allocate;
  clutter_class->paint = mnb_notification_urgent_paint;
  clutter_class->pick = mnb_notification_urgent_pick;
  clutter_class->get_preferred_height
    = mnb_notification_urgent_get_preferred_height;
  clutter_class->get_preferred_width
    = mnb_notification_urgent_get_preferred_width;
  clutter_class->map = mnb_notification_urgent_map;
  clutter_class->unmap = mnb_notification_urgent_unmap;
  clutter_class->key_press_event = mnb_notification_urgent_key_press_event;

  urgent_signals[SYNC_INPUT_REGION] =
    g_signal_new ("sync-input-region",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MnbNotificationUrgentClass,
                                   sync_input_region),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static gint
id_compare (gconstpointer a, gconstpointer b)
{
  MnbNotification *notification = MNB_NOTIFICATION (a);
  guint find_id = GPOINTER_TO_INT (b);
  return mnb_notification_get_id (notification) - find_id;
}

static ClutterActor*
find_widget (ClutterGroup *container, guint32 id)
{
  GList *children, *l;
  ClutterActor *w;

  children = clutter_container_get_children (CLUTTER_CONTAINER(container));
  l = g_list_find_custom (children, GINT_TO_POINTER (id), id_compare);
  w = l ? l->data : NULL;
  g_list_free (children);
  return w;
}

static void
on_closed (MnbNotification *notification, MoblinNetbookNotifyStore *store)
{
  moblin_netbook_notify_store_close (store,
                                     mnb_notification_get_id (notification),
                                     ClosedDismissed);
}

static void
on_action (MnbNotification *notification,
           gchar           *action,
           MoblinNetbookNotifyStore *store)
{
  moblin_netbook_notify_store_action (store,
                                      mnb_notification_get_id (notification),
                                      action);
}

#if 0
/*
 * This should either be connected or deleted.
 */
static void
on_control_appear_anim_completed (ClutterAnimation *anim,
                                  MnbNotificationUrgent *urgent)
{
  g_signal_emit (urgent, urgent_signals[SYNC_INPUT_REGION], 0);
}
#endif

static void
last_focused_weak_notify (gpointer data, GObject *object)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (data)->priv;

  if ((GObject*)priv->last_focused == object)
    priv->last_focused = NULL;
}

void meta_window_unmake_fullscreen (MetaWindow  *window);

static void
handle_fullscreen_application (MutterPlugin *plugin)
{
  MetaScreen   *screen  = mutter_plugin_get_screen (plugin);
  MetaDisplay  *display = meta_screen_get_display (screen);
  MetaWindow   *mw = NULL;
  gboolean      fullscreen = FALSE;

  g_object_get (display, "focus-window", &mw, NULL);

  if (!mw)
    {
      g_warning (G_STRLOC " Could not obtain currently focused window!");
      return;
    }

  g_object_get (mw, "fullscreen", &fullscreen, NULL);

  if (!fullscreen)
    {
      g_warning (G_STRLOC " Currently focused window is not fullscreen!");
      return ;
    }

  meta_window_unmake_fullscreen (mw);
}

static void
on_notification_added (MoblinNetbookNotifyStore *store,
                       Notification             *notification,
                       MnbNotificationUrgent   *urgent)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (urgent)->priv;
  ClutterActor *w;
  MutterPlugin *plugin;

  if (!notification->is_urgent)
    return;

  plugin = moblin_netbook_get_plugin_singleton ();

  if (moblin_netbook_compositor_disabled (plugin))
    handle_fullscreen_application (plugin);

  w = find_widget (priv->notifiers, notification->id);


  if (!w)
    {
      w = mnb_notification_new ();
      g_signal_connect (w, "closed", G_CALLBACK (on_closed), store);
      g_signal_connect (w, "action", G_CALLBACK (on_action), store);

      clutter_container_add_actor (CLUTTER_CONTAINER (priv->notifiers),
                                   CLUTTER_ACTOR(w));
      clutter_actor_hide (CLUTTER_ACTOR(w));

      clutter_actor_set_width (CLUTTER_ACTOR(w), URGENT_WIDTH);

      mnb_notification_update (MNB_NOTIFICATION (w), notification);

      priv->n_notifiers++;
    }
  else
    {
      mnb_notification_update (MNB_NOTIFICATION (w), notification);
      return;
    }

  if (priv->n_notifiers == 1)
    {
      MutterPlugin *plugin = moblin_netbook_get_plugin_singleton ();
      ClutterActor *self   = CLUTTER_ACTOR (urgent);
      ClutterActor *stage  = clutter_actor_get_stage (self);

      priv->active = w;
      /* run appear anim ? */
      clutter_actor_show (CLUTTER_ACTOR(priv->notifiers));
      clutter_actor_show (CLUTTER_ACTOR(w));

      /* ensure correct stacking before showing */
      clutter_actor_raise_top (self);
      clutter_actor_show_all (self);

      priv->last_focused = clutter_stage_get_key_focus (CLUTTER_STAGE (stage));

      if (priv->last_focused)
        {
          g_object_weak_ref (G_OBJECT (priv->last_focused),
                             last_focused_weak_notify,
                             urgent);
        }

      clutter_stage_set_key_focus (CLUTTER_STAGE (stage), self);
      moblin_netbook_stash_window_focus (plugin, CurrentTime);
    }

  g_signal_emit (urgent, urgent_signals[SYNC_INPUT_REGION], 0);
}


static void
on_notification_closed (MoblinNetbookNotifyStore *store,
                        guint id,
                        guint reason,
                        MnbNotificationUrgent *urgent)
{
  MnbNotificationUrgentPrivate *priv = MNB_NOTIFICATION_URGENT (urgent)->priv;
  ClutterActor *w;

  w = find_widget (priv->notifiers, id);

  if (w)
    {
      if (w == priv->active)
        priv->active = NULL;

      priv->n_notifiers--;
      clutter_container_remove_actor (CLUTTER_CONTAINER (priv->notifiers),
                                      CLUTTER_ACTOR(w));

      if (priv->active == NULL && priv->n_notifiers > 0)
        {
          priv->active =
            clutter_group_get_nth_child (CLUTTER_GROUP (priv->notifiers), 0);

          clutter_actor_show (CLUTTER_ACTOR(priv->active));
        }
      else
        {
          MutterPlugin *plugin = moblin_netbook_get_plugin_singleton ();
          ClutterActor *self   = CLUTTER_ACTOR (urgent);
          ClutterActor *stage  = clutter_actor_get_stage (self);

          clutter_actor_hide (CLUTTER_ACTOR(urgent));
          clutter_actor_hide (CLUTTER_ACTOR(priv->notifiers));
          clutter_stage_set_key_focus (CLUTTER_STAGE (stage),
                                       priv->last_focused);

          if (priv->last_focused)
            {
              g_object_weak_unref (G_OBJECT (priv->last_focused),
                                   last_focused_weak_notify,
                                   urgent);
              priv->last_focused = NULL;
            }

          moblin_netbook_unstash_window_focus (plugin, CurrentTime);
        }

      g_signal_emit (urgent, urgent_signals[SYNC_INPUT_REGION], 0);
    }
}

void
mnb_notification_urgent_set_store (MnbNotificationUrgent    *self,
                                   MoblinNetbookNotifyStore *notify_store)
{
  g_signal_connect (notify_store,
                    "notification-added",
                    G_CALLBACK (on_notification_added),
                    self);

  g_signal_connect (notify_store,
                    "notification-closed",
                    G_CALLBACK (on_notification_closed),
                    self);
}

static void
mnb_notification_urgent_init (MnbNotificationUrgent *self)
{
  MnbNotificationUrgentPrivate *priv = GET_PRIVATE (self);

  self->priv = priv;

  priv->notifiers = CLUTTER_GROUP(clutter_group_new ());

  clutter_actor_set_parent (CLUTTER_ACTOR(priv->notifiers),
                            CLUTTER_ACTOR(self));

  clutter_actor_hide (CLUTTER_ACTOR(priv->notifiers));

  clutter_actor_set_reactive (CLUTTER_ACTOR(priv->notifiers), TRUE);
  clutter_actor_set_reactive (CLUTTER_ACTOR(self), TRUE);
}

ClutterActor*
mnb_notification_urgent_new (void)
{
  return g_object_new (MNB_TYPE_NOTIFICATION_URGENT, NULL);
}

