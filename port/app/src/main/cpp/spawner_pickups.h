/* Written by tools/spawner_pickups.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_PICKUPS_H
#define SPAWNER_PICKUPS_H

/*
 * The spawner ITEMS list past the collectibles (the owner, 07.10.2026): what
 * Game::Spawn makes of each row (type, variant, subtype; a chest closed,
 * subtype 1), its section, and its name in the sets en ru de es jp kr zh.
 * gfx/port_pickup_icons.anm2, every frame at 100 %, pivot top left:
 *   "Icons"    frame k = SP_PICKUP[k]
 *   "Pills"    0 pill, 1 horse pill, 2 gold pill, 3 gold horse pill
 *   "Golden"   frame id - 1 = golden trinket id, the game's gold shader baked
 *   "Health"   frame = SP_H_* (the STATS health rows), the HUD's own
 *   "Counters" 0 coins, 1 bombs, 2 keys, the HUD's own
 */
#define SP_G_KEYS 1
#define SP_G_BOMBS 2
#define SP_G_COINS 3
#define SP_G_HEARTS 4
#define SP_G_BATTERIES 5
#define SP_G_LOOT 10

#define SP_PICKUPS 47
static const struct {
    unsigned short type, variant, subtype;
    unsigned char group;
    unsigned char w, h;                  /* the picture */
    const char *name[7];
} SP_PICKUP[SP_PICKUPS] = {
    { 5, 30, 1, SP_G_KEYS, 14, 20, { "Key", "Ключ", "Schlüssel", "Llave", "鍵", "열쇠", "钥匙" } },
    { 5, 30, 2, SP_G_KEYS, 14, 20, { "Golden Key", "Золотой ключ", "Goldener Schlüssel", "Llave dorada", "金の鍵", "황금열쇠", "金钥匙" } },
    { 5, 30, 3, SP_G_KEYS, 17, 24, { "Key Ring", "Связка ключей", "Schlüsselring", "Llavero", "鍵ｘ2", "1+1 열쇠", "双钥匙" } },
    { 5, 30, 4, SP_G_KEYS, 14, 20, { "Charged Key", "Заряженный ключ", "Aufgeladener Schlüssel", "Llave cargada", "帯電した鍵", "충전된 열쇠", "充能钥匙" } },
    { 5, 40, 1, SP_G_BOMBS, 20, 21, { "Bomb", "Бомба", "Bombe", "Bomba", "爆弾", "폭탄", "炸弹" } },
    { 5, 40, 2, SP_G_BOMBS, 27, 23, { "Double Bomb", "Двойная бомба", "Doppelbombe", "Bomba doble", "爆弾ｘ2", "1+1 폭탄", "双炸弹" } },
    { 5, 40, 4, SP_G_BOMBS, 20, 21, { "Golden Bomb", "Золотая бомба", "Goldene Bombe", "Bomba dorada", "金爆弾", "황금폭탄", "金炸弹" } },
    { 5, 40, 7, SP_G_BOMBS, 26, 24, { "Giga Bomb", "Гига-бомба", "Gigabombe", "Bomba gigante", "巨大爆弾", "기가폭탄", "巨型炸弹" } },
    { 5, 20, 1, SP_G_COINS, 18, 13, { "Penny", "Пенни", "Münze", "Moneda", "ペニー", "페니", "硬币" } },
    { 5, 20, 2, SP_G_COINS, 18, 13, { "Nickel", "Пятак", "Nickel", "Níquel", "ニッケル", "니켈", "镍币" } },
    { 5, 20, 3, SP_G_COINS, 18, 13, { "Dime", "Гривенник", "Zehncentstück", "10 centavos", "ダイム", "다임", "铸币" } },
    { 5, 20, 4, SP_G_COINS, 27, 16, { "Double Penny", "Двойной пенни", "Doppelpenny", "Moneda doble", "ダブルペニー", "1+1 페니", "双硬币" } },
    { 5, 20, 5, SP_G_COINS, 18, 13, { "Lucky Penny", "Счастливый Пенни", "Glückspenny", "Moneda de la suerte", "ラッキー ペニー", "행운의 동전", "幸运硬币" } },
    { 5, 20, 6, SP_G_COINS, 18, 13, { "Sticky Nickel", "Липкий пятак", "Klebriger Nickel", "Níquel pegajoso", "粘着ニッケル", "끈적이는 니켈", "粘性镍币" } },
    { 5, 20, 7, SP_G_COINS, 18, 13, { "Golden Penny", "Золотой пенни", "Gold Penny", "Moneda dorada", "ゴールデンペニー", "황금페니", "金硬币" } },
    { 5, 10, 1, SP_G_HEARTS, 16, 13, { "Heart", "Красное сердце", "Herz", "Corazón", "ハート", "빨간하트", "红心" } },
    { 5, 10, 2, SP_G_HEARTS, 16, 14, { "Half Heart", "Половина сердца", "Halbes Herz", "Medio corazón", "半ハート", "빨간하트 반칸", "半红心" } },
    { 5, 10, 3, SP_G_HEARTS, 16, 13, { "Soul Heart", "Синее сердце", "Seelenherz", "Corazón de alma", "青ハート", "소울하트", "魂心" } },
    { 5, 10, 4, SP_G_HEARTS, 16, 14, { "Eternal Heart", "Вечное сердце", "Ewiges Herz", "Corazón eterno", "白ハート", "이터널하트", "永恒之心" } },
    { 5, 10, 5, SP_G_HEARTS, 24, 14, { "Double Heart", "Двойное сердце", "Doppeltes Herz", "Corazón doble", "ハートｘ2", "1+1 빨간하트", "双红心" } },
    { 5, 10, 6, SP_G_HEARTS, 16, 13, { "Black Heart", "Чёрное сердце", "Schwarzes Herz", "Corazón negro", "黒ハート", "블랙하트", "黑心" } },
    { 5, 10, 7, SP_G_HEARTS, 16, 13, { "Gold Heart", "Золотое сердце", "Goldenes Herz", "Corazón de oro", "金ハート", "황금하트", "金心" } },
    { 5, 10, 8, SP_G_HEARTS, 16, 14, { "Half Soul Heart", "Половина синего сердца", "Halbes Seelenherz", "Medio corazón de alma", "半青ハート", "소울하트 반칸", "半魂心" } },
    { 5, 10, 9, SP_G_HEARTS, 16, 13, { "Scared Heart", "Пугливое сердце", "Verängstigtes Herz", "Corazón asustado", "脅えるハート", "도망가는 빨간하트", "胆小的红心" } },
    { 5, 10, 10, SP_G_HEARTS, 16, 13, { "Blended Heart", "Смешанное сердце", "Gemischtes Herz", "Corazón mezclado", "混合ハート", "블렌드하트", "混合心" } },
    { 5, 10, 11, SP_G_HEARTS, 16, 13, { "Bone Heart", "Костяное сердце", "Knochenherz", "Corazón de hueso", "骨ハート", "뼈하트", "骨心" } },
    { 5, 10, 12, SP_G_HEARTS, 16, 13, { "Rotten Heart", "Гнилое сердце", "Faules Herz", "Corazón podrido", "腐ったハート", "썩은하트", "腐心" } },
    { 5, 90, 1, SP_G_BATTERIES, 14, 17, { "Lil' Battery", "Маленькая батарейка", "Kleine Batterie", "Batería pequeña", "電池", "리틀 배터리", "小电池" } },
    { 5, 90, 2, SP_G_BATTERIES, 12, 14, { "Micro Battery", "Микробатарейка", "Mikrobatterie", "Mini Batería", "ミニ電池", "마이크로 배터리", "微型电池" } },
    { 5, 90, 3, SP_G_BATTERIES, 16, 20, { "Mega Battery", "Мегабатарейка", "Megabatterie", "Mega batería", "メガ電池", "메가 배터리", "超级电池" } },
    { 5, 90, 4, SP_G_BATTERIES, 14, 20, { "Golden Battery", "Золотая батарейка", "Goldene Batterie", "Batería dorada", "金電池", "황금 배터리", "金电池" } },
    { 5, 69, 1, SP_G_LOOT, 20, 22, { "Grab Bag", "Мешочек", "Sack", "Saco", "サック", "자루", "福袋" } },
    { 5, 69, 2, SP_G_LOOT, 20, 22, { "Black Sack", "Чёрный мешочек", "Schwarzer Sack", "Saco negro", "黒サック", "검은자루", "黑福袋" } },
    { 5, 50, 1, SP_G_LOOT, 30, 23, { "Chest", "Сундук", "Truhe", "Cofre", "宝箱", "상자", "箱子" } },
    { 5, 51, 1, SP_G_LOOT, 30, 23, { "Bomb Chest", "Каменный сундук", "Bombentruhe", "Cofre de piedra", "石宝箱", "돌상자", "石头箱子" } },
    { 5, 52, 1, SP_G_LOOT, 32, 22, { "Spiked Chest", "Шипастый сундук", "Stacheltruhe", "Cofre con espinas", "トゲ付き宝箱", "가시상자", "刺箱子" } },
    { 5, 53, 1, SP_G_LOOT, 30, 23, { "Eternal Chest", "Вечный сундук", "Ewige Truhe", "Cofre eterno", "白宝箱", "하얀상자", "永恒箱子" } },
    { 5, 54, 1, SP_G_LOOT, 30, 23, { "Mimic Chest", "Сундук-мимик", "Mimic-Truhe", "Cofre mímico", "罠宝箱", "함정상자", "拟态箱子" } },
    { 5, 55, 1, SP_G_LOOT, 30, 23, { "Old Chest", "Старый сундук", "Alte Truhe", "Cofre viejo", "古びた宝箱", "오래된상자", "旧箱子" } },
    { 5, 56, 1, SP_G_LOOT, 30, 23, { "Wooden Chest", "Деревянный сундук", "Holztruhe", "Cofre de madera", "木の宝箱", "나무상자", "木箱子" } },
    { 5, 57, 1, SP_G_LOOT, 32, 24, { "Mega Chest", "Мега-сундук", "Megatruhe", "Mega cofre", "巨大宝箱", "메가상자", "大箱子" } },
    { 5, 58, 1, SP_G_LOOT, 30, 23, { "Haunted Chest", "Призрачный сундук", "Spuktruhe", "Cofre embrujado", "呪いの宝箱", "유령상자", "闹鬼箱子" } },
    { 5, 60, 1, SP_G_LOOT, 30, 23, { "Golden Chest", "Золотой сундук", "Goldene Truhe", "Cofre dorado", "金宝箱", "황금상자", "金箱子" } },
    { 5, 360, 1, SP_G_LOOT, 30, 23, { "Red Chest", "Красный сундук", "Rote Truhe", "Cofre rojo", "赤宝箱", "빨간상자", "红箱子" } },
    { 4, 3, 0, SP_G_LOOT, 23, 24, { "Troll Bomb", "Тролль-бомба", "Trollbombe", "Bomba troll", "トロル爆弾", "트롤폭탄", "即爆炸弹" } },
    { 4, 4, 0, SP_G_LOOT, 21, 24, { "Mega Troll Bomb", "Мега тролль-бомба", "Mega-Trollbombe", "Bomba megatroll", "メガトロル爆弾", "슈퍼 트롤폭탄", "超级即爆炸弹" } },
    { 4, 18, 0, SP_G_LOOT, 23, 24, { "Golden Troll Bomb", "Золотая тролль-бомба", "Goldene Trollbombe", "Bomba troll dorada", "金トロル爆弾", "황금 트롤폭탄", "金即爆炸弹" } },
};

