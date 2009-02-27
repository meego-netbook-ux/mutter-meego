#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "mnb-status-entry.h"
#include "marshal.h"

#define H_PADDING       (6.0)

#define MNB_STATUS_ENTRY_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MNB_TYPE_STATUS_ENTRY, MnbStatusEntryPrivate))

struct _MnbStatusEntryPrivate
{
  ClutterActor *status_entry;
  ClutterActor *service_label;
  ClutterActor *button;

  gchar *service_name;
  gchar *status_text;
  gchar *status_time;

  NbtkPadding padding;

  guint in_hover  : 1;
  guint is_active : 1;
};

enum
{
  PROP_0,

  PROP_SERVICE_NAME
};

enum
{
  STATUS_CHANGED,

  LAST_SIGNAL
};

static guint entry_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (MnbStatusEntry, mnb_status_entry, NBTK_TYPE_WIDGET);

static gchar *
format_time_for_display (GTimeVal *time_)
{
  GTimeVal now;
  struct tm tm_mtime;
  const gchar *format = NULL;
  gchar *locale_format = NULL;
  gchar buf[256];
  gchar *retval = NULL;
  gint secs_diff;
 
  g_return_val_if_fail (time_->tv_usec >= 0 && time_->tv_usec < G_USEC_PER_SEC, NULL);
 
  g_get_current_time (&now);
 
#ifdef HAVE_LOCALTIME_R
  localtime_r ((time_t *) &(time_->tv_sec), &tm_mtime);
#else
  {
    struct tm *ptm = localtime ((time_t *) &(time_->tv_usec));
 
    if (!ptm)
      {
        g_warning ("ptm != NULL failed");
        return NULL;
      }
    else
      memcpy ((void *) &tm_mtime, (void *) ptm, sizeof (struct tm));
  }
#endif /* HAVE_LOCALTIME_R */
 
  secs_diff = now.tv_sec - time_->tv_sec;
 
  /* within the hour */
  if (secs_diff < 60)
    retval = g_strdup (_("Less than a minute ago"));
  else
    {
      gint mins_diff = secs_diff / 60;
 
      if (mins_diff < 60)
        retval = g_strdup_printf (ngettext ("About a minute ago",
                                            "About %d minutes ago",
                                            mins_diff),
                                  mins_diff);
      else if (mins_diff < 360)
        {
          gint hours_diff = mins_diff / 60;
 
          retval = g_strdup_printf (ngettext ("About an hour ago",
                                              "About %d hours ago",
                                              hours_diff),
                                    hours_diff);
        }
    }
 
  if (retval)
    return retval;
  else
    {
      GDate d1, d2;
      gint days_diff;
 
      g_date_set_time_t (&d1, now.tv_sec);
      g_date_set_time_t (&d2, time_->tv_sec);
 
      days_diff = g_date_get_julian (&d1) - g_date_get_julian (&d2);
 
      if (days_diff == 0)
        format = _("Today at %H:%M");
      else if (days_diff == 1)
        format = _("Yesterday at %H:%M");
      else
        {
          if (days_diff > 1 && days_diff < 7)
            format = _("Last %A at %H:%M"); /* day of the week */
          else
            format = _("%x at %H:%M"); /* any other date */
        }
    }
 
  locale_format = g_locale_from_utf8 (format, -1, NULL, NULL, NULL);
 
  if (strftime (buf, sizeof (buf), locale_format, &tm_mtime) != 0)
    retval = g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
  else
    retval = g_strdup (_("Unknown"));
 
  g_free (locale_format);
 
  return retval;
}

