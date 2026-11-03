/*
 * THE REAL TABLE (1.74, the research notes of 11.10.2026, keyboard): the keyboard's live
 * bindings, read and turned round by objc.c's own code (kb_bound,
 * kb_table_to_ios, shim_keys_restart_bound) over a keyboard device laid out in
 * the fake image as the game keeps it - the device at input + 0xa8, its vtable,
 * the action count at +0x1c, the rows at +0x20, [0xabec7c] slots a row, the
 * image's HID -> GLFW table (0x945ad4) and keyboard defaults (0xa9b1e0).
 *
 * The game's own loading, modelled from the image (tables.h, run.py):
 *   - the keyboard device starts on its defaults (0xa9c6e8, slots in order);
 *   - 0x866374 reads inputconfigs.dat into a record of 35 rows x 2: all -1,
 *     then at the Name attribute both slots of every pair of [0xabec80] (the
 *     pad's table 0xa9c5c0, the last pair of an action winning), then every
 *     "a-k" attribute with a <= 34 and k < [0xabec7c];
 *   - 0x865fac hands all rows to the device; its store (0x8a8a58) keeps what
 *     falls inside its 34 actions;
 *   - the writer (0x866a50) refreshes the keyboard's record from the live table
 *     and writes it; read back, it goes through the same loading.
 *
 * Tables: the image's defaults, the owner's port inputconfigs.dat (the iOS
 * numbering), the owner's PC inputconfigs.dat (13.09.2026, the PC numbering).
 */

#define REC_ROWS 35
static unsigned char g_kbdev[0x100];
static int g_rows[REC_ROWS][T_SLOTS];
static int *g_rowp[REC_ROWS];

static void image_tables(void) {
    memcpy(g_img + KB_HID_TO_GLFW, T_HID_TO_GLFW, sizeof(T_HID_TO_GLFW));
    *(int *) (g_img + KB_PER_ACTION) = T_SLOTS;
    *(unsigned long long *) (g_img + 0xa9b1e0) = (unsigned long long) T_KB_PAIRS;
    *(int *) (g_img + 0xa9b1e8) = (int) (sizeof(T_KB_PAIRS) / sizeof(T_KB_PAIRS[0]));
    *(unsigned long long *) (g_img + KB_INPUT + KB_DEVICE) = (unsigned long long) g_kbdev;
    *(unsigned long long *) g_kbdev = (unsigned long long) (g_img + KB_VTABLE);
    *(unsigned *) (g_kbdev + KB_ACTIONS) = T_ACTIONS;
    for (int a = 0; a < REC_ROWS; a++) g_rowp[a] = g_rows[a];
    *(unsigned long long *) (g_kbdev + KB_BINDINGS) = (unsigned long long) g_rowp;
}

/* the device on its defaults */
static void device_defaults(void) {
    for (int a = 0; a < REC_ROWS; a++) for (int k = 0; k < T_SLOTS; k++) g_rows[a][k] = -1;
    for (unsigned i = 0; i < sizeof(T_KB_PAIRS) / sizeof(T_KB_PAIRS[0]); i++) {
        int *row = g_rows[T_KB_PAIRS[i][0]];
        for (int k = 0; k < T_SLOTS; k++) if (row[k] == -1) { row[k] = T_KB_PAIRS[i][1]; break; }
    }
}

/* 0x866374's record from a file's attributes, then 0x865fac into the device */
static void load_file(const int (*attrs)[3], int n) {
    int rec[REC_ROWS][T_SLOTS];
    for (int a = 0; a < REC_ROWS; a++) for (int k = 0; k < T_SLOTS; k++) rec[a][k] = -1;
    for (unsigned i = 0; i < sizeof(T_PAD_PAIRS) / sizeof(T_PAD_PAIRS[0]); i++)
        for (int k = 0; k < T_SLOTS; k++) rec[T_PAD_PAIRS[i][0]][k] = T_PAD_PAIRS[i][1];
    for (int i = 0; i < n; i++)
        if (attrs[i][0] <= 0x22 && attrs[i][1] < T_SLOTS) rec[attrs[i][0]][attrs[i][1]] = attrs[i][2];
    for (int a = 0; a < T_ACTIONS; a++) for (int k = 0; k < T_SLOTS; k++) g_rows[a][k] = rec[a][k];
}

