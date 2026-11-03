local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local FRAGMENT_BACKDROP = Isaac.GetEntityVariantByName("(Samael) Fragment Backdrop Layer")
local FRAGMENT_ENTRANCE_EFFECT = Isaac.GetEntityVariantByName("(Samael) Fragment Entrance")

local kNumFragmentCombatRooms = 4

local function AllowFragmentBackdrops()
	return game:GetRoom():GetBackdropType() ~= BackdropType.DARKROOM
end

local ShapeToName = {
	[RoomShape.ROOMSHAPE_IV] = "IV",
	[RoomShape.ROOMSHAPE_1x2] = "1x2",
	[RoomShape.ROOMSHAPE_2x2] = "2x2",
	[RoomShape.ROOMSHAPE_IH] = "IH",
	[RoomShape.ROOMSHAPE_LTR] = "LTR",
	[RoomShape.ROOMSHAPE_LTL] = "LTL",
	[RoomShape.ROOMSHAPE_2x1] = "2x1",
	[RoomShape.ROOMSHAPE_1x1] = "1x1",
	[RoomShape.ROOMSHAPE_LBL] = "LBL",
	[RoomShape.ROOMSHAPE_LBR] = "LBR",
	[RoomShape.ROOMSHAPE_IIH] = "IIH",
	[RoomShape.ROOMSHAPE_IIV] = "IIV"
}

local TOP_CORNER_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {},
	[RoomShape.ROOMSHAPE_1x2] = {0, 1},
	[RoomShape.ROOMSHAPE_2x2] = {0, 1},
	[RoomShape.ROOMSHAPE_IH] = {},
	[RoomShape.ROOMSHAPE_LTR] = {0, 4, 9},
	[RoomShape.ROOMSHAPE_LTL] = {1, 6, 10},
	[RoomShape.ROOMSHAPE_2x1] = {0, 1},
	[RoomShape.ROOMSHAPE_1x1] = {0, 1},
	[RoomShape.ROOMSHAPE_LBL] = {0, 1},
	[RoomShape.ROOMSHAPE_LBR] = {0, 1},
	[RoomShape.ROOMSHAPE_IIH] = {},
	[RoomShape.ROOMSHAPE_IIV] = {},
}

local BOTTOM_CORNER_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {},
	[RoomShape.ROOMSHAPE_1x2] = {2, 3},
	[RoomShape.ROOMSHAPE_2x2] = {2, 3},
	[RoomShape.ROOMSHAPE_IH] = {},
	[RoomShape.ROOMSHAPE_LTR] = {2, 3},
	[RoomShape.ROOMSHAPE_LTL] = {2, 3},
	[RoomShape.ROOMSHAPE_2x1] = {2, 3},
	[RoomShape.ROOMSHAPE_1x1] = {2, 3},
	[RoomShape.ROOMSHAPE_LBL] = {3, 11, 15},
	[RoomShape.ROOMSHAPE_LBR] = {2, 8, 13},
	[RoomShape.ROOMSHAPE_IIH] = {},
	[RoomShape.ROOMSHAPE_IIV] = {},
}

local ANY_CORNER_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_1x2] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_2x2] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_IH] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_LTR] = {0, 2, 3, 4, 9},
	[RoomShape.ROOMSHAPE_LTL] = {1, 2, 3, 6, 10},
	[RoomShape.ROOMSHAPE_2x1] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_1x1] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_LBL] = {0, 1, 3, 11, 15},
	[RoomShape.ROOMSHAPE_LBR] = {0, 1, 2, 8, 13},
	[RoomShape.ROOMSHAPE_IIH] = {0, 1, 2, 3},
	[RoomShape.ROOMSHAPE_IIV] = {0, 1, 2, 3},
}

local FULL_TOP_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {{0, 1}},
	[RoomShape.ROOMSHAPE_1x2] = {{0, 1}},
	[RoomShape.ROOMSHAPE_2x2] = {{0, 1, 4, 6}},
	[RoomShape.ROOMSHAPE_IH] = {{0, 1}},
	[RoomShape.ROOMSHAPE_LTR] = {{0, 4}},
	[RoomShape.ROOMSHAPE_LTL] = {{1, 6}},
	[RoomShape.ROOMSHAPE_2x1] = {{0, 1, 4, 6}},
	[RoomShape.ROOMSHAPE_1x1] = {{0, 1}},
	[RoomShape.ROOMSHAPE_LBL] = {{0, 1, 4, 6}},
	[RoomShape.ROOMSHAPE_LBR] = {{0, 1, 4, 6}},
	[RoomShape.ROOMSHAPE_IIH] = {{0, 1, 4, 6}},
	[RoomShape.ROOMSHAPE_IIV] = {{0, 1}},
}

local FULL_BOTTOM_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {{2, 3}},
	[RoomShape.ROOMSHAPE_1x2] = {{2, 3}},
	[RoomShape.ROOMSHAPE_2x2] = {{2, 3, 13, 15}},
	[RoomShape.ROOMSHAPE_IH] = {{2, 3}},
	[RoomShape.ROOMSHAPE_LTR] = {{2, 3, 13, 15}},
	[RoomShape.ROOMSHAPE_LTL] = {{2, 3, 13, 15}},
	[RoomShape.ROOMSHAPE_2x1] = {{2, 3, 5, 7}},
	[RoomShape.ROOMSHAPE_1x1] = {{2, 3}},
	[RoomShape.ROOMSHAPE_LBL] = {{3, 15}},
	[RoomShape.ROOMSHAPE_LBR] = {{2, 13}},
	[RoomShape.ROOMSHAPE_IIH] = {{2, 3, 5, 7}},
	[RoomShape.ROOMSHAPE_IIV] = {{2, 3}},
}

local FULL_LEFT_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {{0, 2}},
	[RoomShape.ROOMSHAPE_1x2] = {{0, 2, 4, 6}},
	[RoomShape.ROOMSHAPE_2x2] = {{0, 2, 10, 11}},
	[RoomShape.ROOMSHAPE_IH] = {{0, 2}},
	[RoomShape.ROOMSHAPE_LTR] = {{0, 2, 10, 11}},
	[RoomShape.ROOMSHAPE_LTL] = {{2, 10}},
	[RoomShape.ROOMSHAPE_2x1] = {{0, 2}},
	[RoomShape.ROOMSHAPE_1x1] = {{0, 2}},
	[RoomShape.ROOMSHAPE_LBL] = {{0, 11}},
	[RoomShape.ROOMSHAPE_LBR] = {{0, 2, 10, 11}},
	[RoomShape.ROOMSHAPE_IIH] = {{0, 2}},
	[RoomShape.ROOMSHAPE_IIV] = {{0, 2, 4, 6}},
}

local FULL_RIGHT_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_IV] = {{1, 3}},
	[RoomShape.ROOMSHAPE_1x2] = {{1, 3, 5, 7}},
	[RoomShape.ROOMSHAPE_2x2] = {{1, 3, 8, 9}},
	[RoomShape.ROOMSHAPE_IH] = {{1, 3}},
	[RoomShape.ROOMSHAPE_LTR] = {{3, 9}},
	[RoomShape.ROOMSHAPE_LTL] = {{1, 3, 8, 9}},
	[RoomShape.ROOMSHAPE_2x1] = {{1, 3}},
	[RoomShape.ROOMSHAPE_1x1] = {{1, 3}},
	[RoomShape.ROOMSHAPE_LBL] = {{1, 3, 8, 9}},
	[RoomShape.ROOMSHAPE_LBR] = {{1, 8}},
	[RoomShape.ROOMSHAPE_IIH] = {{1, 3}},
	[RoomShape.ROOMSHAPE_IIV] = {{1, 3, 5, 7}},
}

local W_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_1x2] = {4, 6},
	[RoomShape.ROOMSHAPE_2x2] = {10, 11},
	[RoomShape.ROOMSHAPE_LTR] = {10, 11},
	[RoomShape.ROOMSHAPE_LBR] = {10, 11},
	[RoomShape.ROOMSHAPE_IIV] = {4, 6},
}

local E_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_1x2] = {5, 7},
	[RoomShape.ROOMSHAPE_2x2] = {8, 9},
	[RoomShape.ROOMSHAPE_LTL] = {8, 9},
	[RoomShape.ROOMSHAPE_LBL] = {8, 9},
	[RoomShape.ROOMSHAPE_IIV] = {5, 7},
}

local N_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_2x2] = {4, 6},
	[RoomShape.ROOMSHAPE_2x1] = {4, 6},
	[RoomShape.ROOMSHAPE_LBL] = {4, 6},
	[RoomShape.ROOMSHAPE_LBR] = {4, 6},
	[RoomShape.ROOMSHAPE_IIH] = {4, 6},
}

local S_WALL_LAYERS = {
	[RoomShape.ROOMSHAPE_2x2] = {13, 15},
	[RoomShape.ROOMSHAPE_LTR] = {13, 15},
	[RoomShape.ROOMSHAPE_LTL] = {13, 15},
	[RoomShape.ROOMSHAPE_2x1] = {5, 7},
	[RoomShape.ROOMSHAPE_IIH] = {5, 7},
}

local ALT_CORNERS_SIDE = {
	[0] = {"side_full_top1"},
	[1] = {"side_full_top1"},
	[2] = {"side_full_bottom1"},
	[3] = {"side_full_bottom1"},
}

local ALT_CORNERS_TOP = {
	[0] = {"full_top1"},
	[1] = {"full_top1"},
	[2] = {"full_bottom1"},
	[3] = {"full_bottom1"},
}

