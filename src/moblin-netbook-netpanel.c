#include "moblin-netbook-netpanel.h"
#include "mwb-radical-bar.h"

G_DEFINE_TYPE (MoblinNetbookNetpanel, moblin_netbook_netpanel, NBTK_TYPE_TABLE)

#define NETPANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MOBLIN_TYPE_NETBOOK_NETPANEL, MoblinNetbookNetpanelPrivate))

struct _MoblinNetbookNetpanelPrivate
{
  NbtkWidget *tabs_table;
  NbtkWidget *favs_table;
  GList *previews;
};

static void
moblin_netbook_netpanel_dispose (GObject *object)
{
  MoblinNetbookNetpanelPrivate *priv = MOBLIN_NETBOOK_NETPANEL (object)->priv;

  if (priv->previews)
    {
      g_list_free (priv->previews);
      priv->previews = NULL;
    }

  G_OBJECT_CLASS (moblin_netbook_netpanel_parent_class)->dispose (object);
}

static void
moblin_netbook_netpanel_finalize (GObject *object)
{
  G_OBJECT_CLASS (moblin_netbook_netpanel_parent_class)->finalize (object);
}

static void
moblin_netbook_netpanel_class_init (MoblinNetbookNetpanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MoblinNetbookNetpanelPrivate));

  object_class->dispose = moblin_netbook_netpanel_dispose;
  object_class->finalize = moblin_netbook_netpanel_finalize;
}

static void
moblin_netbook_netpanel_init (MoblinNetbookNetpanel *self)
{
  NbtkWidget *table, *bar, *label, *more_button;
  
  MoblinNetbookNetpanelPrivate *priv = self->priv = NETPANEL_PRIVATE (self);

  nbtk_table_set_col_spacing (NBTK_TABLE (self), 6);
  nbtk_table_set_row_spacing (NBTK_TABLE (self), 6);
  
  /* Construct internet panel (except tab/page previews) */
  
  /* Construct entry table */
  table = nbtk_table_new ();
  nbtk_table_set_col_spacing (NBTK_TABLE (table), 6);
  nbtk_table_set_row_spacing (NBTK_TABLE (table), 6);
  clutter_actor_set_name (CLUTTER_ACTOR (table), "netpanel-entrytable");

  nbtk_table_add_widget_full (NBTK_TABLE (self), table, 0, 0, 1, 1,
                              NBTK_X_EXPAND | NBTK_X_FILL, 0.5, 0.5);

  /* Construct entry table widgets */

  label = nbtk_label_new ("Internet");
  bar = mwb_radical_bar_new ();
  nbtk_table_add_widget_full (NBTK_TABLE (table), label, 0, 0, 1, 1,
                              0, 0.0, 0.5);
  nbtk_table_add_widget_full (NBTK_TABLE (table), bar, 0, 1, 1, 1,
                              NBTK_X_EXPAND | NBTK_X_FILL, 0.5, 0.5);

  /* Construct tabs preview table */
  priv->tabs_table = nbtk_table_new ();
  nbtk_table_set_col_spacing (NBTK_TABLE (priv->tabs_table), 6);
  nbtk_table_set_row_spacing (NBTK_TABLE (priv->tabs_table), 6);

  nbtk_table_add_widget_full (NBTK_TABLE (self), priv->tabs_table, 1, 0, 1, 1,
                              NBTK_X_EXPAND | NBTK_X_FILL, 0.5, 0.5);

  /* Construct tabs previews table widgets */
  label = nbtk_label_new ("Tabs");
  nbtk_table_add_widget_full (NBTK_TABLE (priv->tabs_table), label, 0, 0, 1, 5,
                              0, 0.0, 0.5);

  /* Construct favourites preview table */
  priv->favs_table = nbtk_table_new ();
  nbtk_table_set_col_spacing (NBTK_TABLE (priv->favs_table), 6);
  nbtk_table_set_row_spacing (NBTK_TABLE (priv->favs_table), 6);

  nbtk_table_add_widget_full (NBTK_TABLE (self), priv->favs_table, 2, 0, 1, 1,
                              NBTK_X_EXPAND | NBTK_X_FILL, 0.5, 0.5);

  /* Construct favourites previews table widgets */
  label = nbtk_label_new ("Favourite pages");
  nbtk_table_add_widget_full (NBTK_TABLE (priv->favs_table), label, 0, 0, 1, 5,
                              0, 0.0, 0.5);
}

NbtkWidget*
moblin_netbook_netpanel_new (void)
{
  return g_object_new (MOBLIN_TYPE_NETBOOK_NETPANEL, NULL);
}

void
moblin_netbook_netpanel_show_contents (MoblinNetbookNetpanel *netpanel)
{
}

void
moblin_netbook_netpanel_hide_contents (MoblinNetbookNetpanel *netpanel)
{
}