/* the writer and a new launch: the live table as attributes, loaded again */
static int g_saved[REC_ROWS * T_SLOTS][3];
static void save_and_reload(void) {
    int n = 0;
    for (int a = 0; a < REC_ROWS; a++)
        for (int k = 0; k < T_SLOTS; k++) {
            g_saved[n][0] = a; g_saved[n][1] = k;
            g_saved[n][2] = a < T_ACTIONS ? g_rows[a][k] : -1;
            n++;
        }
    device_defaults();
    load_file((const int (*)[3]) g_saved, n);
}

/*
 * What the live table must be: the file's own choices for 0..15 (the rows both
 * numberings share - Controls binds only those), the iOS defaults for 16..33.
 * -1 when it is, else the first row that is not.
 */
static int g_want[REC_ROWS][T_SLOTS];
static void want_from_loaded(void) {
    int live[REC_ROWS][T_SLOTS];
    memcpy(live, g_rows, sizeof(live));
    device_defaults();
    memcpy(g_want, g_rows, sizeof(g_want));
    for (int a = 0; a <= 15; a++) for (int k = 0; k < T_SLOTS; k++) g_want[a][k] = live[a][k];
    memcpy(g_rows, live, sizeof(live));
}
static int differs_from_want(void) {
    for (int a = 0; a < T_ACTIONS; a++)
        for (int k = 0; k < T_SLOTS; k++) if (g_rows[a][k] != g_want[a][k]) return a;
    return -1;
}

static void show_rows(const char *name, int from, int to) {
    printf("  %s: rows %d..%d:", name, from, to);
    for (int a = from; a <= to; a++) printf(" [%d,%d]", g_rows[a][0], g_rows[a][1]);
    printf("\n");
}

#define HID_DOWN 0x51
#define HID_SPACE 0x2c

/* the restart key: every HID usage asked once; only R may be it */
static int restart_keys(char *out, int room) {
    int n = 0, count = 0;
    out[0] = 0;
    for (int hid = 0; hid < 256; hid++) {
        if (!shim_keys_restart_bound(hid)) continue;
        if (hid != HID_R) count += 100;
        count++;
        if (n < room - 8) n += snprintf(out + n, (size_t) (room - n), " %#x", hid);
    }
    return count;
}

static void one_table(const char *name, const int (*attrs)[3], int nattrs, int pc) {
    image_tables();
    device_defaults();
    if (attrs) load_file(attrs, nattrs);
    want_from_loaded();
    fresh();
    show_rows(name, 16, 33);

    /* a frame with a key: the delivery has its look at the table first */
    key(HID_SPACE, 1); frame(); key(HID_SPACE, 0); frame();
    show_rows(name, 16, 33);
    int bad = differs_from_want();
    CHECK(bad < 0, "%s: after the first key: the file's 0..15, the iOS defaults' 16..33 (row %d is not)", name, bad);
    CHECK(g_rows[27][0] == 263 && g_rows[28][0] == 262 && g_rows[29][0] == 265 && g_rows[30][0] == 264,
          "%s: the menu's arrows at 27..30", name);
    CHECK(g_rows[31][0] == 81 && g_rows[32][0] == 69 && g_rows[33][0] == 258, "%s: Q/E/Tab at 31..33", name);
    char bound[160];
    int count = restart_keys(bound, sizeof(bound));
    printf("  %s: restart keys (HID):%s\n", name, count ? bound : " none");
    CHECK(count == 1, "%s: R and only R is the restart key", name);

    /* the Down arrow held two seconds in a run, as a player shooting down holds it */
    key(HID_DOWN, 1); frames(120);
    printf("  %s: Down arrow held 120 frames - fades %d, resets %d\n", name, g_fades, g_resets);
    CHECK(g_fades == 0 && g_resets == 0, "%s: shooting down restarts nothing", name);
    key(HID_DOWN, 0); frames(2);
    key(HID_R, 1); frames(60);
    printf("  %s: R held 60 frames - fades %d, resets %d\n", name, g_fades, g_resets);
    CHECK(g_resets == 1, "%s: R restarts the run", name);
    key(HID_R, 0); frames(2);
    CHECK(!combo(), "%s: let go, nothing held", name);

    /* the keyboard's bindings saved (0x866a50) and read at the next launch: the same table, nothing turned */
#if DESK_HAS_TURN
    int turned = g_kb_turned;
    save_and_reload();
    key(HID_SPACE, 1); frame(); key(HID_SPACE, 0); frame();
    bad = differs_from_want();
    CHECK(bad < 0 && g_kb_turned == turned, "%s: saved and read back - the same table (row %d), turned again %d",
          name, bad, g_kb_turned - turned);
    save_and_reload();
    key(HID_SPACE, 1); frame(); key(HID_SPACE, 0); frame();
    CHECK(differs_from_want() < 0 && g_kb_turned == turned, "%s: a second round trip - still the same", name);
    if (pc) {
        /* the PC file never saved over: the next launch reads it again and turns it again, once */
        device_defaults();
        load_file(attrs, nattrs);
        key(HID_SPACE, 1); frame(); key(HID_SPACE, 0); frame();
        key(HID_SPACE, 1); frame(); key(HID_SPACE, 0); frame();
        CHECK(differs_from_want() < 0 && g_kb_turned == turned + 1,
              "%s: the PC file read again - turned once more, and only once (%d)", name, g_kb_turned - turned);
    }
    printf("  %s: turned %d time(s) in all\n", name, g_kb_turned);
#endif
}