static const struct { unsigned char w, h; } SP_PILL_ICON[4] = { { 17, 17 }, { 23, 23 }, { 17, 17 }, { 23, 23 } };
#define SP_GOLDEN_TOP 189                 /* frames: trinket ids 1..this */
#define SP_TRINKET_PICTURE 24

#define SP_H_CONTAINERS 0
#define SP_H_RED 1
#define SP_H_SOUL 2
#define SP_H_BLACK 3
#define SP_H_BONE 4
#define SP_H_BROKEN 5
#define SP_H_ETERNAL 6
#define SP_H_GOLDEN 7
#define SP_H_ROTTEN 8
#define SP_H_SOUL_CHARGE 9
#define SP_H_BLOOD_CHARGE 10
#define SP_H_COIN_CONTAINERS 11
#define SP_H_COIN_HEARTS 12
#define SP_H_COIN_BROKEN 13
#define SP_HEALTH_ROWS 14

static const struct { unsigned char w, h; } SP_HEALTH_ICON[SP_HEALTH_ROWS] = { { 15, 14 }, { 15, 14 }, { 15, 14 }, { 15, 14 }, { 15, 14 }, { 15, 14 }, { 15, 14 }, { 16, 15 }, { 15, 14 }, { 10, 10 }, { 10, 10 }, { 14, 14 }, { 14, 14 }, { 14, 14 } };
static const char *const SP_HEALTH_NAME[7][SP_HEALTH_ROWS] = {
    { "Heart containers", "Red hearts", "Soul hearts", "Black hearts", "Bone hearts", "Broken hearts", "Eternal heart", "Golden hearts", "Rotten hearts", "Soul charges", "Blood charges", "Coin containers", "Coin hearts", "Broken hearts" },   /* en */
    { "Контейнеры сердец", "Красные сердца", "Синие сердца", "Чёрные сердца", "Костяные сердца", "Разбитые сердца", "Вечное сердце", "Золотые сердца", "Гнилые сердца", "Заряды души", "Заряды крови", "Монетные контейнеры", "Монетные сердца", "Разбитые сердца" },   /* ru */
    { "Herzcontainer", "Rote Herzen", "Seelenherzen", "Schwarze Herzen", "Knochenherzen", "Gebrochene Herzen", "Ewiges Herz", "Goldene Herzen", "Verrottete Herzen", "Seelenladungen", "Blutladungen", "Münzcontainer", "Münzherzen", "Gebrochene Herzen" },   /* de */
    { "Contenedores", "Corazones rojos", "Corazones de alma", "Corazones negros", "Corazones de hueso", "Corazones rotos", "Corazón eterno", "Corazones dorados", "Corazones podridos", "Cargas de alma", "Cargas de sangre", "Contenedores de moneda", "Corazones de moneda", "Corazones rotos" },   /* es */
    { "ハートのうつわ", "赤ハート", "ソウルハート", "ブラックハート", "ボーンハート", "こわれたハート", "エターナルハート", "ゴールデンハート", "くさったハート", "ソウルチャージ", "ブラッドチャージ", "コインのうつわ", "コインハート", "こわれたハート" },   /* jp */
    { "하트 용기", "빨간 하트", "소울 하트", "블랙 하트", "뼈 하트", "부서진 하트", "이터널 하트", "황금 하트", "썩은 하트", "소울 충전", "피 충전", "동전 용기", "동전 하트", "부서진 하트" },   /* kr */
    { "心之容器", "红心", "魂心", "黑心", "骨心", "碎心", "永恒之心", "金心", "腐心", "魂之充能", "血之充能", "硬币容器", "硬币心", "碎心" },   /* zh */
};

