/*
 * PLANTILLA: UN PERSONAJE NUEVO CON SUS STATS, EN C (sin Lua).
 * Sacada de ffchars.c (Fiend Folio). Copia este archivo, cambia MY_FIRST, la tabla g_chars y listo.
 *
 * QUE HACE: envuelve EvaluateItems (0x4f0ee4, player), la funcion del motor que recalcula los stats del
 * jugador. Deja que el original termine y despues aplica los cambios del personaje SOLO a los stats que
 * el juego pidio recalcular en esa llamada (flags en player+0x1958; el original las borra, por eso se
 * leen antes). Es lo mismo que hace un callback MC_EVALUATE_CACHE de un mod de PC, pero nativo.
 *
 * OFFSETS DEL JUGADOR (medidos en isaac.arm64, ver statsplus.c del source):
 *   +0x1738 tipo, +0x1834 MaxFireDelay, +0x1838 ShotSpeed, +0x1844 Damage, +0x1854 TearRange,
 *   +0x194c MoveSpeed, +0x1950 Luck, +0x1958 flags pendientes, +0x1dbc multiplicador D8 de rango.
 * BITS DE LAS FLAGS: 1 dano, 2 cadencia (retraso), 4 vel. de tiro, 8 alcance, 16 velocidad, 1<<10 suerte.
 * HasCollectible = 0x4eeb74(player, id, 0).
 *
 * QUE NO CUBRE (aun): TearFlags, TearColor, LaserColor, tamano. Faltan los offsets; mira el README.
 *
 * REQUISITO: el id del personaje (MY_FIRST) debe existir en la tabla de personajes del juego.
 * Sin parche el juego trae 41 (ids 0..40). El parche "charslots" la agranda a 61 (ids 41..60).
 */
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

void shim_hook(void *at, void *to);

#define FN_EVALUATE_ITEMS   0x4f0ee4
#define FN_HAS_COLLECTIBLE  0x4eeb74
#define PLAYER_TYPE         0x1738
#define PLAYER_FIRE_DELAY   0x1834
#define PLAYER_SHOT_SPEED   0x1838
#define PLAYER_DAMAGE       0x1844
#define PLAYER_RANGE        0x1854
#define PLAYER_SPEED        0x194c
#define PLAYER_LUCK         0x1950
#define PLAYER_CACHE_FLAGS  0x1958

#define CF_DAMAGE     (1u << 0)
#define CF_FIREDELAY  (1u << 1)
#define CF_SHOTSPEED  (1u << 2)
#define CF_RANGE      (1u << 3)
#define CF_SPEED      (1u << 4)
#define CF_LUCK       (1u << 10)

/* === LO QUE CAMBIAS =================================================================================== */
#define MY_FIRST  41           /* id del primer personaje nuevo (el orden de tu players.xml fusionado)     */
#define MY_COUNT  1            /* cuantos personajes lleva esta tabla                                      */

typedef struct {
    const char *name;
    float dmg_mul, dmg_add;    /* Damage = Damage * dmg_mul + dmg_add                                      */
    float tears_abp;           /* 0 = nada. Multiplicador del RETRASO al estilo Antibirth+ (1.3 = mas lento)*/
    float speed_add;           /* MoveSpeed += speed_add                                                   */
    float shot_mul, shot_add;  /* ShotSpeed = ShotSpeed * shot_mul + shot_add                              */
    float luck_add;            /* Luck += luck_add                                                         */
    float range_add;           /* TearRange += range_add (unidades del juego: 40 = 1 punto en pantalla)    */
} my_char_t;

static const my_char_t g_chars[MY_COUNT] = {
    /*        nombre     dmg_mul dmg_add tears  speed  shot_mul shot_add luck  range */
    /* 41 */ { "Samael",  1.0f,   0.5f,   0.0f, -0.15f, 1.0f,    0.0f,    0.0f, 0.0f },   /* samael.lua:5448 dano +0.5, :5459 velocidad -0.15 */
};
/* ===================================================================================================== */

typedef void (*eval_fn)(unsigned long player);
static eval_fn g_eval;

/* igual que tearsMult(delay, convertTearMultABPToRep(m)) de Fiend Folio */
static float tears_mult_abp(float delay, float m) {
    float conv = 11.0f / (10.0f * m + 1.0f);
    float d = 30.0f / ((30.0f / (delay + 1.0f)) * conv) - 1.0f;
    return d < -0.99f ? -0.99f : d;
}

static void my_apply(unsigned long p, const my_char_t *c, unsigned fl) {
    if (fl & CF_DAMAGE)    { float *d = (float *) (p + PLAYER_DAMAGE);     *d = *d * c->dmg_mul + c->dmg_add; }
    if ((fl & CF_FIREDELAY) && c->tears_abp > 0) {
        float *f = (float *) (p + PLAYER_FIRE_DELAY); *f = tears_mult_abp(*f, c->tears_abp);
    }
    if (fl & CF_SHOTSPEED) { float *s = (float *) (p + PLAYER_SHOT_SPEED); *s = *s * c->shot_mul + c->shot_add; }
    if (fl & CF_SPEED)     *(float *) (p + PLAYER_SPEED) += c->speed_add;
    if (fl & CF_RANGE)     *(float *) (p + PLAYER_RANGE) += c->range_add;
    if (fl & CF_LUCK)      *(float *) (p + PLAYER_LUCK)  += c->luck_add;
}

static void my_evaluate(unsigned long p) {
    unsigned fl = *(const unsigned *) (p + PLAYER_CACHE_FLAGS);    /* antes de que el original las borre */
    g_eval(p);
    int t = *(const int *) (p + PLAYER_TYPE) - MY_FIRST;
    if (t < 0 || t >= MY_COUNT) return;
    my_apply(p, &g_chars[t], fl);
}

/* al cargar, antes de que la imagen se cierre: EvaluateItems pasa a ser nuestra */
void shim_mychar_install(unsigned char *base) {
    uint32_t *fn = (uint32_t *) (base + FN_EVALUATE_ITEMS);
    static const uint32_t want[4] = { 0x6db63befu, 0x6d0133edu, 0x6d022bebu, 0x6d0323e9u };   /* 4 x stp */
    for (int i = 0; i < 4; i++)
        if (fn[i] != want[i]) { LOG("mychar: EvaluateItems no empieza como se midio - no se toca"); return; }
    uint8_t *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) return;
    memcpy(page, fn, 16);                 /* las 4 palabras que va a cubrir el salto */
    shim_hook(page + 16, fn + 4);         /* ...y seguir en la quinta */
    if (mprotect(page, 4096, PROT_READ | PROT_EXEC) != 0) { munmap(page, 4096); return; }
    __builtin___clear_cache((char *) page, (char *) page + 32);
    g_eval = (eval_fn) page;
    shim_hook(fn, (void *) my_evaluate);
    LOG("mychar: EvaluateItems es nuestra (%d personaje(s))", MY_COUNT);
}