local ALT_LIMIT = {
	[RoomShape.ROOMSHAPE_IV] = 1,
	[RoomShape.ROOMSHAPE_IH] = 1,
	[RoomShape.ROOMSHAPE_IIH] = 1,
	[RoomShape.ROOMSHAPE_IIV] = 1,
}

local FRAGMENT_OVERLAYS = {
	{
		GFX = {"side1"},
		ALT_CORNERS = ALT_CORNERS_SIDE,
		LAYERS = FULL_LEFT_WALL_LAYERS,
		NO_ENTRANCE = true,
		NO_EXIT = true,
	},
	{
		GFX = {"top1"},
		ALT_CORNERS = ALT_CORNERS_TOP,
		LAYERS = FULL_TOP_WALL_LAYERS,
		NO_EXIT = true,
	},
	{
		GFX = {"side1"},
		ALT_CORNERS = ALT_CORNERS_SIDE,
		LAYERS = FULL_RIGHT_WALL_LAYERS,
		NO_ENTRANCE = true,
		NO_EXIT = true,
	},
	{
		GFX = {"bottom1"},
		ALT_CORNERS = ALT_CORNERS_TOP,
		LAYERS = FULL_BOTTOM_WALL_LAYERS,
		NO_ENTRANCE = true,
	},
	
	{
		GFX = {"cracks1"},
		LAYERS = W_WALL_LAYERS,
		IS_CRACKS = true,
	},
	{
		GFX = {"cracks1"},
		LAYERS = N_WALL_LAYERS,
		IS_CRACKS = true,
	},
	{
		GFX = {"cracks1"},
		LAYERS = E_WALL_LAYERS,
		IS_CRACKS = true,
	},
	{
		GFX = {"cracks1"},
		LAYERS = S_WALL_LAYERS,
		IS_CRACKS = true,
	},
	
	{
		GFX = {"top_corner1"},
		LAYERS = TOP_CORNER_LAYERS,
		NO_EXIT = true,
		IS_CORNER = true,
	},
	{
		GFX = {"bottom_corner1"},
		LAYERS = BOTTOM_CORNER_LAYERS,
		NO_ENTRANCE = true,
		IS_CORNER = true,
	},
	{
		GFX = {"cracks1"},
		LAYERS = ANY_CORNER_LAYERS,
		IS_CORNER = true,
		IS_CRACKS = true,
	},
}
for k, tab in pairs(FRAGMENT_OVERLAYS) do
	tab.ID = k
end

local fragmentBackdrop
local manualFragmentBackdropLayers

function mod:ManuallyApplyFragmentBackdrop(layers, offset)
	manualFragmentBackdropLayers = layers
	local fragmentBackdrop = Isaac.Spawn(EntityType.ENTITY_EFFECT, FRAGMENT_BACKDROP, 0, lib.ZeroVector, lib.ZeroVector, nil)
	fragmentBackdrop:GetData().samaelFragmentBackdropOverrideOffset = offset
	manualFragmentBackdropLayers = nil
	return fragmentBackdrop
end

function mod:MaybeApplyFragmentBackdrop(force)
	if AllowFragmentBackdrops() and (mod:IsFragmentRoom() or force) and (not fragmentBackdrop or not fragmentBackdrop:Exists()) then
		fragmentBackdrop = Isaac.Spawn(EntityType.ENTITY_EFFECT, FRAGMENT_BACKDROP, 0, lib.ZeroVector, lib.ZeroVector, nil)
	end
end

if StageAPI and StageAPI.Loaded then
	StageAPI.AddCallback("Samael", "POST_CHANGE_ROOM_GFX", 999, function()
		if mod:IsFragmentRoom() or (fragmentBackdrop and fragmentBackdrop:Exists()) then
			mod:MaybeApplyFragmentBackdrop()
		end
	end)
end

local function GetFragmentLayerFilepath(name)
	return "gfx/samael_fragment/" .. name .. ".png"
end

function mod:FragmentBackdropInit(eff)
	if eff.SubType == 1 then return end
	
	local room = game:GetRoom()
	local roomShape = room:GetRoomShape()
	
	local sprite = eff:GetSprite()
	
	local rngShift = 35
	
	if mod:IsFragmentCombatRoom() then
		rngShift = rngShift + (mod:GetFragmentData().roomsCleared or 0)
	end
	
	local isFlooded = false
	local hasMovingWater = false
	local hasPriority = {}
	local blacklisted = {}
	local noBreakCorners = {}
	
	for gridIndex, tab in pairs(lib.GetRoomSpawns()) do
		for _, spawn in pairs(tab) do
			if spawn.Type == mod.ENTITIES.FORCE_BROKEN_WALL.ID and spawn.Variant == mod.ENTITIES.FORCE_BROKEN_WALL.Var then
				hasPriority[spawn.SubType] = true
			elseif spawn.Type == mod.ENTITIES.FORCE_INTACT_WALL.ID and spawn.Variant == mod.ENTITIES.FORCE_INTACT_WALL.Var then
				blacklisted[spawn.SubType] = true
				local overlay = FRAGMENT_OVERLAYS[spawn.SubType] or {}
				local layerSet = overlay.LAYERS or {}
				for _, layer in pairs(layerSet[roomShape] or {}) do
					for _, i in pairs((type(layer) == "table") and layer or {layer}) do
						noBreakCorners[i] = true
					end
				end
			elseif spawn.Type == 970 and spawn.Variant == 1 and spawn.SubType < 4 then
				hasMovingWater = true
			elseif spawn.Type == 970 and spawn.Variant == 1 and spawn.SubType == 11 then
				isFlooded = true
			end
		end
	end
	
	local rng = RNG()
	rng:SetSeed(room:GetDecorationSeed(), rngShift)
	
	local overlays = {}
	local nonPriorityOverlays = {}
	for k, tab in pairs(FRAGMENT_OVERLAYS) do
		if manualFragmentBackdropLayers then
			if manualFragmentBackdropLayers[k] then
				table.insert(overlays, tab)
			end
		elseif not blacklisted[k] then
			if hasPriority[k] then
				table.insert(overlays, tab)
			elseif mod:IsFragmentRoom() and (not isFlooded or tab.IS_CRACKS) then
				table.insert(nonPriorityOverlays, tab)
			end
		end
	end
	lib.Shuffle(nonPriorityOverlays, rng)
	for _, tab in pairs(nonPriorityOverlays) do
		table.insert(overlays, tab)
	end
	
	local usedLayers = {}
	local numAltsUsed = 0
	
	for _, tab in ipairs(overlays) do
		if not (mod:IsFragmentEntrance() and tab.NO_ENTRANCE) then
			for _, gfx in pairs(tab.GFX) do
				gfx = GetFragmentLayerFilepath(gfx)
				local options = tab.LAYERS[roomShape]
				local layer = lib.PickRandom(options, rng)
				if manualFragmentBackdropLayers and type(manualFragmentBackdropLayers[tab.ID]) ~= "boolean" then
					layer = manualFragmentBackdropLayers[tab.ID]
				end
				
				if type(layer) == "table" then
					local conflict = false
					for _, i in pairs(layer) do
						if usedLayers[i] or (tab.IS_CORNER and noBreakCorners[i]) then
							conflict = true
						end
					end
					if not conflict then
						for _, i in pairs(layer) do
							local spriteSheet = gfx
							local useAltSprites = not hasMovingWater and not noBreakCorners[i] and tab.ALT_CORNERS and tab.ALT_CORNERS[i] and not (ALT_LIMIT[roomShape] and numAltsUsed >= ALT_LIMIT[roomShape]) and rng:RandomInt(2) == 0
							if manualFragmentBackdropLayers then
								useAltSprites = manualFragmentBackdropLayers.ALT and manualFragmentBackdropLayers.ALT[i] or false
							end
							if useAltSprites then
								spriteSheet = GetFragmentLayerFilepath(lib.PickRandom(tab.ALT_CORNERS[i], rng))
								numAltsUsed = numAltsUsed + 1
							end
							sprite:ReplaceSpritesheet(i, spriteSheet)
							usedLayers[i] = true
						end
					end
				elseif layer and not usedLayers[layer] and not (tab.IS_CORNER and noBreakCorners[layer]) then
					sprite:ReplaceSpritesheet(layer, gfx)
					usedLayers[layer] = true
				end
			end
		end
	end
	
	sprite:LoadGraphics()
	
	-- Clear wall decorations because they don't look good with the broken walls.
	for _, eff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.BACKDROP_DECORATION)) do
		eff:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.FragmentBackdropInit, FRAGMENT_BACKDROP)

local BackdropOffset = {
	DEFAULT = Vector(260, 0),
	[RoomShape.ROOMSHAPE_IV] = Vector(115, 0),
	[RoomShape.ROOMSHAPE_IIV] = Vector(115, 0),
}

function mod:FragmentBackdrop(eff)
	local room = game:GetRoom()
	local roomShape = room:GetRoomShape()
	
	if not AllowFragmentBackdrops() then
		eff:Remove()
		return
	end
	
	local offset = BackdropOffset[roomShape] or BackdropOffset.DEFAULT
	if eff:GetData().samaelFragmentBackdropOverrideOffset then
		offset = offset + eff:GetData().samaelFragmentBackdropOverrideOffset
	end
	local pos = room:GetTopLeftPos() + offset
	eff.Position = lib.ZeroVector
	eff.SpriteOffset = (pos / 40) * 26
	
	if not eff:GetData().NoRenderFlags then
		eff:AddEntityFlags(EntityFlag.FLAG_RENDER_WALL)
		eff:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR)
	end
	
	local anim = "" .. ShapeToName[room:GetRoomShape()] .. "_room"
	eff:GetSprite():SetFrame(anim, 0)
	
	if eff.SubType == 1 and math.floor(game:GetFrameCount() / 30) % 2 == 0 then
		eff.Color = lib.InvisibleColor
	else
		eff.Color = lib.NullColor
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.FragmentBackdrop, FRAGMENT_BACKDROP)

