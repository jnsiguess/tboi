/*
 * The desk stand for the players' HUD: the real hudgeom.c, one line-up per run.
 *
 *   build.sh && ./desk.exe W H HUDOFFSET GREED TYPES [TRINKETS]
 *
 *   TYPES     eight player types, comma-separated, -1 for an empty entry
 *             (entry k + 4 is entry k's twin: Jacob 19 in k, Esau 20 in k + 4)
 *   TRINKETS  eight digits, 0..3: bit 0 slot 0 holds a trinket, bit 1 slot 1
 *
 * Prints "layout entry K L corner X,Y" per entry and one "op ..." line per call -
 * the words hudlayout.c writes to the report as "hud piece: ..." - which
 * tools/hudesk/hudcheck.py runs through the image's own renderers (hudemu.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../port/app/src/main/cpp/hudgeom.h"

static const char *OPS[] = { "hearts", "active", "trinket", "pocket", "grid", "spells" };

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "desk.exe W H HUDOFFSET GREED TYPES [TRINKETS]\n");
        return 2;
    }
    hg_in in;
    memset(&in, 0, sizeof(in));
    in.W = (float) atof(argv[1]);
    in.H = (float) atof(argv[2]);
    in.f = 1.0f - (float) atof(argv[3]);
    in.greed = atoi(argv[4]);
    char *p = argv[5];
    for (int k = 0; k < HG_ENTRIES && p && *p; k++) {
        int t = (int) strtol(p, &p, 10);
        in.present[k] = t >= 0;
        in.type[k] = t;
        if (*p == ',') p++;
    }
    for (int k = 0; k < HG_ENTRIES && argc > 6 && argv[6][k]; k++) {
        int b = argv[6][k] - '0';
        in.trinket[k][0] = b & 1;
        in.trinket[k][1] = (b >> 1) & 1;
    }
    hg_out o;
    hg_layout(&in, &o);
    printf("entries %d mine %d\n", o.n, o.mine);
    for (int k = 0; k < HG_ENTRIES; k++)
        if (in.present[k])
            printf("layout entry %d %d drawn %d corner %.2f,%.2f\n", k, o.layout[k], o.draw[k],
                   (double) o.corner[k].x, (double) o.corner[k].y);
    for (int i = 0; i < o.nops; i++) {
        const hg_op *q = &o.op[i];
        printf("op %s entry %d slot %d at %.2f,%.2f scale %.2f index %d name %s more %d\n", OPS[q->op], q->entry,
               q->slot, (double) q->x, (double) q->y, (double) q->s, q->index, q->name_right ? "right" : "left", q->more);
    }
    return 0;
}
