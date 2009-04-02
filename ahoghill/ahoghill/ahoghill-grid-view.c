#include <bickley/bkl.h>

#include <bognor-regis/br-queue.h>

#include "mnb-entry.h"
#include "ahoghill-grid-view.h"
#include "ahoghill-results-pane.h"
#include "ahoghill-search-pane.h"

enum {
    PROP_0,
};

typedef struct _Source {
    KozoDB *db;

    GPtrArray *items;
    GHashTable *uri_to_item;
} Source;

struct _AhoghillGridViewPrivate {
    ClutterActor *search_pane;
    ClutterActor *results_pane;
    ClutterActor *playqueues_pane;

    BklWatcher *watcher;
    GPtrArray *dbs;

    BrQueue *local_queue;
};

#define GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), AHOGHILL_TYPE_GRID_VIEW, AhoghillGridViewPrivate))
G_DEFINE_TYPE (AhoghillGridView, ahoghill_grid_view, NBTK_TYPE_TABLE);

static void
ahoghill_grid_view_finalize (GObject *object)
{
    G_OBJECT_CLASS (ahoghill_grid_view_parent_class)->finalize (object);
}

static void
ahoghill_grid_view_dispose (GObject *object)
{
    G_OBJECT_CLASS (ahoghill_grid_view_parent_class)->dispose (object);
}

static void
ahoghill_grid_view_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    switch (prop_id) {

    default:
        break;
    }
}

static void
ahoghill_grid_view_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    switch (prop_id) {

    default:
        break;
    }
}

static void
ahoghill_grid_view_class_init (AhoghillGridViewClass *klass)
{
    GObjectClass *o_class = (GObjectClass *) klass;

    o_class->dispose = ahoghill_grid_view_dispose;
    o_class->finalize = ahoghill_grid_view_finalize;
    o_class->set_property = ahoghill_grid_view_set_property;
    o_class->get_property = ahoghill_grid_view_get_property;

    g_type_class_add_private (klass, sizeof (AhoghillGridViewPrivate));
}

static Source *
create_source (BklDBSource *s)
{
    Source *source;
    GError *error = NULL;
    int i;

    source = g_new0 (Source, 1);
    source->db = bkl_db_get_for_path (s->db_uri, s->name, &error);
    if (error != NULL) {
        g_warning ("Error loading %s (%s): %s", s->name, s->db_uri,
                   error->message);
        g_error_free (error);
        g_free (source);
        return NULL;
    }

    source->items = bkl_db_get_items (source->db, FALSE, &error);
    if (source->items == NULL) {
        g_warning ("Error getting items: %s", error->message);
        kozo_db_unref (source->db);
        g_free (source);
        return NULL;
    }

    source->uri_to_item = g_hash_table_new (g_str_hash, g_str_equal);
    for (i = 0; i < source->items->len; i++) {
        BklItem *item = source->items->pdata[i];
        g_hash_table_insert (source->uri_to_item,
                             (char *) bkl_item_get_uri (item), item);
    }

    return source;
}

static void
init_bickley (gpointer data)
{
    AhoghillGridView *view = (AhoghillGridView *) data;
    AhoghillGridViewPrivate *priv = view->priv;
    GList *sources, *s;
    GError *error = NULL;

    priv->watcher = g_object_new (BKL_TYPE_WATCHER, NULL);
    sources = bkl_watcher_get_sources (priv->watcher, &error);
    if (error != NULL) {
        g_warning ("Error getting sources\n");
    }

    priv->dbs = g_ptr_array_sized_new (g_list_length (sources));
    for (s = sources; s; s = s->next) {
        Source *source = create_source ((BklDBSource *) s->data);

        g_ptr_array_add (priv->dbs, source);
    }
}

static void
init_bognor (AhoghillGridView *grid)
{
    AhoghillGridViewPrivate *priv = grid->priv;

    priv->local_queue = g_object_new (BR_TYPE_QUEUE,
                                      "object-path", BR_LOCAL_QUEUE_PATH,
                                      NULL);
}

static gboolean
finish_init (gpointer data)
{
    AhoghillGridView *grid = data;

    init_bognor (grid);
    init_bickley (grid);

    return FALSE;
}

