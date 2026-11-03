/* the spawner desk's tests (07.10.2026), included at the end of desk.c */

static int count_kind(unsigned char kind) {
    int n = 0;
    for (int i = 0; i < g_rows_n; i++) n += g_rows[i].kind == kind;
    return n;
}

static int find_row(unsigned char kind, unsigned id) {
    for (int i = 0; i < g_rows_n; i++) if (g_rows[i].kind == kind && g_rows[i].id == id) return i;
    return -1;
}

static int find_pickup_row(int type, int variant, int subtype) {
    for (int i = 0; i < g_rows_n; i++)
        if (g_rows[i].kind == KIND_PICKUP && SP_PICKUP[g_rows[i].id].type == type
            && SP_PICKUP[g_rows[i].id].variant == variant && SP_PICKUP[g_rows[i].id].subtype == subtype)
            return i;
    return -1;
}

static void choose(int k) {
    list_t *l = &g_list[L_ITEMS];
    l->chosen = k;
    g_line_focus = 0;
    list_follow(l);
    for (int i = 0; i < 60; i++) frame();          /* the window follows at its own pace */
}

/* ------------------------------------------------------------------ 1. the whole list */
static void test_whole_list(void) {
    printf("1. the whole list, nothing typed\n");
    g_set = 0; g_eid_lang = 0;
    open_card(TAB_ITEMS);
    CHECK(g_rows_n > 1300, "rows %d", g_rows_n);
    CHECK(g_rows[0].kind == KIND_HEAD && g_rows[0].id == SP_SEC_ITEMS, "the first row is the ITEMS heading");
    CHECK(g_rows[1].kind == KIND_ITEM && g_rows[1].id == 1, "then The Sad Onion (%d %d)", g_rows[1].kind, g_rows[1].id);
    CHECK(g_list[L_ITEMS].chosen == 1, "the first choosable row chosen: %d", g_list[L_ITEMS].chosen);
    static const int order[] = { SP_SEC_ITEMS, SP_SEC_KEYS, SP_SEC_BOMBS, SP_SEC_COINS, SP_SEC_HEARTS, SP_SEC_BATTERIES,
                                 SP_SEC_CARDS, SP_SEC_RUNES, SP_SEC_PILLS, SP_SEC_TRINKETS, SP_SEC_LOOT };
    int h = 0, ok = 1;
    for (int i = 0; i < g_rows_n; i++)
        if (g_rows[i].kind == KIND_HEAD) { if (h >= 11 || g_rows[i].id != order[h]) ok = 0; h++; }
    CHECK(ok && h == 11, "eleven headings in the mock-up's order (%d)", h);
    int items = count_kind(KIND_ITEM), cards = 0, runes = 0;
    for (int i = 0; i < g_rows_n; i++)
        if (g_rows[i].kind == KIND_CARD) { if (EID_CARD_KIND[g_rows[i].id] == 2) runes++; else cards++; }
    printf("   collectibles %d, cards %d, runes %d, pills %d, trinkets %d, pickups %d\n", items, cards, runes,
           count_kind(KIND_PILL), count_kind(KIND_TRINKET), count_kind(KIND_PICKUP));
    CHECK(items >= 700, "collectibles %d", items);
    CHECK(cards + runes == 97, "every card and rune once (%d)", cards + runes);
    CHECK(find_row(KIND_CARD, 81) > find_row(KIND_HEAD, SP_SEC_RUNES), "a soul (81) is under RUNES, as the game types it");
    CHECK(count_kind(KIND_PILL) == 102, "50 pills, each with its horse pill, and the gold pill twice (%d)", count_kind(KIND_PILL));
    CHECK(count_kind(KIND_TRINKET) == 2 * 188, "every trinket and its golden one (%d)", count_kind(KIND_TRINKET));
    int t = find_row(KIND_TRINKET, 1);
    CHECK(t > 0 && g_rows[t + 1].kind == KIND_TRINKET && g_rows[t + 1].id == (1 | TRINKET_GOLD), "golden beside the plain");
    int p = find_row(KIND_PILL, 0);
    CHECK(p > 0 && g_rows[p + 1].id == (0 | PILL_HORSE), "the horse pill beside the pill");
    CHECK(count_kind(KIND_PICKUP) == SP_PICKUPS, "every pickup once (%d)", count_kind(KIND_PICKUP));
    CHECK(find_pickup_row(5, 340, 1) < 0 && find_pickup_row(5, 390, 1) < 0, "no story chests");
    CHECK(find_pickup_row(4, 18, 0) > find_row(KIND_HEAD, SP_SEC_LOOT), "the golden troll bomb under LOOT");
    CHECK(find_pickup_row(5, 30, 2) > find_row(KIND_HEAD, SP_SEC_KEYS) && find_pickup_row(5, 30, 2) < find_row(KIND_HEAD, SP_SEC_BOMBS),
          "the golden key under KEYS");
    /* the positions: a heading its picture's height and the air, a row 28 */
    list_t *l = &g_list[L_ITEMS];
    CHECK(fabsf(row_h(l, 1) - 28.0f) < 0.01f, "a row is 28 (%.1f)", row_h(l, 1));
    CHECK(fabsf(row_h(l, 0) - (float) SP_SECTION[0][SP_SEC_ITEMS].h - 7.0f) < 0.01f, "a heading its height + 7");
    close_card();
}