--------------------------------------------------
-- Fragment Utilities
--------------------------------------------------

function mod:GetFragmentData()
	return mod:GetFloorData("Fragment")
end

function mod:ClearFragmentData()
	mod:GetAllFloorData().Fragment = {}
end

local function GetFragmentCombatRoomIndex()
	local data = mod:GetFragmentData()
	local createdRoom = false
	
	if not data.FragmentRoomIndex then
		for idx = 0, 168 do
			if mod:IsFragmentCombatRoom(idx) then
				lib.Log("Found existing Fragment combat room @ " .. idx)
				data.FragmentRoomIndex = idx
				break
			end
		end
		if not data.FragmentRoomIndex then
			createdRoom = true
			local idx = mod:ReserveRoom()
			if idx then
				data.FragmentRoomIndex = idx
				lib.Log("Claimed GridIndex " .. idx .. " for the Fragment combat rooms.")
			else
				data.FragmentRoomIndex = -1
			end
		end
	end
	
	return data.FragmentRoomIndex, createdRoom
end

local function GetFragmentEntranceIndex()
	local data = mod:GetFragmentData()
	local createdRoom = false
	
	if not data.FragmentEntranceIndex then
		for idx = 0, 168 do
			if mod:IsFragmentEntrance(idx) then
				lib.Log("Found existing Fragment Entrance @ " .. idx)
				data.FragmentEntranceIndex = idx
				break
			end
		end
		if not data.FragmentEntranceIndex then
			createdRoom = true
			local idx = mod:ReserveRoom()
			if idx then
				data.FragmentEntranceIndex = idx
				lib.Log("Claimed GridIndex " .. idx .. " for the Fragment entrance.")
			else
				data.FragmentEntranceIndex = -1
			end
		end
	end
	
	return data.FragmentEntranceIndex, createdRoom
end

function mod:IsFragmentRoom(idx)
	if mod.BasementRenovatorFragmentTesting then
		return true
	end
	
	local data = mod:GetFragmentData()
	local roomDesc
	if idx then
		if idx == data.FragmentRoomIndex or idx == data.FragmentEntranceIndex then
			return true
		end
		roomDesc = game:GetLevel():GetRoomByIdx(idx)
	else
		roomDesc = game:GetLevel():GetCurrentRoomDesc()
	end
	return (roomDesc.GridIndex and (roomDesc.GridIndex == data.FragmentRoomIndex or roomDesc.GridIndex == data.FragmentEntranceIndex))
			or (roomDesc.Data and roomDesc.Data.Name:find("(Samael/Fragment)"))
end

function mod:IsFragmentEntrance(idx)
	local data = mod:GetFragmentData()
	local roomDesc
	if idx then
		if idx == data.FragmentEntranceIndex then
			return true
		end
		roomDesc = game:GetLevel():GetRoomByIdx(idx)
	else
		roomDesc = game:GetLevel():GetCurrentRoomDesc()
	end
	if roomDesc.GridIndex and roomDesc.GridIndex == data.FragmentEntranceIndex then
		return true
	end
	local roomData = roomDesc.Data
	return roomData and roomData.Name:find("(Samael/Fragment)") and roomData.Name:find("Entrance")
end

function mod:IsFragmentCombatRoom(idx)
	return mod:IsFragmentRoom(idx) and not mod:IsFragmentEntrance(idx)
end

local lastGoto

local function GoTo(command)
	lastGoto = command
	lib.Log("Calling: goto " .. command)
	return Isaac.ExecuteCommand("goto " .. command)
end

local function GetFragmentSeed()
	local seed = mod:GetAllFloorData().FragmentSeed or game:GetLevel():GetDungeonPlacementSeed()
	local rng = RNG()
	rng:SetSeed(seed, 1)
	local newSeed = rng:Next()
	mod:GetAllFloorData().FragmentSeed = newSeed
	return newSeed
end

local function GetFragmentReturnIndex()
	local data = mod:GetFragmentData()
	local idx = data.returnIdx or mod:GetLastKnownGridIndex()
	if mod:IsFragmentRoom(idx) or mod:IsDeathDealRoom(idx) then
		idx = game:GetLevel():GetStartingRoomIndex()
	end
	return idx
end

--------------------------------------------------
-- Room Loading
--------------------------------------------------

-- Stolen from StageAPI
local UnsupportedTypes = {
	[0] = true, -- null entity
	[970] = true, -- room darkness, water flow, water disabler, lava disabler, quest door
	[969] = true, -- events
	[1009] = true, -- event rock
	[3009] = true, -- event pit
	[3002] = true, -- button rail
	[6000] = true, -- rail
	[6001] = true, -- rail over pit
}

local SingleSpawn = {
	[EntityType.ENTITY_PICKUP] = true,
	[EntityType.ENTITY_SLOT] = true,
	[EntityType.ENTITY_FIREPLACE] = true,
	[EntityType.ENTITY_EFFECT] = true,
	[EntityType.ENTITY_SHOPKEEPER] = true,
	[EntityType.ENTITY_STONEHEAD] = true,
	[EntityType.ENTITY_CONSTANT_STONE_SHOOTER] = true,
	[EntityType.ENTITY_STONE_EYE] = true,
	[EntityType.ENTITY_BRIMSTONE_HEAD] = true,
	[EntityType.ENTITY_GAPING_MAW] = true,
	[EntityType.ENTITY_BROKEN_GAPING_MAW] = true,
	[EntityType.ENTITY_QUAKE_GRIMACE] = true,
	[EntityType.ENTITY_BOMB_GRIMACE] = true,
}

local spawnedDecos = {}

local function ClearDummyDecos()
	for gridIndex, _ in pairs(spawnedDecos) do
		local gridEntity = game:GetRoom():GetGridEntity(gridIndex)
		if gridEntity and gridEntity:GetType() == 1 then
			game:GetRoom():RemoveGridEntity(gridIndex, 0, false)
		end
		spawnedDecos[gridIndex] = nil
	end
end

local SAFE_FF_ENEMIES = FiendFolio and {
	[0] = FiendFolio.FF.ScytheRider,
	[1] = FiendFolio.FF.Reaper,
	[2] = FiendFolio.FF.Shi,
	[3] = FiendFolio.FF.Gravedigger,
}

mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, function(_, npc)
	if npc.Variant >= 1000 then
		npc:Remove()
	elseif npc.Variant == mod.ENTITIES.SAFE_FF_ENEMY.Var then
		local tab = SAFE_FF_ENEMIES and SAFE_FF_ENEMIES[npc.SubType]
		if tab then
			local ffEnemy = Isaac.Spawn(tab.ID, tab.Var or 0, tab.Sub or 0, npc.Position, lib.ZeroVector, nil)
			if npc:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) or (npc.SpawnerEntity and npc.SpawnerEntity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)) then
				ffEnemy:AddCharmed(EntityRef(Isaac.GetPlayer()), -1)
			end
			ffEnemy:GetData().samaelPermaCharmMarked = npc:GetData().samaelPermaCharmMarked or (npc.SpawnerEntity and npc.SpawnerEntity:GetData().samaelPermaCharmMarked)
		end
		npc:Remove()
	end
end, SamaelMod.SHARED_ENTITY_ID)

local function ShouldPermaCharmAndMark(gridIndex)
	local room = game:GetRoom()
	local roomSpawns = lib.GetRoomSpawns()
	
	local indexesToCheck = {gridIndex}
	
	if gridIndex % room:GetGridWidth() > 0 then
		table.insert(indexesToCheck, gridIndex - 1)
	end
	if gridIndex % room:GetGridWidth() < room:GetGridWidth()-1 then
		table.insert(indexesToCheck, gridIndex + 1)
	end
	if math.floor(gridIndex / room:GetGridWidth()) < room:GetGridHeight()-1 then
		table.insert(indexesToCheck, gridIndex + room:GetGridWidth())
	end
	if math.floor(gridIndex / room:GetGridWidth()) > 0 then
		table.insert(indexesToCheck, gridIndex - room:GetGridWidth())
	end
	
	for _, idx in pairs(indexesToCheck) do
		for _, spawn in pairs(roomSpawns[idx] or {}) do
			if spawn.Type == mod.ENTITIES.CHARM_MARK.ID and spawn.Variant == mod.ENTITIES.CHARM_MARK.Var then
				return mod.ENTITIES.CHARM_MARK.SubType
			end
		end
	end
end

function mod:PreSpawnEntity(eType, eVariant, eSubType, gridIndex)
	if eType == EntityType.ENTITY_FIRE_WORM and eVariant == 1000 then
		return {999, SamaelMod.ENTITIES.DUMMY.Var, 0}
	end
	
	local enemyPortal = mod:CheckEnemyPortalTile(gridIndex)
	if enemyPortal then
		return {mod.ENTITIES.ENEMY_PORTAL.ID, mod.ENTITIES.ENEMY_PORTAL.Var, enemyPortal}
	elseif eType == mod.ENTITIES.CHARM_MARK.ID and eVariant == mod.ENTITIES.CHARM_MARK.Var then
		local candidates = {}
		for _, spawn in pairs(lib.GetRoomSpawns()[gridIndex] or {}) do
			if spawn.Weight > 0 and (spawn.Type ~= mod.ENTITIES.CHARM_MARK.ID or spawn.Variant ~= mod.ENTITIES.CHARM_MARK.Var) then
				table.insert(candidates, {ID={spawn.Type, spawn.Variant, spawn.SubType}, Weight=spawn.Weight})
			end
		end
		if #candidates > 0 then
			local rng = RNG()
			rng:SetSeed(game:GetRoom():GetSpawnSeed(), 35)
			return lib.PickRandom(candidates).ID
		end
	end