static const struct { unsigned char w, h; } SP_COUNTER_ICON[3] = { { 9, 11 }, { 13, 13 }, { 9, 12 } };
static const char *const SP_COUNTER_NAME[7][3] = {   /* coins, bombs, keys */
    { "Coins", "Bombs", "Keys" },   /* en */
    { "Монеты", "Бомбы", "Ключи" },   /* ru */
    { "Münzen", "Bomben", "Schlüssel" },   /* de */
    { "Monedas", "Bombas", "Llaves" },   /* es */
    { "コイン", "ボム", "カギ" },   /* jp */
    { "동전", "폭탄", "열쇠" },   /* kr */
    { "硬币", "炸弹", "钥匙" },   /* zh */
};

static const char *const SP_GROUND[7] = {
    "On the ground:",   /* en */
    "На полу:",   /* ru */
    "Auf den Boden:",   /* de */
    "Al suelo:",   /* es */
    "ゆかに置く:",   /* jp */
    "바닥에:",   /* kr */
    "放在地上:",   /* zh */
};

static const char *const SP_ON[7] = {
    "ON",   /* en */
    "ВКЛ",   /* ru */
    "AN",   /* de */
    "SÍ",   /* es */
    "オン",   /* jp */
    "켜짐",   /* kr */
    "开",   /* zh */
};