static void
on_button_clicked (NbtkButton *button,
                   MnbStatusEntry *entry)
{
  MnbStatusEntryPrivate *priv = entry->priv;
  ClutterActor *text;

  text = nbtk_entry_get_clutter_text (NBTK_ENTRY (priv->status_entry));

  if (!priv->is_active)
    {
      nbtk_button_set_label (NBTK_BUTTON (priv->button), "Post");

      clutter_actor_set_reactive (text, TRUE);

      clutter_text_set_editable (CLUTTER_TEXT (text), TRUE);
      clutter_text_set_activatable (CLUTTER_TEXT (text), TRUE);
      clutter_text_set_text (CLUTTER_TEXT (text), priv->status_text);

      clutter_actor_hide (priv->service_label);

      clutter_actor_grab_key_focus (priv->status_entry);

      nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), "active");

      priv->is_active = TRUE;
    }
  else
    {
      nbtk_button_set_label (NBTK_BUTTON (priv->button), "Edit");

      clutter_actor_set_reactive (text, FALSE);

      clutter_text_set_editable (CLUTTER_TEXT (text), FALSE);
      clutter_text_set_activatable (CLUTTER_TEXT (text), FALSE);

      clutter_actor_show (priv->service_label);

      nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), "active");

      priv->is_active = FALSE;

      g_signal_emit (entry, entry_signals[STATUS_CHANGED], 0,
                     clutter_text_get_text (CLUTTER_TEXT (text)));
    }
}

static void
mnb_status_entry_get_preferred_width (ClutterActor *actor,
                                      ClutterUnit   for_height,
                                      ClutterUnit  *min_width_p,
                                      ClutterUnit  *natural_width_p)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (actor)->priv;
  ClutterUnit min_width, natural_width;

  clutter_actor_get_preferred_width (priv->status_entry, for_height,
                                     &min_width,
                                     &natural_width);

  if (min_width_p)
    *min_width_p = priv->padding.left + min_width + priv->padding.right;

  if (natural_width_p)
    *natural_width_p = priv->padding.left
                     + natural_width
                     + priv->padding.right;
}

static void
mnb_status_entry_get_preferred_height (ClutterActor *actor,
                                       ClutterUnit   for_width,
                                       ClutterUnit  *min_height_p,
                                       ClutterUnit  *natural_height_p)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (actor)->priv;
  ClutterUnit min_height, natural_height;

  clutter_actor_get_preferred_height (priv->status_entry, for_width,
                                      &min_height,
                                      &natural_height);

  if (min_height_p)
    *min_height_p = priv->padding.top + min_height + priv->padding.bottom;

  if (natural_height_p)
    *natural_height_p = priv->padding.top
                      + natural_height
                      + priv->padding.bottom;
}

static void
mnb_status_entry_allocate (ClutterActor          *actor,
                           const ClutterActorBox *box,
                           gboolean               origin_changed)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (actor)->priv;
  ClutterActorClass *parent_class;
  ClutterUnit available_width, available_height;
  ClutterUnit min_width, min_height;
  ClutterUnit natural_width, natural_height;
  ClutterUnit button_width, button_height;
  ClutterUnit service_width, service_height;
  ClutterUnit text_width, text_height;
  NbtkPadding border = { 0, };
  ClutterActorBox child_box = { 0, };

  parent_class = CLUTTER_ACTOR_CLASS (mnb_status_entry_parent_class);
  parent_class->allocate (actor, box, origin_changed);

