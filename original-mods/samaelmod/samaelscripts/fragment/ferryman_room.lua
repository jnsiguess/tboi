local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local FERRYMAN_ROOM_BASE_SPAWN_CHANCE = 0.1
local FERRYMAN_ROOM_ADDED_SPAWN_CHANCE = 0.1
local FERRYMAN_ROOM_MIN_SPAWN_CHANCE = 0.0
local FERRYMAN_ROOM_MAX_SPAWN_CHANCE = 0.4

local MIN_ROOM_ID = 61900
local MAX_ROOM_ID = 61940

local function FerrymenUnlocked()
	return not mod.ContentManager:EntityLockedOrDisabled(mod.ENTITIES.FERRYMAN.ID, mod.ENTITIES.FERRYMAN.Var)
end

function mod:GetFerrymanRoomSpawnChance()
	return mod:GetAllRunData().FerrymanRoomSpawnChance or FERRYMAN_ROOM_BASE_SPAWN_CHANCE
end

function mod:UpdateFerrymanRoomSpawnChance()
	local currentChance = mod:GetFerrymanRoomSpawnChance()
	if currentChance < FERRYMAN_ROOM_MAX_SPAWN_CHANCE then
		mod:GetAllRunData().FerrymanRoomSpawnChance = currentChance + FERRYMAN_ROOM_ADDED_SPAWN_CHANCE
	end
end

function mod:ResetFerrymanRoomSpawnChance()
	mod:GetAllRunData().FerrymanRoomSpawnChance = FERRYMAN_ROOM_MIN_SPAWN_CHANCE
end

local freezePlayers = false
mod:AddPriorityCallback(ModCallbacks.MC_POST_UPDATE, CallbackPriority.EARLY, function()
	if freezePlayers then
		for _, player in pairs(lib.GetPlayers()) do
			player.ControlsEnabled = true
		end
		freezePlayers = false
	end
end)
mod:AddPriorityCallback(ModCallbacks.MC_POST_PLAYER_RENDER, CallbackPriority.EARLY, function(_, player)
	if freezePlayers then
		player.ControlsEnabled = false
		player.Velocity = lib.ZeroVector
		if player:GetData().samaelFloorStartPos then
			player.Position = player:GetData().samaelFloorStartPos
		end
	end
end)

local function LoadFerrymanRooms()
	lib.Log("Loading Ferryman roomdata...")
	
	for _, player in pairs(lib.GetPlayers()) do
		player:GetData().samaelFloorStartPos = Vector(player.Position.X, player.Position.Y)
	end
	
	SAMAEL_FERRYMAN_ROOMS = {}
	
	for id=MIN_ROOM_ID, MAX_ROOM_ID do
		local gotoresult = Isaac.ExecuteCommand("goto s.default." .. id)
		if gotoresult == "Error changing room." then
			lib.LogErr("Error trying to load Ferryman room: " .. id)
		else
			local inserted = false
			for doorSlot=0,3 do
				local roomData = game:GetLevel():GetRoomByIdx(-3).Data
				if (roomData.Doors & (1 << doorSlot) ~= 0) then
					local tab = lib.GetOrInit(SAMAEL_FERRYMAN_ROOMS, doorSlot)
					table.insert(tab, roomData)
					inserted = true
				end
			end
			if not inserted then
				lib.LogErr("Failed to insert Ferryman room into list: " .. id)
			end
		end
	end
	
	lib.Log("Ferryman roomdata loaded. Returning to starting room...")
	
	local startingRoomIdx = game:GetLevel():GetStartingRoomIndex()
	game:StartRoomTransition(startingRoomIdx, Direction.NO_DIRECTION, RoomTransitionAnim.FADE)
	game:GetLevel():GetRoomByIdx(startingRoomIdx).VisitedCount = 0
	freezePlayers = true
	
	lib.Log("Ferryman roomdata loading completed.")
end

local function ValidRoom(idx)
	local room = game:GetLevel():GetRoomByIdx(idx)
	return room and room.Data and room.GridIndex > -1
end

local function CheckValidLocation(idx)
	local rooms = 0
	local connectedDoor = -1

	if idx % 13 > 0 and ValidRoom(idx - 1) then
		rooms = rooms + 1
		connectedDoor = DoorSlot.LEFT0
	end
	if idx >= 13 and ValidRoom(idx - 13) then
		rooms = rooms + 1
		connectedDoor = DoorSlot.UP0
	end
	if idx % 13 < 12 and ValidRoom(idx + 1) then
		rooms = rooms + 1
		connectedDoor = DoorSlot.RIGHT0
	end
	if idx < 156 and ValidRoom(idx + 13) then
		rooms = rooms + 1
		connectedDoor = DoorSlot.DOWN0
	end
	
	if rooms == 1 and connectedDoor ~= -1 then
		return true, connectedDoor
	end
	return false
end