end

function mod:FragmentPreRoomEntitySpawn(eType, eVariant, eSubType, gridIndex, seed)
	local override = mod:PreSpawnEntity(eType, eVariant, eSubType, gridIndex)
	
	if not mod:IsFragmentRoom() then
		return override
	elseif not UnsupportedTypes[eType] and eType < 1000 then
		local data = mod:GetFragmentData()
		if not data.SingleSpawns then
			data.SingleSpawns = {}
		end
		
		if override then
			eType = override[1]
			eVariant = override[2]
			eSubType = override[3]
		end
		
		if eType == 999 then
			eType = 1000
		end
		
		local isPitFiller = (eType == EntityType.ENTITY_FIRE_WORM and eVariant == 1000)
		local isFragmentEntity = (eType == mod.SHARED_ENTITY_ID) and (eVariant == mod.ENTITIES.LOST_SOUL or eVariant == mod.ENTITIES.FRAGMENT_PORTAL)
		local isMetaEntity = (eType == mod.SHARED_ENTITY_ID) and eVariant >= 1000
		
		if not isPitFiller and not isFragmentEntity and not isMetaEntity and not game:GetRoom():IsClear() and not data.SingleSpawns[gridIndex] then
			local shouldBeFriendly = ShouldPermaCharmAndMark(gridIndex)
			local entity
			if shouldBeFriendly then
				for _, ent in pairs(Isaac.FindByType(eType, eVariant, eSubType)) do
					if ent:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
						entity = ent
						entity.Position = game:GetRoom():GetGridPosition(gridIndex) 
						break
					end
				end
			end
			if not entity then
				entity = game:Spawn(eType, eVariant, game:GetRoom():GetGridPosition(gridIndex), lib.ZeroVector, nil, eSubType, GetFragmentSeed())
			end
			if SingleSpawn[eType] then
				data.SingleSpawns[gridIndex] = true
			elseif entity:IsActiveEnemy() then
				data.HasEnemies = true
			end
			if shouldBeFriendly then
				entity:GetData().samaelPermaCharmMarked = true
			end
		end
		spawnedDecos[gridIndex] = true
		return {0, 20, 0}
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_PRE_ROOM_ENTITY_SPAWN, CallbackPriority.EARLY, mod.FragmentPreRoomEntitySpawn)

if StageAPI and StageAPI.Loaded then
	StageAPI.AddCallback("Samael", "PRE_SPAWN_ENTITY", -2, function(entityInfo, list, gridIndex)
		return mod:PreSpawnEntity(entityInfo.Data.Type, entityInfo.Data.Variant, entityInfo.Data.SubType, gridIndex)
	end)
end

mod:AddPriorityCallback(ModCallbacks.MC_POST_NPC_INIT, -1, function(_, npc)
	if game:GetRoom():GetFrameCount() <= 1 and mod.BasementRenovatorFragmentTesting and ShouldPermaCharmAndMark(game:GetRoom():GetGridIndex(npc.Position)) then
		npc:GetData().samaelPermaCharmMarked = true
	end
end)

mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
	if npc:GetData().samaelPermaCharmMarked then
		npc:AddCharmed(EntityRef(Isaac.GetPlayer()), -1)
		npc:AddEntityFlags(EntityFlag.FLAG_BAITED)
	end
end)

mod:AddCallback(ModCallbacks.MC_PRE_USE_ITEM, function()
	local data = mod:GetFragmentData()
	if mod:IsFragmentRoom() and data.HasEnemies then
		game:GetRoom():SetClear(false)
	end
end, CollectibleType.COLLECTIBLE_D7)

mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, function()
	local data = mod:GetFragmentData()
	if mod:IsFragmentRoom() and data.HasEnemies then
		game:GetRoom():SetClear(false)
	end
end, CollectibleType.COLLECTIBLE_D7)

function mod:FragmentReward(pos)
	pos = pos or game:GetRoom():GetCenterPos() - Vector(0, 80)
	sfxManager:Play(SoundEffect.SOUND_SLOTSPAWN)
	for i=1, 3 do
		local pickup = Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, 3, pos, RandomVector() * 2, nil):ToPickup()
	end
	mod:GetFragmentData().LastSpawnedReward = game:GetFrameCount()
end

function mod:FragmentReplaceTrollBombs(bomb)
	if mod:IsFragmentEntrance() then
		local lastReward = mod:GetFragmentData().LastSpawnedReward
		if lastReward and game:GetFrameCount() - lastReward < 2 then
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_BOMB, 1, bomb.Position, RandomVector() * 2, nil)
			bomb:Remove()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.FragmentReplaceTrollBombs, BombVariant.BOMB_TROLL)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.FragmentReplaceTrollBombs, BombVariant.BOMB_SUPERTROLL)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.FragmentReplaceTrollBombs, BombVariant.BOMB_GOLDENTROLL)

-- BR Detection

local checkedBrTestingRoom = false

local function CheckIfTestingFragmentRoomInBasementRenevator()
	checkedBrTestingRoom = false
	if BasementRenovator and BasementRenovator.TestRoomData and game:GetRoom():GetType() == RoomType.ROOM_DEFAULT then
		for gridIndex, tab in pairs(lib.GetRoomSpawns()) do
			for _, spawn in pairs(tab) do
				if spawn.Type == mod.ENTITIES.LOST_SOUL.ID and spawn.Variant == mod.ENTITIES.LOST_SOUL.Var then
					mod.BasementRenovatorFragmentTesting = true
					return
				end
			end
		end
	end
	mod.BasementRenovatorFragmentTesting = false
end

mod:AddCallback(ModCallbacks.MC_PRE_ROOM_ENTITY_SPAWN, function()
	if BasementRenovator and not checkedBrTestingRoom and BasementRenovator.InTestRoom and BasementRenovator:InTestRoom() then
		CheckIfTestingFragmentRoomInBasementRenevator()
		checkedBrTestingRoom = true
	end
end)

-- NEW ROOM

local replacingRoom

function mod:FragmentEarlyNewRoom()
	local data = mod:GetFragmentData()
	local room = game:GetRoom()
	local level = game:GetLevel()
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	
	if roomDesc.GridIndex == data.FragmentRoomIndex and mod:IsFragmentCombatRoom() and data.generateNewRoom ~= false then
		lib.Log("Fragment room loading process started.")
		data.generateNewRoom = false
		mod:FadeIn()
		
		-- Clear the room completely.
		if StageAPI and StageAPI.Loaded then
			lib.Log("Clearing StageAPI grids...")
			for _, customGrid in pairs(StageAPI.GetCustomGrids()) do
				customGrid:Remove(false)
			end
			local grids = StageAPI.GetRoomCustomGrids()
			for k, v in pairs(grids) do
				grids[k] = nil
			end
		end
		lib.Log("Clearing GridEntities...")
		for i=0, room:GetGridSize() do
			local gridEntity = game:GetRoom():GetGridEntity(i)
			if gridEntity then
				room:RemoveGridEntity(i, 0, false)
				gridEntity:Update()
			end
			room:SetGridPath(i, 0)
		end
		lib.Log("Clearing entities...")
		for _, entity in pairs(Isaac.GetRoomEntities()) do
			if not entity:ToPlayer() and not entity:ToFamiliar()
					and not entity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)
					and not entity:HasEntityFlags(EntityFlag.FLAG_PERSISTENT) then
				entity:Remove()
			end
		end
		lib.Log("Running update...")
		room:Update()
		
		lib.Log("Loading next fragment room...")
		mod:NextFragmentRoom()
		lib.Log("Copying roomdata...")
		lib.CopyGotoRoomData(GetFragmentCombatRoomIndex(), RoomDescriptor.FLAG_PORTAL_LINKED)
		
		lib.Log("Moving back to entrance...")
		local entranceIdx = GetFragmentEntranceIndex()
		game:ChangeRoom(entranceIdx)
		replacingRoom = GetFragmentCombatRoomIndex()
		
		data.CurrentRoomSeed = GetFragmentSeed()
		data.RoomSeeds = data.RoomSeeds or {}
		table.insert(data.RoomSeeds, data.CurrentRoomSeed)
		
		lib.Log("Done with Fragment loading part 1/2.")
		return true
	end
	
	if replacingRoom then
		lib.Log("Fragment loading part 2...")
		local mutableRoomDesc = level:GetRoomByIdx(replacingRoom)
		mutableRoomDesc.Clear = false
		mutableRoomDesc.ClearCount = 0
		mutableRoomDesc.VisitedCount = 0
		data.SingleSpawns = {}
		data.HasEnemies = false
		lib.Log("Starting transition to Fragment combat room...")
		game:StartRoomTransition(replacingRoom, -1, RoomTransitionAnim.FADE, nil, 0)
		replacingRoom = nil
		lib.Log("Done with Fragment loading part 2/2.")
		return true
	end
	
	if mod:IsFragmentEntrance() and roomDesc.Data and roomDesc.Data.Variant < 61800 then
		-- Sent to fragment entrance without proper initialization. Fix it.
		-- This can happen with BR testing.
		lib.Log("Player has entered uninitialized Fragment. Probably BasementRenevator testing.")
		lib.Log("Initializing the Fragment properly...")
		local clearedCombatRoom = data.clearedCombatRoom
		local savedSoul = data.savedSoul
		mod:GoToFragment()
		mod:GetFragmentData().clearedCombatRoom = clearedCombatRoom
		mod:GetFragmentData().savedSoul = savedSoul
		lib.Log("Finished Fragment initialization fix.")
		return true
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_NEW_ROOM, CallbackPriority.EARLY, mod.FragmentEarlyNewRoom)

