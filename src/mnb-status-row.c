#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <stdlib.h>

#include <mojito-client/mojito-client.h>

#include "mnb-status-row.h"
#include "mnb-status-entry.h"

#define MNB_STATUS_ROW_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MNB_TYPE_STATUS_ROW, MnbStatusRowPrivate))

#define ICON_SIZE       (CLUTTER_UNITS_FROM_FLOAT (48.0))
#define H_PADDING       (CLUTTER_UNITS_FROM_FLOAT (9.0))

struct _MnbStatusRowPrivate
{
  ClutterActor *icon;
  ClutterActor *entry;

  gchar *service_name;

  gchar *status_text;
  gchar *status_time;

  NbtkPadding padding;

  guint in_hover  : 1;

  ClutterUnit icon_separator_x;

  MojitoClient *client;
  MojitoClientView *view;
};

enum
{
  PROP_0,

  PROP_SERVICE_NAME
};

G_DEFINE_TYPE (MnbStatusRow, mnb_status_row, NBTK_TYPE_WIDGET);

static void
mnb_status_row_get_preferred_width (ClutterActor *actor,
                                    ClutterUnit   for_height,
                                    ClutterUnit  *min_width_p,
                                    ClutterUnit  *natural_width_p)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;
  ClutterUnit min_width, natural_width;

  clutter_actor_get_preferred_width (priv->entry, for_height,
                                     &min_width,
                                     &natural_width);

  if (min_width_p)
    *min_width_p = priv->padding.left + ICON_SIZE + priv->padding.right;

  if (natural_width_p)
    *natural_width_p = priv->padding.left
                     + ICON_SIZE + H_PADDING + natural_width
                     + priv->padding.right;
}

static void
mnb_status_row_get_preferred_height (ClutterActor *actor,
                                     ClutterUnit   for_width,
                                     ClutterUnit  *min_height_p,
                                     ClutterUnit  *natural_height_p)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;

  if (min_height_p)
    *min_height_p = priv->padding.top + ICON_SIZE + priv->padding.bottom;

  if (natural_height_p)
    *natural_height_p = priv->padding.top + ICON_SIZE + priv->padding.bottom;
}

static void
mnb_status_row_allocate (ClutterActor          *actor,
                           const ClutterActorBox *box,
                           gboolean               origin_changed)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;
  ClutterActorClass *parent_class;
  ClutterUnit available_width, available_height;
  ClutterUnit min_width, min_height;
  ClutterUnit natural_width, natural_height;
  ClutterUnit button_width, button_height;
  ClutterUnit text_width, text_height;
  NbtkPadding border = { 0, };
  ClutterActorBox child_box = { 0, };

  parent_class = CLUTTER_ACTOR_CLASS (mnb_status_row_parent_class);
  parent_class->allocate (actor, box, origin_changed);

//  nbtk_widget_get_border (NBTK_WIDGET (actor), &border);

  available_width  = box->x2 - box->x1
                   - priv->padding.left - priv->padding.right
                   - border.left - border.right;
  available_height = box->y2 - box->y1
                   - priv->padding.top - priv->padding.bottom
                   - border.top - border.right;

  clutter_actor_get_preferred_size (priv->entry,
                                    &min_width, &min_height,
                                    &natural_width, &natural_height);

  /* layout
   *
   * +--------------------------------------------------------+
   * | +---+ | +-----------------------------------+--------+ |
   * | | X | | |xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx... | xxxxxx | |
   * | +---+ | +-----------------------------------+--------+ |
   * +--------------------------------------------------------+
   *
   *         +-------------- MnbStatusEntry ----------------+
   *   icon  |  text                               | button |
   */

  /* icon */
  child_box.x1 = border.left + priv->padding.left;
  child_box.y1 = border.top  + priv->padding.top;
  child_box.x2 = child_box.x1 + ICON_SIZE;
  child_box.y2 = child_box.y1 + ICON_SIZE;
  clutter_actor_allocate (priv->icon, &child_box, origin_changed);

  /* separator */
  priv->icon_separator_x = child_box.x2 + H_PADDING;

  /* text */
  text_width = available_width
             - ICON_SIZE
             - H_PADDING;
  clutter_actor_get_preferred_height (priv->entry, text_width,
                                      NULL,
                                      &text_height);

  child_box.x1 = border.left + priv->padding.left
               + ICON_SIZE
               + H_PADDING;
  child_box.y1 = (int) border.top + priv->padding.top
               + ((ICON_SIZE - text_height) / 2);
  child_box.x2 = child_box.x1 + text_width;
  child_box.y2 = child_box.y1 + text_height;
  clutter_actor_allocate (priv->entry, &child_box, origin_changed);
}

static void
mnb_status_row_paint (ClutterActor *actor)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;

  CLUTTER_ACTOR_CLASS (mnb_status_row_parent_class)->paint (actor);

  if (priv->icon && CLUTTER_ACTOR_IS_VISIBLE (priv->icon))
    clutter_actor_paint (priv->icon);

  if (priv->entry && CLUTTER_ACTOR_IS_VISIBLE (priv->entry))
    clutter_actor_paint (priv->entry);
}

static void
mnb_status_row_pick (ClutterActor       *actor,
                     const ClutterColor *pick_color)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;

  CLUTTER_ACTOR_CLASS (mnb_status_row_parent_class)->pick (actor,
                                                           pick_color);

  if (priv->icon && clutter_actor_should_pick_paint (priv->icon))
    clutter_actor_paint (priv->icon);

  if (priv->entry && clutter_actor_should_pick_paint (priv->entry))
    clutter_actor_paint (priv->entry);
}

