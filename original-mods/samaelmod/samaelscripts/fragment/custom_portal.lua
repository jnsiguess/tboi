local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local kEnemyPortalOffset = Vector(0, -20)

--------------------------------------------------
-- Default enemy pools for each floor.

local PORTAL_FLOOR_POOL = {
	-- Chapter 1
	[lib.FloorName.BASEMENT] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GAPER},
		{EntityType.ENTITY_GAPER, 1},
		{EntityType.ENTITY_BLURB},
		{EntityType.ENTITY_BUBBLES},
		{EntityType.ENTITY_FATTY},
		{EntityType.ENTITY_FATTY, 1}, -- Pale fatty
		{EntityType.ENTITY_CONJOINED_FATTY},
		{EntityType.ENTITY_CYCLOPIA},
		{EntityType.ENTITY_MINISTRO},
		{EntityType.ENTITY_CLOTTY},
		{EntityType.ENTITY_MEGA_CLOTTY},
		{EntityType.ENTITY_CLOTTY, 2}, -- I.Blob
		{EntityType.ENTITY_HIVE},
		{EntityType.ENTITY_POOTER},
		{EntityType.ENTITY_POOTER, 1}, -- Super pooter
		{EntityType.ENTITY_SUCKER},
		{EntityType.ENTITY_FLY_L2},
		{EntityType.ENTITY_FULL_FLY},
		{EntityType.ENTITY_SQUIRT},
		{EntityType.ENTITY_FLY_TRAP},
		{EntityType.ENTITY_DUKIE},
		{EntityType.ENTITY_NEEDLE},
		{EntityType.ENTITY_DUMP},
	},
	[lib.FloorName.CELLAR] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_NEST},
		{EntityType.ENTITY_BIGSPIDER},
		{EntityType.ENTITY_SPIDER_L2},
		{EntityType.ENTITY_HOPPER, 1}, -- Trite
		{EntityType.ENTITY_BABY_LONG_LEGS},
		{EntityType.ENTITY_BABY_LONG_LEGS, 1},
		{EntityType.ENTITY_CRAZY_LONG_LEGS},
		{EntityType.ENTITY_CRAZY_LONG_LEGS, 1},
		{EntityType.ENTITY_WALKINGBOIL, 2}, -- Walking sack
		{EntityType.ENTITY_HOPPER, 2}, -- Eggy
		{EntityType.ENTITY_THE_HAUNT, 10}, -- Lil haunt
		{EntityType.ENTITY_DUST},
		{EntityType.ENTITY_MIGRAINE},
	},
	[lib.FloorName.BURNING_BASEMENT] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_FLAMINGHOPPER},
		{EntityType.ENTITY_GAPER, 2}, -- Flaming gaper
		{EntityType.ENTITY_FATTY, 2}, -- Flaming fatty
		{EntityType.ENTITY_SKINNY},
		{EntityType.ENTITY_SKINNY, 2}, -- Crispy
		{EntityType.ENTITY_RAGLING},
		{EntityType.ENTITY_RAGLING, 1},
		{EntityType.ENTITY_ROCK_SPIDER, 2}, -- Coal spider
		{EntityType.ENTITY_CLOTTY, 3}, -- Flaming clotty
		{EntityType.ENTITY_NEEDLE},
		{EntityType.ENTITY_GYRO, 1}, -- Flaming gyro
	},
	[lib.FloorName.DOWNPOUR] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GUSHER},
		{EntityType.ENTITY_BLURB},
		{EntityType.ENTITY_BUBBLES},
		{EntityType.ENTITY_GAPER},
		{EntityType.ENTITY_GAPER, 1},
		{EntityType.ENTITY_WILLO},
		{EntityType.ENTITY_PREY},
		{EntityType.ENTITY_PREY, 1}, -- Mullighoul
		{EntityType.ENTITY_CHARGER, 1}, -- Drowned charger
		{EntityType.ENTITY_WILLO_L2},
		{EntityType.ENTITY_BLOATY},
		{EntityType.ENTITY_BOOMFLY, 2}, -- Drowned boom fly
		{EntityType.ENTITY_HIVE, 1}, -- Drowned hive
	},
	[lib.FloorName.DROSS] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_BLURB},
		{EntityType.ENTITY_BUBBLES},
		{EntityType.ENTITY_GAPER},
		{EntityType.ENTITY_GAPER, 1},
		{EntityType.ENTITY_FARTIGAN},
		{EntityType.ENTITY_DIP, 3}, -- Big corn
		{EntityType.ENTITY_SPLURT},
		{EntityType.ENTITY_CLOGGY},
		{EntityType.ENTITY_DUMP},
		{EntityType.ENTITY_DUKIE},
		{EntityType.ENTITY_SQUIRT},
		{EntityType.ENTITY_GURGLING, 2}, -- Turdling
		{EntityType.ENTITY_SQUIRT, 1}, -- Dank squirt
		{EntityType.ENTITY_CLOTTY, 1}, -- Clot
	},
	-- Chapter 2
	[lib.FloorName.CAVES] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GLOBIN},
		{EntityType.ENTITY_LEAPER},
		{EntityType.ENTITY_BOOMFLY},
		{EntityType.ENTITY_BOOMFLY, 1}, -- Red boomfly
		{EntityType.ENTITY_SUCKER},
		{EntityType.ENTITY_ONE_TOOTH},
		{EntityType.ENTITY_FAT_BAT},
		{EntityType.ENTITY_FATTY, 1}, -- Pale fatty
		{EntityType.ENTITY_SKINNY, 1}, -- Rotty
		{EntityType.ENTITY_FAT_SACK},
		{EntityType.ENTITY_BLUBBER},
		{EntityType.ENTITY_ROCK_SPIDER},
		{EntityType.ENTITY_HALF_SACK},
		{EntityType.ENTITY_GYRO},
		{EntityType.ENTITY_SQUIRT},
		{EntityType.ENTITY_DINGA},
		{EntityType.ENTITY_FACELESS},
		{EntityType.ENTITY_DUMP},
	},
	[lib.FloorName.CATACOMBS] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_SUCKER, 1}, -- Spit
		{EntityType.ENTITY_NEEDLE},
		{EntityType.ENTITY_ROUND_WORM},
		{EntityType.ENTITY_NIGHT_CRAWLER},
		{EntityType.ENTITY_CHARGER},
		{EntityType.ENTITY_CHARGER, 3}, -- Carrion princess
		{EntityType.ENTITY_SPITTY},
		{EntityType.ENTITY_CONJOINED_SPITTY},
		{EntityType.ENTITY_KEEPER},
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_VIS, 2}, -- Chubber
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_GURGLE},
		{EntityType.ENTITY_BOOMFLY},
		{EntityType.ENTITY_BOOMFLY, 1}, -- Red boomfly
		{EntityType.ENTITY_HOPPER, 2}, -- Eggy
		{EntityType.ENTITY_MIGRAINE},
	},
	[lib.FloorName.FLOODED_CAVES] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_BLURB},
		{EntityType.ENTITY_BUBBLES},
		{EntityType.ENTITY_PREY},
		{EntityType.ENTITY_CHARGER, 1}, -- Drowned charger
		{EntityType.ENTITY_BLOATY},
		{EntityType.ENTITY_BOOMFLY, 2}, -- Drowned boomfly
		{EntityType.ENTITY_HIVE, 1}, -- Drowned hive
		{EntityType.ENTITY_ONE_TOOTH},
		{EntityType.ENTITY_FAT_BAT},
		{EntityType.ENTITY_FAT_SACK},
		{EntityType.ENTITY_BLUBBER},
		{EntityType.ENTITY_HALF_SACK},
		{EntityType.ENTITY_LEAPER},
	},
	[lib.FloorName.MINES] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GURGLE},
		{EntityType.ENTITY_GURGLE, 1}, -- Crackle
		{EntityType.ENTITY_FACELESS},
		{EntityType.ENTITY_FLY_BOMB},
		{EntityType.ENTITY_DANNY},
		{EntityType.ENTITY_DANNY, 1}, -- Coal boy
		{EntityType.ENTITY_BLASTER},
		{EntityType.ENTITY_BOUNCER},
		{EntityType.ENTITY_QUAKEY},
		{EntityType.ENTITY_HARDY},
		{EntityType.ENTITY_GYRO},
		{EntityType.ENTITY_GYRO, 1},
		{EntityType.ENTITY_MOLE},
		{EntityType.ENTITY_ROCK_SPIDER},
		{EntityType.ENTITY_ROCK_SPIDER, 2},
		{EntityType.ENTITY_BOOMFLY, 3}, -- Dragon Fly
		{EntityType.ENTITY_BOOMFLY, 3, 1},
		{EntityType.ENTITY_MIGRAINE},
	},
	[lib.FloorName.ASHPIT] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_GURGLE},
		{EntityType.ENTITY_GURGLE, 1}, -- Crackle
		{EntityType.ENTITY_NECRO},
		{EntityType.ENTITY_DUST},
		{EntityType.ENTITY_CHARGER, 3}, -- Carrion Princess
		{EntityType.ENTITY_BIG_BONY},
		{EntityType.ENTITY_FLESH_MAIDEN},
		{EntityType.ENTITY_CLICKETY_CLACK},
		{EntityType.ENTITY_NEEDLE, 1}, -- Pasty
		{EntityType.ENTITY_BOOMFLY, 4}, -- Bone fly
		{EntityType.ENTITY_BOOMFLY, 3}, -- Dragon Fly
		{EntityType.ENTITY_BOOMFLY, 3, 1},
		{EntityType.ENTITY_MIGRAINE},
	},
	-- Chapter 3
	[lib.FloorName.DEPTHS] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_KNIGHT},
		{EntityType.ENTITY_FLOATING_KNIGHT},
		{EntityType.ENTITY_FAT_SACK},
		{EntityType.ENTITY_LEAPER},
		{EntityType.ENTITY_GURGLING},
		{EntityType.ENTITY_VIS},
		{EntityType.ENTITY_VIS, 2}, -- Chubber
		{EntityType.ENTITY_WIZOOB},
		{EntityType.ENTITY_PON},
		{EntityType.ENTITY_KNIGHT, 2}, -- Loose knight
		{EntityType.ENTITY_POISON_MIND},
		{EntityType.ENTITY_BRAIN},
		{EntityType.ENTITY_MEMBRAIN},
		{EntityType.ENTITY_MRMAW},
		{EntityType.ENTITY_MRMAW, 2}, -- Mr red maw
		{EntityType.ENTITY_WHIPPER},
		{EntityType.ENTITY_WHIPPER, 1}, -- Snapper
	},
	[lib.FloorName.NECROPOLIS] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_KNIGHT},
		{EntityType.ENTITY_FLOATING_KNIGHT},
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_CHARGER, 3}, -- Carrion princess
		{EntityType.ENTITY_BIG_BONY},
		{EntityType.ENTITY_FLESH_MAIDEN},
		{EntityType.ENTITY_CLICKETY_CLACK},
		{EntityType.ENTITY_NEEDLE, 1}, -- Pasty
		{EntityType.ENTITY_BOOMFLY, 4}, -- Bone fly
		{EntityType.ENTITY_FAT_SACK},
		{EntityType.ENTITY_LEAPER},
		{EntityType.ENTITY_KEEPER},
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_SWARMER},
		{EntityType.ENTITY_WIZOOB},
		{EntityType.ENTITY_VIS, 1}, -- Double vis
		{EntityType.ENTITY_THE_HAUNT, 10}, -- Lil haunt
		{EntityType.ENTITY_REVENANT},
		{EntityType.ENTITY_HOPPER, 2}, -- Eggy
		{EntityType.ENTITY_WHIPPER},
		{EntityType.ENTITY_WHIPPER, 1}, -- Snapper
	},
	[lib.FloorName.DANK_DEPTHS] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_DEATHS_HEAD, 1}, -- Dank deaths head
		{EntityType.ENTITY_CLOTTY, 1}, -- Clot
		{EntityType.ENTITY_GLOBIN, 2}, -- Dank globin
		{EntityType.ENTITY_BLACK_GLOBIN},
		{EntityType.ENTITY_BLACK_GLOBIN_HEAD},
		{EntityType.ENTITY_SQUIRT, 1}, -- Dank squirt
		{EntityType.ENTITY_SPLURT},
		{EntityType.ENTITY_CLOGGY},
		{EntityType.ENTITY_CHARGER, 2}, -- Dank charger
		{EntityType.ENTITY_TARBOY},
		{EntityType.ENTITY_BUTT_SLICKER},
		{EntityType.ENTITY_SUCKER, 2}, -- Ink
		{EntityType.ENTITY_GUTS, 2}, -- Slog
		{EntityType.ENTITY_LEAPER, 1}, -- Dank leaper
		{EntityType.ENTITY_FISTULA_MEDIUM, 1}, -- Teratoma
		{EntityType.ENTITY_FISTULA_SMALL, 1},
	},
	[lib.FloorName.MAUSOLEUM] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GLOBIN, 3}, -- Cursed Globin
		{EntityType.ENTITY_KNIGHT, 2}, -- Loose knight
		{EntityType.ENTITY_PON},
		{EntityType.ENTITY_REVENANT},
		{EntityType.ENTITY_RAGLING},
		{EntityType.ENTITY_VIS_VERSA},
		{EntityType.ENTITY_WHIPPER},
		{EntityType.ENTITY_WHIPPER, 1}, -- Snapper
		{EntityType.ENTITY_WHIPPER, 2}, -- Flagellant
		{EntityType.ENTITY_CULTIST, 1}, -- Red cultist
		{EntityType.ENTITY_BABY_BEGOTTEN},
		{EntityType.ENTITY_VIS_FATTY},
		{EntityType.ENTITY_VIS_FATTY, 1}, -- The baby
		{EntityType.ENTITY_MAW, 2}, -- Psychic maw
		{EntityType.ENTITY_THE_HAUNT, 10}, -- lil haunt
		{EntityType.ENTITY_CANDLER},
		{EntityType.ENTITY_BIG_BONY},
	},
	[lib.FloorName.GEHENNA] = {
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_GLOBIN, 3}, -- Cursed Globin
		{EntityType.ENTITY_BLACK_KNIGHT},
		{EntityType.ENTITY_BONE_KNIGHT},
		{EntityType.ENTITY_SUCKER, 3}, -- Soul sucker
		{EntityType.ENTITY_WHIPPER},
		{EntityType.ENTITY_WHIPPER, 1}, -- Snapper
		{EntityType.ENTITY_CULTIST, 1}, -- Red cultist
		{EntityType.ENTITY_BABY_BEGOTTEN},
		{EntityType.ENTITY_VIS_FATTY},
		{EntityType.ENTITY_VIS_FATTY, 1}, -- The baby
		{EntityType.ENTITY_VIS_VERSA},
		{EntityType.ENTITY_LITTLE_HORN},
		{EntityType.ENTITY_GOAT},
		{EntityType.ENTITY_GOAT, 1}, -- Black goat
		{EntityType.ENTITY_KNIGHT, 4}, -- Black knight
		{EntityType.ENTITY_MORNINGSTAR},
		{EntityType.ENTITY_POOFER},
		{EntityType.ENTITY_REVENANT},
	},
	-- Chapter 4
	[lib.FloorName.WOMB] = {
		{EntityType.ENTITY_FRED},
		{EntityType.ENTITY_GURGLING},
		{EntityType.ENTITY_LUMP},
		{EntityType.ENTITY_GUTS},
		{EntityType.ENTITY_MEMBRAIN, 1}, -- Mama guts
		{EntityType.ENTITY_SWINGER},
		{EntityType.ENTITY_OOB},
		{EntityType.ENTITY_PARA_BITE},
		{EntityType.ENTITY_MEGA_CLOTTY},
		{EntityType.ENTITY_VIS},
		{EntityType.ENTITY_VIS, 1}, -- Double vis
		{EntityType.ENTITY_VIS, 2}, -- Chubber
		{EntityType.ENTITY_GURDY_JR},
		{EntityType.ENTITY_CHARGER},
		{EntityType.ENTITY_BABY},
		{EntityType.ENTITY_UNBORN},
	},
	[lib.FloorName.UTERO] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GLOBIN},
		{EntityType.ENTITY_GLOBIN, 1},
		{EntityType.ENTITY_COHORT},
		{EntityType.ENTITY_FLOATING_HOST},
		{EntityType.ENTITY_MEATBALL},
		{EntityType.ENTITY_FLESH_MOBILE_HOST},
		{EntityType.ENTITY_LEECH},
		{EntityType.ENTITY_ADULT_LEECH},
		{EntityType.ENTITY_NEEDLE},
		{EntityType.ENTITY_OOB},
		{EntityType.ENTITY_PEEPER_FATTY},
		{EntityType.ENTITY_TUMOR},
		{EntityType.ENTITY_TUMOR, 1}, -- Planetoid
		{EntityType.ENTITY_BLASTOCYST_MEDIUM},
		{EntityType.ENTITY_BLASTOCYST_SMALL},
		{EntityType.ENTITY_BABY, 3}, -- Wrinkly baby
		{EntityType.ENTITY_POOFER},
	},
	[lib.FloorName.SCARRED_WOMB] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_FAT_SACK},
		{EntityType.ENTITY_BLUBBER},
		{EntityType.ENTITY_HALF_SACK},
		{EntityType.ENTITY_GUTS, 1}, -- Scarred guts
		{EntityType.ENTITY_MAW, 1}, -- Red maw
		{EntityType.ENTITY_VIS, 1}, -- Double vis
		{EntityType.ENTITY_VIS, 3}, -- Scarred vis
		{EntityType.ENTITY_RED_GHOST},
		{EntityType.ENTITY_FLESH_DEATHS_HEAD},
		{EntityType.ENTITY_FISTULOID},
		{EntityType.ENTITY_LEPER},
		{EntityType.ENTITY_FACELESS},
		{EntityType.ENTITY_MRMAW},
		{EntityType.ENTITY_MRMAW, 2}, -- Mr red maw
		{EntityType.ENTITY_PARA_BITE, 1}, -- Scarred para bite
		{EntityType.ENTITY_FISTULA_MEDIUM},
		{EntityType.ENTITY_FISTULA_SMALL},
		{EntityType.ENTITY_FLESH_MAIDEN},
		{EntityType.ENTITY_VIS_FATTY},
	},
	[lib.FloorName.CORPSE] = {
		{EntityType.ENTITY_GAPER, 3}, -- Rotten gaper
		{EntityType.ENTITY_GUTTED_FATTY},
		{EntityType.ENTITY_SUCKER, 4}, -- Mama fly
		{EntityType.ENTITY_TWITCHY},
		{EntityType.ENTITY_GAPER_L2},
		{EntityType.ENTITY_GAPER_L2, 1}, -- L2 Horf
		{EntityType.ENTITY_GAPER_L2, 2}, -- L2 Gusher
		{EntityType.ENTITY_CYST},
		{EntityType.ENTITY_MEMBRAIN, 2}, -- Dead Meat
		{EntityType.ENTITY_CHARGER},
		{EntityType.ENTITY_CHARGER_L2},
		{EntityType.ENTITY_LEECH},
		{EntityType.ENTITY_ADULT_LEECH},
		{EntityType.ENTITY_WHIPPER, 2}, -- Flagellant
		{EntityType.ENTITY_COHORT},
		{EntityType.ENTITY_UNBORN},
		{EntityType.ENTITY_BABY, 3}, -- Wrinkly baby
		{EntityType.ENTITY_CULTIST, 1}, -- Red cultist
	},
	[lib.FloorName.BLUE_WOMB] = {
		{EntityType.ENTITY_HUSH_GAPER},
		{EntityType.ENTITY_CONJOINED_FATTY, 1}, -- Blue conjoined fatty
	},
	-- Chapter 5
	[lib.FloorName.SHEOL] = {
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_NULLS},
		{EntityType.ENTITY_KNIGHT, 1}, -- Selfless knight
		{EntityType.ENTITY_BABY_BEGOTTEN},
		{EntityType.ENTITY_BONE_KNIGHT},
		{EntityType.ENTITY_LEECH, 1}, -- Black leech
		{EntityType.ENTITY_BLACK_MAW},
		{EntityType.ENTITY_CAMILLO_JR},
		{EntityType.ENTITY_WIZOOB},
		{EntityType.ENTITY_RED_GHOST},
		{EntityType.ENTITY_IMP},
		{EntityType.ENTITY_THE_HAUNT, 10}, -- Lil haunt
		{EntityType.ENTITY_WHIPPER},
		{EntityType.ENTITY_WHIPPER, 1}, -- Snapper
		{EntityType.ENTITY_WHIPPER, 2}, -- Flagellant
		{EntityType.ENTITY_GOAT, 1}, -- Black goat
		{EntityType.ENTITY_KNIGHT, 4}, -- Black knight
		{EntityType.ENTITY_CULTIST, 1}, -- Red cultist
		{EntityType.ENTITY_BLACK_GLOBIN},
	},
	[lib.FloorName.CATHEDRAL] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_BABY, 1}, -- Angel baby
		{EntityType.ENTITY_RAGLING},
		{EntityType.ENTITY_PSY_TUMOR},
		{EntityType.ENTITY_PSY_HORF},
		{EntityType.ENTITY_MAW, 2}, -- Psychic maw
		{EntityType.ENTITY_KEEPER},
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_LEECH, 2}, -- Holy leech
		{EntityType.ENTITY_WIZOOB},
		{EntityType.ENTITY_HIVE, 2}, -- Holy mulligan
		{EntityType.ENTITY_CANDLER},
		{EntityType.ENTITY_THE_HAUNT, 10}, -- lil haunt
		{EntityType.ENTITY_BONY, 1}, -- Holy bony
		{EntityType.ENTITY_REVENANT},
	},
	-- Chapter 6
	[lib.FloorName.DARK_ROOM] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_WIZOOB},
		{EntityType.ENTITY_RED_GHOST},
		{EntityType.ENTITY_THE_HAUNT, 10}, -- Lil haunt
		{EntityType.ENTITY_NECRO},
		{EntityType.ENTITY_DUST},
		{EntityType.ENTITY_BIG_BONY},
		{EntityType.ENTITY_SHADY},
		{EntityType.ENTITY_VIS_VERSA},
		{EntityType.ENTITY_GOAT, 1}, -- Black goat
		{EntityType.ENTITY_KNIGHT, 4}, -- Black knight
		{EntityType.ENTITY_CULTIST, 1}, -- Red cultist
		{EntityType.ENTITY_WHIPPER, 2}, -- Flagellant
		{EntityType.ENTITY_GLOBIN, 3}, -- Cursed Globin
		{EntityType.ENTITY_VIS_FATTY},
		{EntityType.ENTITY_REVENANT},
		{EntityType.ENTITY_MIGRAINE},
		{EntityType.ENTITY_CHARGER, 3}, -- Carrion princess
		{EntityType.ENTITY_BONE_KNIGHT},
	},
	[lib.FloorName.CHEST] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GREED_GAPER},
		{EntityType.ENTITY_HUSH_GAPER},
		{EntityType.ENTITY_CONJOINED_FATTY, 1}, -- Blue conjoined fatty
		{EntityType.ENTITY_KEEPER},
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_DUKE},
		{EntityType.ENTITY_CULTIST, 1}, -- Red cultist
		{EntityType.ENTITY_BONY, 1}, -- Holy bony
		{EntityType.ENTITY_HIVE, 2}, -- Holy mulligan
		{EntityType.ENTITY_WIZOOB},
		{EntityType.ENTITY_THE_HAUNT, 10}, -- Lil haunt
		{EntityType.ENTITY_CANDLER},
	},
	[lib.FloorName.SHOP] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GREED_GAPER},
		{EntityType.ENTITY_KEEPER},
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_MONSTRO},
		{EntityType.ENTITY_DUKE},
		{EntityType.ENTITY_GREED},
		{EntityType.ENTITY_HUSH_GAPER},
		{EntityType.ENTITY_CONJOINED_FATTY, 1}, -- Blue conjoined fatty
	},
	-- Chapter 7
	[lib.FloorName.VOID]	= {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_GREED_GAPER},
		{EntityType.ENTITY_GAPER, 2}, -- Flaming gaper
		{EntityType.ENTITY_FLAMINGHOPPER},
		{EntityType.ENTITY_SKINNY, 2}, -- Crispy
		{EntityType.ENTITY_BOOMFLY, 2}, -- Drowned boom fly
		{EntityType.ENTITY_HIVE, 1}, -- Drowned hive
		{EntityType.ENTITY_CHARGER, 1}, -- Drowned charger
		{EntityType.ENTITY_DEATHS_HEAD, 1}, -- Dank deaths head
		{EntityType.ENTITY_SQUIRT, 1}, -- Dank squirt
		{EntityType.ENTITY_GLOBIN, 2}, -- Dank globin
		{EntityType.ENTITY_GUTS, 1}, -- Scarred guts
		{EntityType.ENTITY_PARA_BITE, 1}, -- Scarred para bite
		{EntityType.ENTITY_VIS, 3}, -- Scarred vis
		{EntityType.ENTITY_STONEY},
		{EntityType.ENTITY_STONEY, 10}, -- Cross stony
	},
	[lib.FloorName.HOME] = {
		{EntityType.ENTITY_BONY},
		{EntityType.ENTITY_CLOTTY, 2}, -- I.Blob
		{EntityType.ENTITY_GAPER, 1}, -- Smiling gaper
		{EntityType.ENTITY_KEEPER},
		{EntityType.ENTITY_HANGER},
		{EntityType.ENTITY_GREED_GAPER},
		{EntityType.ENTITY_HUSH_GAPER},
	},
}

