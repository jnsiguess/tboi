/*
 * Desk test of the Bag of Crafting's filtered list (hotfix 1.72, perf item 4):
 * eid_craft.c's block "the filtered list" and bag_used, cut out as they are by
 * run.py (craft_cut.h), against the per-frame loop it replaced, on random
 * recipe lists, orders and bags; and the cache: made again only when an
 * input changed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ITEM 732
#define INGREDIENTS 30
#define MAX_RECIPES 2048
typedef struct { unsigned char ing[8]; short made; } recipe_t;
typedef struct { unsigned hash; int count; recipe_t list[MAX_RECIPES]; int pending; } results_t;
static short g_sorted[MAX_ITEM + 1];
static int g_sorted_n;
static unsigned g_results_gen, g_sorted_gen;
#include "craft_cut.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* 1.72 as released: the loop of every frame */
static int old_list(const results_t *res, const unsigned char *bag, int bag_n, int tcain, short *out) {
    int n = 0;
    for (int s = 0; s < g_sorted_n; s++) {
        int id = g_sorted[s];
        for (int i = 0; i < res->count; i++) {
            const recipe_t *r = &res->list[i];
            if (r->made != id) continue;
            if (!tcain && bag_used(r->ing, bag, bag_n) != bag_n) continue;
            out[n++] = (short) i;
        }
    }
    return n;
}

static unsigned rnd_state = 172;
static unsigned rnd(void) { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; return rnd_state; }

static results_t res;

static void random_case(void) {
    res.count = (int) (rnd() % (MAX_RECIPES + 1));
    for (int i = 0; i < res.count; i++) {
        for (int k = 0; k < 8; k++) res.list[i].ing[k] = (unsigned char) (1 + rnd() % (INGREDIENTS - 1));
        int pick = (int) (rnd() % 10);
        res.list[i].made = pick == 0 ? -1 : pick == 1 ? (short) (rnd() % 900) : (short) (1 + rnd() % MAX_ITEM);
    }
    g_results_gen++;
    /* an order of some of the items, as sort_items leaves it */
    static short ids[MAX_ITEM];
    int n = 0;
    for (int id = 1; id <= MAX_ITEM; id++) if (rnd() % 10) ids[n++] = (short) id;
    for (int i = n - 1; i > 0; i--) { int j = (int) (rnd() % (unsigned) (i + 1)); short t = ids[i]; ids[i] = ids[j]; ids[j] = t; }
    memcpy(g_sorted, ids, sizeof(short) * (size_t) n);
    g_sorted_n = n;
    g_sorted_gen++;
}

int main(void) {
    static short want[MAX_RECIPES * 2];
    unsigned char bag[8];
    double old_ms = 0, new_ms = 0, hit_ms = 0;
    int cases = 300;
    for (int c = 0; c < cases; c++) {
        random_case();
        int bag_n = (int) (rnd() % 9);
        for (int k = 0; k < bag_n; k++) bag[k] = (unsigned char) (1 + rnd() % (INGREDIENTS - 1));
        int tcain = rnd() % 4 == 0;
        clock_t a = clock();
        int wn = old_list(&res, bag, bag_n, tcain, want);
        clock_t b = clock();
        int gn = filtered_now(&res, bag, bag_n, tcain);
        clock_t d = clock();
        int gn2 = filtered_now(&res, bag, bag_n, tcain);           /* the next frame: from the cache */
        clock_t e = clock();
        old_ms += (double) (b - a) * 1000.0 / CLOCKS_PER_SEC;
        new_ms += (double) (d - b) * 1000.0 / CLOCKS_PER_SEC;
        hit_ms += (double) (e - d) * 1000.0 / CLOCKS_PER_SEC;
        CHECK(wn == gn && gn == gn2, "case %d: %d recipes before, %d now", c, wn, gn);
        CHECK(!memcmp(want, g_filtered, sizeof(short) * (size_t) (wn < gn ? wn : gn)), "case %d: the order differs", c);
    }
    printf("300 random lists: the same recipes in the same order; the old loop %.3f ms a frame, "
           "making the list %.3f ms, a frame from the cache %.4f ms (this PC)\n",
           old_ms / cases, new_ms / cases, hit_ms / cases);

    /* the cache: nothing changed - the same list even if the memory under it was touched */
    random_case();
    bag[0] = 3; bag[1] = 5;
    int n0 = filtered_now(&res, bag, 2, 0);
    for (int i = 0; i < res.count; i++) res.list[i].made = -1;    /* no gen++: must not be looked at */
    CHECK(filtered_now(&res, bag, 2, 0) == n0, "unchanged inputs: the cached list");
    g_results_gen++;
    CHECK(filtered_now(&res, bag, 2, 0) == 0, "new results: made again");
    random_case();
    int a1 = filtered_now(&res, bag, 2, 0);
    bag[1] = 6;
    int a2 = filtered_now(&res, bag, 2, 0);
    int w2 = old_list(&res, bag, 2, 0, want);
    CHECK(a2 == w2, "another bag: made again (%d, want %d, was %d)", a2, w2, a1);
    int a3 = filtered_now(&res, bag, 2, 1);
    CHECK(a3 == old_list(&res, bag, 2, 1, want), "Tainted Cain: made again");
    int a4 = filtered_now(&res, bag, 3, 1);
    CHECK(a4 == old_list(&res, bag, 3, 1, want), "a third ingredient: made again");
    short t = g_sorted[0]; g_sorted[0] = g_sorted[1]; g_sorted[1] = t; g_sorted_gen++;
    filtered_now(&res, bag, 3, 1);
    int w5 = old_list(&res, bag, 3, 1, want);
    CHECK(!memcmp(want, g_filtered, sizeof(short) * (size_t) w5), "a new order (language, sort mode): made again");
    printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