//  nbtk_widget_get_border (NBTK_WIDGET (actor), &border);

  available_width  = box->x2 - box->x1
                   - priv->padding.left - priv->padding.right
                   - border.left - border.right;
  available_height = box->y2 - box->y1
                   - priv->padding.top - priv->padding.bottom
                   - border.top - border.right;

  clutter_actor_get_preferred_size (priv->button,
                                    &min_width, &min_height,
                                    &natural_width, &natural_height);

  if (natural_width >= available_width)
    {
      if (min_width >= available_width)
        button_width = available_width;
      else
        button_width = min_width;
    }
  else
    button_width = natural_width;

  if (natural_height >= available_height)
    {
      if (min_height >= available_width)
        button_height = available_height;
      else
        button_height = min_height;
    }
  else
    button_height = natural_height;

  /* layout
   *
   * +------------------------------------------------+
   * | +---------------------+-------------+--------+ |
   * | |xxxxxxxxxxxxxxxxx... |xxxxxxxxxxxxx| xxxxxx | |
   * | +---------------------+-------------+--------+ |
   * +------------------------------------------------+
   *
   *    status               | service     | button
   */

  clutter_actor_get_preferred_width (priv->service_label,
                                     available_height,
                                     NULL,
                                     &service_width);

  /* status entry */
  text_width = available_width
             - button_width
             - service_width
             - (2 * H_PADDING);

  clutter_actor_get_preferred_height (priv->status_entry, text_width,
                                      NULL,
                                      &text_height);

  child_box.x1 = (int) border.left + priv->padding.left;
  child_box.y1 = (int) border.top + priv->padding.top;
  child_box.x2 = (int) child_box.x1 + text_width;
  child_box.y2 = (int) child_box.y1 + text_height;
  clutter_actor_allocate (priv->status_entry, &child_box, origin_changed);

  /* service label */
  child_box.x1 = available_width
               - (border.right + priv->padding.right)
               - button_width
               - H_PADDING
               - service_width;
  child_box.y1 = border.top + priv->padding.top;
  child_box.x2 = child_box.x1 + service_width;
  child_box.y2 = child_box.y1 + text_height;
  clutter_actor_allocate (priv->service_label, &child_box, origin_changed);

  /* button */
  child_box.x1 = available_width
               - (border.right + priv->padding.right)
               - button_width;
  child_box.y1 = border.top + priv->padding.top;
  child_box.x2 = child_box.x1 + button_width;
  child_box.y2 = child_box.y1 + text_height;
  clutter_actor_allocate (priv->button, &child_box, origin_changed);
}

static void
mnb_status_entry_paint (ClutterActor *actor)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (actor)->priv;

  CLUTTER_ACTOR_CLASS (mnb_status_entry_parent_class)->paint (actor);

  if (priv->status_entry && CLUTTER_ACTOR_IS_VISIBLE (priv->status_entry))
    clutter_actor_paint (priv->status_entry);

  if (priv->service_label && CLUTTER_ACTOR_IS_VISIBLE (priv->service_label))
    clutter_actor_paint (priv->service_label);

  if (priv->button && CLUTTER_ACTOR_IS_VISIBLE (priv->button))
    clutter_actor_paint (priv->button);
}

static void
mnb_status_entry_pick (ClutterActor       *actor,
                       const ClutterColor *pick_color)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (actor)->priv;

  CLUTTER_ACTOR_CLASS (mnb_status_entry_parent_class)->pick (actor,
                                                             pick_color);

  if (priv->status_entry && clutter_actor_should_pick_paint (priv->status_entry))
    clutter_actor_paint (priv->status_entry);

  if (priv->service_label && clutter_actor_should_pick_paint (priv->service_label))
    clutter_actor_paint (priv->service_label);

  if (priv->button && clutter_actor_should_pick_paint (priv->button))
    clutter_actor_paint (priv->button);
}

static void
mnb_status_entry_style_changed (NbtkWidget *widget)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (widget)->priv;
  NbtkPadding *padding = NULL;

  nbtk_stylable_get (NBTK_STYLABLE (widget),
                     "padding", &padding,
                     NULL);

  if (padding)
    {
      priv->padding = *padding;

      g_boxed_free (NBTK_TYPE_PADDING, padding);
    }

  /* chain up */
  NBTK_WIDGET_CLASS (mnb_status_entry_parent_class)->style_changed (widget);
}

static void
mnb_status_entry_finalize (GObject *gobject)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (gobject)->priv;

  g_free (priv->service_name);
  g_free (priv->status_text);
  g_free (priv->status_time);

  clutter_actor_destroy (priv->service_label);
  clutter_actor_destroy (priv->status_entry);
  clutter_actor_destroy (priv->button);

  G_OBJECT_CLASS (mnb_status_entry_parent_class)->finalize (gobject);
}