local function GetDefaultEnemyPoolForFloor()
	return PORTAL_FLOOR_POOL[lib.GetCurrentFloorName(false)]
end

--------------------------------------------------
-- Actual logic

function mod:CheckEnemyPortalTile(gridIndex)
	local roomSpawns = lib.GetRoomSpawns()
	
	if roomSpawns[gridIndex] then
		for _, spawn in pairs(roomSpawns[gridIndex]) do
			if spawn.Type == mod.ENTITIES.ENEMY_PORTAL.ID and spawn.Variant == mod.ENTITIES.ENEMY_PORTAL.Var then
				return spawn.SubType
			end
		end
	end
end

function mod:EnemyPortalInit(portal)
	if portal.Variant == mod.ENTITIES.ENEMY_PORTAL.Var then
		portal.SpriteOffset = kEnemyPortalOffset
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.EnemyPortalInit, mod.ENTITIES.ENEMY_PORTAL.ID)

local function GetPortalSpawnsFromRoomLayout(pos)
	local gridIdx = game:GetRoom():GetGridIndex(pos)
	local roomSpawns = lib.GetRoomSpawns()
	
	if not roomSpawns[gridIdx] then return end
	
	local portalPool = {}
	
	local foundSelf = false
	for _, spawn in pairs(roomSpawns[gridIdx]) do
		if not foundSelf and spawn.Type == mod.ENTITIES.ENEMY_PORTAL.ID and spawn.Variant == mod.ENTITIES.ENEMY_PORTAL.Var then
			foundSelf = true
		elseif not (spawn.Type == mod.ENTITIES.CHARM_MARK.ID and spawn.Variant == mod.ENTITIES.CHARM_MARK.Var) then
			table.insert(portalPool, spawn)
		end
	end
	
	if #portalPool > 0 then
		return portalPool
	end