function mod:UpdateRoomDisplayFlags(idx)
	local level = game:GetLevel()
	local roomdesc = level:GetRoomByIdx(idx)
	local roomdata = roomdesc.Data
	if level:GetRoomByIdx(roomdesc.GridIndex).DisplayFlags then
		if level:GetRoomByIdx(roomdesc.GridIndex) ~= level:GetCurrentRoomDesc().GridIndex then
			if roomdata then 
				if level:GetStateFlag(LevelStateFlag.STATE_FULL_MAP_EFFECT) then
					roomdesc.DisplayFlags = RoomDescriptor.DISPLAY_ICON
				elseif roomdata.Type ~= RoomType.ROOM_DEFAULT and roomdata.Type ~= RoomType.ROOM_SECRET and roomdata.Type ~= RoomType.ROOM_SUPERSECRET and roomdata.Type ~= RoomType.ROOM_ULTRASECRET and level:GetStateFlag(LevelStateFlag.STATE_COMPASS_EFFECT) then
					roomdesc.DisplayFlags = RoomDescriptor.DISPLAY_ICON
				elseif roomdata and level:GetStateFlag(LevelStateFlag.STATE_BLUE_MAP_EFFECT) and (roomdata.Type == RoomType.ROOM_SECRET or roomdata.Type == RoomType.ROOM_SUPERSECRET) then
					roomdesc.DisplayFlags = RoomDescriptor.DISPLAY_ICON
				elseif level:GetStateFlag(LevelStateFlag.STATE_MAP_EFFECT) then
					roomdesc.DisplayFlags = RoomDescriptor.DISPLAY_BOX
				else
					roomdesc.DisplayFlags = RoomDescriptor.DISPLAY_NONE
				end
			end
		end
	end
end

local function FindValidRoomLocations()
	local level = game:GetLevel()
	
	lib.Log("Looking for valid locations to place a Ferryman room...")
	
	local validLocations = {}
	
	for i = level:GetRooms().Size, 0, -1 do
		local roomDesc = level:GetRooms():Get(i-1)
		if roomDesc and roomDesc.GridIndex ~= level:GetStartingRoomIndex() and roomDesc.GridIndex ~= level:GetCurrentRoomIndex()
				and roomDesc.Data and roomDesc.Data.Type == RoomType.ROOM_DEFAULT and roomDesc.Data.Subtype ~= 34 then
			for doorSlot=0, 7 do
				local validDoorSlot = (roomDesc.Data.Doors & (1 << doorSlot) ~= 0)
				if validDoorSlot then
					local adjIdx = mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot)
					if adjIdx and not ValidRoom(adjIdx) then
						local valid, requiredDoor = CheckValidLocation(adjIdx)
						if valid and SAMAEL_FERRYMAN_ROOMS[requiredDoor] and #SAMAEL_FERRYMAN_ROOMS[requiredDoor] > 0 then
							table.insert(validLocations, {
								FromIndex = roomDesc.GridIndex,
								FromDoor = doorSlot,
								ToDoor = requiredDoor,
								ToIndex = adjIdx,
							})
						end
					end
				end
			end
		end
	end
	
	lib.Log("Finished looking for Ferryman room locations.")
	
	return validLocations
end

--[[if MinimapAPI then
	local sprite = Sprite()
	sprite:Load("gfx/ui/minimapapi/taintedtreasureicon.anm2", true)
	sprite:SetFrame("CustomIconTaintedTreasureRoom", 0)
	MinimapAPI:AddIcon("FerrymanRoom", sprite)
end]]

local function TryPlaceFerrymanRoom(locationsToTry, rng)
	local level = game:GetLevel()
	
	lib.Log("Going to try to place a Ferryman room on the grid...")
	
	lib.Shuffle(locationsToTry, rng)
	
	for _, tab in ipairs(locationsToTry) do
		lib.Log("About to call MakeRedRoomDoor...")
		local success = level:MakeRedRoomDoor(tab.FromIndex, tab.FromDoor)
		lib.Log("MakeRedRoomDoor called.")
		lib.SuppressSound(SoundEffect.SOUND_UNLOCK00)
		if success then
			lib.Log("Successfully created a red room. Loading Ferryman room data...")
			local roomData = lib.PickRandom(SAMAEL_FERRYMAN_ROOMS[tab.ToDoor], rng)
			lib.PasteRoomData(tab.ToIndex, roomData)
			mod:UpdateRoomDisplayFlags(tab.ToIndex)
			level:UpdateVisibility()
			if MinimapAPI then
				local minimaproom = MinimapAPI:GetRoomByIdx(tab.ToIndex)
				if minimaproom then
					minimaproom.Color = Color(MinimapAPI.Config.DefaultRoomColorR, MinimapAPI.Config.DefaultRoomColorG, MinimapAPI.Config.DefaultRoomColorB, 1, 0, 0, 0)
					minimaproom.PermanentIcons = {}
				end
			end
			lib.Log("Ferryman room successfully placed!")
			return tab.ToIndex
		else
			lib.Log("MakeRedRoomDoor call FAILED.")
		end
	end
	
	lib.Log("Couldn't place a ferryman room.")
end

local function CharonClubCardActive()
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(mod.ITEMS.CHARON_CLUB_CARD) then
			return true
		end
	end
	return false
end

