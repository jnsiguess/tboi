/* Written by tools/spawner_stats.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_STRINGS_H
#define SPAWNER_STRINGS_H

/*
 * The spawner card's words, one row per language set in the order of
 * spawner_title.h (en ru de es jp kr zh). All are written in the description
 * face and were checked against the face each set is written with. The
 * formats are printf formats; their arguments are given beside each table.
 * The stat icons: gfx/port_stat_icons.anm2 "Icons", frame k = stat k
 * (SP_STAT_ICON square, 100 %, pivot top left).
 */
#define SP_STAT_ICON 16
#define SP_STAT_SPEED 0
#define SP_STAT_TEARS 1
#define SP_STAT_DAMAGE 2
#define SP_STAT_RANGE 3
#define SP_STAT_SHOTSPEED 4
#define SP_STAT_LUCK 5
#define SP_CHARACTERS 41

/* the tabs as text: items, enemies, teleport, stats */
static const char *const SP_TAB_NAME[7][4] = {
    { "ITEMS", "ENEMIES", "TELEPORT", "STATS" },   /* en */
    { "ПРЕДМЕТЫ", "ВРАГИ", "ТЕЛЕПОРТ", "СТАТЫ" },   /* ru */
    { "ITEMS", "GEGNER", "TELEPORT", "WERTE" },   /* de */
    { "OBJETOS", "ENEMIGOS", "TELEPORTE", "STATS" },   /* es */
    { "アイテム", "てき", "テレポート", "ステータス" },   /* jp */
    { "아이템", "적", "텔레포트", "능력치" },   /* kr */
    { "道具", "敌人", "传送", "属性" },   /* zh */
};

/* the enemy-list headings as text: bosses, minibosses, monsters */
static const char *const SP_GROUP_NAME[7][3] = {
    { "BOSSES", "MINIBOSSES", "MONSTERS" },   /* en */
    { "БОССЫ", "МИНИБОССЫ", "МОНСТРЫ" },   /* ru */
    { "ENDGEGNER", "ZWISCHENGEGNER", "MONSTER" },   /* de */
    { "JEFES", "MINIJEFES", "MONSTRUOS" },   /* es */
    { "ボス", "ちゅうボス", "モンスター" },   /* jp */
    { "보스", "미니보스", "몬스터" },   /* kr */
    { "头目", "小头目", "怪物" },   /* zh */
};

/* the stats, SP_STAT_* order */
static const char *const SP_STAT_NAME[7][6] = {
    { "Speed", "Tears", "Damage", "Range", "Shot speed", "Luck" },   /* en */
    { "Скорость", "Слёзы", "Урон", "Дальность", "Скорость выстрела", "Удача" },   /* ru */
    { "Tempo", "Tränen", "Schaden", "Reichweite", "Schusstempo", "Glück" },   /* de */
    { "Velocidad", "Lágrimas", "Daño", "Alcance", "Vel. de lágrimas", "Suerte" },   /* es */
    { "移動速度", "連射速度", "攻撃力", "射程", "弾速", "運" },   /* jp */
    { "이동속도", "연사", "공격력", "사거리", "탄속", "행운" },   /* kr */
    { "移速", "射速", "伤害", "射程", "弹速", "幸运" },   /* zh */
};

/* (int player, from 1) */
static const char *const SP_PLAYER[7] = {
    "PLAYER %d",   /* en */
    "ИГРОК %d",   /* ru */
    "SPIELER %d",   /* de */
    "JUGADOR %d",   /* es */
    "プレイヤー%d",   /* jp */
    "플레이어 %d",   /* kr */
    "玩家%d",   /* zh */
};

/* (int player from 1, const char *character) - the mock-up's "< PLAYER 1: Isaac >", the arrows are the port's */
static const char *const SP_PLAYER_FMT[7] = {
    "PLAYER %d: %s",   /* en */
    "ИГРОК %d: %s",   /* ru */
    "SPIELER %d: %s",   /* de */
    "JUGADOR %d: %s",   /* es */
    "プレイヤー%d: %s",   /* jp */
    "플레이어 %d: %s",   /* kr */
    "玩家%d: %s",   /* zh */
};

/* the bottom line of the stats tab */
static const char *const SP_RESET[7] = {
    "RESET",   /* en */
    "СБРОСИТЬ",   /* ru */
    "ZURÜCKSETZEN",   /* de */
    "RESTABLECER",   /* es */
    "リセット",   /* jp */
    "초기화",   /* kr */
    "重置",   /* zh */
};