function mod:FragmentNewRoom()
	CheckIfTestingFragmentRoomInBasementRenevator()
	
	local data = mod:GetFragmentData()
	local room = game:GetRoom()
	local level = game:GetLevel()
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	
	if room:GetType() ~= RoomType.ROOM_ERROR then
		for _, soul in pairs(Isaac.FindByType(mod.ENTITIES.LOST_SOUL.ID, mod.ENTITIES.LOST_SOUL.Var)) do
			soul:Remove()
		end
	end
	
	local roomHasFragmentWalls = false
	
	for gridIndex, tab in pairs(lib.GetRoomSpawns()) do
		for _, spawn in pairs(tab) do
			if spawn.Type == mod.ENTITIES.FORCE_BROKEN_WALL.ID and spawn.Variant == mod.ENTITIES.FORCE_BROKEN_WALL.Var then
				roomHasFragmentWalls = true
			end
		end
	end
	
	mod:MaybeApplyFragmentBackdrop(roomHasFragmentWalls)
	
	if not mod:IsFragmentRoom() then return end
	
	lib.Log("Fragment NewRoom started...")
	
	data.CurrentRoomSeed = data.CurrentRoomSeed or GetFragmentSeed()
	local rng = RNG()
	rng:SetSeed(data.CurrentRoomSeed, 35)
	
	-- Check room layout for Stuff
	local entryPosOptions = {}
	local backdropOptions = {}
	local lostSoulPositions = {}
	local openPortalForSoul = false
	for gridIndex, tab in pairs(lib.GetRoomSpawns()) do
		for _, spawn in pairs(tab) do
			if not backdrop and spawn.Type == mod.ENTITIES.BACKDROP_REPLACER.ID and spawn.Variant == mod.ENTITIES.BACKDROP_REPLACER.Var then
				table.insert(backdropOptions, spawn.SubType)
			end
			if not entryPos and spawn.Type == mod.ENTITIES.PLAYER_POS.ID and spawn.Variant == mod.ENTITIES.PLAYER_POS.Var then
				table.insert(entryPosOptions, gridIndex)
			end
			if not data.soulDied and not lostSoul and spawn.Type == mod.ENTITIES.LOST_SOUL.ID and spawn.Variant == mod.ENTITIES.LOST_SOUL.Var then
				table.insert(lostSoulPositions, {
					SubType = spawn.SubType,
					GridIndex = gridIndex,
				})
			end
			if not openPortalForSoul and spawn.Type == mod.ENTITIES.FRAGMENT_PORTAL.ID and spawn.Variant == mod.ENTITIES.FRAGMENT_PORTAL.Var then
				openPortalForSoul = spawn.SubType == 1
			end
		end
	end
	
	lib.Log("Finished checking room layout.")
	
	local entryPos
	
	if #entryPosOptions > 0 then
		entryPos = game:GetRoom():GetGridPosition(lib.PickRandom(entryPosOptions, rng))
	end
	
	local lostSoul
	
	if #lostSoulPositions > 0 then
		lib.Log("Spawning a Lost Soul...")
		local soulInfo = lib.PickRandom(lostSoulPositions, rng)
		local pos = game:GetRoom():GetGridPosition(soulInfo.GridIndex)
		lostSoul = Isaac.Spawn(mod.ENTITIES.LOST_SOUL.ID, mod.ENTITIES.LOST_SOUL.Var, soulInfo.SubType, pos, lib.ZeroVector, nil)
	end
	
	data.openPortalForSoul = openPortalForSoul
	data.soulWaiting = nil
	
	if #backdropOptions > 0 then
		lib.Log("Changing the backdrop...")
		local backdrop = lib.PickRandom(backdropOptions, rng)
		game:ShowHallucination(-1, backdrop)
		lib.SuppressSound(SoundEffect.SOUND_DEATH_CARD)
		for i=0, room:GetGridSize() do
			local gridEntity = room:GetGridEntity(i)
			if gridEntity then
				gridEntity:Init(gridEntity.Desc.SpawnSeed)
				gridEntity:PostInit()
			end
		end
	end
	
	data.soulHash = lostSoul and GetPtrHash(lostSoul) or nil
	
	-- Safety enforcement that one and only one lost soul spawns.
	lib.ScheduleForUpdate(function()
		local realSoul
		for _, soul in pairs(Isaac.FindByType(mod.ENTITIES.LOST_SOUL.ID, mod.ENTITIES.LOST_SOUL.Var)) do
			if not realSoul or GetPtrHash(soul) == data.soulHash then
				if realSoul then
					lib.Log("Removing a duplicate Lost Soul.")
					realSoul:Remove()
				end
				realSoul = soul
			else
				lib.Log("Removing a duplicate Lost Soul.")
				soul:Remove()
			end
		end
		if not realSoul and not data.soulDied and mod:IsFragmentCombatRoom() then
			lib.Log("No Lost Soul found! Spawning one...")
			local pos = room:FindFreePickupSpawnPosition(entryPos or Isaac.GetPlayer().Position, 0, true)
			Isaac.Spawn(mod.ENTITIES.LOST_SOUL.ID, mod.ENTITIES.LOST_SOUL.Var, 1, pos, lib.ZeroVector, nil)
		end
	end)
	
	lib.Log("Finished common Fragment newroom code.")
	
	if mod:IsFragmentEntrance() then
		data.soulDied = nil
		
		lib.Log("Generating entrance portals...")
		
		if data.generatePortals then
			for _, portal in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT)) do
				portal:Remove()
			end
			for _, pos in pairs(mod:FindFragmentPortalSpawnLocations()) do
				Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, 1000 + GetFragmentCombatRoomIndex(), pos, lib.ZeroVector, nil)
			end
			data.generatePortals = false
		end
		
		lib.Log("Spawning statue...")
		local statue = mod:FindReaperStatue()
		
		if data.pendingReward then
			lib.Log("Spawning reward...")
			mod:FragmentReward(statue.Position + Vector(0, 60))
			data.pendingReward = false
		end
		if data.soulHeadedToBoat then
			lib.Log("Checking rescued souls...")
			data.soulsInBoat = data.soulsInBoat or {}
			table.insert(data.soulsInBoat, data.soulHeadedToBoat)
			data.soulHeadedToBoat = nil
		end
		
		entryPos = entryPos or room:GetCenterPos() - Vector(0, 40)
		
		if data.clearedCombatRoom then
			lib.Log("Player cleared a combat room. Removing a portal...")
			data.clearedCombatRoom = false
			data.generateNewRoom = true
			data.roomsCleared = (data.roomsCleared or 0) + 1
			
			local portalToRemove = data.usedFragmentPortal
			local removedPortal = false
			
			local portals = Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, 1000 + GetFragmentCombatRoomIndex())
			
			for _, portal in pairs(portals) do
				if not portalToRemove or portal.InitSeed == portalToRemove then
					portal:Remove()
					removedPortal = true
					entryPos = portal.Position
					break
				end
			end
			
			if not removedPortal and portals[1] then
				portals[1]:Remove()
				entryPos = portals[1].Position
				removedPortal = true
			end
		end
		
		
		if data.savedSoul then
			lib.Log("Player saved a Soul.")
			data.savedSouls = (data.savedSouls or 0) + 1
			local soul = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, entryPos)
			soul.TargetPosition = statue.Position + Vector(0, 80)
			data.pendingReward = true
			data.soulHeadedToBoat = data.savedSoul
			data.savedSoul = nil
			
			mod.ContentManager:GrantCustomAchievement(mod.ACHIEVEMENTS.SAVED_ONE_SOUL)
		end
		
		if not data.cleared and data.savedSouls == 4 then
			lib.Log("Player saved all four souls!")
			data.cleared = true
			room:RemoveGridEntity(room:GetGridIndex(statue.Position), 0, false)
			
			local delay = 30
			local statuePos = statue.Position
			mod:FadeReaperStatue(statue, delay)
			
			lib.ScheduleForUpdate(function()
				local itemPos = room:FindFreePickupSpawnPosition(statuePos, 0, false)
				Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, mod:GetDeathPoolItem(2), itemPos, lib.ZeroVector, nil)
				sfxManager:Play(160, 2.0, 0, false, 0.5)
			end, delay)
			
			mod.ContentManager:GrantCustomAchievement(mod.ACHIEVEMENTS.SAVED_ALL_SOULS)
		end
		
		if statue and data.cleared then
			statue:Remove()
		end
		
		if AllowFragmentBackdrops() then
			lib.Log("Loading entrance gfx...")
			if mod:IsFragmentEntrance() then
				local gfxPos = room:GetCenterPos() + Vector(0, -55) + Vector(0, -2)
				local fragmentEntranceGfx = Isaac.Spawn(EntityType.ENTITY_EFFECT, FRAGMENT_ENTRANCE_EFFECT, 0, gfxPos, lib.ZeroVector, nil)
				fragmentEntranceGfx.Color = Color(0,0,0,0.5)
				fragmentEntranceGfx:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR)
				fragmentEntranceGfx:AddEntityFlags(EntityFlag.FLAG_RENDER_WALL)
				
				local gfxPos = room:GetCenterPos() + Vector(0, -55) + Vector(0, -6)
				local fragmentEntranceGfx = Isaac.Spawn(EntityType.ENTITY_EFFECT, FRAGMENT_ENTRANCE_EFFECT, 0, gfxPos, lib.ZeroVector, nil)
				fragmentEntranceGfx.Color = Color(0,0,0,0.5)
				fragmentEntranceGfx:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR)
				fragmentEntranceGfx:AddEntityFlags(EntityFlag.FLAG_RENDER_WALL)
			end
			
			mod:DrawFragmentBackdrop()
			
			-- Hide pits
			for i=0, room:GetGridSize() do
				local gridEntity = room:GetGridEntity(i)
				if gridEntity and gridEntity:ToPit() then
					gridEntity:GetSprite().Scale = lib.ZeroVector
				end
			end
		end
		
		for _, dirt in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.DIRT_PATCH)) do
			dirt:ToEffect().State = 2
			dirt:Remove()
		end
	end
	
	mod:FadeIn()
	
	if entryPos then
		lib.Log("Fixing player entry positions...")
		for _, player in pairs(lib.GetPlayers()) do
			player.Position = entryPos
			lib.ScheduleForUpdate(function()
				player.Position = entryPos
			end)
		end
	end
	
	ClearDummyDecos()
	
	lib.Log("Fragment MC_POST_NEW_ROOM completed.")
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.FragmentNewRoom)