static const char *const SP_OFF[7] = {
    "OFF",   /* en */
    "ВЫКЛ",   /* ru */
    "AUS",   /* de */
    "NO",   /* es */
    "オフ",   /* jp */
    "꺼짐",   /* kr */
    "关",   /* zh */
};

static const char *const SP_HORSE_FMT[7] = {
    "%s (horse)",   /* en */
    "%s (лошадиная)",   /* ru */
    "%s (Pferd)",   /* de */
    "%s (caballo)",   /* es */
    "%s (ウマ)",   /* jp */
    "%s (말)",   /* kr */
    "%s (马)",   /* zh */
};

static const char *const SP_GOLD_FMT[7] = {
    "%s (gold)",   /* en */
    "%s (золото)",   /* ru */
    "%s (golden)",   /* de */
    "%s (dorado)",   /* es */
    "%s (ゴールド)",   /* jp */
    "%s (황금)",   /* kr */
    "%s (金)",   /* zh */
};

static const char *const SP_CHARGE[7] = {
    "CHARGE ACTIVE ITEMS",   /* en */
    "ЗАРЯДИТЬ АКТИВНЫЕ ПРЕДМЕТЫ",   /* ru */
    "AKTIVE ITEMS LADEN",   /* de */
    "CARGAR ACTIVOS",   /* es */
    "アクティブをじゅうでん",   /* jp */
    "액티브 충전",   /* kr */
    "充满主动道具",   /* zh */
};

