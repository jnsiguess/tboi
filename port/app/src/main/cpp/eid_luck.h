/*
 * What a luck-based chance is worth at this much luck.
 *
 * Written by tools/eid_luck.py out of the mod's own LuckFormulas
 * (features/eid_data.lua) - do not edit here. The chance is a percentage,
 * clamped to 0..100 by the caller, exactly as the mod clamps it.
 */
#include <math.h>

static int eid_luck_chance(unsigned kind, unsigned id, float luck, float *out) {
    switch (kind * 10000u + id) {
        case 10055u: *out = (float) ((50 + luck*10)); return 1;  /* Mom's Eye: Base 50%, 100% at 5 Luck */
        case 10087u: *out = (float) ((25 + luck*5)); return 1;  /* Loki's Horns: Base 25%, 100% at 15 Luck */
        case 10089u: *out = (float) ((100 / fmaxf(1, (4 - floorf(luck/5))))); return 1;  /* Spider Bite: Base 25%, 100% at 15 Luck */
        case 10103u: *out = (float) ((100 / fmaxf(1, (4 - floorf(luck/4))))); return 1;  /* The Common Cold: Base 25%, 100% at 12 Luck */
        case 10110u: *out = (float) ((100 / fmaxf(2, (5 - floorf(luck*0.15))))); return 1;  /* Mom's Contacts: Base 20%, 50% at 20 Luck */
        case 10150u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck))))); return 1;  /* Tough Love: Base 10%, 100% at 9 Luck */
        case 10162u: *out = (float) ((100 / fmaxf(1, (5 - floorf(luck*0.15))))); return 1;  /* Celtic Cross: Base 20%, 100% at 26.67 Luck */
        case 10200u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck/3))))); return 1;  /* Mom's Eyeshadow: Base 10%, 100% at 27 Luck */
        case 10201u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck/3))))); return 1;  /* Iron Bar: Base 10%, 100% at 27 Luck */
        case 10217u: *out = (float) ((100 / fmaxf(1, (20 - floorf(luck*2))))); return 1;  /* Mom's Wig: Base 5%, 100% at 9.5 Luck */
        case 10219u: *out = (float) ((20 + luck)); return 1;  /* Old Bandage: Base 20%, 100% at 80 Luck */
        case 10225u: *out = (float) ((8 + luck*2)); return 1;  /* Gimpy: Base 8%, 100% at 46 Luck */
        case 10228u: *out = (float) ((1500 / fmaxf(15, (100 - floorf(luck))))); return 1;  /* Mom's Perfume: Base 15%, 100% at 85 Luck */
        case 10230u: *out = (float) ((1500 / fmaxf(15, (100 - floorf(luck))))); return 1;  /* Abaddon: Base 15%, 100% at 85 Luck */
        case 10231u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck*0.5))))); return 1;  /* Ball of Tar: Base 10%, 100% at 18 Luck */
        case 10257u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck*0.7))))); return 1;  /* Fire Mind: Base 10%, 100% at 12.86 Luck */
        case 10259u: *out = (float) ((100 / fmaxf(1, (3 - floorf(luck*0.1))))); return 1;  /* Dark Matter: Base 33.3%, 100% at 20 Luck */
        case 10303u: *out = (float) ((100 / fmaxf(1, (4 - floorf(luck*0.3))))); return 1;  /* Virgo: Base 25%, 100% at 10 Luck */
        case 10374u: *out = (float) ((100 / fmaxf(2, (10 - floorf(luck*0.9))))); return 1;  /* Holy Light: Base 10%, 50% at 8.89 Luck */
        case 10408u: *out = (float) ((25 + luck*2.5)); return 1;  /* Athame: Base 25%, 100% at 30 Luck */
        case 10410u: *out = (float) ((100 / fmaxf(10, (30 - floorf(luck))))); return 1;  /* Evil Eye: Base 3.33%, 10% at 20 Luck */
        case 10443u: *out = (float) ((100 / fmaxf(1, (15 - floorf(luck))))); return 1;  /* Apple!: Base 6.66%, 100% at 14 Luck */
        case 10461u: *out = (float) ((100 / fmaxf(2, (7 - floorf(luck))))); return 1;  /* Parasitoid: Base 14.3%, 50% at 5 Luck */
        case 10495u: *out = (float) ((100 / fmaxf(2, (12 - floorf(luck))))); return 1;  /* Ghost Pepper: Base 8.33%, 50% at 10 Luck */
        case 10496u: *out = (float) ((100 / fmaxf(4, (30 - floorf(luck*2))))); return 1;  /* Euthanasia: Base 3.33%, 25% at 13 Luck */
        case 10503u: *out = (float) ((100 / fmaxf(5, (20 - floorf(luck))))); return 1;  /* Little Horn: Base 5%, 20% at 15 Luck */
        case 10575u: *out = (float) ((100 / fmaxf(1, (floorf(5 - luck))))); return 1;  /* The Intruder: Base 20%, 100% at 3.01 Luck */
        case 10576u: *out = (float) (fminf(luck*0.5 + 6.25, 10)); return 1;  /* Dirty Mind: Base 6.25%, 10% at 7.5 Luck */
        case 10606u: *out = (float) ((100 / fmaxf(5, (20 - floorf(luck))))); return 1;  /* Ocular Rift: Base 5%, 20% at 15 Luck */
        case 10616u: *out = (float) ((100 / fmaxf(2, (12 - floorf(luck))))); return 1;  /* Bird's Eye: Base 8.33%, 50% at 10 Luck */
        case 10617u: *out = (float) ((100 / fmaxf(1, (6 - floorf(luck))))); return 1;  /* Lodestone: Base 16.6%, 100% at 5 Luck */
        case 10618u: *out = (float) ((100 / fmaxf(1, (6 - ceilf(luck))))); return 1;  /* Rotten Tomato: Base 16.6%, 100% at 4.01 Luck */
        case 10637u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck))))); return 1;  /* Knockout Drops: Base 10%, 100% at 9 Luck */
        case 20008u: *out = (float) ((100 / fmaxf(1, (20 - floorf(luck*0.5))))); return 1;  /* Cartridge: Base 5%, 100% at 38 Luck */
        case 20030u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck*0.5))))); return 1;  /* Pinky Eye: Base 10%, 100% at 18 Luck */
        case 20031u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck*0.5))))); return 1;  /* Push Pin: Base 10%, 100% at 18 Luck */
        case 20046u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck*0.5))))); return 1;  /* Isaac's Fork: Base 10%, 100% at 18 Luck */
        case 20048u: *out = (float) ((100 / fmaxf(2, (20 - floorf(luck*0.3))))); return 1;  /* A Missing Page: Base 5%, 50% at 60 Luck */
        case 20058u: *out = (float) ((100 / fmaxf(1, (15 - floorf(luck*1.5))))); return 1;  /* Samson's Lock: Base 6.66%, 100% at 9.34 Luck */
        case 20059u: *out = (float) ((100 / fmaxf(1, (4 - floorf(luck))))); return 1;  /* Cain's Eye: Base 25%, 100% at 3 Luck */
        case 20060u: *out = (float) ((100 / fmaxf(1, (20 - floorf(luck*2.5))))); return 1;  /* Eve's Bird Foot: Base 5%, 100% at 7.6 Luck */
        case 20095u: *out = (float) ((100 / fmaxf(1, (33 - floorf(luck))))); return 1;  /* Black Tooth: Base 3.03%, 100% at 32 Luck */
        case 20096u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck))))); return 1;  /* Ouroboros Worm: Base 10%, 100% at 9 Luck */
        case 20129u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck))))); return 1;  /* Jawbreaker: Base 10%, 100% at 9 Luck */
        case 20130u: *out = (float) ((100 / fmaxf(1, (10 - floorf(luck*0.5))))); return 1;  /* Chewed Pen: Base 10%, 100% at 18 Luck */
        case 20135u: *out = (float) ((luck*2 + 20)); return 1;  /* A Lighter: Base 20%, 100% at 40 Luck */
        case 20143u: *out = (float) (fminf(luck*3 + 20, 33)); return 1;  /* Old Capacitor: Base 20%, 33% at 4.34 Luck */
        case 20188u: *out = (float) ((luck*2 + 20)); return 1;  /* Ice Cube: Base 20%, 100% at 40 Luck */
        default: return 0;
    }
}