/*
 * THE KEY QUEUE THROUGH A STALL (uikit.c shim_key_push_ex / shim_key_next_ex,
 * the real code, cut out by run.py): Java pushes as Android delivers - a held
 * key repeats every 50 ms after 500 ms - and the frame thread empties the queue
 * once a frame, in objc.c's order (a repeat goes nowhere but the spawner's
 * search). A run restarting is a load: the frames stop while R is still held.
 */
static void android_hold(const int *hids, int n, int ms) {
    for (int t = 0; t < ms; t++)
        if (t >= 500 && (t - 500) % 50 == 0)
            for (int i = 0; i < n; i++) shim_key_push_ex(hids[i], 1, 0, 0, 1);
}

static void queue_stall(void) {
    image_tables();
    device_defaults();
    fresh();
    int walk[2] = { 0x07 /* D */, HID_DOWN };
    shim_key_push_ex(walk[0], 1, 0, 0, 0);
    shim_key_push_ex(walk[1], 1, 0, 0, 0);
    shim_key_push_ex(HID_R, 1, 0, 0, 0);
    int code, down, mods, ch, rep;
    while (shim_key_next_ex(&code, &down, &mods, &ch, &rep)) if (!rep) key(code, down);
    frames(1);
    int held[3] = { walk[0], walk[1], HID_R };
    android_hold(held, 3, 3000);                /* the frames stop for 3 s */
    shim_key_push_ex(walk[0], 0, 0, 0, 0);
    shim_key_push_ex(walk[1], 0, 0, 0, 0);
    shim_key_push_ex(HID_R, 0, 0, 0, 0);
    int ups = 0;
    while (shim_key_next_ex(&code, &down, &mods, &ch, &rep)) {
        if (rep) continue;
        if (!down) ups++;
        key(code, down);
    }
    frames(2);
    int resets = g_resets;
    frames(200);
    printf("  queue through a 3 s stall, three keys held: releases out of the queue %d of 3, R still held for restart %d,"
           " resets in the next 200 frames %d\n", ups, g_rkeys, g_resets - resets);
    CHECK(ups == 3, "no release is lost in the queue (lost %d)", 3 - ups);
    CHECK(g_rkeys == 0 && !combo(), "R let go during the load is let go in the game");
    CHECK(g_resets == resets, "and no run restarts after it (resets %d)", g_resets - resets);
    for (int i = 0; i < 256; i++) shim_reset_key(i, 0);
    frames(2);
}

static void tables_cases(void) {
    one_table("the image's defaults", NULL, 0, 0);
    one_table("the port's inputconfigs.dat", T_PORT_ATTRS, (int) (sizeof(T_PORT_ATTRS) / sizeof(T_PORT_ATTRS[0])), 0);
    one_table("the PC's inputconfigs.dat", T_PC_ATTRS, (int) (sizeof(T_PC_ATTRS) / sizeof(T_PC_ATTRS[0])), 1);
    queue_stall();
}