static const char *const SP_CHARGED[7] = {
    "Charged",   /* en */
    "Заряжено",   /* ru */
    "Geladen",   /* de */
    "Cargado",   /* es */
    "じゅうでんした",   /* jp */
    "충전됨",   /* kr */
    "已充满",   /* zh */
};

static const char *const SP_NO_ACTIVE[7] = {
    "No active item",   /* en */
    "Нет активного предмета",   /* ru */
    "Kein aktives Item",   /* de */
    "Sin objeto activo",   /* es */
    "アクティブがない",   /* jp */
    "액티브 아이템 없음",   /* kr */
    "没有主动道具",   /* zh */
};

static const char *const SP_SOUL_FMT[7] = {
    "%s: %s",   /* en */
    "%s: %s",   /* ru */
    "%s: %s",   /* de */
    "%s: %s",   /* es */
    "%s: %s",   /* jp */
    "%s: %s",   /* kr */
    "%s: %s",   /* zh */
};

static const char *const SP_LIMIT_FMT[7] = {
    "the game allows %s",   /* en */
    "игра даёт не больше: %s",   /* ru */
    "das Spiel erlaubt %s",   /* de */
    "el juego permite %s",   /* es */
    "ゲームの上限: %s",   /* jp */
    "게임 한도: %s",   /* kr */
    "游戏上限：%s",   /* zh */
};

static const char *const SP_HELD[7] = {
    "Only held:",   /* en */
    "Только мои:",   /* ru */
    "Nur eigene:",   /* de */
    "Solo míos:",   /* es */
    "もってるだけ:",   /* jp */
    "보유만:",   /* kr */
    "仅已拥有:",   /* zh */
};

static const char *const SP_CROSS[7] = {
    "×",   /* en */
    "×",   /* ru */
    "×",   /* de */
    "×",   /* es */
    "×",   /* jp */
    "×",   /* kr */
    "×",   /* zh */
};

static const char *const SP_CLEAR_ENEMIES[7] = {
    "CLEAR ENEMIES",   /* en */
    "УБРАТЬ ВРАГОВ",   /* ru */
    "GEGNER WEG",   /* de */
    "QUITAR ENEMIGOS",   /* es */
    "てきをけす",   /* jp */
    "적 제거",   /* kr */
    "清除敌人",   /* zh */
};

static const char *const SP_CLEAR_PICKUPS[7] = {
    "CLEAR PICKUPS",   /* en */
    "УБРАТЬ ПРЕДМЕТЫ",   /* ru */
    "ITEMS WEG",   /* de */
    "QUITAR OBJETOS",   /* es */
    "アイテムをけす",   /* jp */
    "아이템 제거",   /* kr */
    "清除物品",   /* zh */
};

static const char *const SP_NOTHING_CLEAR[7] = {
    "Nothing to clear",   /* en */
    "Убирать нечего",   /* ru */
    "Nichts zu entfernen",   /* de */
    "No hay nada que quitar",   /* es */
    "けすものがない",   /* jp */
    "제거할 것이 없음",   /* kr */
    "没有可清除的",   /* zh */
};

#define SP_COLLECTIBLE_TOP 732
static const unsigned char SP_COLLECTIBLE_REAL[92] = {   /* a bit per id the game has (items.xml) */
    0xfe, 0xff, 0xff, 0xff, 0xff, 0xf7, 0xff, 0xdf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf7, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xdf, 0xef, 0xbf, 0xff,
    0xff, 0xfe, 0xbf, 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0xff, 0x1f,
};

#define SP_TRINKET_TOP 189
static const unsigned char SP_TRINKET_REAL[24] = {   /* a bit per id the game has (items.xml) */
    0xfe, 0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f,
};

#endif
