#include <mojito-client/mojito-item.h>
#include "penge-myspace-tile.h"
#include "penge-utils.h"

G_DEFINE_TYPE (PengeMyspaceTile, penge_myspace_tile, PENGE_TYPE_PEOPLE_TILE)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PENGE_TYPE_MYSPACE_TILE, PengeMyspaceTilePrivate))

typedef struct _PengeMyspaceTilePrivate PengeMyspaceTilePrivate;

struct _PengeMyspaceTilePrivate {
  MojitoItem *item;
};

enum
{
  PROP_0,
  PROP_ITEM
};

static void
penge_myspace_tile_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  PengeMyspaceTilePrivate *priv = GET_PRIVATE (object);

  switch (property_id) {
    case PROP_ITEM:
      g_value_set_pointer (value, priv->item);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
penge_myspace_tile_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  PengeMyspaceTilePrivate *priv = GET_PRIVATE (object);

  switch (property_id) {
    case PROP_ITEM:
      priv->item = g_value_get_pointer (value);
      mojito_item_ref (priv->item);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
penge_myspace_tile_dispose (GObject *object)
{
  G_OBJECT_CLASS (penge_myspace_tile_parent_class)->dispose (object);
}

static void
penge_myspace_tile_finalize (GObject *object)
{
  G_OBJECT_CLASS (penge_myspace_tile_parent_class)->finalize (object);
}

static gboolean
_button_press_event (ClutterActor *actor,
                     ClutterEvent *event,
                     gpointer      userdata)
{
  PengeMyspaceTilePrivate *priv = GET_PRIVATE (userdata);
  const gchar *url;
  GError *error = NULL;

  url = g_hash_table_lookup (priv->item->props,
                             "url");

  if (!g_app_info_launch_default_for_uri (url,
                                     NULL,
                                     &error))
  {
    g_warning (G_STRLOC ": Error launching uri (%s): %s",
               url,
               error->message);
    g_clear_error (&error);
  } else {
    penge_utils_signal_activated (actor);
  }

  return TRUE;
}

static void
penge_myspace_tile_constructed (GObject *object)
{
  PengeMyspaceTilePrivate *priv = GET_PRIVATE (object);
  const gchar *content;
  const gchar *author;
  const gchar *authoricon_path;
  gchar *date;
  NbtkWidget *body;
  NbtkWidget *label;
  ClutterActor *tmp_text;

  g_return_if_fail (priv->item != NULL);

  content = g_hash_table_lookup (priv->item->props,
                                 "content");
  author = g_hash_table_lookup (priv->item->props,
                                "author");
  authoricon_path = g_hash_table_lookup (priv->item->props,
                                         "authoricon");

  date = penge_utils_format_time (&(priv->item->date));

  g_object_set (object,
                "primary-text",
                author,
                "secondary-text",
                date,
                "icon-path",
                authoricon_path,
                NULL);
  g_free (date);

  body = nbtk_bin_new ();
  nbtk_widget_set_style_class_name (body,
                                    "PengeMyspaceTileBackground");
  label = nbtk_label_new (content);
  nbtk_widget_set_style_class_name (label, "PengeMyspaceTileLabel");
  nbtk_bin_set_child (NBTK_BIN (body), (ClutterActor *)label);
  nbtk_bin_set_alignment (NBTK_BIN (body), NBTK_ALIGN_TOP, NBTK_ALIGN_TOP);
  tmp_text = nbtk_label_get_clutter_text (NBTK_LABEL (label));
  clutter_text_set_line_wrap (CLUTTER_TEXT (tmp_text), TRUE);
  clutter_text_set_line_wrap_mode (CLUTTER_TEXT (tmp_text),
                                   PANGO_WRAP_WORD_CHAR);
  clutter_text_set_ellipsize (CLUTTER_TEXT (tmp_text),
                              PANGO_ELLIPSIZE_NONE);
  clutter_text_set_line_alignment (CLUTTER_TEXT (tmp_text),
                                   PANGO_ALIGN_LEFT);

  g_object_set (object,
                "body",
                body,
                NULL);

  g_signal_connect (object,
                    "button-press-event",
                    (GCallback)_button_press_event,
                    object);

  if (G_OBJECT_CLASS (penge_myspace_tile_parent_class)->constructed)
    G_OBJECT_CLASS (penge_myspace_tile_parent_class)->constructed (object);
}

static void
penge_myspace_tile_class_init (PengeMyspaceTileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (PengeMyspaceTilePrivate));

  object_class->get_property = penge_myspace_tile_get_property;
  object_class->set_property = penge_myspace_tile_set_property;
  object_class->dispose = penge_myspace_tile_dispose;
  object_class->finalize = penge_myspace_tile_finalize;
  object_class->constructed = penge_myspace_tile_constructed;

  pspec = g_param_spec_pointer ("item",
                                "Item",
                                "Client side item to render",
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_ITEM, pspec);;


}

static void
penge_myspace_tile_init (PengeMyspaceTile *self)
{
}
