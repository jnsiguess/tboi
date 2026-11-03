local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local NLMode = {
	NONE = 0,
	NO_L = 1,
	OLD = 2,
	NEW = 3,
}

local BackdropData = {}
mod.BackdropData = BackdropData

local function Backdrop(backdropType, prefix, name, nlMode)
	local path = "gfx/backdrop/"
	
	if not prefix then
		prefix = path
	else
		prefix = path .. prefix .. "_"
	end
	
	local tab = {
		Gfx = prefix .. name .. ".png",
	}
	
	if nlMode ~= NLMode.NONE then
		tab.NGfx = prefix .. name .. "_nfloor.png"
	end
	
	if nlMode == NLMode.OLD then
		tab.LGfx = prefix .. "l" .. name .. "floor.png"
	elseif nlMode == NLMode.NEW then
		tab.LGfx = prefix .. name .. "_lfloor.png"
	end
	
	BackdropData[backdropType] = tab
end

Backdrop(BackdropType.BASEMENT, "01", "basement", NLMode.OLD)
Backdrop(BackdropType.CELLAR, "02", "cellar", NLMode.OLD)
Backdrop(BackdropType.BURNT_BASEMENT, "13", "the burning basement", NLMode.OLD)
Backdrop(BackdropType.CAVES, "03", "caves", NLMode.OLD)
Backdrop(BackdropType.CATACOMBS, "04", "catacombs", NLMode.OLD)
Backdrop(BackdropType.FLOODED_CAVES, "14", "the drowned caves", NLMode.OLD)
Backdrop(BackdropType.DEPTHS, "05", "depths", NLMode.OLD)
Backdrop(BackdropType.NECROPOLIS, "06", "necropolis", NLMode.OLD)
Backdrop(BackdropType.DANK_DEPTHS, "15", "the dank depths", NLMode.OLD)
Backdrop(BackdropType.WOMB, "07", "the womb", NLMode.OLD)
Backdrop(BackdropType.UTERO, "08", "utero", NLMode.OLD)
Backdrop(BackdropType.SCARRED_WOMB, "16", "the scarred womb", NLMode.OLD)
Backdrop(BackdropType.BLUE_WOMB, "18", "blue womb", NLMode.NO_L)
Backdrop(BackdropType.SHEOL, "09", "sheol", NLMode.OLD)
Backdrop(BackdropType.CATHEDRAL, "10", "cathedral", NLMode.OLD)
Backdrop(BackdropType.DARKROOM, "12", "darkroom", NLMode.OLD)
Backdrop(BackdropType.CHEST, "11", "chest", NLMode.OLD)
Backdrop(BackdropType.SHOP, "0b", "shop", NLMode.NO_L)
Backdrop(BackdropType.ISAAC, "0c", "isaacsroom", NLMode.NONE)
Backdrop(BackdropType.BARREN, "0d", "barrenroom", NLMode.NONE)
Backdrop(BackdropType.SECRET, "0f", "secretroom", NLMode.NO_L)
Backdrop(BackdropType.DICE, "0e", "diceroom", NLMode.NO_L)
Backdrop(BackdropType.ARCADE, "0e", "arcade", NLMode.NONE)
Backdrop(BackdropType.ERROR_ROOM, nil, "effects_error", NLMode.NONE)
Backdrop(BackdropType.BLUE_WOMB_PASS, "17", "blue secret", NLMode.NONE)
Backdrop(BackdropType.GREED_SHOP, "0b", "ultragreedshop", NLMode.NONE)
Backdrop(BackdropType.SACRIFICE, "0g", "sacrificeroom", NLMode.NO_L)
Backdrop(BackdropType.DOWNPOUR, "01x", "downpour", NLMode.NEW)
Backdrop(BackdropType.MINES, "03x", "mines", NLMode.NEW)
Backdrop(BackdropType.MAUSOLEUM, "05x", "mausoleum", NLMode.NEW)
Backdrop(BackdropType.CORPSE, "07x", "corpse", NLMode.NEW)
Backdrop(BackdropType.PLANETARIUM, nil, "planetarium", NLMode.NONE)
Backdrop(BackdropType.DOWNPOUR_ENTRANCE, "0ax", "downpour entrance", NLMode.NONE)
Backdrop(BackdropType.MINES_ENTRANCE, "03x", "mines", NLMode.NEW) --???
Backdrop(BackdropType.MAUSOLEUM_ENTRANCE, "0cx", "mausoleum entrance", NLMode.NONE)
Backdrop(BackdropType.CORPSE_ENTRANCE, "0dx", "corpse entrance", NLMode.NONE)
Backdrop(BackdropType.MAUSOLEUM2, "05x", "mausoleum2", NLMode.NEW)
Backdrop(BackdropType.MAUSOLEUM3, "05x", "mausoleumb", NLMode.NEW)
Backdrop(BackdropType.MAUSOLEUM4, "05x", "mausoleum_boss", NLMode.NONE)
Backdrop(BackdropType.CORPSE2, "07x", "corpse2", NLMode.NEW)
Backdrop(BackdropType.CORPSE3, "07x", "corpse3", NLMode.NEW)
Backdrop(BackdropType.DROSS, "02x", "dross", NLMode.NEW)
Backdrop(BackdropType.ASHPIT, "04x", "ashpit", NLMode.NEW)
Backdrop(BackdropType.GEHENNA, "06x", "gehenna", NLMode.NEW)
Backdrop(BackdropType.MORTIS, "07x", "corpse2", NLMode.NEW) -- ???
Backdrop(BackdropType.ISAACS_BEDROOM, "0ex", "isaacs_bedroom", NLMode.NONE)
Backdrop(BackdropType.HALLWAY, "0fx", "hallway", NLMode.NO_L)
Backdrop(BackdropType.MOMS_BEDROOM, "0gx", "moms_bedroom", NLMode.NONE)
Backdrop(BackdropType.CLOSET, nil, "house_closet", NLMode.NONE)
Backdrop(BackdropType.CLOSET_B, nil, "house_closet_b", NLMode.NONE)
Backdrop(BackdropType.DOGMA, nil, "house_dogma", NLMode.NONE)
Backdrop(BackdropType.MINES_SHAFT, "03x", "mines_dark", NLMode.NONE)
Backdrop(BackdropType.ASHPIT_SHAFT, "04x", "ashpit_dark", NLMode.NONE)
Backdrop(BackdropType.DARK_CLOSET, nil, "special_closet", NLMode.NONE)