static gboolean
mnb_status_row_enter (ClutterActor *actor,
                      ClutterCrossingEvent *event)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;

  if (!mnb_status_entry_get_is_active (MNB_STATUS_ENTRY (priv->entry)))
    {
      mnb_status_entry_set_in_hover (MNB_STATUS_ENTRY (priv->entry), TRUE);
      mnb_status_entry_show_button (MNB_STATUS_ENTRY (priv->entry), TRUE);
    }

  priv->in_hover = TRUE;
}

static gboolean
mnb_status_row_leave (ClutterActor *actor,
                        ClutterCrossingEvent *event)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (actor)->priv;

  if (!mnb_status_entry_get_is_active (MNB_STATUS_ENTRY (priv->entry)))
    {
      mnb_status_entry_set_in_hover (MNB_STATUS_ENTRY (priv->entry), FALSE);
      mnb_status_entry_show_button (MNB_STATUS_ENTRY (priv->entry), FALSE);
    }

  priv->in_hover = FALSE;
}

static void
mnb_status_row_style_changed (NbtkWidget *widget)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (widget)->priv;
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
  NBTK_WIDGET_CLASS (mnb_status_row_parent_class)->style_changed (widget);
}

static void
on_mojito_view_open (MojitoClient     *client,
                     MojitoClientView *view,
                     gpointer          user_data)
{
  MnbStatusRow *row = user_data;
  MnbStatusRowPrivate *priv = row->priv;
  GList *items = mojito_client_view_get_sorted_items (view);
  MojitoItem *item = (items != NULL && items->data != NULL) ? items->data
                                                            : NULL;

  if (item)
    {
      const gchar *status_text;

      status_text = g_hash_table_lookup (item->props, "content");
      mnb_status_entry_set_status_text (MNB_STATUS_ENTRY (priv->entry),
                                        status_text,
                                        NULL);
    }
}

static void
mnb_status_row_finalize (GObject *gobject)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (gobject)->priv;

  g_object_unref (priv->client);

  g_free (priv->service_name);

  g_free (priv->status_text);
  g_free (priv->status_time);

  clutter_actor_destroy (priv->icon);
  clutter_actor_destroy (priv->entry);

  G_OBJECT_CLASS (mnb_status_row_parent_class)->finalize (gobject);
}

static void
mnb_status_row_set_property (GObject      *gobject,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (gobject)->priv;

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
mnb_status_row_get_property (GObject    *gobject,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MnbStatusRowPrivate *priv = MNB_STATUS_ROW (gobject)->priv;

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
mnb_status_row_constructed (GObject *gobject)
{
  MnbStatusRow *row = MNB_STATUS_ROW (gobject);
  MnbStatusRowPrivate *priv = row->priv;

  g_assert (priv->service_name != NULL);

  priv->client = mojito_client_new ();
  mojito_client_open_view_for_service (priv->client, priv->service_name, 1,
                                       on_mojito_view_open,
                                       row);

  priv->entry = CLUTTER_ACTOR (mnb_status_entry_new (priv->service_name));
  clutter_actor_set_parent (CLUTTER_ACTOR (priv->entry),
                            CLUTTER_ACTOR (row));

  if (G_OBJECT_CLASS (mnb_status_row_parent_class)->constructed)
    G_OBJECT_CLASS (mnb_status_row_parent_class)->constructed (gobject);
}

static void
mnb_status_row_class_init (MnbStatusRowClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  NbtkWidgetClass *widget_class = NBTK_WIDGET_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (MnbStatusRowPrivate));

  gobject_class->constructed = mnb_status_row_constructed;
  gobject_class->set_property = mnb_status_row_set_property;
  gobject_class->get_property = mnb_status_row_get_property;
  gobject_class->finalize = mnb_status_row_finalize;

  actor_class->get_preferred_width = mnb_status_row_get_preferred_width;
  actor_class->get_preferred_height = mnb_status_row_get_preferred_height;
  actor_class->allocate = mnb_status_row_allocate;
  actor_class->paint = mnb_status_row_paint;
  actor_class->pick = mnb_status_row_pick;
  actor_class->enter_event = mnb_status_row_enter;
  actor_class->leave_event = mnb_status_row_leave;

  widget_class->style_changed = mnb_status_row_style_changed;

  pspec = g_param_spec_string ("service-name",
                               "Service Name",
                               "The name of the Mojito service",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (gobject_class, PROP_SERVICE_NAME, pspec);
}

static void
mnb_status_row_init (MnbStatusRow *self)
{
  MnbStatusRowPrivate *priv;
  ClutterActor *text;
  gchar *no_icon_file;

  self->priv = priv = MNB_STATUS_ROW_GET_PRIVATE (self);

  no_icon_file = g_build_filename (PLUGIN_PKGDATADIR,
                                   "theme",
                                   "status",
                                   "no_image_icon.png",
                                   NULL);
  priv->icon = clutter_texture_new_from_file (no_icon_file, NULL);
  if (G_UNLIKELY (priv->icon == NULL))
    {
      const ClutterColor color = { 204, 204, 0, 255 };

      priv->icon = clutter_rectangle_new_with_color (&color);
    }

  clutter_actor_set_size (priv->icon, ICON_SIZE, ICON_SIZE);
  clutter_actor_set_parent (priv->icon, CLUTTER_ACTOR (self));

  g_free (no_icon_file);
}

NbtkWidget *
mnb_status_row_new (const gchar *service_name)
{
  g_return_val_if_fail (service_name != NULL, NULL);

  return g_object_new (MNB_TYPE_STATUS_ROW,
                       "service-name", service_name,
                       NULL);
}