-- Make sure weird things don't happen during room transitions.
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, function()
	if (mod:IsFragmentRoom() or mod:IsDeathDealRoom() or replacingRoom) and game:GetRoom():GetFrameCount() <= 1 then
		return false
	end
end, EntityType.ENTITY_PLAYER)

function mod:NoCollisionDuringTransition()
	if (mod:IsFragmentRoom() or mod:IsDeathDealRoom() or replacingRoom) and game:GetRoom():GetFrameCount() <= 1 then
		return true
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_PRE_PLAYER_COLLISION, CallbackPriority.EARLY, mod.NoCollisionDuringTransition)
mod:AddPriorityCallback(ModCallbacks.MC_PRE_PICKUP_COLLISION, CallbackPriority.EARLY, mod.NoCollisionDuringTransition)

mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, function(_, eff)
	if mod:IsFragmentRoom() and game:GetRoom():GetFrameCount() == -1 then
		eff:Remove()
	end
end, EffectVariant.BOMB_CRATER)

local testOffset = Vector(0, 40)

function mod:DrawFragmentBackdrop()
	if not mod:IsFragmentEntrance() then return end
	
	local room = game:GetRoom()
	
	local gfxPos = room:GetCenterPos() + Vector(0, -95) + testOffset
	local fragmentEntranceGfx = Isaac.Spawn(EntityType.ENTITY_EFFECT, FRAGMENT_ENTRANCE_EFFECT, 0, gfxPos, lib.ZeroVector, nil)
	fragmentEntranceGfx:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR)
	fragmentEntranceGfx:AddEntityFlags(EntityFlag.FLAG_RENDER_WALL)
	--fragmentEntranceGfx.Color = GetFragmentWallColor()
	
	--[[if mod:InFragmentExit() then
		fragmentEntranceGfx:GetSprite():Play("Exit", true)
	end]]
end

local ChunkBackdropOverride = {
	[BackdropType.MAUSOLEUM] = BackdropType.MAUSOLEUM3,
	[BackdropType.WOMB] = BackdropType.UTERO,
}

function mod:SpawnFragmentChunk(idx, pos, floatAmp, floatSpeed, flipped)
	local x = game:GetFrameCount() + 10 * idx * floatAmp
	pos = pos + Vector(0, floatAmp * math.sin(math.pi * x / (60 / floatSpeed))) + testOffset
	local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, FRAGMENT_ENTRANCE_EFFECT, 1, pos, lib.ZeroVector, nil)
	local sprite = eff:GetSprite()
	
	local stageApiBackdrop = lib.InStageApiFloor() and lib.Access(StageAPI.GetCurrentRoom(), "Data", "RoomGfx", "Backdrops", 1, "Walls", 1)
	if type(stageApiBackdrop) == "string" then
		sprite:ReplaceSpritesheet(1, stageApiBackdrop)
		sprite:LoadGraphics()
	else
		local backdropType = game:GetRoom():GetBackdropType()
		local backdropData = mod.BackdropData[ChunkBackdropOverride[backdropType] or backdropType]
		if backdropData then
			sprite:ReplaceSpritesheet(1, backdropData.Gfx)
			sprite:LoadGraphics()
		end
	end
	
	eff:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR)
	eff:AddEntityFlags(EntityFlag.FLAG_RENDER_WALL)
	eff:AddEntityFlags(EntityFlag.FLAG_TRANSITION_UPDATE)
	
	sprite:Play("Rock" .. idx, true)
	sprite.FlipX = flipped
end

local lastRockUpdate = 0
function mod:HandleFragmentRocks()
	if not mod:IsFragmentEntrance() or not AllowFragmentBackdrops() then return end
	
	local currentFrame = Isaac.GetFrameCount()
	
	if lastRockUpdate < currentFrame then
		mod:DrawFragmentBackdrop()
		
		local centerPos = game:GetRoom():GetCenterPos()
		mod:SpawnFragmentChunk(1, centerPos + Vector(105, -5), 4, 1.0)
		mod:SpawnFragmentChunk(2, centerPos + Vector(-280, 30), 3, 0.75)
		mod:SpawnFragmentChunk(3, centerPos + Vector(-130, -5), 3, 1.0)
		mod:SpawnFragmentChunk(4, centerPos + Vector(220, 100), 5, 1.1)
		mod:SpawnFragmentChunk(5, centerPos + Vector(-115, 100), 4, 1.0)
		mod:SpawnFragmentChunk(6, centerPos + Vector(15, 150), 4, 1.0)
		mod:SpawnFragmentChunk(7, centerPos + Vector(15, 90), 4, 1.1)
		mod:SpawnFragmentChunk(8, centerPos + Vector(175, 175), 4, 1.1)
		mod:SpawnFragmentChunk(8, centerPos + Vector(-90, 225), 4, 1.1, true)
		mod:SpawnFragmentChunk(4, centerPos + Vector(25, 225), 5, 1.1, true)
	end
	
	lastRockUpdate = currentFrame
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.HandleFragmentRocks)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, function()
	if Isaac.GetFrameCount() - lastRockUpdate > 1 then
		mod:HandleFragmentRocks()
	end
end)

--------------------------------------------------
-- ROOM SELECTION STUFF
--------------------------------------------------

local FRAGMENT_ROOM_ID_BASE = 61700

local MAX_ROOM_PICK_ATTEMPTS = 50
local MAX_GLOBAL_ROOM_CHANCE = 0.25

-- For fallback code only.
local FF_ROOM_ID_BASE = 17000
local FF_ROOM_CHANCE = 0.5

local function ValidRoom(id, customRoomsOnly, globalRoomsOnly)
	if GoTo("d." .. id) == "Error changing room." then
		return false
	end
	if not customRoomsOnly then
		return true
	end
	local data = game:GetLevel():GetRoomByIdx(-3).Data
	return data and data.Name:find("(Samael/Fragment)") and (not globalRoomsOnly or data.Subtype == 100)
end

local function FindMaxRoomId(start, customRoomsOnly, globalRoomsOnly)
	local id = start or 0
	local numGotos = 0
	
	local steps = customRoomsOnly and {50, 25, 10, 5, 1} or {1000, 500, 250, 100, 50, 25, 10, 5, 1}
	
	for _, step in pairs(steps) do
		while ValidRoom(id + step, customRoomsOnly, globalRoomsOnly) do
			id = id + step
			numGotos = numGotos + 1
		end
	end
	
	Isaac.DebugString("Fragment Generation: Found max room ID " .. id .. " in " .. numGotos .. " goto's")
	return id
end

local function RoomHasEnemies()
	for _, tab in pairs(lib.GetRoomSpawns(game:GetLevel():GetRoomByIdx(-3).Data)) do
		for _, spawn in pairs(tab) do
			if not mod:EntityTypeRespawnForbidden(spawn.Type) then
				return true
			end
		end
	end
	return false
end

local function IsAllowedFragmentRoom()
	local data = game:GetLevel():GetRoomByIdx(-3).Data
	return data.Weight > 0 and RoomHasEnemies() and not (data.Difficulty >= 20 and data.Variant > 1000)
end

local function IsPreferredFragmentRoom()
	local data = game:GetLevel():GetRoomByIdx(-3).Data
	local difficulty = data.Difficulty
	return difficulty >= 10
end

-- Picks a non-custom room to use for a fragment. Old method, but used as a fallback still.
function mod:NonCustomFragmentRoom()
	local data = mod:GetFragmentData()
	if not data.MaxVanillaRoomID then
		lib.Log("Finding max non-custom room ID...")
		data.MaxVanillaRoomID = FindMaxRoomId()
	end
	if FiendFolio and not data.MaxFiendFolioRoomId then
		lib.Log("Finding max FF room ID...")
		data.MaxFiendFolioRoomId = FindMaxRoomId(FF_ROOM_ID_BASE)
	end
	
	local attempts = 0
	local gotoStr
	local str
	while not str or str == "Error changing room." or not IsAllowedFragmentRoom() or (not IsPreferredFragmentRoom() and attempts < MAX_ROOM_PICK_ATTEMPTS) do
		local seed = GetFragmentSeed()
		if data.MaxFiendFolioRoomId and FiendFolio and StageAPI and ((seed % 101) / 100) <= FF_ROOM_CHANCE then
			gotoStr = "d." .. FF_ROOM_ID_BASE + (seed % (data.MaxFiendFolioRoomId + 1 - FF_ROOM_ID_BASE))
		else
			gotoStr = "d." .. seed % (data.MaxVanillaRoomID + 1)
		end
		str = GoTo(gotoStr)
		attempts = attempts + 1
	end
	
	lib.Log("Picked a non-custom Fragment room (" .. gotoStr ..") with attempts = " .. attempts)
	
	return gotoStr