local function MaybeAddFerrymanRoom()
	local level = game:GetLevel()
	local charonClubCardActive = CharonClubCardActive()
	local stage = level:GetStage()
	
	-- Do not generate in Greed Mode, Home, or Blue Womb.
	if game:IsGreedMode() or stage == LevelStage.STAGE8 or stage == LevelStage.STAGE4_3 then return end
	
	-- Do not generate on endgame floors, unless Charon Club Card is held.
	if not charonClubCardActive and (stage >= LevelStage.STAGE6 or level:IsAscent() or game.Challenge == mod.CHALLENGES.THE_REAPER.ID) then return end
	
	local seed = game:GetSeeds():GetStageSeed(level:GetStage())
	local rng = RNG()
	rng:SetSeed(seed, 35)
	
	-- Roll whether to spawn a room.
	if not charonClubCardActive and rng:RandomFloat() > mod:GetFerrymanRoomSpawnChance() then return end
	
	local validLocations = FindValidRoomLocations()
	
	if #validLocations == 0 then return end
	
	local idx = TryPlaceFerrymanRoom(validLocations, rng)
	if idx then
		lib.Log("Placed Ferryman room at " .. idx)
		return true
	end
end

mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, function()
	if FerrymenUnlocked() and not game:IsGreedMode() then
		if not SAMAEL_FERRYMAN_ROOMS and not lib.InStageApiFloor() then
			LoadFerrymanRooms()
		end
		if SAMAEL_FERRYMAN_ROOMS then
			if MaybeAddFerrymanRoom() then
				mod:ResetFerrymanRoomSpawnChance()
			else
				mod:UpdateFerrymanRoomSpawnChance()
			end
		end
	end
end)

mod:AddPriorityCallback(ModCallbacks.MC_POST_NEW_ROOM, CallbackPriority.LATE, function()
	local level = game:GetLevel()
	local room = game:GetRoom()
	local data = mod:GetAllCurrentRoomData()
	
	if room:IsFirstVisit() and CharonClubCardActive() then
		local ferrymanPos
		if level:GetStage() == LevelStage.STAGE8 and level:GetCurrentRoomIndex() == 98 then
			ferrymanPos = room:GetCenterPos() + Vector(100,0)
			data.HomeClosetFragment = true
		elseif level:GetStage() == LevelStage.STAGE4_3 and level:GetCurrentRoomIndex() == level:GetStartingRoomIndex() then
			ferrymanPos = room:GetBottomRightPos() - Vector(60,60)
			data.BlueWombFragment = true
		end
		if ferrymanPos and #Isaac.FindByType(mod.ENTITIES.FERRYMAN.ID, mod.ENTITIES.FERRYMAN.Var) == 0 then
			mod:SpawnFerryman(false, ferrymanPos)
		end
	end
	
	if data.HomeClosetFragment then
		local eff = mod:ManuallyApplyFragmentBackdrop({[3] = true,}, Vector(-240, 0))
		eff:GetData().NoRenderFlags = true
	elseif data.BlueWombFragment then
		SamaelMod:ManuallyApplyFragmentBackdrop({[10] = 3,})
	end
end)

-- StageAPI / FF: Disable custom pits in Ferryman rooms if they don't contain "alt" sprites (for water, etc).
-- Ferryman rooms have water-filled pits enabled by default.
if StageAPI and StageAPI.Loaded then
	local function ShouldForceDefaultPits(pits, altPits)
		if not StageAPI.InOverriddenStage() and game:GetRoom():HasWaterPits() and pits and (not altPits or pits.File == altPits.File) then
			for gridIndex, tab in pairs(lib.GetRoomSpawns()) do
				for _, spawn in pairs(tab) do
					if spawn.Type == mod.ENTITIES.FERRYMAN.ID and spawn.Variant == mod.ENTITIES.FERRYMAN.Var and spawn.SubType == 0 then
						return true
					end
				end
			end
		end
		return false
	end
	
	local forceDefaultPits = nil
	
	StageAPI.AddCallback("Samael", "PRE_CHANGE_PIT_GFX", 999, function(_, grid, index, usingPitFile, usingBridgeFilename, usingAlt)
		if forceDefaultPits == nil and game:GetRoom():GetFrameCount() == 0 then
			forceDefaultPits = ShouldForceDefaultPits(usingPitFile, usingAlt)
		end
	end)
	
	mod:AddPriorityCallback(ModCallbacks.MC_POST_NEW_ROOM, CallbackPriority.LATE, function()
		if forceDefaultPits then
			local baseFloorInfo = StageAPI.GetBaseFloorInfo()
			local grids = baseFloorInfo.RoomGfx.Grids
			local defaultPits = grids.PitFiles or StageAPI.BaseGridGfx.Caves.PitFiles
			local defaultAltPits = grids.AltPitFiles or StageAPI.BaseGridGfx.Caves.AltPitFiles
			StageAPI.ChangeGrids({
				PitFiles = defaultPits,
				AltPitFiles = defaultAltPits,
			})
		end
		forceDefaultPits = nil
	end)
	
	mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
		if forceDefaultPits ~= nil then
			forceDefaultPits = nil
		end
	end)
end
