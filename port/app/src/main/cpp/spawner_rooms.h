/* Written by tools/spawner_rooms.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_ROOMS_H
#define SPAWNER_ROOMS_H

/*
 * The teleport tab's rooms, in the order of the column: type is the game's
 * RoomType, subtype tells the boss challenge (RoomType 11, subtype 1) from the
 * plain one (0) and is 0 elsewhere. icon = frame of "Icons" in
 * gfx/port_room_icons.anm2 (SP_ROOM_ICON square, 100 %, pivot top left); a room
 * the minimap has no icon for wears the plain room cell (frame 0, as the start).
 * name[] in the sets' order (en ru de es jp kr zh), shortened where the 420
 * card asks (approved 04.10.2026); English where the face lacks a character.
 * "Stages": the floor icons (progress.anm2 "Levels"), SP_STAGE_W x SP_STAGE_H.
 * "Here" (frame 0) is the minimap's RoomCurrent cell, SP_HERE_W x SP_HERE_H;
 * "Seam" (frame 0) is 1 x 8 of the paper's grey, repeated down between the columns.
 */
#define SP_ROOM_ICON 24
#define SP_HERE_W 9
#define SP_HERE_H 8
#define SP_SEAM_H 8
#define SP_STAGE_W 26
#define SP_STAGE_H 18
#define SP_STAGES 28

#define SP_ROOMS 27

static const struct {
    unsigned char type, subtype;
    short icon;
    const char *name[7];
} SP_ROOM[SP_ROOMS] = {
    { 1, 0, 0, { "Starting Room", "Стартовая", "Startraum", "Sala inicial", "開始時の部屋", "시작방", "初始房间" } },   /* DEFAULT */
    { 4, 0, 1, { "Treasure Room", "Сокровищница", "Schatzraum", "Sala del tesoro", "トレジャールーム", "보물방", "宝箱房" } },   /* TREASURE */
    { 24, 0, 2, { "Planetarium", "Планетарий", "Planetarium", "Planetario", "プラネタリウム", "천체관", "星象房" } },   /* PLANETARIUM */
    { 5, 0, 3, { "Boss Room", "Босс", "Bossraum", "Sala del jefe", "ボス部屋", "보스방", "头目房" } },   /* BOSS */
    { 2, 0, 4, { "Shop", "Магазин", "Laden", "Tienda", "お店", "상점", "商店" } },   /* SHOP */
    { 6, 0, 5, { "Miniboss Room", "Мини-босс", "Miniboss-Raum", "Sala de minijefe", "中ボス部屋", "미니보스방", "小头目房" } },   /* MINIBOSS */
    { 7, 0, 6, { "Secret Room", "Секретная", "Geheimraum", "Sala secreta", "隠し部屋", "비밀방", "隐藏房" } },   /* SECRET */
    { 8, 0, 7, { "Super Secret Room", "Суперсекретная", "Super-Geheimraum", "Sala supersecreta", "スーパー隠し部屋", "일급 비밀방", "超级隐藏房" } },   /* SUPERSECRET */
    { 29, 0, 8, { "Ultra Secret Room", "Ультрасекретная", "Ultra-Geheimraum", "Sala ultrasecreta", "離れ隠し部屋", "특급 비밀방", "究极隐藏房" } },   /* ULTRASECRET */
    { 10, 0, 9, { "Curse Room", "Проклятая", "Fluchraum", "Sala maldita", "呪いの部屋", "저주방", "诅咒房" } },   /* CURSE */
    { 13, 0, 10, { "Sacrifice Room", "Жертвенная", "Opferraum", "Sala del sacrif.", "生贄部屋", "희생방", "献祭房" } },   /* SACRIFICE */
    { 12, 0, 11, { "Library", "Библиотека", "Bibliothek", "Biblioteca", "図書館", "책방", "图书馆" } },   /* LIBRARY */
    { 9, 0, 12, { "Arcade", "Аркада", "Arkadenraum", "Arcade", "アーケード", "오락실", "赌博房" } },   /* ARCADE */
    { 11, 0, 13, { "Challenge Room", "Испытание", "Challenge-Raum", "Sala de desafío", "チャレンジルーム", "도전방", "挑战房" } },   /* CHALLENGE */
    { 11, 1, 14, { "Boss Challenge", "Испытание боссов", "Boss-Challenge", "Desafío de jefes", "ボスチャレンジ", "보스 도전방", "头目挑战房" } },   /* CHALLENGE boss */
    { 21, 0, 15, { "Dice Room", "Игральная кость", "Würfelraum", "Sala de dados", "ダイス部屋", "주사위방", "骰子房" } },   /* DICE */
    { 20, 0, 16, { "Vault", "Хранилище", "Tresorraum", "Bóveda", "宝物庫", "금고방", "宝库" } },   /* CHEST */
    { 14, 0, 17, { "Devil Room", "Дьявол", "Teufelsraum", "Sala del Diablo", "悪魔部屋", "악마방", "恶魔房" } },   /* DEVIL */
    { 15, 0, 18, { "Angel Room", "Ангел", "Engelsraum", "Sala del Ángel", "天使部屋", "천사방", "天使房" } },   /* ANGEL */
    { 22, 0, 0, { "Black Market", "Чёрный рынок", "Schwarzmarkt", "Mercado negro", "ブラックマーケット", "암시장", "黑市" } },   /* BLACK_MARKET */
    { 16, 0, 19, { "Crawlspace", "Ретро-сокр.", "Kriechkeller", "Mazmorra", "クロールスペース", "사다리방", "夹层" } },   /* DUNGEON */
    { 17, 0, 0, { "Boss Rush", "Босс-раш", "Boss-Ansturm", "Boss Rush", "ボスラッシュ", "보스러시", "头目车轮战" } },   /* BOSSRUSH */
    { 18, 0, 20, { "Isaac's Bedroom", "Спальня Айзека", "Isaacs Schlafz.", "Dorm. de Isaac", "アイザックの寝室", "아이작의 침실", "以撒的卧室" } },   /* ISAACS */
    { 19, 0, 21, { "Barren Bedroom", "Пустая спальня", "Kahles Schlafz.", "Dormitorio vacío", "殺風景な寝室", "텅 빈 침실", "简陋的卧室" } },   /* BARREN */
    { 27, 0, 0, { "Secret Exit", "Тайный выход", "Geheimer Ausgang", "Salida secreta", "秘密の出口", "비밀 출구", "秘密出口" } },   /* SECRET_EXIT */
    { 28, 0, 0, { "Blue Room", "Синяя комната", "Blauer Raum", "Sala azul", "青い部屋", "파란 방", "蓝色房间" } },   /* BLUE */
    { 3, 0, 0, { "I AM ERROR", "I AM ERROR", "I AM ERROR", "I AM ERROR", "I AM ERROR", "오류방", "错误房" } },   /* ERROR */
};

#endif