/* (const char *typed, double factor) - while a value is typed */
static const char *const SP_TYPING_FMT[7] = {
    "typing: %s  (items add ×%.2f from now)",   /* en */
    "ввод: %s  (×%.2f к предметам дальше)",   /* ru */
    "Eingabe: %s  (Items ab jetzt ×%.2f)",   /* de */
    "escribiendo: %s  (objetos ×%.2f desde ahora)",   /* es */
    "入力: %s  (以後のアイテム ×%.2f)",   /* jp */
    "입력: %s  (이후 아이템 ×%.2f)",   /* kr */
    "输入: %s  (之后的道具 ×%.2f)",   /* zh */
};

/* the limit line of each stat, while it is stepped or typed; arguments: Speed (double min, double max), Tears (double max), Damage (double min), Range (double min), Shot speed (double step, double min), Luck (none: "") */
static const char *const SP_STAT_HINT_FMT[7][6] = {
    { "speed: min %.2f, max %.2f", "tears: max %.0f", "damage: min %.2f", "range: min %.2f", "shot speed: steps of %.1f, min %.2f", "" },   /* en */
    { "скорость: мин %.2f, макс %.2f", "слёзы: макс %.0f", "урон: мин %.2f", "дальность: мин %.2f", "скорость выстрела: шаг %.1f, мин %.2f", "" },   /* ru */
    { "Tempo: min. %.2f, max. %.2f", "Tränen: max. %.0f", "Schaden: min. %.2f", "Reichweite: min. %.2f", "Schusstempo: Schritte von %.1f, min. %.2f", "" },   /* de */
    { "velocidad: mín. %.2f, máx. %.2f", "lágrimas: máx. %.0f", "daño: mín. %.2f", "alcance: mín. %.2f", "vel. de lágrimas: pasos de %.1f, mín. %.2f", "" },   /* es */
    { "移動速度: 最小 %.2f、最大 %.2f", "連射速度: 最大 %.0f", "攻撃力: 最小 %.2f", "射程: 最小 %.2f", "弾速: %.1f きざみ、最小 %.2f", "" },   /* jp */
    { "이동속도: 최소 %.2f, 최대 %.2f", "연사: 최대 %.0f", "공격력: 최소 %.2f", "사거리: 최소 %.2f", "탄속: %.1f 단위, 최소 %.2f", "" },   /* kr */
    { "移速: 最小 %.2f, 最大 %.2f", "射速: 最大 %.0f", "伤害: 最小 %.2f", "射程: 最小 %.2f", "弹速: 步长 %.1f, 最小 %.2f", "" },   /* zh */
};

/* every character by PlayerType (players.xml), the game's own names; the tainted
   ones (21..40) the game names exactly as the plain ones; English where a set's
   face lacks a character */