static void
mnb_status_entry_set_property (GObject      *gobject,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (gobject)->priv;

  switch (prop_id)
    {
    case PROP_SERVICE_NAME:
      g_free (priv->service_name);
      priv->service_name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
mnb_status_entry_get_property (GObject    *gobject,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MnbStatusEntryPrivate *priv = MNB_STATUS_ENTRY (gobject)->priv;

  switch (prop_id)
    {
    case PROP_SERVICE_NAME:
      g_value_set_string (value, priv->service_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
mnb_status_entry_constructed (GObject *gobject)
{
  MnbStatusEntry *entry = MNB_STATUS_ENTRY (gobject);
  MnbStatusEntryPrivate *priv = entry->priv;

  g_assert (priv->service_name != NULL);

  if (G_OBJECT_CLASS (mnb_status_entry_parent_class)->constructed)
    G_OBJECT_CLASS (mnb_status_entry_parent_class)->constructed (gobject);
}

static void
mnb_status_entry_class_init (MnbStatusEntryClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  NbtkWidgetClass *widget_class = NBTK_WIDGET_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (MnbStatusEntryPrivate));

  gobject_class->constructed = mnb_status_entry_constructed;
  gobject_class->set_property = mnb_status_entry_set_property;
  gobject_class->get_property = mnb_status_entry_get_property;
  gobject_class->finalize = mnb_status_entry_finalize;

  actor_class->get_preferred_width = mnb_status_entry_get_preferred_width;
  actor_class->get_preferred_height = mnb_status_entry_get_preferred_height;
  actor_class->allocate = mnb_status_entry_allocate;
  actor_class->paint = mnb_status_entry_paint;
  actor_class->pick = mnb_status_entry_pick;

  widget_class->style_changed = mnb_status_entry_style_changed;

  pspec = g_param_spec_string ("service-name",
                               "Service Name",
                               "The name of the Mojito service",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (gobject_class, PROP_SERVICE_NAME, pspec);

  entry_signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MnbStatusEntryClass, status_changed),
                  NULL, NULL,
                  moblin_netbook_marshal_VOID__STRING,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);
}

static void
mnb_status_entry_init (MnbStatusEntry *self)
{
  MnbStatusEntryPrivate *priv;
  ClutterActor *text;

  self->priv = priv = MNB_STATUS_ENTRY_GET_PRIVATE (self);

  priv->status_entry =
    CLUTTER_ACTOR (nbtk_entry_new ("Enter your status here..."));
  nbtk_widget_set_style_class_name (NBTK_WIDGET (priv->status_entry),
                                    "MnbStatusEntryText");
  clutter_actor_set_parent (priv->status_entry, CLUTTER_ACTOR (self));
  text = nbtk_entry_get_clutter_text (NBTK_ENTRY (priv->status_entry));
  clutter_text_set_editable (CLUTTER_TEXT (text), FALSE);
  clutter_text_set_single_line_mode (CLUTTER_TEXT (text), TRUE);
  clutter_text_set_use_markup (CLUTTER_TEXT (text), TRUE);

  priv->service_label =
    CLUTTER_ACTOR (nbtk_label_new (""));
  nbtk_widget_set_style_class_name (NBTK_WIDGET (priv->service_label),
                                    "MnbStatusEntrySubText");
  clutter_actor_set_parent (priv->service_label, CLUTTER_ACTOR (self));
  text = nbtk_label_get_clutter_text (NBTK_LABEL (priv->service_label));
  clutter_text_set_editable (CLUTTER_TEXT (text), FALSE);
  clutter_text_set_single_line_mode (CLUTTER_TEXT (text), TRUE);
  clutter_text_set_use_markup (CLUTTER_TEXT (text), FALSE);

  priv->button = CLUTTER_ACTOR (nbtk_button_new_with_label ("Edit"));
  nbtk_widget_set_style_class_name (NBTK_WIDGET (priv->button),
                                    "MnbStatusEntryButton");
  clutter_actor_hide (priv->button);
  clutter_actor_set_reactive (priv->button, TRUE);
  clutter_actor_set_parent (priv->button, CLUTTER_ACTOR (self));
  g_signal_connect (priv->button, "clicked",
                    G_CALLBACK (on_button_clicked),
                    self);
}

NbtkWidget *
mnb_status_entry_new (const gchar *service_name)
{
  g_return_val_if_fail (service_name != NULL, NULL);

  return g_object_new (MNB_TYPE_STATUS_ENTRY,
                       "service-name", service_name,
                       NULL);
}

void
mnb_status_entry_show_button (MnbStatusEntry *entry,
                              gboolean        show)
{
  g_return_if_fail (MNB_IS_STATUS_ENTRY (entry));

  if (show)
    clutter_actor_show (entry->priv->button);
  else
    clutter_actor_hide (entry->priv->button);
}

gboolean
mnb_status_entry_get_is_active (MnbStatusEntry *entry)
{
  g_return_val_if_fail (MNB_IS_STATUS_ENTRY (entry), FALSE);

  return entry->priv->is_active;
}

void
mnb_status_entry_set_is_active (MnbStatusEntry *entry,
                                gboolean        is_active)
{
  g_return_if_fail (MNB_IS_STATUS_ENTRY (entry));

  if (entry->priv->is_active != is_active)
    {
      entry->priv->is_active = is_active;

      if (entry->priv->is_active)
        clutter_actor_hide (entry->priv->service_label);
      else
        clutter_actor_show (entry->priv->service_label);

      /* styling */
      if (entry->priv->is_active)
        nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), "active");
      else
        {
          if (entry->priv->in_hover)
            nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), "hover");
          else
            nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), NULL);
        }
    }
}

