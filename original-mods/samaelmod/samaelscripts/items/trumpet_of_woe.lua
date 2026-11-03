local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local TRUMPET_OF_WOE = mod.ITEMS.TRUMPET_OF_WOE

local TRUMPET_SOUND = Isaac.GetSoundIdByName("SamaelTrumpetOfWoe")
local TUMPET_SOUND = Isaac.GetSoundIdByName("SamaelTumpetOfWoe")

local function SpawnLocust(player, pos)
	local locustType = (Random() % 5) + 1
	local n = 1
	if locustType == 5 then
		n = n + Random() % 4
	end
	for i=1, n do
		local fly = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FamiliarVariant.BLUE_FLY, locustType, pos, lib.ZeroVector, player):ToFamiliar()
		fly.Parent = player
		fly:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		local enemies = Isaac.FindInRadius(player.Position, 9999, EntityPartition.ENEMY)
		if #enemies > 0 then
			fly.Target = enemies[(Random() % #enemies)+1]
		end
		fly:SetColor(lib.InvisibleColor, 60, 1, true, false)
	end
end

local function GetRandomRoomEdgePos(rng)
	local room = game:GetRoom()
	
	local padding = 300
	local center = room:GetCenterPos()
	local topLeft = room:GetTopLeftPos()
	topLeft = topLeft + (topLeft - center):Resized(padding)
	local bottomRight = room:GetBottomRightPos()
	bottomRight = bottomRight + (bottomRight - center):Resized(padding)
	local topRight = Vector(bottomRight.X, topLeft.Y)
	local bottomLeft = Vector(topLeft.X, bottomRight.Y)
	
	local wallOptions = {
		{topLeft, topRight},
		{topRight, bottomRight},
		{bottomRight, bottomLeft},
		{bottomLeft, topLeft},
	}
	
	local wall = wallOptions[rng:RandomInt(#wallOptions)+1]
	
	return lib.Lerp(wall[1], wall[2], rng:RandomFloat())
end

--[[local function SpawnPurgatoryGhosts(player)
	for i=1, 8 do
		lib.ScheduleForUpdate(function()
			local pos = room:GetRandomPosition(20)
			Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PURGATORY, 1, pos, lib.ZeroVector, player):ToEffect()
		end, i * 3)
	end
end

local function SpawnHungrySouls(player)
	for i=1, 8 do
		lib.ScheduleForUpdate(function()
			local pos = room:GetRandomPosition(20)
			local soul = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HUNGRY_SOUL, 0, pos, lib.ZeroVector, player):ToEffect()
			soul.LifeSpan = 180
			soul.Timeout = 100 + Random() % 80
		end, i * 3)
	end
end]]

local function SpawnFiendFolioReapers(player, rng)
	local pos = GetRandomRoomEdgePos(rng)
	local flankOffset = (room:GetCenterPos() - pos):Resized(130)
	local gang = {
		{"Reaper", pos},
		{"ScytheRider", pos + flankOffset:Rotated(130)},
		{"ScytheRider", pos + flankOffset:Rotated(-130)},
	}
	for _, tab in pairs(gang) do
		local guy = mod.THANATOPHILIA_MINIONS[tab[1]]:Spawn(player)
		guy:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		guy:Update()
		guy.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
		guy:GetData().trumpetOfWoeEntry = true
		guy.Position = tab[2]
		guy:SetColor(lib.InvisibleColor, 60, 1, true, false)
	end
end

function mod:TrumpetOfWoe(_, rng, player, useFlags, slot)
	local room = game:GetRoom()
	local data = mod:GetPersistentPlayerData(player)
	
	local woe = data.trumpetWoe or 0
	
	if woe % 3 == 0 then
		for i=1, 13 do
			SpawnLocust(player, GetRandomRoomEdgePos(rng))
		end
	elseif woe % 3 == 1 then
		if FiendFolio and rng:RandomInt(3) > 0 then
			SpawnFiendFolioReapers(player, rng)
		else
			for i=1, 3 do
				local miniReaper = Isaac.Spawn(mod.ENTITIES.MINI_REAPER.ID, mod.ENTITIES.MINI_REAPER.Var, 2, GetRandomRoomEdgePos(rng), lib.ZeroVector, player):ToFamiliar()
				miniReaper:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
				miniReaper.Coins = 30*30 -- 30 seconds
				miniReaper:Update()
				miniReaper:SetColor(lib.InvisibleColor, 30, 1, true, false)
			end
		end
	else--if woe % 3 == 2 then
		mod:TriggerSpinningScythes(player, 3)
		local alreadySpawned = false
		for _, death in pairs(Isaac.FindByType(EntityType.ENTITY_BEAST, 40)) do
			if death:GetData().isThanatophiliaMinion then
				alreadySpawned = true
				break
			end
		end
		if not alreadySpawned then
			local thebigguy = mod.THANATOPHILIA_MINIONS.UltraDeath:Spawn(player)
			thebigguy.State = 16  -- Spawn doing the slash attack.
			thebigguy:SetColor(lib.InvisibleColor, 20, 1, true, false)
		end
	end
	
	data.trumpetWoe = (woe + 1) % 3
	
	local sound = (rng:RandomFloat() <= 0.01) and TUMPET_SOUND or TRUMPET_SOUND
	
	if useFlags & UseFlag.USE_CARBATTERY ~= 0 then
		lib.ScheduleForUpdate(function()
			sfxManager:Play(sound)
		end, 20)
	else
		sfxManager:Play(sound)
	end
	
	return {ShowAnim = true}
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, CallbackPriority.EARLY, mod.TrumpetOfWoe, TRUMPET_OF_WOE)

function mod:ScytheRiderUpdate(npc)
	local room = game:GetRoom()
	local data = npc:GetData()
	
	if data.trumpetOfWoeEntry and (
			(npc.Type == FiendFolio.FF.ScytheRider.ID and npc.Variant == FiendFolio.FF.ScytheRider.Var)
			or (npc.Type == FiendFolio.FF.Reaper.ID and npc.Variant == FiendFolio.FF.Reaper.Var)) then
		if room:IsPositionInRoom(npc.Position, npc.Size) then
			npc.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
			data.trumpetOfWoeEntry = nil
		else
			local data = npc:GetData()
			data.state = "idle"
			data.scythestate = 1
			npc.StateFrame = 0
			local targetVel = (room:GetCenterPos() - npc.Position):Resized(6.66)
			npc.Velocity = lib.Lerp(npc.Velocity, targetVel, 0.1)
		end
	end
	
	if room:GetFrameCount() == 0 and npc.Type == FiendFolio.FF.RiderScythe.ID and npc.Variant == FiendFolio.FF.RiderScythe.Var and FiendFolio:isFriend(npc) then
		npc:Remove()
	end
end
if FiendFolio then
	mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ScytheRiderUpdate)
end
