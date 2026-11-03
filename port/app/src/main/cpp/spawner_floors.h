/* Written by tools/spawner_floors.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_FLOORS_H
#define SPAWNER_FLOORS_H

/*
 * The teleport tab's floors: stage and stage_type as Level::SetStage takes them,
 * chapter groups the column (1..4 the chapters, 5 Sheol..Chest, 6 Void and Home;
 * Greed: 1..4, 5 Sheol, 6 The Shop, 7 Ultra Greed), mode 0 a normal run, 1 Greed
 * (its own stage numbers 1..7). The normal floors come first, SP_FLOORS_NORMAL of
 * them, then the Greed ones. name[] in the sets' order (en ru de es jp kr zh),
 * the game's own (stringtable, Stages) with its " I" / " II", a few paths
 * shortened for the 420 card (approved 04.10.2026).
 */
#define SP_FLOORS 60
#define SP_FLOORS_NORMAL 45
#define SP_FLOORS_GREED 15

static const struct {
    unsigned char stage, stage_type, chapter;
    unsigned char mode;     /* 0 normal, 1 greed */
    unsigned char level;    /* frame of "Stages" in gfx/port_room_icons.anm2 (0x7eeabc) */
    const char *name[7];
} SP_FLOOR[SP_FLOORS] = {
    { 1, 0, 1, 0, 0, { "Basement I", "Подвал I", "Souterrain I", "Sótano I", "地下室 I", "지하실 I", "地下室 I" } },
    { 2, 0, 1, 0, 0, { "Basement II", "Подвал II", "Souterrain II", "Sótano II", "地下室 II", "지하실 II", "地下室 II" } },
    { 1, 1, 1, 0, 1, { "Cellar I", "Погреб I", "Keller I", "Bodega I", "セラー I", "지하 창고 I", "地窖 I" } },
    { 2, 1, 1, 0, 1, { "Cellar II", "Погреб II", "Keller II", "Bodega II", "セラー II", "지하 창고 II", "地窖 II" } },
    { 1, 2, 1, 0, 2, { "Burning Bsmt I", "Горящий Подвал I", "Flammen-Sout. I", "Sót. en llamas I", "燃え上がる地下 I", "불타는 지하실 I", "燃烧地下室 I" } },
    { 2, 2, 1, 0, 2, { "Burning Bsmt II", "Горящий Подвал II", "Flammen-Sout. II", "Sót. en llamas II", "燃え上がる地下 II", "불타는 지하실 II", "燃烧地下室 II" } },
    { 1, 4, 1, 0, 19, { "Downpour I", "Водосток I", "Platzregen I", "Aguacero I", "ダウンポアー I", "폭우 I", "下水道 I" } },
    { 2, 4, 1, 0, 19, { "Downpour II", "Водосток II", "Platzregen II", "Aguacero II", "ダウンポアー II", "폭우 II", "下水道 II" } },
    { 1, 5, 1, 0, 20, { "Dross I", "Нечистоты I", "Schlacke I", "Desechos I", "ドロス I", "찌꺼기 I", "污水井 I" } },
    { 2, 5, 1, 0, 20, { "Dross II", "Нечистоты II", "Schlacke II", "Desechos II", "ドロス II", "찌꺼기 II", "污水井 II" } },
    { 3, 0, 2, 0, 3, { "Caves I", "Пещеры I", "Höhlen I", "Cuevas I", "洞窟 I", "동굴 I", "洞穴 I" } },
    { 4, 0, 2, 0, 3, { "Caves II", "Пещеры II", "Höhlen II", "Cuevas II", "洞窟 II", "동굴 II", "洞穴 II" } },
    { 3, 1, 2, 0, 4, { "Catacombs I", "Катакомбы I", "Katakomben I", "Catacumbas I", "地下墳墓 I", "지하 묘지 I", "墓穴 I" } },
    { 4, 1, 2, 0, 4, { "Catacombs II", "Катакомбы II", "Katakomben II", "Catacumbas II", "地下墳墓 II", "지하 묘지 II", "墓穴 II" } },
    { 3, 2, 2, 0, 5, { "Flooded Caves I", "Затопл. Пещеры I", "Überfl. Höhlen I", "Cuevas inund. I", "浸水洞窟 I", "침수된 동굴 I", "淹水洞穴 I" } },
    { 4, 2, 2, 0, 5, { "Flooded Caves II", "Затопл. Пещеры II", "Überfl. Höhlen II", "Cuevas inund. II", "浸水洞窟 II", "침수된 동굴 II", "淹水洞穴 II" } },
    { 3, 4, 2, 0, 21, { "Mines I", "Шахты I", "Minen I", "Minas I", "マインズ I", "광산 I", "矿洞 I" } },
    { 4, 4, 2, 0, 21, { "Mines II", "Шахты II", "Minen II", "Minas II", "マインズ II", "광산 II", "矿洞 II" } },
    { 3, 5, 2, 0, 22, { "Ashpit I", "Зольник I", "Aschegrube I", "Foso de cenizas I", "アッシュピット I", "잿구덩이 I", "灰坑 I" } },
    { 4, 5, 2, 0, 22, { "Ashpit II", "Зольник II", "Aschegrube II", "Foso de cenizas II", "アッシュピット II", "잿구덩이 II", "灰坑 II" } },
    { 5, 0, 3, 0, 6, { "Depths I", "Глубины I", "Tiefen I", "Profundidades I", "深淵 I", "나락 I", "深牢 I" } },
    { 6, 0, 3, 0, 6, { "Depths II", "Глубины II", "Tiefen II", "Profundidades II", "深淵 II", "나락 II", "深牢 II" } },
    { 5, 1, 3, 0, 7, { "Necropolis I", "Некрополис I", "Nekropolis I", "Necrópolis I", "ネクロポリス I", "공동묘지 I", "坟场 I" } },
    { 6, 1, 3, 0, 7, { "Necropolis II", "Некрополис II", "Nekropolis II", "Necrópolis II", "ネクロポリス II", "공동묘지 II", "坟场 II" } },
    { 5, 2, 3, 0, 8, { "Dank Depths I", "Сырые Глубины I", "Feuchte Tiefen I", "Prof. viscosas I", "湿った深淵 I", "습한 나락 I", "阴湿深牢 I" } },
    { 6, 2, 3, 0, 8, { "Dank Depths II", "Сырые Глубины II", "Feuchte Tiefen II", "Prof. viscosas II", "湿った深淵 II", "습한 나락 II", "阴湿深牢 II" } },
    { 5, 4, 3, 0, 23, { "Mausoleum I", "Мавзолей I", "Mausoleum I", "Mausoleo I", "マウソレウム I", "영묘 I", "陵墓 I" } },
    { 6, 4, 3, 0, 23, { "Mausoleum II", "Мавзолей II", "Mausoleum II", "Mausoleo II", "マウソレウム II", "영묘 II", "陵墓 II" } },
    { 5, 5, 3, 0, 24, { "Gehenna I", "Геенна I", "Gehenna I", "Gehena I", "ゲヘナ I", "게헨나 I", "炼狱 I" } },
    { 6, 5, 3, 0, 24, { "Gehenna II", "Геенна II", "Gehenna II", "Gehena II", "ゲヘナ II", "게헨나 II", "炼狱 II" } },
    { 7, 0, 4, 0, 9, { "Womb I", "Матка I", "Schoß I", "Matriz I", "子宮 I", "자궁 I", "子宫 I" } },
    { 8, 0, 4, 0, 9, { "Womb II", "Матка II", "Schoß II", "Matriz II", "子宮 II", "자궁 II", "子宫 II" } },
    { 7, 1, 4, 0, 10, { "Utero I", "Утроба I", "Uterus I", "Útero I", "ユテロ I", "자궁 속 I", "血宫 I" } },
    { 8, 1, 4, 0, 10, { "Utero II", "Утроба II", "Uterus II", "Útero II", "ユテロ II", "자궁 속 II", "血宫 II" } },
    { 7, 2, 4, 0, 11, { "Scarred Womb I", "Рубцовая Матка I", "Vernarb. Schoß I", "Matriz rasgada I", "傷だらけの子宮 I", "상처난 자궁 I", "结痂子宫 I" } },
    { 8, 2, 4, 0, 11, { "Scarred Womb II", "Рубцовая Матка II", "Vernarb. Schoß II", "Matriz rasgada II", "傷だらけの子宮 II", "상처난 자궁 II", "结痂子宫 II" } },
    { 7, 4, 4, 0, 25, { "Corpse I", "Труп I", "Leichnam I", "Cadáver I", "コープス I", "시체 I", "尸宫 I" } },
    { 8, 4, 4, 0, 25, { "Corpse II", "Труп II", "Leichnam II", "Cadáver II", "コープス II", "시체 II", "尸宫 II" } },
    { 9, 0, 4, 0, 12, { "???", "???", "???", "???", "？？？", "???", "???" } },
    { 10, 0, 5, 0, 13, { "Sheol", "Преисподняя", "Scheol", "Sheol", "シェオル", "저승", "阴间" } },
    { 10, 1, 5, 0, 14, { "Cathedral", "Собор", "Kathedrale", "Catedral", "大聖堂", "대성당", "教堂" } },
    { 11, 0, 5, 0, 15, { "Dark Room", "Темная Комната", "Dunkler Raum", "Cuarto Oscuro", "真っ暗な部屋", "어두운 방", "暗室" } },
    { 11, 1, 5, 0, 16, { "Chest", "Сундук", "Truhe", "Cofre", "チェスト", "상자", "玩具箱" } },
    { 12, 0, 6, 0, 18, { "The Void", "Пустота", "Die Leere", "El Vacío", "ザ ボイド", "공허", "虚空" } },
    { 13, 0, 6, 0, 27, { "Home", "Дом", "Zuhause", "Casa", "ホーム", "집", "家" } },
    { 1, 0, 1, 1, 0, { "Basement", "Подвал", "Souterrain", "Sótano", "地下室", "지하실", "地下室" } },
    { 1, 1, 1, 1, 1, { "Cellar", "Погреб", "Keller", "Bodega", "セラー", "지하 창고", "地窖" } },
    { 1, 2, 1, 1, 2, { "Burning Bsmt", "Горящий Подвал", "Flammen-Sout.", "Sót. en llamas", "燃え上がる地下", "불타는 지하실", "燃烧地下室" } },
    { 2, 0, 2, 1, 3, { "Caves", "Пещеры", "Höhlen", "Cuevas", "洞窟", "동굴", "洞穴" } },
    { 2, 1, 2, 1, 4, { "Catacombs", "Катакомбы", "Katakomben", "Catacumbas", "地下墳墓", "지하 묘지", "墓穴" } },
    { 2, 2, 2, 1, 5, { "Flooded Caves", "Затопл. Пещеры", "Überfl. Höhlen", "Cuevas inund.", "浸水洞窟", "침수된 동굴", "淹水洞穴" } },
    { 3, 0, 3, 1, 6, { "Depths", "Глубины", "Tiefen", "Profundidades", "深淵", "나락", "深牢" } },
    { 3, 1, 3, 1, 7, { "Necropolis", "Некрополис", "Nekropolis", "Necrópolis", "ネクロポリス", "공동묘지", "坟场" } },
    { 3, 2, 3, 1, 8, { "Dank Depths", "Сырые Глубины", "Feuchte Tiefen", "Prof. viscosas", "湿った深淵", "습한 나락", "阴湿深牢" } },
    { 4, 0, 4, 1, 9, { "Womb", "Матка", "Schoß", "Matriz", "子宮", "자궁", "子宫" } },
    { 4, 1, 4, 1, 10, { "Utero", "Утроба", "Uterus", "Útero", "ユテロ", "자궁 속", "血宫" } },
    { 4, 2, 4, 1, 11, { "Scarred Womb", "Рубцовая Матка", "Vernarb. Schoß", "Matriz rasgada", "傷だらけの子宮", "상처난 자궁", "结痂子宫" } },
    { 5, 0, 5, 1, 13, { "Sheol", "Преисподняя", "Scheol", "Sheol", "シェオル", "저승", "阴间" } },
    { 6, 0, 6, 1, 16, { "The Shop", "Магазин", "Der Laden", "La tienda", "ザ ショップ", "상점", "商店" } },
    { 7, 0, 7, 1, 16, { "Ultra Greed", "Ультра Алчность", "Ultra-Gier", "Ultra Codicia", "ウルトラグリード", "극심한 탐욕", "究极贪婪" } },
};

#endif