gboolean
mnb_status_entry_get_in_hover (MnbStatusEntry *entry)
{
  g_return_val_if_fail (MNB_IS_STATUS_ENTRY (entry), FALSE);

  return entry->priv->in_hover;
}

void
mnb_status_entry_set_in_hover (MnbStatusEntry *entry,
                               gboolean        in_hover)
{
  g_return_if_fail (MNB_IS_STATUS_ENTRY (entry));

  if (entry->priv->in_hover != in_hover)
    {
      entry->priv->in_hover = in_hover;

      if (entry->priv->in_hover)
        nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), "hover");
      else
        {
          if (entry->priv->is_active)
            nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), "active");
          else
            nbtk_widget_set_style_pseudo_class (NBTK_WIDGET (entry), NULL);
        }
    }
}

void
mnb_status_entry_set_status_text (MnbStatusEntry *entry,
                                  const gchar    *status_text,
                                  GTimeVal       *status_time)
{
  MnbStatusEntryPrivate *priv;
  ClutterActor *text;
  gchar *service_line;

  g_return_if_fail (MNB_IS_STATUS_ENTRY (entry));
  g_return_if_fail (status_text != NULL);

  priv = entry->priv;

  g_free (priv->status_text);
  g_free (priv->status_time);

  priv->status_text = g_strdup (status_text);

  if (status_time)
    priv->status_time = format_time_for_display (status_time);

  text = nbtk_entry_get_clutter_text (NBTK_ENTRY (priv->status_entry));
  clutter_text_set_markup (CLUTTER_TEXT (text), priv->status_text);

  service_line = g_strdup_printf ("%s - %s",
                                  priv->status_time == NULL,
                                  priv->service_name);

  text = nbtk_label_get_clutter_text (NBTK_LABEL (priv->service_label));
  clutter_text_set_markup (CLUTTER_TEXT (text), service_line);
  g_free (service_line);
}

G_CONST_RETURN gchar *
mnb_status_entry_get_status_text (MnbStatusEntry *entry)
{
  g_return_val_if_fail (MNB_IS_STATUS_ENTRY (entry), NULL);

  return entry->priv->status_text;
}
