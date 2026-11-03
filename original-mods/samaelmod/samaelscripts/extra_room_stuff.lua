local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local function IsExtraRoom(idx)
	if mod.IsFragmentRoom and mod.IsDeathDealRoom then
		return mod:IsFragmentRoom(idx) or mod:IsDeathDealRoom(idx)
	end
end

local function IsConnectedToExtraRoom(doorSlot)
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	local adjIdx = mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot)
	return adjIdx and IsExtraRoom(adjIdx)
end

function mod:ReserveRoom()
	local level = game:GetLevel()
	
	lib.Log("Started the process of reserving an off-the-grid room...")
	
	local fallbacks = {}
	
	for idx = 0, 168 do
		local roomDesc = level:GetRoomByIdx(idx)
		
		if roomDesc.GridIndex == -1 then
			local adj = {}
			-- Left
			if idx % 13 > 0 then
				table.insert(adj, {
					Idx = idx - 1,
					Door = DoorSlot.RIGHT0,
				})
			end
			-- Up
			if idx >= 13 then
				table.insert(adj, {
					Idx = idx - 13,
					Door = DoorSlot.DOWN0,
				})
			end
			-- Right
			if idx % 13 < 12 then
				table.insert(adj, {
					Idx = idx + 1,
					Door = DoorSlot.LEFT0,
				})
			end
			-- Down
			if idx < 156 then
				table.insert(adj, {
					Idx = idx + 13,
					Door = DoorSlot.UP0,
				})
			end
			
			local paramsToTry = {}
			local possibleFallbacks = {}
			local hasAdjacentRoom = false
			local canBeFallback = true
			
			for _, tab in pairs(adj) do
				local adjRoomDesc = level:GetRoomByIdx(tab.Idx)
				if not hasAdjacentRoom and adjRoomDesc.GridIndex == -1 then
					table.insert(paramsToTry, tab)
				end
				if canBeFallback and adjRoomDesc.GridIndex > -1 then
					hasAdjacentRoom = true
					if adjRoomDesc.Data.Shape ~= RoomShape.ROOMSHAPE_1x1 then
						canBeFallback = false
					else
						table.insert(possibleFallbacks, tab)
					end
				end
			end
			
			if not hasAdjacentRoom then
				for _, tab in pairs(paramsToTry) do
					lib.Log("Trying to make a red room from GridIndex " .. tab.Idx .. " via door " .. tab.Door)
					if level:MakeRedRoomDoor(tab.Idx, tab.Door) then
						lib.Log("Success! Reserved GridIndex: " .. idx)
						return idx
					end
					lib.Log("Didn't work.")
				end
			elseif canBeFallback then
				for _, tab in pairs(possibleFallbacks) do
					if not fallbacks[idx] then
						fallbacks[idx] = {}
					end
					table.insert(fallbacks[idx], tab)
				end
			end
		end
	end
	
	lib.LogErr("Failed to reserve an unused GridIndex without any adjacent rooms.")
	
	for idx, list in pairs(fallbacks) do
		for _, tab in pairs(list) do
			if level:MakeRedRoomDoor(tab.Idx, tab.Door) then
				return idx
			end
		end
	end
	
	lib.LogErr("Completely failed to reserve an unused GridIndex.")
end

function mod:ExtraRoomUpdate()
	local room = game:GetRoom()
	
	-- No Doors connecting to my special rooms.
	for i=0, 7 do
		local door = room:GetDoor(i)
		if door then
			if IsExtraRoom() or IsConnectedToExtraRoom(i) then
				room:RemoveDoor(i)
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.ExtraRoomUpdate)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.ExtraRoomUpdate)

function mod:NoRedRoomDoors(eff)
	if IsExtraRoom() then
		eff:Remove()
	elseif not eff:GetData().samaelCheckedConnectedToExtraRoom then
		local room = game:GetRoom()
		for i=0, 7 do
			if room:IsDoorSlotAllowed(i) and room:GetDoorSlotPosition(i):Distance(eff.Position) < 5 and IsConnectedToExtraRoom(i) then
				eff:Remove()
				return
			end
		end
		eff:GetData().samaelCheckedConnectedToExtraRoom = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.NoRedRoomDoors, EffectVariant.DOOR_OUTLINE)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.NoRedRoomDoors, EffectVariant.DOOR_OUTLINE)

local function HideGridIndex(idx)
	if idx then
		local level = game:GetLevel()
		local roomDesc = level:GetRoomByIdx(idx)
		if roomDesc.DisplayFlags ~= 0 then
			roomDesc.DisplayFlags = 0
			level:UpdateVisibility()
		end
	end
end

function mod:HideMap()
	local level = game:GetLevel()
	local data = mod:GetAllFloorData()
	
	if IsExtraRoom() then
		if level:GetCurses() & LevelCurse.CURSE_OF_THE_LOST == 0 then
			level:AddCurse(LevelCurse.CURSE_OF_THE_LOST)
			data.HidMap = true
		end
	elseif data.HidMap then
		level:RemoveCurses(LevelCurse.CURSE_OF_THE_LOST)
		level:UpdateVisibility()
		data.HidMap = false
		data.UpdateVisibility = true
	elseif data.UpdateVisibility then
		level:UpdateVisibility()
		data.UpdateVisibility = false
	end
	
	HideGridIndex(mod:GetDeathDealData().DeathDealRoomIndex)
	HideGridIndex(mod:GetFragmentData().FragmentEntranceIndex)
	HideGridIndex(mod:GetFragmentData().FragmentRoomIndex)
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.HideMap)

if MinimapAPI then
	MinimapAPI:RemoveDisplayFlagsCallbacks(mod.Name)
	MinimapAPI:AddDisplayFlagsCallback(mod, function(self, room, dflags)
		if room.Descriptor and IsExtraRoom(room.Descriptor.GridIndex) then
			return 0
		end
	end)
end