end

local function GetPreviousRooms()
	return lib.GetOrInit(mod:GetFragmentData(), "PREV_ROOMS")
end

-- Safety validations on the room we pick.
local function SkipFragmentRoom()
	local roomData = game:GetLevel():GetRoomByIdx(-3).Data
	local prevRooms = GetPreviousRooms()
	
	if prevRooms[roomData.Variant] or prevRooms[roomData.Name] then
		lib.Log("Skipped fragment room #".. roomData.Variant .. " since we already picked it previously this floor.")
		return true
	end
	
	for _, tab in pairs(lib.GetRoomSpawns(roomData)) do
		for _, spawn in pairs(tab) do
			if spawn.Type == mod.ENTITIES.SAFE_FF_ENEMY.ID and spawn.Variant == mod.ENTITIES.SAFE_FF_ENEMY.Var and not FiendFolio then
				lib.Log("Skipped fragment room #".. roomData.Variant .. " since it contains a Fiend Folio enemy.")
				return true
			end
		end
	end
	
	return false
end

local DEBUG_ROOM = nil

-- Picks the next fragment room and goto's it.
function mod:NextFragmentRoom()
	local data = mod:GetFragmentData()
	local prevRooms = GetPreviousRooms()
	
	lib.Log("Picking/loading next Fragment room...")
	
	if not data.MaxGlobalRoomId then
		lib.Log("Finding max global room ID...")
		data.MaxGlobalRoomId = FindMaxRoomId(FRAGMENT_ROOM_ID_BASE, true, true)
	end
	if not data.MaxRoomID then
		lib.Log("Finding max local room ID...")
		data.MaxRoomID = FindMaxRoomId(data.MaxGlobalRoomId, true)
	end
	
	local numGlobalRooms = data.MaxGlobalRoomId - FRAGMENT_ROOM_ID_BASE + 1
	local totalRooms = data.MaxRoomID - FRAGMENT_ROOM_ID_BASE + 1
	
	local attempts = 0
	local gotoStr
	local str
	while (not str or str == "Error changing room." or SkipFragmentRoom()) and attempts < MAX_ROOM_PICK_ATTEMPTS do
		local seed = GetFragmentSeed()
		local baseID = FRAGMENT_ROOM_ID_BASE
		local maxID = data.MaxRoomID
		
		if DEBUG_ROOM then
			gotoStr = "d." .. DEBUG_ROOM
			str = GoTo(gotoStr)
			break
		end
		
		-- If there's more global rooms than not, roll specifically whether to pick a global room.
		-- This favours the appearance of non-global rooms.
		if attempts <= MAX_ROOM_PICK_ATTEMPTS*0.5 and numGlobalRooms / totalRooms > MAX_GLOBAL_ROOM_CHANCE then
			lib.Log("Intentionally choosing whether to pick a global room...")
			local rng = RNG()
			rng:SetSeed(seed, 35)
			if rng:RandomFloat() <= MAX_GLOBAL_ROOM_CHANCE then
				lib.Log("Gonna pick a global room.")
				maxID = baseID + numGlobalRooms - 1
			else
				lib.Log("Gonna pick a local room.")
				baseID = baseID + numGlobalRooms
			end
		end
		
		local roomID = baseID + (seed % (maxID - baseID + 1))
		lib.Log("Picked: " .. roomID)
		
		if prevRooms[roomID] and attempts <= MAX_ROOM_PICK_ATTEMPTS*0.5 then
			lib.Log("Tried to pick a duplicate room.")
			gotoStr = nil
			str = nil
		else
			gotoStr = "d." .. roomID
			str = GoTo(gotoStr)
		end
		attempts = attempts + 1
	end
	
	lib.Log("Finished loop with attempts = " .. attempts)
	
	if attempts >= MAX_ROOM_PICK_ATTEMPTS then
		lib.Log("Failed to pick a Fragment room. Falling back to a vanilla room...")
		return mod:NonCustomFragmentRoom()
	end
	
	-- Try to avoid duplicates later.
	local prevRooms = GetPreviousRooms()
	local roomData = game:GetLevel():GetRoomByIdx(-3).Data
	prevRooms[roomData.Name] = true
	prevRooms[roomData.Variant] = true
	
	lib.Log("Chose Fragment room: " .. gotoStr)
	return gotoStr
end

--------------------------------------------------
-- MISC FRAGMENT UTILS
--------------------------------------------------

local fadeIn
local fragmentRooms = {}

function mod:FadeIn()
	fadeIn = Sprite()
	fadeIn:Load("gfx/ui/bossoverlay_whiteout.anm2", true)
	fadeIn.PlaybackSpeed = 1.0
	fadeIn.Color = Color(0,0,0,1,-1,-1,-1)
	fadeIn.Scale = Vector(10, 10)
	fadeIn:Play("Fade", true)
	fadeIn:SetFrame(15)
end

function mod:GoToFragment()
	lib.Log("Fragment loading initiated!")
	
	local cidx = mod:GetFragmentData().FragmentRoomIndex
	mod:ClearFragmentData()
	local data = mod:GetFragmentData()
	data.FragmentRoomIndex = cidx
	data.generatePortals = true
	
	data.returnIdx = mod:GetLastKnownGridIndex()
	
	local level = game:GetLevel()
	local entranceIdx, newlyCreatedEntrance = GetFragmentEntranceIndex()
	local combatIdx, newlyCreatedCombatRoom = GetFragmentCombatRoomIndex()
	
	if newlyCreatedCombatRoom then
		lib.Log("Newly created the Fragment combat room.")
		GoTo("s.default.61800")
		lib.CopyGotoRoomData(combatIdx, RoomDescriptor.FLAG_PORTAL_LINKED)
	end
	
	if newlyCreatedEntrance then
		lib.Log("Newly created the Fragment entrance room.")
		local roomid = 61810
		if game:GetLevel():GetAbsoluteStage() == LevelStage.STAGE6 and game:GetLevel():GetStageType() == StageType.STAGETYPE_ORIGINAL then
			roomid = 61801
		end
		GoTo("s.default." .. roomid)
		lib.CopyGotoRoomData(entranceIdx, RoomDescriptor.FLAG_PORTAL_LINKED)
	end
	
	if newlyCreatedEntrance or newlyCreatedCombatRoom then
		lib.Log("Since we newly created the Fragment rooms, go back to the previous room to preserve what the last room was.")
		game:ChangeRoom(mod:GetLastKnownGridIndex())
	end
	lib.Log("Starting transition to Fragment entrance...")
	game:StartRoomTransition(entranceIdx, -1, RoomTransitionAnim.FADE)
	
	game:GetHUD():ShowItemText("Fragment")
	mod:FadeIn()
	
	lib.Log("Finished Fragment loading.")
end

function mod:FragmentFadeIn()
	if fadeIn then
		if fadeIn:IsFinished() then
			fadeIn = nil
		else
			fadeIn:Render(lib.ZeroVector,lib.ZeroVector,lib.ZeroVector)
			if not game:IsPaused() and not buildingFragment then
				fadeIn:Update()
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.FragmentFadeIn)

local fragmentFerryman
local fragmentPortal

function mod:FindFragmentPortalSpawnLocations()
	local positions = {}
	for gridIndex, spawns in pairs(lib.GetRoomSpawns()) do
		for _, spawn in pairs(spawns) do
			if spawn.Type == mod.ENTITIES.FRAGMENT_PORTAL.ID and spawn.Variant == mod.ENTITIES.FRAGMENT_PORTAL.Var then
				table.insert(positions, game:GetRoom():GetGridPosition(gridIndex))
				break
			end
		end
	end
	return positions
end

local function FindDoorSlotForPortal()
	local room = game:GetRoom()
	
	local pos
	local dist
	
	for i=0, 7 do
		if room:IsDoorSlotAllowed(i) then
			local doorPos = room:GetDoorSlotPosition(i)
			local doorClosestPlayerDist
			for _, player in pairs(lib.GetPlayers()) do
				local doorPlayerDist = doorPos:Distance(player.Position)
				if not doorClosestPlayerDist or doorPlayerDist < doorClosestPlayerDist then
					doorClosestPlayerDist = doorPlayerDist
				end
			end
			if not pos or doorClosestPlayerDist > dist then
				pos = doorPos
				dist = doorClosestPlayerDist
			end
		end
	end
	
	return room:GetClampedPosition(pos, 20)
end

local function SpawnFragmentPortal()
	local portalSubType = 1000 + GetFragmentEntranceIndex()
	
	local portals = Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, portalSubType)
	
	if portals[1] and portals[1]:Exists() then
		fragmentPortal = portals[1]:ToEffect()
	else
		local pos
		local portalPositions = mod:FindFragmentPortalSpawnLocations()
		if #portalPositions > 0 then
			local rng = RNG()
			rng:SetSeed(mod:GetFragmentData().CurrentRoomSeed or game:GetRoom():GetDecorationSeed(), 40)
			pos = lib.PickRandom(portalPositions, rng)
		else
			pos = FindDoorSlotForPortal()
		end
		fragmentPortal = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, portalSubType, pos, lib.ZeroVector, nil):ToEffect()
		fragmentPortal:GetSprite():Play("Appear", true)
	end
end