/* ------------------------------------------------------------------ 2. what the card draws, every set */
static void test_layout(void) {
    printf("2. the layout in every set: five rows, the toggle, nothing past the paper\n");
    static const char *SETS[7] = { "en", "ru", "de", "es", "jp", "kr", "zh" };
    for (int set = 0; set < 7; set++) {
        g_set = set; g_eid_lang = set;
        open_card(TAB_ITEMS);
        list_t *l = &g_list[L_ITEMS];
        float window = l->bottom - l->top;
        /* rows of 28 that lie wholly inside the window, the heading not counted */
        int whole = (int) (window / 28.0f);
        printf("   %s: list %.0f..%.0f (%.0f units, %d whole rows), the query's air given back %.0f, the title's %.0f\n",
               SETS[set], l->top - g_card_y, l->bottom - g_card_y, window, whole, g_query_cut, g_title_cut);
        if (set != 5) CHECK(whole >= 5, "%s: five whole rows (%d)", SETS[set], whole);
        else if (whole < 5) printf("   NOTE kr: %d whole rows - the Korean title (35) and tabs (20) are the tallest\n", whole);
        const txt_t *tg = txt_find(SP_GROUND[set]);
        CHECK(tg != NULL, "%s: the toggle is drawn", SETS[set]);
        float right = g_card_x + g_card_w - CARD_PAD;
        const txt_t *v = txt_find(SP_OFF[set]);
        CHECK(v && v->x + v->w <= right + 0.01f, "%s: OFF inside the card (%.1f <= %.1f)", SETS[set], v ? v->x + v->w : -1, right);
        const txt_t *q = txt_find(">");
        CHECK(q && q->x + q->w < g_toggle_x, "%s: the line ends before the toggle", SETS[set]);
        /* a long query: the line shows its end, never reaching the toggle */
        type_text("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        q = txt_find(">");
        CHECK(q && q->x + q->w <= g_toggle_x - 12.0f + 0.01f, "%s: a long query stays clear of the toggle", SETS[set]);
        clear_query();
        close_card();
    }
    g_set = 0; g_eid_lang = 0;
}

/* every row of the whole list, chosen with its count: the name clear of the count, the count inside the card */
static void test_widths(void) {
    printf("3. every row's name against its count, every set\n");
    static const char *SETS[7] = { "en", "ru", "de", "es", "jp", "kr", "zh" };
    for (int set = 0; set < 7; set++) {
        g_set = set; g_eid_lang = set;
        open_card(TAB_ITEMS);
        float right = g_card_x + g_card_w - CARD_PAD;
        float x = g_card_x + CARD_PAD + 32.0f + 8.0f;
        int over = 0, over_plain = 0;
        char worst[160] = "";
        float worst_w = 0.0f;
        for (int k = 0; k < g_rows_n; k++) {
            const hit_t *r = &g_rows[k];
            if (r->kind == KIND_HEAD) continue;
            char buf[160];
            const char *name = row_name(r, buf, sizeof(buf));
            float w = shim_eid_width_scaled(name, 1.5f);
            float room = right - x;
            if (count_of(r)) room -= shim_eid_width_scaled("<  99  >", 1.5f) + 6.0f;
            if (w > room) {
                if (count_of(r)) over++; else over_plain++;
                if (w - room > worst_w) { worst_w = w - room; snprintf(worst, sizeof(worst), "%s", name); }
            }
        }
        printf("   %s: %d names run into their count, %d past the card; the worst by %.1f: %s\n", SETS[set], over,
               over_plain, worst_w, worst);
        CHECK(over_plain == 0, "%s: no name past the card", SETS[set]);
        close_card();
    }
    g_set = 0; g_eid_lang = 0;
}

/* the longest names, drawn chosen with a count: cut with "…" before the count, never under it */
static void test_cut(void) {
    printf("3b. a long name on a row with a count is cut before the count\n");
    for (int set = 0; set < 4; set++) {
        g_set = set; g_eid_lang = set;
        open_card(TAB_ITEMS);
        int worst = -1;
        float worst_w = 0.0f;
        for (int k = 0; k < g_rows_n; k++) {
            char b[160];
            if (!count_of(&g_rows[k])) continue;
            float w = shim_eid_width_scaled(row_name(&g_rows[k], b, sizeof(b)), 1.5f);
            if (w > worst_w) { worst_w = w; worst = k; }
        }
        g_count[count_slot(&g_rows[worst])] = 98;          /* 99: the widest count */
        choose(worst);
        frame();
        const txt_t *cnt = txt_find("<  99  >");
        const txt_t *nm = NULL;
        for (int i = 0; i < g_ntxt; i++)
            if (strstr(g_txt[i].text, "\xe2\x80\xa6")) nm = &g_txt[i];
        CHECK(cnt && nm && nm->x + nm->w <= cnt->x - 6.0f + 0.01f, "set %d: '%s' ends at %.1f, the count starts at %.1f",
              set, nm ? nm->text : "(not cut)", nm ? nm->x + nm->w : -1.0f, cnt ? cnt->x : -1.0f);
        if (nm) printf("   set %d: %s\n", set, nm->text);
        g_count[count_slot(&g_rows[worst])] = 0;
        close_card();
    }
    g_set = 0; g_eid_lang = 0;
}

/* the pictures: every kind from its own sheet and frame */
static void test_pictures(void) {
    printf("3c. the pictures of the new kinds\n");
    g_set = 0; g_eid_lang = 0;
    open_card(TAB_ITEMS);
    int k = find_row(KIND_TRINKET, 19 | TRINKET_GOLD);
    choose(k);
    frame();
    int golden = 0, plain_pics = 0;
    for (int i = 0; i < g_npics; i++) if (!strcmp(g_pics[i].anim, "Golden") && g_pics[i].frame == 18) golden = 1;
    CHECK(golden, "the golden trinket 19 from \"Golden\" frame 18");
    k = find_pickup_row(5, 57, 1);
    choose(k);
    frame();
    int icon = 0;
    for (int i = 0; i < g_npics; i++) if (!strcmp(g_pics[i].anim, "Icons") && g_pics[i].frame == g_rows[k].id) icon = 1;
    CHECK(icon, "the mega chest from \"Icons\" frame %d", g_rows[k].id);
    k = find_row(KIND_PILL, PILL_GOLD | PILL_HORSE);
    choose(k);
    frame();
    int pill = 0;
    for (int i = 0; i < g_npics; i++) if (!strcmp(g_pics[i].anim, "Pills") && g_pics[i].frame == 3) pill = 1;
    CHECK(pill, "the gold horse pill from \"Pills\" frame 3");
    (void) plain_pics;
    choose(1);
    frame();
    int head = 0;
    for (int i = 0; i < g_npics; i++) if (!strcmp(g_pics[i].anim, "Sections") && g_pics[i].frame == SP_SEC_ITEMS) head = 1;
    CHECK(head, "the ITEMS heading from \"Sections\"");
    close_card();
}

/* ------------------------------------------------------------------ 4. search and scroll */
static void test_search(void) {
    printf("4. the search and the scroll\n");
    g_set = 0; g_eid_lang = 0;
    open_card(TAB_ITEMS);
    type_text("gold");
    CHECK(g_rows_n > 5 && count_kind(KIND_HEAD) == 0, "a search lists hits alone, no headings (%d rows)", g_rows_n);
    int kinds[8] = { 0 };
    for (int i = 0; i < g_rows_n; i++) kinds[g_rows[i].kind]++;
    printf("   'gold': %d rows - items %d, trinkets %d, cards %d, pills %d, pickups %d\n", g_rows_n, kinds[KIND_ITEM],
           kinds[KIND_TRINKET], kinds[KIND_CARD], kinds[KIND_PILL], kinds[KIND_PICKUP]);
    CHECK(kinds[KIND_PICKUP] >= 5, "golden key, bomb, penny, heart, battery... found (%d)", kinds[KIND_PICKUP]);
    CHECK(kinds[KIND_PILL] >= 2, "the gold pill and its horse one");
    CHECK(find_pickup_row(5, 30, 2) >= 0, "Golden Key found");
    char b[160];
    printf("   first rows:");
    for (int i = 0; i < 6 && i < g_rows_n; i++) printf(" [%s]", row_name(&g_rows[i], b, sizeof(b)));
    printf("\n");
    clear_query();
    CHECK(count_kind(KIND_HEAD) == 11, "the whole list back when the line is empty");
    type_text("114");
    CHECK(g_rows_n >= 1 && g_rows[0].kind == KIND_ITEM && g_rows[0].id == 114, "a number finds its item first (Mom's Knife)");
    clear_query();
    /* Russian names and the English ones both */
    g_set = 1; g_eid_lang = 1;
    clear_query();
    type_text("ключ");
    CHECK(find_pickup_row(5, 30, 1) >= 0 && find_pickup_row(5, 30, 2) >= 0, "ru: Ключ, Золотой ключ found");
    clear_query();
    type_text("chest");
    CHECK(find_pickup_row(5, 50, 1) >= 0, "ru set: the English 'chest' finds Сундук");
    clear_query();
    /* the scroll: a pad's walk down keeps the chosen row in the window */
    g_set = 0; g_eid_lang = 0;
    list_t *l = &g_list[L_ITEMS];
    for (int i = 0; i < 40; i++) press(ACTION_DOWN);
    for (int i = 0; i < 60; i++) frame();
    float top = l->pos[l->chosen] - l->scroll, bottom = l->pos[l->chosen + 1] - l->scroll;
    CHECK(top >= -0.01f && bottom <= l->bottom - l->top + 0.01f, "the chosen row (%d) inside the window after 40 downs", l->chosen);
    int k = find_row(KIND_HEAD, SP_SEC_KEYS);
    choose(k + 1);
    CHECK(g_rows[l->chosen].kind == KIND_PICKUP, "the key row chosen");
    press(ACTION_UP);
    CHECK(g_rows[l->chosen].kind == KIND_ITEM, "up from the first key skips the KEYS heading to the last collectible");
    close_card();
}

/* ------------------------------------------------------------------ 5. counts and the toggle */
static void test_counts_toggle(void) {
    printf("5. the counts and the toggle, by pad, keyboard and finger\n");
    g_set = 0; g_eid_lang = 0;
    memset(g_count, 0, sizeof(g_count));
    g_ground = 0;
    open_card(TAB_ITEMS);
    list_t *l = &g_list[L_ITEMS];
    /* up from the first row: the line; right turns the toggle and saves it */
    CHECK(l->chosen == 1 && !g_line_focus, "the first row chosen");
    press(ACTION_UP);
    CHECK(g_line_focus, "up from the first row reaches the line");
    int saves = g_saves;
    press(ACTION_RIGHT);
    CHECK(g_ground == 1 && g_saves == saves + 1, "right on the line: On the ground ON, port-touch.ini written");
    press(ACTION_CONFIRM);
    CHECK(g_ground == 0, "confirm on the line turns it back");
    shim_spawner_key(79, 1, 0, 0, 0);               /* the keyboard's right arrow */
    shim_spawner_key(79, 0, 0, 0, 0);
    frame();
    CHECK(g_ground == 1, "the keyboard's right arrow on the line turns it");
    press(ACTION_DOWN);
    CHECK(!g_line_focus && l->chosen == 1, "down leaves the line for the row it was on");
    /* a collectible's left and right stay the player switch's */
    press(ACTION_RIGHT);
    CHECK(g_ground == 1, "right on a collectible does not touch the toggle");
    /* the count: a key row */
    int k = find_pickup_row(5, 30, 2) ;
    choose(k);
    press(ACTION_RIGHT);
    press(ACTION_RIGHT);
    CHECK(count_of(&g_rows[k]) == 3, "two rights: 3 (%d)", count_of(&g_rows[k]));
    for (int i = 0; i < 5; i++) press(ACTION_LEFT);
    CHECK(count_of(&g_rows[k]) == 1, "never below 1 (%d)", count_of(&g_rows[k]));
    count_step(500);
    CHECK(count_of(&g_rows[k]) == 99, "never above 99 (%d)", count_of(&g_rows[k]));
    count_step(-98);
    CHECK(count_of(&g_rows[k + 1]) == 1, "the next row keeps its own count");
    /* the hold: a held right speeds up like STATS */
    g_held[ACTION_RIGHT] = 1;
    g_pressed[ACTION_RIGHT] = 1;
    frame();
    int after_press = count_of(&g_rows[k]);
    CHECK(after_press == 2, "a held right steps once at once (%d)", after_press);
    g_held[ACTION_RIGHT] = 0;
    frame();
    /* the drawn count: "<  N  >" on the chosen row, "xN" on another */
    count_step(3);                                   /* 5 */
    g_count[count_slot(&g_rows[k - 1])] = 6;         /* the row above: 7 */
    frame();
    CHECK(txt_find("<  5  >") != NULL, "the chosen row's count drawn between arrows");
    CHECK(txt_find("\xc3\x97" "7") != NULL, "another row's count drawn as x7");
    /* the finger: the arrows step, the name gives */
    const txt_t *c = txt_find("<  5  >");
    tap(g_cnt_r1 - 2.0f, c->y + 8.0f);
    CHECK(count_of(&g_rows[k]) == 6, "a tap on > steps (%d)", count_of(&g_rows[k]));
    tap(g_cnt_l0 + 2.0f, c->y + 8.0f);
    CHECK(count_of(&g_rows[k]) == 5, "a tap on < steps back (%d)", count_of(&g_rows[k]));
    /* the finger on the toggle */
    int was = g_ground;
    const txt_t *tg = txt_find(SP_GROUND[0]);
    tap(tg->x + 10.0f, tg->y + 8.0f);
    CHECK(g_ground == !was, "a tap on the toggle turns it");
    tap(g_card_x + 60.0f, tg->y + 8.0f);
    CHECK(g_ground == !was && g_ime == 1, "a tap on the line left of it brings the keyboard, the toggle stays");
    close_card();
}

/* ------------------------------------------------------------------ 6. what the give asks of the game */
static void give_row_at(int k) {
    if (getenv("DESK_TRACE")) printf("   give row %d\n", k);
    calls_clear();
    g_desk_spawned_n = 0;
    shim_spawner_give(k);
}

static void test_give(void) {
    printf("6. the give: what is asked of the game\n");
    g_set = 0; g_eid_lang = 0;
    g_report = fopen("NUL", "w");
    open_card(TAB_ITEMS);
    memset(g_count, 0, sizeof(g_count));
    memset(g_player[0] + 0x1ab0, 0, 8);
    int sad = find_row(KIND_ITEM, 1);
    /* a collectible */
    g_ground = 0; give_row_at(sad);
    CHECK(calls_find("AddCollectible 1 charge 0 first 1") && !calls_find("Spawn"), "OFF: AddCollectible");
    CHECK(calls_find("SetReadOnly 1") != NULL, "the run closed before the give");
    g_ground = 1; give_row_at(sad);
    CHECK(calls_find("Spawn 5.100.1 spawner none") && calls_find("Spawn 1000.15.0") && calls_find("FindFree step 40")
          && !calls_find("AddCollectible"), "ON: a pedestal beside the player, FindFree 40, and the poof");
    /* a pickup into the player, three of them */
    int key = find_pickup_row(5, 30, 2);
    g_count[count_slot(&g_rows[key])] = 2;
    g_ground = 0; give_row_at(key);
    CHECK(calls_count("Spawn 5.30.2") == 3 && calls_count("Collide 5.30.2 ") == 3, "OFF: 3 golden keys spawned and collided");
    g_ground = 1; give_row_at(key);
    CHECK(calls_count("Spawn 5.30.2") == 3 && calls_count("Collide") == 0, "ON: 3 on the floor, none collided");
    /* hearts the character cannot take stay on the floor (the model refuses) */
    g_collide_takes = 0;
    g_ground = 0; give_row_at(find_pickup_row(5, 10, 1));
    CHECK(calls_count("Collide") == 1, "a refused heart: the collision asked, the pickup left where it lies");
    g_collide_takes = 1;
    /* loot: always on the floor, closed chests */
    g_ground = 0;
    give_row_at(find_pickup_row(5, 50, 1));
    CHECK(calls_find("Spawn 5.50.1") && !calls_find("Collide"), "a chest: closed (1), never collided");
    give_row_at(find_pickup_row(5, 53, 1));
    CHECK(calls_find("Spawn 5.53.2") != NULL, "the eternal chest: 2, as the game closes it (%s)", g_ncalls ? g_calls[g_ncalls - 1] : "");
    give_row_at(find_pickup_row(5, 57, 1));
    const char *m = calls_find("Spawn 5.57.");
    int ms = m ? atoi(strrchr(m, '.') + 1) : -1;
    CHECK(ms >= 1 && ms <= 7, "the mega chest: 1 + rand 4 + rand 4 (%d)", ms);
    give_row_at(find_pickup_row(4, 3, 0));
    CHECK(calls_find("Spawn 4.3.0") && !calls_find("Collide"), "a troll bomb: 4.3.0, never collided");
    give_row_at(find_pickup_row(5, 69, 2));
    CHECK(calls_find("Spawn 5.69.2") && !calls_find("Collide"), "a black sack on the floor");
    /* pills */
    give_row_at(find_row(KIND_PILL, 4));
    CHECK(calls_find("ForcePill pool ok effect 4") && calls_find("AddPill 5"), "a pill by effect: ForceAddPillEffect, AddPill");
    give_row_at(find_row(KIND_PILL, 4 | PILL_HORSE));
    CHECK(calls_find("AddPill 2053") != NULL, "a horse pill: the colour | 0x800");
    give_row_at(find_row(KIND_PILL, PILL_GOLD));
    CHECK(calls_find("AddPill 14") && !calls_find("ForcePill"), "the gold pill: colour 14, no effect forced");
    give_row_at(find_row(KIND_PILL, PILL_GOLD | PILL_HORSE));
    CHECK(calls_find("AddPill 2062") != NULL, "the gold horse pill: 14 | 0x800");
    g_ground = 1; give_row_at(find_row(KIND_PILL, 4));
    CHECK(calls_find("Spawn 5.70.5") != NULL, "ON: the pill on the floor in its colour");
    /* cards */
    g_ground = 0;
    int card = find_row(KIND_CARD, 1);
    g_count[count_slot(&g_rows[card])] = 1;
    give_row_at(card);
    CHECK(calls_count("AddCard 1") == 2, "OFF: AddCard twice for a count of 2");
    g_ground = 1; give_row_at(card);
    CHECK(calls_count("Spawn 5.300.1") == 2, "ON: two on the floor");
    /* trinkets, golden */
    g_ground = 0;
    memset(g_player[0] + 0x1ab0, 0, 8);
    int gold = find_row(KIND_TRINKET, 19 | TRINKET_GOLD);
    give_row_at(gold);
    CHECK(calls_find("AddTrinket 32787 first 1") != NULL, "a golden trinket: AddTrinket(19 | 0x8000)");
    give_row_at(gold);
    CHECK(calls_find("Spawn 5.350.32787 spawner player") != NULL, "the slot taken: on the floor, the game's swap");
    g_ground = 1; give_row_at(find_row(KIND_TRINKET, 1));
    CHECK(calls_find("Spawn 5.350.1 spawner none") && !calls_find("AddTrinket"), "ON: a trinket on the floor");
    /* a heading gives nothing */
    give_row_at(0);
    CHECK(g_ncalls == 0, "a heading gives nothing");
    g_ground = 0;
    close_card();
    fclose(g_report);
    g_report = NULL;
}

/* ------------------------------------------------------------------ 7. STATS: the rows per character, the deltas */
static int srow_find(int kind, int id, int other) {
    for (int k = 0; k < g_list[L_STATS].count; k++)
        if (g_srow[k].kind == kind && g_srow[k].id == id && g_srow[k].other == other) return k;
    return -1;
}

static void stat_choose(int k) {
    g_stat_focus = FOCUS_LIST;
    g_list[L_STATS].chosen = k;
    list_follow(&g_list[L_STATS]);
    for (int i = 0; i < 40; i++) frame();
}

static void health_names(char *out, size_t n) {
    out[0] = 0;
    for (int k = 0; k < g_list[L_STATS].count; k++)
        if (g_srow[k].kind == SR_HEALTH) {
            size_t at = strlen(out);
            snprintf(out + at, n - at, "%s%s%s", at ? ", " : "", g_srow[k].other ? "other:" : "", SP_HEALTH_NAME[0][g_srow[k].id]);
        }
}

static void as_character(int type) {
    memset(g_player[0] + 0x1600, 0, 0xf00);
    *(int *) (g_player[0] + 0x1738) = type;
    *(unsigned long long *) (g_player[0] + 0x2460) = 0;
    if (type == 16 || type == 17) {
        memset(g_player[1], 0, 0x3000);
        *(int *) (g_player[1] + 0x1738) = type == 16 ? 17 : 16;
        *(unsigned long long *) (g_player[0] + 0x2460) = (unsigned long long) g_player[1];
        *(unsigned long long *) (g_player[1] + 0x2460) = (unsigned long long) g_player[0];
    }
    frame();
}

static void test_stats(void) {
    printf("7. STATS: the rows per character, the deltas through the game's adders\n");
    g_set = 0; g_eid_lang = 0;
    g_report = fopen("NUL", "w");
    open_card(TAB_STATS);
    static const struct { int type; const char *name; const char *want; } CH[] = {
        { 0, "Isaac", "Heart containers, Red hearts, Soul hearts, Black hearts, Bone hearts, Broken hearts, Eternal heart, Golden hearts, Rotten hearts" },
        { 14, "Keeper", "Coin containers, Coin hearts, Golden hearts, Broken hearts" },
        { 33, "T.Keeper", "Coin containers, Coin hearts, Golden hearts, Broken hearts" },
        { 18, "Bethany", "Heart containers, Red hearts, Bone hearts, Broken hearts, Eternal heart, Golden hearts, Rotten hearts, Soul charges" },
        { 36, "T.Bethany", "Soul hearts, Black hearts, Bone hearts, Broken hearts, Golden hearts, Blood charges" },
        { 4, "???", "Soul hearts, Black hearts, Bone hearts, Broken hearts, Golden hearts" },
        { 10, "The Lost", "" },
        { 31, "T.Lost", "" },
        { 16, "The Forgotten", "Bone hearts, Red hearts, Broken hearts, Eternal heart, Golden hearts, Rotten hearts, other:Soul hearts, other:Black hearts" },
        { 17, "The Soul", "Soul hearts, Black hearts, Broken hearts, Golden hearts, other:Bone hearts, other:Red hearts, other:Rotten hearts" },
    };
    char got[512];
    for (unsigned i = 0; i < sizeof(CH) / sizeof(CH[0]); i++) {
        as_character(CH[i].type);
        health_names(got, sizeof(got));
        CHECK(!strcmp(got, CH[i].want), "%s: [%s]", CH[i].name, got);
        int head = srow_find(SR_HEAD, SP_SEC_HEALTH, 0);
        CHECK((head >= 0) == (CH[i].want[0] != 0), "%s: the HEALTH heading only with rows", CH[i].name);
    }
    /* the deltas */
    as_character(0);
    *(int *) (g_player[0] + 0x16b8) = 6;
    *(int *) (g_player[0] + 0x16bc) = 5;
    frame();
    int k = srow_find(SR_HEALTH, SP_H_RED, 0);
    stat_choose(k);
    CHECK(txt_find("<  2\xc2\xbd  >") != NULL, "red 5 halves shown 2½ between arrows");
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddHearts 1 1") != NULL, "right, then A: AddHearts(+1 half, as is)");
    calls_clear(); press(ACTION_LEFT); press(ACTION_LEFT); press(ACTION_CONFIRM);
    CHECK(calls_count("AddHearts -2 1") == 1 && calls_count("AddHearts") == 1, "left twice, then A: one AddHearts(-2)");
    k = srow_find(SR_HEALTH, SP_H_CONTAINERS, 0);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddMaxHearts 2 0") != NULL, "containers: a whole heart, AddMaxHearts(2, 0)");
    k = srow_find(SR_HEALTH, SP_H_BLACK, 0);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddBlackHearts 1") != NULL, "black up: AddBlackHearts");
    frame();
    CHECK(shim_health_get((unsigned long long) g_player[0], SP_H_BLACK, 0) == 1, "black read back from the mask: 1 half");
    calls_clear(); press(ACTION_LEFT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddSoulHearts -1") != NULL, "black down: AddSoulHearts(-1), the game's own loss");
    k = srow_find(SR_HEALTH, SP_H_ROTTEN, 0);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddRottenHearts 2 1") != NULL, "rotten up: AddRottenHearts(2 halves, as is)");
    /* typing a value: the delta to it */
    k = srow_find(SR_HEALTH, SP_H_SOUL, 0);
    stat_choose(k);
    int before = shim_health_get((unsigned long long) g_player[0], SP_H_SOUL, 0);
    press(ACTION_CONFIRM);                            /* typing starts */
    shim_spawner_text("3"); shim_spawner_text("."); shim_spawner_text("5");
    calls_clear(); shim_spawner_text("\n"); frame();
    char want[40];
    snprintf(want, sizeof(want), "AddSoulHearts %d", 7 - before);
    CHECK(calls_find(want) != NULL, "typed 3.5 soul hearts: AddSoulHearts(%d) (%s)", 7 - before, g_ncalls ? g_calls[0] : "-");
    /* counters */
    k = srow_find(SR_COUNTER, 0, 0);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddCoins 1") != NULL, "coins: AddCoins(+1)");
    *(int *) (g_player[0] + 0x16e0) = 99;
    calls_clear(); g_note_left = 0; press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(g_note_left > 0 && strstr(g_note, "99"), "at the game's limit: the note says so (%s)", g_note);
    /* The Forgotten: The Soul's rows go through the body, read from the soul */
    as_character(16);
    k = srow_find(SR_HEALTH, SP_H_SOUL, 1);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddSoulHearts 1") && *(int *) (g_player[1] + 0x16c4) == 1, "The Soul's soul hearts: the adder on the body, read from the soul");
    CHECK(txt_find("The Soul: Soul hearts") != NULL, "named 'The Soul: Soul hearts'");
    /* Keeper */
    as_character(14);
    k = srow_find(SR_HEALTH, SP_H_COIN_HEARTS, 0);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddHearts 2 1") != NULL, "a coin heart: AddHearts(2, as is), whole");
    /* Bethany */
    as_character(18);
    k = srow_find(SR_HEALTH, SP_H_SOUL_CHARGE, 0);
    stat_choose(k);
    calls_clear(); press(ACTION_RIGHT); press(ACTION_CONFIRM);
    CHECK(calls_find("AddSoulCharge 1") != NULL, "Bethany: AddSoulCharge");
    /* the foot */
    as_character(0);
    g_stat_focus = FOCUS_FOOT; g_foot_button = FOOT_CHARGE;
    *(int *) (g_player[0] + 0x1964) = 105;              /* the primary: D6 */
    *(int *) (g_player[0] + 0x1964 + 0x1c) = 0;
    *(int *) (g_player[0] + 0x1964 + 0x38) = 263;       /* the pocket active */
    *(int *) (g_player[0] + 0x1964 + 0x54) = 77;        /* slot 3: not ours */
    calls_clear(); press(ACTION_CONFIRM);
    CHECK(calls_find("FullCharge slot 0 force 0") && calls_find("FullCharge slot 2 force 0")
          && !calls_find("FullCharge slot 1") && !calls_find("FullCharge slot 3"), "CHARGE: the primary and the pocket, not the empty Schoolbag");
    *(int *) (g_player[0] + 0x1964 + 0x1c) = 534;
    calls_clear(); g_note_left = 0; press(ACTION_CONFIRM);
    CHECK(calls_count("FullCharge") == 3, "with the Schoolbag's: three");
    for (int i = 0; i < 4; i++) *(int *) (g_player[0] + 0x1964 + i * 0x1c) = 0;
    calls_clear(); g_note_left = 0; press(ACTION_CONFIRM);
    CHECK(calls_count("FullCharge") == 0 && strstr(g_note, SP_NO_ACTIVE[0]), "no active item: said, nothing charged");
    g_note_left = 0; frame();
    CHECK(txt_find(SP_CHARGE[0]) && txt_find(SP_RESET[0]), "the foot draws both buttons");
    /* the walk: the list's end goes to the foot, the foot's left/right picks the button */
    g_stat_focus = FOCUS_LIST;
    stat_choose(g_list[L_STATS].count - 1);
    press(ACTION_DOWN);
    CHECK(g_stat_focus == FOCUS_FOOT, "down past the last row: the foot");
    press(ACTION_RIGHT);
    CHECK(g_foot_button == FOOT_RESET, "right on the foot: RESET");
    press(ACTION_UP);
    CHECK(g_stat_focus == FOCUS_LIST, "up from the foot: the list");
    close_card();
    fclose(g_report);
    g_report = NULL;
}

/* ------------------------------------------------------------------ 8. STATS fits, every set */
static void test_stats_layout(void) {
    printf("8. STATS in every set: names clear of the values\n");
    static const char *SETS[7] = { "en", "ru", "de", "es", "jp", "kr", "zh" };
    as_character(0);
    for (int set = 0; set < 7; set++) {
        g_set = set; g_eid_lang = set;
        open_card(TAB_STATS);
        float x = g_card_x + CARD_PAD + 24.0f, right = g_card_x + g_card_w - CARD_PAD;
        float vw = shim_eid_width_scaled("<  12\xc2\xbd  >", 1.5f);
        int bad = 0;
        for (int r = 0; r < SP_HEALTH_ROWS; r++)
            if (x + shim_eid_width_scaled(SP_HEALTH_NAME[set][r], 1.5f) > right - vw - 4.0f) {
                bad++;
                printf("   %s: '%s' meets its value\n", SETS[set], SP_HEALTH_NAME[set][r]);
            }
        char soul[96];
        snprintf(soul, sizeof(soul), SP_SOUL_FMT[set], SP_CHARACTER[17][set], SP_HEALTH_NAME[set][SP_H_BLACK]);
        if (x + shim_eid_width_scaled(soul, 1.5f) > right - vw - 4.0f) { bad++; printf("   %s: '%s' meets its value\n", SETS[set], soul); }
        float foot = shim_eid_width_scaled(SP_CHARGE[set], 1.5f) + 24.0f + shim_eid_width_scaled(SP_RESET[set], 1.5f);
        CHECK(foot <= g_card_w - 2 * CARD_PAD, "%s: the foot's two buttons fit (%.0f)", SETS[set], foot);
        CHECK(bad == 0, "%s: every health name clear of its value", SETS[set]);
        CHECK(shim_eid_width_scaled(SP_APPLY_HINT[set], 1.5f) <= g_card_w - 2 * CARD_PAD, "%s: the apply hint fits the foot (%.0f)", SETS[set], shim_eid_width_scaled(SP_APPLY_HINT[set], 1.5f));
        close_card();
    }
    g_set = 0; g_eid_lang = 0;
}

/* ------------------------------------------------------------------ 9. STATS: set with the arrows, apply once */
static void test_confirm(void) {
    printf("9. STATS: the arrows set a value, the row or A applies it once, with the confirmation\n");
    static const char *HINT[7] = {
        "apply: tap the row, A or ENTER", "применить: тап по строке, A или ENTER",
        "übernehmen: Zeile antippen, A oder ENTER", "aplicar: toca la fila, A o ENTER", "決定：行をタップ、AかENTER",
        "적용: 줄을 탭, A 또는 ENTER", "应用：点该行、A或ENTER",
    };
    for (int i = 0; i < 7; i++) CHECK(!strcmp(SP_APPLY_HINT[i], HINT[i]), "the apply hint of set %d: %s", i, SP_APPLY_HINT[i]);
    g_set = 0; g_eid_lang = 0;
    g_report = fopen("NUL", "w");
    as_character(0);
    *(int *) (g_player[0] + 0x16b8) = 6;
    *(int *) (g_player[0] + 0x16bc) = 2;
    *(float *) (g_player[0] + 0x1db4) = 1.0f;
    *(float *) (g_player[0] + 0x1844) = 3.5f;
    open_card(TAB_STATS);
    /* steps: the value waits, nothing reaches the game, nothing sounds */
    int k = srow_find(SR_HEALTH, SP_H_RED, 0);
    stat_choose(k);
    Sleep(200);
    calls_clear(); g_flash_row = -1;
    press(ACTION_RIGHT); press(ACTION_RIGHT);
    CHECK(g_ncalls == 0 && g_flash_row == -1, "two steps: no game call, no sound, no flash (%d calls)", g_ncalls);
    CHECK(txt_find("<  2 ?  >") != NULL, "the waiting value shown with ' ?': 2 hearts");
    CHECK(txt_find(SP_APPLY_HINT[0]) != NULL, "the foot says how to apply");
    CHECK(*(int *) (g_player[0] + 0x16bc) == 2, "the game's red hearts untouched");
    /* the held repeat: still nothing */
    g_held[ACTION_RIGHT] = 1;
    for (int i = 0; i < 5; i++) frame();
    g_held[ACTION_RIGHT] = 0;
    frame();
    CHECK(calls_count("Sound") == 0 && calls_count("AddHearts") == 0, "held: nothing applied, nothing sounds");
    /* A applies once */
    int want = g_pend.units;
    calls_clear(); press(ACTION_CONFIRM);
    char line[40];
    snprintf(line, sizeof(line), "AddHearts %d 1", want - 2);
    CHECK(calls_count("AddHearts") == 1 && calls_find(line) && calls_count("Sound 128") == 1 && g_flash_row == 2000 + k,
          "A: one AddHearts(%d), sound 128, the row flashes", want - 2);
    CHECK(!g_pend.on && !txt_find(" ?"), "applied: nothing waits");
    /* a tap on the row (its name) applies too */
    Sleep(170);
    press(ACTION_LEFT);
    const txt_t *nm = txt_find(SP_HEALTH_NAME[0][SP_H_RED]);
    calls_clear();
    tap(nm->x + 4.0f, nm->y + 8.0f);
    CHECK(calls_count("AddHearts -1 1") == 1 && calls_count("Sound 128") == 1, "a tap on the row's name applies it");
    /* another row drops it */
    press(ACTION_RIGHT);
    calls_clear(); press(ACTION_DOWN); press(ACTION_UP);
    CHECK(g_ncalls == 0 && !g_pend.on && !txt_find(" ?"), "down and back: dropped, nothing applied, the game's value shown");
    /* another tab drops it */
    press(ACTION_RIGHT);
    calls_clear(); g_want_tab = TAB_ITEMS; frame(); g_want_tab = TAB_STATS; frame();
    stat_choose(k);
    CHECK(g_ncalls == 0 && !g_pend.on, "another tab: dropped");
    /* another body drops it */
    press(ACTION_RIGHT);
    g_pend.p = 1;                                    /* as if set for another player */
    frame();
    CHECK(!g_pend.on && !txt_find(" ?"), "another body: dropped");
    /* closing the card drops it */
    press(ACTION_RIGHT);
    calls_clear(); close_card(); open_card(TAB_STATS);
    CHECK(!g_pend.on && calls_count("AddHearts") == 0, "the card closed: dropped");
    /* the game refuses: the note, no sound */
    k = srow_find(SR_COUNTER, 0, 0);
    stat_choose(k);
    *(int *) (g_player[0] + 0x16e0) = 99;
    Sleep(170);
    press(ACTION_RIGHT);
    calls_clear(); g_flash_row = -1; g_note_left = 0; press(ACTION_CONFIRM);
    CHECK(calls_count("AddCoins 1") == 1 && calls_count("Sound 128") == 0 && g_flash_row == -1 && g_note_left > 0,
          "refused at 99: the note, no sound, no flash");
    /* a stat: damage through D8 */
    k = srow_find(SR_STAT, 2, 0);
    stat_choose(k);
    Sleep(170);
    press(ACTION_RIGHT); press(ACTION_RIGHT);
    CHECK(txt_find("4.50 ?") != NULL && fabsf(*(float *) (g_player[0] + 0x1844) - 3.5f) < 0.01f, "damage waits at 4.50, the game's still 3.50");
    calls_clear(); g_note_left = 0; press(ACTION_CONFIRM);
    CHECK(calls_count("Sound 128") == 1 && fabsf(*(float *) (g_player[0] + 0x1844) - 4.5f) < 0.01f,
          "A: damage 4.50, one sound (%.2f)", *(float *) (g_player[0] + 0x1844));
    /* the arrows stop at what the game allows */
    for (int i = 0; i < 20; i++) press(ACTION_LEFT);
    CHECK(g_pend.on && fabsf(g_pend.value - 0.5f) < 0.001f, "damage arrows stop at 0.50 (%.2f)", g_pend.value);
    pend_drop();
    /* typed past the limit: the note, no sound (unchanged) */
    Sleep(170);
    press(ACTION_CONFIRM);
    shim_spawner_text("0"); shim_spawner_text("."); shim_spawner_text("1");
    calls_clear(); shim_spawner_text("\n"); frame();
    CHECK(calls_count("Sound 128") == 0 && g_note_left > 0, "damage 0.1 typed: below its 0.5, the limit said, no sound");
    /* typed and taken: applied at once */
    k = srow_find(SR_HEALTH, SP_H_SOUL, 0);
    stat_choose(k);
    Sleep(170);
    press(ACTION_CONFIRM);
    shim_spawner_text("2");
    calls_clear(); shim_spawner_text("\n"); frame();
    CHECK(calls_count("Sound 128") == 1, "soul hearts typed: applied at once, the sound");
    /* the foot: CHARGE with an item, RESET with an edit to undo */
    *(int *) (g_player[0] + 0x1964) = 105;
    g_stat_focus = FOCUS_FOOT; g_foot_button = FOOT_CHARGE;
    Sleep(170);
    calls_clear(); press(ACTION_CONFIRM);
    CHECK(calls_count("Sound 128") == 1 && g_flash_row == 3000 + FOOT_CHARGE, "CHARGE: the sound, the flash");
    CHECK(txt_find(SP_CHARGED[0]) && txt_find(SP_CHARGED[0])->g == FLASH[1], "'Charged' in the button's place, flashing");
    *(int *) (g_player[0] + 0x1964) = 0;
    Sleep(170);
    calls_clear(); g_flash_row = -1; press(ACTION_CONFIRM);
    CHECK(calls_count("Sound 128") == 0 && g_flash_row == -1, "CHARGE with no active item: nothing played");
    g_foot_button = FOOT_RESET;
    Sleep(170);
    calls_clear(); press(ACTION_CONFIRM);
    CHECK(calls_count("Sound 128") == 1 && g_flash_row == 3000 + FOOT_RESET, "RESET after the damage edit: the sound, the flash");
    close_card();
    fclose(g_report);
    g_report = NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    image_up();
    test_whole_list();
    test_layout();
    test_widths();
    test_cut();
    test_pictures();
    test_search();
    test_counts_toggle();
    test_give();
    test_stats();
    test_stats_layout();
    test_confirm();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