static void
search_clicked_cb (MnbEntry         *entry,
                   AhoghillGridView *grid)
{
    AhoghillGridViewPrivate *priv = grid->priv;
    const char *text;
    GPtrArray *items;
    int i;

    text = mnb_entry_get_text (entry);
    if (text == NULL) {
        return;
    }

    items = g_ptr_array_new ();
    for (i = 0; i < priv->dbs->len; i++) {
        GError *error = NULL;
        Source *source = priv->dbs->pdata[i];
        GList *results;

        results = kozo_db_index_lookup (source->db, text, &error);
        if (error) {
            g_warning ("Error searching for %s in %s: %s", text,
                       kozo_db_get_name (source->db), error->message);
            g_error_free (error);

            continue;
        }

        if (results) {
            GList *r;

            for (r = results; r; r = r->next) {
                BklItem *item;

                item = g_hash_table_lookup (source->uri_to_item, r->data);
                if (item != NULL) {
                    g_ptr_array_add (items, item);
                }

                g_free (r->data);
            }

            g_list_free (results);
        }
    }

    ahoghill_results_pane_set_results
        (AHOGHILL_RESULTS_PANE (priv->results_pane), items);

    g_ptr_array_free (items, TRUE);
}

static void
item_clicked_cb (AhoghillResultsPane *pane,
                 BklItem             *item,
                 AhoghillGridView    *grid)
{
    AhoghillGridViewPrivate *priv = grid->priv;
    GError *error = NULL;

    if (bkl_item_get_item_type (item) != BKL_ITEM_TYPE_AUDIO) {
        return;
    }

    br_queue_add_uri (priv->local_queue, bkl_item_get_uri (item), &error);
    if (error != NULL) {
        g_warning ("%s: Error adding %s to queue: %s", G_STRLOC,
                   bkl_item_get_uri (item), error->message);
        g_error_free (error);
        return;
    }

    br_queue_play (priv->local_queue, &error);
    if (error != NULL) {
        g_warning ("%s: Error playing local queue: %s", G_STRLOC,
                   error->message);
        g_error_free (error);
    }
}

static void
ahoghill_grid_view_init (AhoghillGridView *self)
{
    NbtkTable *table = (NbtkTable *) self;
    AhoghillGridViewPrivate *priv = GET_PRIVATE (self);
    NbtkWidget *entry;
    ClutterColor blue = { 0x00, 0x00, 0xff, 0xff };

    bkl_init ();

    clutter_actor_set_name (CLUTTER_ACTOR (self), "media-pane-vbox");
    clutter_actor_set_size (CLUTTER_ACTOR (self), 1024, 500);

    self->priv = priv;
    priv->search_pane = g_object_new (AHOGHILL_TYPE_SEARCH_PANE, NULL);
    clutter_actor_set_size (priv->search_pane, 1024, 50);
    nbtk_table_add_actor_full (table, priv->search_pane,
                               0, 0, 1, 4, NBTK_X_EXPAND, 0.0, 0.0);

    entry = ahoghill_search_pane_get_entry (AHOGHILL_SEARCH_PANE (priv->search_pane));
    g_signal_connect (entry, "button-clicked",
                      G_CALLBACK (search_clicked_cb), self);

    priv->results_pane = g_object_new (AHOGHILL_TYPE_RESULTS_PANE, NULL);
    clutter_actor_set_size (priv->results_pane, 800, 400);
    nbtk_table_add_actor_full (table, priv->results_pane,
                               1, 0, 1, 3,
                               NBTK_X_FILL | NBTK_Y_FILL | NBTK_X_EXPAND | NBTK_Y_EXPAND,
                               0.0, 0.0);
    g_signal_connect (priv->results_pane, "item-clicked",
                      G_CALLBACK (item_clicked_cb), self);

    /* priv->playqueues_pane = g_object_new (AHOGHILL_TYPE_PLAYQUEUES_PANE, */
    /*                                        NULL); */
    priv->playqueues_pane = clutter_rectangle_new_with_color (&blue);
    clutter_actor_set_size (priv->playqueues_pane, 150, 400);
    nbtk_table_add_actor_full (table, priv->playqueues_pane,
                               1, 3, 1, 1, NBTK_Y_FILL, 0.0, 0.0);

    nbtk_table_set_row_spacing (table, 8);
    nbtk_table_set_col_spacing (table, 8);

    /* Init Bickley and Bognor lazily */
    g_idle_add (finish_init, self);
}

