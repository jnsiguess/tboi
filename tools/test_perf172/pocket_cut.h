/* cut out of hudlayout.c by run.py - do not edit */
typedef struct { unsigned long long at; uint32_t v[4]; } word_t;   /* v[mode] */
enum { PA_GAME = 0, PA_ACTIVE = 1, PA_BAG = 2, PA_BAG_FULL = 3 };
static const word_t PA_WORDS[] = {
    /*            game         active       bag (a row)  bag (full) */
    { 0x6e920c, { 0x1E349001u, 0x1E341001u, 0x1E341001u, 0x1E349001u } },  /* fmov s1, #-10 -> #-8: name's y (left) */
    { 0x6e9224, { 0x1E349001u, 0x1E341001u, 0x1E341001u, 0x1E349001u } },  /* fmov s1, #-10 -> #-8: name's y (right) */
    { 0x6e9260, { 0x1E321001u, 0x1E231001u, 0x1E231001u, 0x1E321001u } },  /* fmov s1, #-4 -> #6: the name's x */
    { 0x6e9280, { 0x1E371000u, 0x1E345000u, 0x1E345000u, 0x1E371000u } },  /* fmov s0, #-24 -> #-9: the active's x */
    { 0x6e9284, { 0x1E365001u, 0x1E341001u, 0x1E341001u, 0x1E365001u } },  /* fmov s1, #-18 -> #-8: the active's y */
    /* the bag in a row: b.ne 0x6e951c -> b 0x6e9738, past its own preview and its name */
    { 0x6e92cc, { 0x54001281u, 0x54001281u, 0x1400011Bu, 0x54001281u } },
    /* the bag at full: its preview is Rep+'s (0x84849b: the same (-34, -3) s), its name is not - b 0x6e951c ->
       b 0x6e9738 after the preview (Rep+ empties the name, 0x8484ea) */
    { 0x6e9330, { 0x1400007Bu, 0x1400007Bu, 0x1400007Bu, 0x14000102u } },
};
#define PA_N (int) (sizeof(PA_WORDS) / sizeof(PA_WORDS[0]))
static int g_pa_on, g_pa_broken;

static uint32_t pa_word(const word_t *w, int mode) { return w->v[mode]; }

static void pa_words(unsigned long long b, int on) {
    if (g_pa_broken || on == g_pa_on) return;
    for (int i = 0; i < PA_N; i++) {
        uint32_t now = *(const uint32_t *) (b + PA_WORDS[i].at);
        int known = 0;
        for (int m = 0; m < 4; m++) known |= now == PA_WORDS[i].v[m];
        if (!known) {
            g_pa_broken = 1;
            LOG("hud: the word at %lx is %08x, not the pocket renderer's - pocket actives stay where the image puts them",
                PA_WORDS[i].at, now);
            return;
        }
    }
    long long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    unsigned long long lo = (b + PA_WORDS[0].at) & ~((unsigned long long) page - 1);
    unsigned long long hi = ((b + PA_WORDS[PA_N - 1].at + 4) + (unsigned long long) page - 1) & ~((unsigned long long) page - 1);
    if (mprotect((void *) lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        g_pa_broken = 1;
        LOG("hud: cannot open the pocket renderer's page - pocket actives stay where the image puts them");
        return;
    }
    for (int i = 0; i < PA_N; i++)
        *(uint32_t *) (b + PA_WORDS[i].at) = pa_word(&PA_WORDS[i], on);
    mprotect((void *) lo, hi - lo, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *) lo, (char *) hi);
    g_pa_on = on;
}