end

local function GetMinecart(entity)
	local minecarts = Isaac.FindByType(EntityType.ENTITY_MINECART, -1, -1, true)
	for _, minecart in ipairs(minecarts) do
		if minecart.Child and GetPtrHash(minecart.Child) == GetPtrHash(entity) then
			return minecart
		end
	end
	return nil
end

function mod:EnemyPortalUpdate(portal)
	if portal.Variant ~= mod.ENTITIES.ENEMY_PORTAL.Var then return end
	
	local data = portal:GetData()
	
	if not data.samaelEnemyPortalInitialized then
		if portal.SubType > 0 then
			portal.I1 = portal.SubType
		end
		data.samaelMinecart = GetMinecart(portal)
		data.samaelEnemyPortalInitialized = true
	end
	
	if not data.samaelEnemyPortalPool then
		data.samaelEnemyPortalPool = GetPortalSpawnsFromRoomLayout(portal.Position) or GetDefaultEnemyPoolForFloor()
	end
	
	if portal.State == 8 then
		local truePos = (data.samaelMinecart or portal).Position
		
		game:MakeShockwave(truePos + kEnemyPortalOffset, 0.04, 0.01, 10)
		sfxManager:Play(SoundEffect.SOUND_DEATH_BURST_LARGE)
		portal.I1 = portal.I1 - 1
		
		local rng = portal:GetDropRNG()
		if data.samaelEnemyPortalPool then
			local spawn = lib.PickRandom(data.samaelEnemyPortalPool, rng)
			if spawn then
				local eType = spawn.Type or spawn.ID or spawn[1]
				local eVariant = spawn.Variant or spawn.Var or spawn[2] or 0
				local eSubType = spawn.SubType or spawn.Sub or spawn[3] or 0
				local entity = game:Spawn(eType, eVariant, truePos, lib.ZeroVector, portal, eSubType, rng:Next())
				if eType == EntityType.ENTITY_PORTAL then
					-- For one funny room only.
					entity.Position = entity.Position + RandomVector() * 30
					if eVariant == mod.ENTITIES.ENEMY_PORTAL.Var then
						entity:GetData().samaelEnemyPortalPool = data.samaelEnemyPortalPool
					end
					entity.HitPoints = math.ceil(portal.HitPoints * 0.5)
				end
				if portal:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
					entity:AddCharmed(EntityRef(Isaac.GetPlayer()), -1)
				end
				entity:GetData().samaelPermaCharmMarked = data.samaelPermaCharmMarked
				entity:AddEntityFlags(EntityFlag.FLAG_DONT_COUNT_BOSS_HP)
			end
		end
		
		if portal.I1 <= 0 then
			portal.Position = portal.Position + portal.SpriteOffset
			portal:Die()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.EnemyPortalUpdate, mod.ENTITIES.ENEMY_PORTAL.ID)