function mod:FragmentUpdate()
	if not mod:IsFragmentRoom() then return end
	
	ClearDummyDecos()
	
	local room = game:GetRoom()
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	local data = mod:GetFragmentData()
	
	if not data.fragmentRoomCleared and room:IsClear() then
		data.fragmentRoomCleared = true
	end
	
	if mod:IsFragmentEntrance() then
		if not fragmentFerryman or not fragmentFerryman:Exists() then
			local ferrymanSpawnPos
			
			local spawns = roomDesc.Data.Spawns
			for i = 0, spawns.Size - 1 do
				local spawn = spawns:Get(i)
				if spawn then
					local entry = spawn:PickEntry(0)
					if entry.Type == EntityType.ENTITY_SLOT and entry.Variant == mod.ENTITIES.FERRYMAN.Var then
						local roomWidth = room:GetGridWidth()
						local gridIdx = roomWidth + 1 + (spawn.X + roomWidth * spawn.Y)
						ferrymanSpawnPos = room:GetGridPosition(gridIdx)
					end
				end
			end
			
			if not ferrymanSpawnPos then
				ferrymanSpawnPos = room:FindFreePickupSpawnPosition(room:GetCenterPos(), 0)
			end
			
			local foundFerryman = false
			
			for _, ferryman in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT, mod.ENTITIES.FERRYMAN.Var, -1)) do
				if foundFerryman then
					ferryman:Remove()
				else
					if ferryman.SubType ~= 1 then
						ferryman.SubType = 1
					end
					fragmentFerryman = ferryman
					foundFerryman = true
				end
			end
			
			if foundFerryman then
				fragmentFerryman.Position = ferrymanSpawnPos
				fragmentFerryman.TargetPosition = ferrymanSpawnPos
			else
				fragmentFerryman = mod:SpawnFerryman(true, ferrymanSpawnPos)
			end
		end
	end
	
	if mod:IsFragmentCombatRoom() then
		if (room:IsClear() and not data.openPortalForSoul) or (data.openPortalForSoul and not data.soulWaiting) then
			if not data.clearedCombatRoom then
				data.clearedCombatRoom = true
			end
			if room:GetFrameCount() > 20 and (not fragmentPortal or not fragmentPortal:Exists()) then
				SpawnFragmentPortal()
			end
		elseif fragmentPortal and fragmentPortal:Exists() then
			fragmentPortal:Remove()
		end
	end
	
	data.soulWaiting = false
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.FragmentUpdate)

function mod:FragmentPortalMarker(portal)
	if portal.Variant ~= mod.ENTITIES.FRAGMENT_PORTAL.Var then return end
	
	if mod:IsFragmentEntrance() then
		local newPortal = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, 1000 + GetFragmentCombatRoomIndex(), portal.Position, lib.ZeroVector, nil)
		newPortal:GetSprite():Play("Appear", true)
	end
	portal:Remove()
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.FragmentPortalMarker, mod.ENTITIES.FRAGMENT_PORTAL.ID)

function mod:FragmentPortalUpdate(portal)
	if portal.SubType == 1000 + GetFragmentEntranceIndex() and (not fragmentPortal or GetPtrHash(portal) ~= GetPtrHash(fragmentPortal)) and not mod:IsDeathDealRoom() then
		portal:Remove()
	end
	
	-- Push pickups away from my portals.
	if portal.SubType >= 1000 and (mod:IsDeathDealRoom() or mod:IsFragmentRoom()) then
		for _, pickup in pairs(Isaac.FindInRadius(portal.Position, 20, EntityPartition.PICKUP)) do
			pickup = pickup:ToPickup()
			if pickup then
				local pushDir = (pickup.Position - portal.Position):Normalized()
				if pushDir:Length() == 0 then
					pushDir = RandomVector()
				end
				if pickup.Price ~= 0 or pickup.Variant == PickupVariant.PICKUP_COLLECTIBLE then
					pickup.TargetPosition = pickup.TargetPosition + pushDir
				elseif pickup.Velocity:Length() < 1 then
					pickup.Velocity = lib.Lerp(pickup.Velocity, pushDir:Resized(2), 0.1)
				end
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.FragmentPortalUpdate, EffectVariant.PORTAL_TELEPORT)

function mod:FragmentPortalSync()
	if not mod:IsFragmentEntrance() or not game:IsPaused() then return end
	
	local portalSubType = 1000 + GetFragmentCombatRoomIndex()
	local portals = Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, portalSubType)
	
	for _, portal in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, portalSubType)) do
		for _, player in pairs(Isaac.FindInRadius(portal.Position, 1, EntityPartition.PLAYER)) do
			if player:GetSprite():IsPlaying("Trapdoor") then
				mod:GetFragmentData().usedFragmentPortal = portal.InitSeed
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.FragmentPortalSync)

mod:AddPostSlotUpdateFunc(function(_, ferryman)
	if (fragmentFerryman and fragmentFerryman:Exists() and GetPtrHash(ferryman) ~= GetPtrHash(fragmentFerryman)) then
		ferryman:Remove()
	end
end, mod.ENTITIES.FERRYMAN.Var)

function mod:FragmentTesting(command, params)
	if command == "fragment" then
		mod:GoToFragment()
	end
end
mod:AddCallback(ModCallbacks.MC_EXECUTE_CMD, mod.FragmentTesting)

--------------------------------------------------
-- NPC Tweaks
--------------------------------------------------

if FiendFolio then
	mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
		if npc.Variant ~= FiendFolio.FF.Gravedigger.Var then return end
		
		local data = npc:GetData()
		if data.init and not data.samaelFragmentInit and mod:IsFragmentRoom() then
			data.samaelFragmentInit = true
			npc.MaxHitPoints = math.ceil(npc.MaxHitPoints * 0.5)
			npc.HitPoints = npc.MaxHitPoints
			npc:AddEntityFlags(EntityFlag.FLAG_DONT_COUNT_BOSS_HP)
			if game:GetRoom():GetAliveEnemiesCount() > 1 then
				data.state = "doYouPreferTheShieldOnOrOff"
				npc.StateFrame = 0
			end
		end
	end, FiendFolio.FF.Gravedigger.ID)
	
	mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
		if npc.Variant ~= FiendFolio.FF.Shi.Var then return end
		
		local sprite = npc:GetSprite()
		
		if mod:IsFragmentRoom() and sprite:IsPlaying("Appear") and npc.FrameCount < 10 then
			sprite:SetFrame(0)
		end
	end, FiendFolio.FF.Shi.ID)
end

-- Dogma TV acts like a Bishop
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, tv)
	if tv.Variant == 1 and mod:IsFragmentRoom() and not tv:GetData().samaelFragmentInit then
		tv:GetData().samaelFragmentInit = true
		tv.MaxHitPoints = math.ceil(tv.MaxHitPoints * 0.4)
		tv.HitPoints = tv.MaxHitPoints
	end
end, EntityType.ENTITY_DOGMA)

mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, function(_, ent, _, _, damageSource)
	if not mod:IsFragmentRoom() then return end
	
	if damageSource.Type == EntityType.ENTITY_DOGMA and damageSource.Variant == 1 then
		return false
	end
	
	if not ent:IsActiveEnemy() or ent.Type == EntityType.ENTITY_DOGMA or ent:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then return end
	
	local tv = Isaac.FindByType(EntityType.ENTITY_DOGMA, 1)[1]
	
	if tv and tv.HitPoints > 0 then
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BISHOP_SHIELD, 0, ent.Position, lib.ZeroVector, tv):ToEffect()
		--eff.PositionOffset = ent.PositionOffset + ent.SpriteOffset + Vector(0, -15 - ent.Size*2)
		if FiendFolio and ent.Type == FiendFolio.FF.Shi.ID and ent.Variant == FiendFolio.FF.Shi.Var then
			eff.SpriteScale = Vector(1.25, 1.25)
			eff.PositionOffset = ent.PositionOffset + ent.SpriteOffset + Vector(0, -40)
		else
			eff.PositionOffset = ent.PositionOffset + ent.SpriteOffset + Vector(0, -25)
		end
		eff.Parent = tv
		eff.Target = ent
		sfxManager:Play(SoundEffect.SOUND_BISHOP_HIT)
		return false
	end
end)

--------------------------------------------------
-- Alt Skins
--------------------------------------------------

function mod:MakeBubblesWhite(npc)
	if mod:IsFragmentRoom() then
		local backdrop = game:GetRoom():GetBackdropType()
		if backdrop ~= BackdropType.FLOODED_CAVES and backdrop ~= BackdropType.DOWNPOUR_ENTRANCE
				and backdrop ~= BackdropType.DOWNPOUR and backdrop ~= BackdropType.DROSS then
			lib.ReplaceEnemySpritesheet(npc, "gfx/monsters/repentance/bloody_bubbles", 0, false)
			lib.ReplaceEnemySpritesheet(npc, "gfx/monsters/repentance/bloody_bubbles", 1)
			npc:GetData().whiteBubbles = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.MakeBubblesWhite, EntityType.ENTITY_BUBBLES)

mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, function(_, npc)
	if not npc:GetData().whiteBubbles then return end
	
	local pos = Vector(npc.Position.X, npc.Position.Y)
	lib.ScheduleForUpdate(function()
		for _, eff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, 2)) do
			if pos:Distance(eff.Position) < 50 then
				eff.Color = Color(1,0,0,1)
			end
		end
		for _, eff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, 132)) do
			if pos:Distance(eff.Position) < 50 then
				eff.Color = Color(1,0,0,1)
			end
		end
	end, 2)
end, EntityType.ENTITY_BUBBLES)