static const char *const SP_CHARACTER[SP_CHARACTERS][7] = {
    { "Isaac", "Исаак", "Isaac", "Isaac", "アイザック", "아이작", "以撒" },   /* 0 ISAAC_NAME */
    { "Magdalene", "Магдалина", "Magdalene", "Magdalena", "マグダレーン", "막달레나", "抹大拉" },   /* 1 MAGDALENE_NAME */
    { "Cain", "Каин", "Kain", "Caín", "ケイン", "카인", "该隐" },   /* 2 CAIN_NAME */
    { "Judas", "Иуда", "Judas", "Judas", "ユダ", "유다", "犹大" },   /* 3 JUDAS_NAME */
    { "???", "???", "???", "???", "？？？", "???", "???" },   /* 4 BLUEBABY_NAME */
    { "Eve", "Ева", "Eva", "Eva", "イブ", "이브", "夏娃" },   /* 5 EVE_NAME */
    { "Samson", "Самсон", "Samson", "Sansón", "サムソン", "삼손", "参孙" },   /* 6 SAMSON_NAME */
    { "Azazel", "Азазель", "Azazel", "Azazel", "アザゼル", "아자젤", "阿撒泻勒" },   /* 7 AZAZEL_NAME */
    { "Lazarus", "Лазарь", "Lazarus", "Lázaro", "ラザラス", "나사로", "拉撒路" },   /* 8 LAZARUS_NAME */
    { "Eden", "Эдем", "Eden", "Edén", "エデン", "에덴", "伊甸" },   /* 9 EDEN_NAME */
    { "The Lost", "Потерянный", "Der Verlorene", "El perdido", "ザ ロスト", "더 로스트", "游魂" },   /* 10 THE_LOST_NAME */
    { "Lazarus", "Лазарь", "Lazarus", "Lázaro", "ラザラス", "나사로", "拉撒路" },   /* 11 LAZARUS2_NAME */
    { "Black Judas", "Темный Иуда", "Schwarzer Judas", "Judas oscuro", "ダーク ユダ", "블랙 유다", "犹大之影" },   /* 12 BLACK_JUDAS_NAME */
    { "Lilith", "Лилит", "Lilith", "Lilith", "リリス", "릴리트", "莉莉丝" },   /* 13 LILITH_NAME */
    { "Keeper", "Хранитель", "Hüter", "Keeper", "キーパー", "키퍼", "店主" },   /* 14 KEEPER_NAME */
    { "Apollyon", "Аполлион", "Apollyon", "Apolión", "アポリオン", "아폴리온", "亚玻伦" },   /* 15 APOLLYON_NAME */
    { "The Forgotten", "Забытый", "Der Vergessene", "El Olvidado", "ザ フォーガットン", "더 포가튼", "遗骸" },   /* 16 THE_FORGOTTEN_NAME */
    { "The Soul", "Душа", "Die Seele", "El alma", "ザ ソウル", "더 소울", "遗骸之魂" },   /* 17 THE_SOUL_NAME */
    { "Bethany", "Вифания", "Bethanie", "Bethany", "ベタニア", "베다니", "伯大尼" },   /* 18 BETHANY_NAME */
    { "Jacob", "Иаков", "Jakob", "Jacob", "ヤコブ", "야곱", "雅各" },   /* 19 JACOB_NAME */
    { "Esau", "Исав", "Esau", "Esaú", "エサウ", "에사우", "以扫" },   /* 20 ESAU_NAME */
    { "Isaac", "Исаак", "Isaac", "Isaac", "アイザック", "아이작", "以撒" },   /* 21 ISAAC_NAME */
    { "Magdalene", "Магдалина", "Magdalene", "Magdalena", "マグダレーン", "막달레나", "抹大拉" },   /* 22 MAGDALENE_NAME */
    { "Cain", "Каин", "Kain", "Caín", "ケイン", "카인", "该隐" },   /* 23 CAIN_NAME */
    { "Judas", "Иуда", "Judas", "Judas", "ユダ", "유다", "犹大" },   /* 24 JUDAS_NAME */
    { "???", "???", "???", "???", "？？？", "???", "???" },   /* 25 BLUEBABY_NAME */
    { "Eve", "Ева", "Eva", "Eva", "イブ", "이브", "夏娃" },   /* 26 EVE_NAME */
    { "Samson", "Самсон", "Samson", "Sansón", "サムソン", "삼손", "参孙" },   /* 27 SAMSON_NAME */
    { "Azazel", "Азазель", "Azazel", "Azazel", "アザゼル", "아자젤", "阿撒泻勒" },   /* 28 AZAZEL_NAME */
    { "Lazarus", "Лазарь", "Lazarus", "Lázaro", "ラザラス", "나사로", "拉撒路" },   /* 29 LAZARUS_NAME */
    { "Eden", "Эдем", "Eden", "Edén", "エデン", "에덴", "伊甸" },   /* 30 EDEN_NAME */
    { "The Lost", "Потерянный", "Der Verlorene", "El perdido", "ザ ロスト", "더 로스트", "游魂" },   /* 31 THE_LOST_NAME */
    { "Lilith", "Лилит", "Lilith", "Lilith", "リリス", "릴리트", "莉莉丝" },   /* 32 LILITH_NAME */
    { "Keeper", "Хранитель", "Hüter", "Keeper", "キーパー", "키퍼", "店主" },   /* 33 KEEPER_NAME */
    { "Apollyon", "Аполлион", "Apollyon", "Apolión", "アポリオン", "아폴리온", "亚玻伦" },   /* 34 APOLLYON_NAME */
    { "The Forgotten", "Забытый", "Der Vergessene", "El Olvidado", "ザ フォーガットン", "더 포가튼", "遗骸" },   /* 35 THE_FORGOTTEN_NAME */
    { "Bethany", "Вифания", "Bethanie", "Bethany", "ベタニア", "베다니", "伯大尼" },   /* 36 BETHANY_NAME */
    { "Jacob", "Иаков", "Jakob", "Jacob", "ヤコブ", "야곱", "雅各" },   /* 37 JACOB_NAME */
    { "Lazarus", "Лазарь", "Lazarus", "Lázaro", "ラザラス", "나사로", "拉撒路" },   /* 38 LAZARUS_NAME */
    { "Jacob", "Иаков", "Jakob", "Jacob", "ヤコブ", "야곱", "雅各" },   /* 39 JACOB_NAME */
    { "The Soul", "Душа", "Die Seele", "El alma", "ザ ソウル", "더 소울", "遗骸之魂" },   /* 40 THE_SOUL_NAME */
};

#endif
