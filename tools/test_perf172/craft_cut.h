/* cut out of eid_craft.c by run.py - do not edit */
static int bag_used(const unsigned char *recipe, const unsigned char *bag, int bag_n) {
    int left[INGREDIENTS + 1] = { 0 };
    for (int i = 0; i < 8; i++) left[recipe[i]]++;
    int used = 0;
    for (int i = 0; i < bag_n; i++) if (left[bag[i]] > 0) { left[bag[i]]--; used++; }
    return used;
}

/* -- the filtered list (tools/test_perf172 cuts this block out and tests it) -- */
/*
 * filteredRecipesList, MADE ONCE (hotfix 1.72, perf item 4). The list on the
 * screen is the recipes in the order of the sorted items, each item's recipes
 * in the order they were found, and without Tainted Cain only those that use
 * the whole bag. It was worked out on every frame the bag was shown, twice -
 * once to count, once to draw: g_sorted_n (~720) times every recipe (up to
 * 2048), up to 1.5 million comparisons a frame, 2-6 ms on a weak phone. Now it
 * is made when something it depends on changes - the recipes (g_results_gen),
 * the order (g_sorted_gen), the bag, Tainted Cain - and in one pass over the
 * recipes and a sort by each item's place, not a pass per item.
 */
static short g_filtered[MAX_RECIPES];      /* indices into g_results.list, in screen order */
static int g_filtered_n;
static struct {
    int valid;
    unsigned results_gen, sorted_gen;
    unsigned char bag[8];
    int bag_n, tcain;
} g_filtered_key;
static short g_rank[MAX_ITEM + 1];          /* an item's place in g_sorted, -1 when it is not there */
static unsigned g_rank_gen;
static const recipe_t *g_filter_list;       /* for the sort below */

static int by_rank_then_found(const void *pa, const void *pb) {
    int a = *(const short *) pa, b = *(const short *) pb;
    int ra = g_rank[g_filter_list[a].made], rb = g_rank[g_filter_list[b].made];
    if (ra != rb) return ra < rb ? -1 : 1;
    return a < b ? -1 : a > b;
}

static void filtered_build(const results_t *res, const unsigned char *bag, int bag_n, int tcain) {
    if (!g_rank_gen || g_rank_gen != g_sorted_gen) {
        for (int id = 0; id <= MAX_ITEM; id++) g_rank[id] = -1;
        for (int s = 0; s < g_sorted_n; s++) g_rank[g_sorted[s]] = (short) s;
        g_rank_gen = g_sorted_gen ? g_sorted_gen : 1;
    }
    g_filtered_n = 0;
    for (int i = 0; i < res->count && i < MAX_RECIPES; i++) {
        const recipe_t *r = &res->list[i];
        if (r->made < 0 || r->made > MAX_ITEM || g_rank[r->made] < 0) continue;
        if (!tcain && bag_used(r->ing, bag, bag_n) != bag_n) continue;
        g_filtered[g_filtered_n++] = (short) i;
    }
    g_filter_list = res->list;
    qsort(g_filtered, (size_t) g_filtered_n, sizeof(short), by_rank_then_found);
}

/* the list for this frame: made again only when its inputs changed */
static int filtered_now(const results_t *res, const unsigned char *bag, int bag_n, int tcain) {
    if (!g_filtered_key.valid || g_filtered_key.results_gen != g_results_gen
        || g_filtered_key.sorted_gen != g_sorted_gen || g_filtered_key.bag_n != bag_n
        || g_filtered_key.tcain != tcain || memcmp(g_filtered_key.bag, bag, (size_t) bag_n)) {
        filtered_build(res, bag, bag_n, tcain);
        g_filtered_key.valid = 1;
        g_filtered_key.results_gen = g_results_gen;
        g_filtered_key.sorted_gen = g_sorted_gen;
        g_filtered_key.bag_n = bag_n;
        g_filtered_key.tcain = tcain;
        memcpy(g_filtered_key.bag, bag, (size_t) bag_n);
    }
    return g_filtered_n;
}