--Backdrop(BackdropType.DUNGEON, "XX", "xxxxxxxx", NLMode.NONE)
--Backdrop(BackdropType.DUNGEON_GIDEON, "XX", "xxxxxxxx", NLMode.NONE)
--Backdrop(BackdropType.DUNGEON_ROTGUT, "XX", "xxxxxxxx", NLMode.NONE)
--Backdrop(BackdropType.DUNGEON_BEAST, "XX", "xxxxxxxx", NLMode.NONE)

--[[local StageBackdrop = {}

function mod:GetStageBackdrop()
	local level = game:GetLevel()
	
	if level:GetStage() == LevelStage.STAGE7 then
		return lib.PickRandom(lib.PickRandom(StageBackdrop))
	end
	
	local tab = StageBackdrop[level:GetStage()]
	if not tab then return end
	return tab[level:GetStageType()] or tab[StageType.STAGETYPE_ORIGINAL]
end

local function AddStageBackdrops(levelStages, tab)
	for _, stage in pairs(levelStages) do
		if not StageBackdrop[stage] then
			StageBackdrop[stage] = {}
		end
		StageBackdrop[stage] = tab
	end
end

-- Basement
AddStageBackdrops({LevelStage.STAGE1_1, LevelStage.STAGE1_2}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.BASEMENT,
	[StageType.STAGETYPE_WOTL] = BackdropType.CELLAR,
	[StageType.STAGETYPE_AFTERBIRTH] = BackdropType.BURNT_BASEMENT,
	[StageType.STAGETYPE_REPENTANCE] = BackdropType.DOWNPOUR,
	[StageType.STAGETYPE_REPENTANCE_B] = BackdropType.DROSS,
})

-- Caves
AddStageBackdrops({LevelStage.STAGE2_1, LevelStage.STAGE2_2}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.CAVES,
	[StageType.STAGETYPE_WOTL] = BackdropType.CATACOMBS,
	[StageType.STAGETYPE_AFTERBIRTH] = BackdropType.FLOODED_CAVES,
	[StageType.STAGETYPE_REPENTANCE] = BackdropType.MINES,
	[StageType.STAGETYPE_REPENTANCE_B] = BackdropType.ASHPIT,
})

-- Depths
AddStageBackdrops({LevelStage.STAGE3_1, LevelStage.STAGE3_2}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.DEPTHS,
	[StageType.STAGETYPE_WOTL] = BackdropType.NECROPOLIS,
	[StageType.STAGETYPE_AFTERBIRTH] = BackdropType.DANK_DEPTHS,
	[StageType.STAGETYPE_REPENTANCE] = BackdropType.MAUSOLEUM,
	[StageType.STAGETYPE_REPENTANCE_B] = BackdropType.GEHENNA,
})

-- Womb
AddStageBackdrops({LevelStage.STAGE4_1, LevelStage.STAGE4_2}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.WOMB,
	[StageType.STAGETYPE_WOTL] = BackdropType.UTERO,
	[StageType.STAGETYPE_AFTERBIRTH] = BackdropType.SCARRED_WOMB,
	[StageType.STAGETYPE_REPENTANCE] = BackdropType.CORPSE,
	[StageType.STAGETYPE_REPENTANCE_B] = BackdropType.MORTIS,
})

-- Blue Womb
AddStageBackdrops({LevelStage.STAGE4_3}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.BLUE_WOMB,
})

-- Shoel / Cathy
AddStageBackdrops({LevelStage.STAGE5}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.SHEOL,
	[StageType.STAGETYPE_WOTL] = BackdropType.CATHEDRAL,
})

-- DarkRoom / Chest
AddStageBackdrops({LevelStage.STAGE6}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.DARKROOM,
	[StageType.STAGETYPE_WOTL] = BackdropType.CHEST,
})

-- Home
AddStageBackdrops({LevelStage.STAGE8}, {
	[StageType.STAGETYPE_ORIGINAL] = BackdropType.HALLWAY,
})]]