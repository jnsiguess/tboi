local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local XIII = mod.ITEMS.XIII

local kMinDeathDealRoomIdx = 61700
local kMaxDeathDealRoomIdx = 61727

local deathTeleFrames = nil
local deathDealPlayer = nil

local function GetDeathDealPlayer()
	local player = deathDealPlayer
	if player and player:Exists() then
		return player
	end
	return Isaac.GetPlayer(0)
end

function mod:GetDeathDealData()
	return mod:GetFloorData("DEATH_DEAL_" .. lib.GetDimension())
end

function mod:IsDeathDealRoom(idx)
	local roomData
	if idx then
		roomData = game:GetLevel():GetRoomByIdx(idx).Data
	else
		roomData = game:GetLevel():GetCurrentRoomDesc().Data
	end
	return roomData and roomData.Name:find("(Samael)") and roomData.Name:find("Death Deal")
end

local function GetPlayersItems(player)
	local itemConfig = Isaac.GetItemConfig()
	local playersItems = {}
	
	local players = {}
	if player then
		table.insert(players, player)
	else
		players = lib.GetPlayers()
	end
	
	for i=1, itemConfig:GetCollectibles().Size-1 do
		local item = itemConfig:GetCollectible(i)
		if item and not item:HasTags(ItemConfig.TAG_QUEST) then
			for _, p in pairs(players) do
				if not p:GetData().MaliceMinion and lib.HasItem(p, i, true)
						and p:GetActiveItem(ActiveSlot.SLOT_POCKET) ~= i
						and p:GetActiveItem(ActiveSlot.SLOT_POCKET2) ~= i then
					table.insert(playersItems, i)
					break
				end
			end
		end
	end
	
	return playersItems
end

local function GetDeathDealRoomIndex()
	local data = mod:GetDeathDealData()
	
	if not data.DeathDealRoomIndex then
		for idx = 0, 168 do
			if mod:IsDeathDealRoom(idx) then
				lib.Log("Found existing Death Deal room @ " .. idx)
				data.DeathDealRoomIndex = idx
				break
			end
		end
		if not data.DeathDealRoomIndex then
			local idx = mod:ReserveRoom()
			if idx then
				data.DeathDealRoomIndex = idx
				lib.Log("Claimed GridIndex " .. idx .. " for the Death Deal room.")
			else
				data.DeathDealRoomIndex = -1
			end
		end
	end
	
	return data.DeathDealRoomIndex
end

local function GoToDeathDealRoom()
	local level = game:GetLevel()
	local deathIdx = GetDeathDealRoomIndex()
	
	if not mod:IsDeathDealRoom(deathIdx) then
		local prevIdx = mod:GetLastKnownGridIndex()
		local rng = GetDeathDealPlayer():GetCardRNG(XIII)
		local roomID = kMinDeathDealRoomIdx + rng:RandomInt((kMaxDeathDealRoomIdx - kMinDeathDealRoomIdx) + 1)
		local result = Isaac.ExecuteCommand("goto s.default." .. roomID)
		if result == "Error changing room." then
			lib.LogErr("Failed to load Death Deal room!")
			return
		end
		
		lib.CopyGotoRoomData(deathIdx, RoomDescriptor.FLAG_PORTAL_LINKED, 617)
		game:ChangeRoom(prevIdx)
	end
	game:StartRoomTransition(deathIdx, -1, RoomTransitionAnim.FADE)
	mod:FadeIn()
end

function mod:Xiii(_, player, useFlags)
	local level = game:GetLevel()
	local data = mod:GetDeathDealData()
	
	if mod:IsDeathDealRoom() then
		for _, pedestal in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, 0)) do
			pedestal:Remove()
		end
		data.GenerateNewDeals = true
		return
	end
	
	local dimension = lib.GetDimension()
	if dimension ~= 0 and not (dimension == 1 and not game:IsGreedMode() and (level:GetStage() == LevelStage.STAGE1_1 or level:GetStage() == LevelStage.STAGE1_2)) then
		lib.RefundInvalidCardUse(player, XIII, useFlags, SoundEffect.SOUND_DEATH)
		return false
	end
	
	local deathIdx = GetDeathDealRoomIndex()
	
	if not deathIdx or deathIdx < 0 then
		lib.RefundInvalidCardUse(player, XIII, useFlags, SoundEffect.SOUND_DEATH)
		return false
	end
	
	data.GenerateNewDeals = true
	data.ReturnIndex = mod:GetLastKnownGridIndex()
	deathDealPlayer = player
	
	deathTeleFrames = 0
	for _, p in pairs(lib.GetPlayers()) do
		p.ControlsEnabled = false
		p:AnimateTeleport(true)
		p.Velocity = lib.ZeroVector
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_CARD, CallbackPriority.EARLY, mod.Xiii, XIII)

local function GetDeathDealReturnIndex()
	local idx = mod:GetDeathDealData().ReturnIndex or mod:GetLastKnownGridIndex()
	if mod:IsDeathDealRoom(idx) then
		idx = game:GetLevel():GetStartingRoomIndex()
	end
	return idx
end

local xiiiPortal

function mod:XiiiPortalUpdate(portal)
	if portal.SubType < 1000 or not mod:IsDeathDealRoom() then return end
	
	if (not xiiiPortal or not xiiiPortal:Exists()) then
		xiiiPortal = portal
	elseif xiiiPortal and GetPtrHash(xiiiPortal) ~= GetPtrHash(portal) then
		portal:Remove()
		return
	end
	
	local sprite = portal:GetSprite()
	local anim = sprite:GetAnimation()
	local shouldClose = #Isaac.FindByType(mod.ENTITIES.DEAL_PARENT_DUMMY.ID, mod.ENTITIES.DEAL_PARENT_DUMMY.Var, mod.ENTITIES.DEAL_PARENT_DUMMY.Sub) > 0
	
	if shouldClose and (sprite:IsFinished("Close Animation") or sprite:IsFinished("Disappear") or anim == "Closed" or anim == "Open Animation" or anim == "Appear") then
		portal:Remove()
		return
	end
	
	if shouldClose and sprite:GetAnimation() == "Opened" then
		sprite:Play("Close Animation", true)
	end
	
	local returnIdx = GetDeathDealReturnIndex()
	local subType = 1000 + returnIdx
	if portal.SubType ~= subType then
		portal.SubType = subType
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.XiiiPortalUpdate, EffectVariant.PORTAL_TELEPORT)

local clearFrames = 0

local function XiiiPortalHandler()
	local room = game:GetRoom()
	
	local isClear = #Isaac.FindByType(mod.ENTITIES.DEAL_PARENT_DUMMY.ID, mod.ENTITIES.DEAL_PARENT_DUMMY.Var, mod.ENTITIES.DEAL_PARENT_DUMMY.Sub) == 0
	if room:GetFrameCount() < 30 then
		isClear = false
	end
	
	if isClear then
		clearFrames = clearFrames + 1
	else
		clearFrames = 0
	end
	
	local positions = {}
	
	if isClear and clearFrames > 30 and (not xiiiPortal or not xiiiPortal:Exists()) then
		for i=0, 7 do
			if room:IsDoorSlotAllowed(i) then
				table.insert(positions, room:GetDoorSlotPosition(i))
			end
		end
		
		local pos = room:GetCenterPos()
		if #positions > 0 then
			pos = lib.PickRandom(positions) or pos
		end
		local freePos = room:FindFreePickupSpawnPosition(pos, 0, true, false)
		
		local returnIdx = GetDeathDealReturnIndex()
		xiiiPortal = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PORTAL_TELEPORT, 1000 + returnIdx, freePos, lib.ZeroVector, nil)
		xiiiPortal:GetSprite():Play("Open Animation", true)
	end
end

function mod:XiiiUpdate()
	local level = game:GetLevel()
	local data = mod:GetDeathDealData()
	
	if deathTeleFrames then
		deathTeleFrames = deathTeleFrames + 1
		if deathTeleFrames >= 15 then
			deathTeleFrames = nil
			for _, p in pairs(lib.GetPlayers()) do
				p.ControlsEnabled = true
			end
			GoToDeathDealRoom()
			return
		end
	end
	
	if mod:IsDeathDealRoom() then
		if game:GetRoom():GetFrameCount() == 1 then
			mod.MusicManager:Play(Music.MUSIC_GAME_OVER, 1.0)
			mod.MusicManager:UpdateVolume()
		end
		
		XiiiPortalHandler()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.XiiiUpdate)

function mod:DeathDealTaken(player, collectibleType)
	if collectibleType then
		local pos = player.Position + Vector(0, -player.Size)
		mod:AddFadingSpriteForItem(collectibleType, pos)
		sfxManager:Play(SoundEffect.SOUND_DOOR_HEAVY_OPEN)
		sfxManager:Play(160, 2.0, 0, false, 0.95)
		player:SetColor(Color(1,1,1,1,1,1,1), 20, 1, true, true)
	end
	
	mod:GetDeathDealData().Deals = nil
end

local function FindDeathDealEntities()
	local dealEntities = Isaac.FindByType(mod.ENTITIES.DEATH_DEAL.ID, mod.ENTITIES.DEATH_DEAL.Var, mod.ENTITIES.DEATH_DEAL.Sub)
	
	if #dealEntities > 0 then
		return dealEntities
	end
	
	local room = game:GetRoom()
	local roomWidth = room:GetGridWidth()
	local spawns = game:GetLevel():GetCurrentRoomDesc().Data.Spawns
	
	for i = 0, spawns.Size - 1 do
		local spawn = spawns:Get(i)
		if spawn then
			local gridIdx = roomWidth + 1 + (spawn.X + roomWidth * spawn.Y)
			local pos = room:GetGridPosition(gridIdx)
			local entry = spawn:PickEntry(0)
			
			if entry.Type == mod.ENTITIES.DEATH_DEAL.ID and entry.Variant == mod.ENTITIES.DEATH_DEAL.Var and entry.Subtype == mod.ENTITIES.DEATH_DEAL.Sub then
				local deal = lib.Spawn(mod.ENTITIES.DEATH_DEAL, pos):ToNPC()
				table.insert(dealEntities, deal)
			end
		end
	end
	
	return dealEntities
end

function mod:FindReaperStatue()
	local search = Isaac.FindByType(mod.ENTITIES.REAPER_STATUE.ID, mod.ENTITIES.REAPER_STATUE.Var, mod.ENTITIES.REAPER_STATUE.Sub)
	
	if search[1] then
		return search[1]
	end
	
	local room = game:GetRoom()
	local roomWidth = room:GetGridWidth()
	local spawns = game:GetLevel():GetCurrentRoomDesc().Data.Spawns
	
	for i = 0, spawns.Size - 1 do
		local spawn = spawns:Get(i)
		if spawn then
			local gridIdx = roomWidth + 1 + (spawn.X + roomWidth * spawn.Y)
			local pos = room:GetGridPosition(gridIdx)
			local entry = spawn:PickEntry(0)
			if entry.Type == 999 and entry.Variant == mod.ENTITIES.REAPER_STATUE.Var and mod.ENTITIES.REAPER_STATUE.Sub then
				return Isaac.Spawn(mod.ENTITIES.REAPER_STATUE.ID, mod.ENTITIES.REAPER_STATUE.Var, mod.ENTITIES.REAPER_STATUE.Sub, pos, lib.ZeroVector, nil)
			end
		end
	end
end

local function TarotClothActive()
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasCollectible(CollectibleType.COLLECTIBLE_TAROT_CLOTH) then
			return true
		end
	end
	return false
end

local function GenerateNewDeals(numDeals)
	local player = GetDeathDealPlayer()
	local tarotClothActive = TarotClothActive()
	local itemConfig = Isaac.GetItemConfig()
	
	local data = mod:GetDeathDealData()
	data.Deals = nil
	
	local playersItems = GetPlayersItems(player)
	
	if #playersItems == 0 then
		playersItems = GetPlayersItems()
		if #playersItems == 0 then
			return
		end
	end
	
	if tarotClothActive then
		local seed = player:GetCardRNG(XIII):Next()
		table.sort(playersItems, function(a,b)
			local qa = math.max(itemConfig:GetCollectible(a).Quality, 2)
			local qb = math.max(itemConfig:GetCollectible(b).Quality, 2)
			if qa ~= qb then
				return qa < qb
			end
			return a % seed < b % seed
		end)
	else
		lib.Shuffle(playersItems, player:GetCardRNG(XIII))
	end
	
	local newDeals = {}
	
	local itemPool = game:GetItemPool()
	local greedMode = game:IsGreedMode()
	
	for i=1, numDeals do
		local requiredItem = playersItems[i] or playersItems[1]
		
		--[[local pool
		
		if i == 1 then
			if greedMode then
				pool = ItemPoolType.POOL_GREED_ANGEL
			else
				pool = ItemPoolType.POOL_ANGEL
			end
		elseif i == 2 then
			if greedMode then
				pool = ItemPoolType.POOL_GREED_DEVIL
			else
				pool = ItemPoolType.POOL_DEVIL
			end
		end
		
		local offeredItem
		if pool then
			offeredItem = itemPool:GetCollectible(pool, true, rng:Next())
			
			if mod:IsLockedModItem(offeredItem) then
				offeredItem = mod:GetDeathPoolItem()
			end
		else
			offeredItem = mod:GetDeathPoolItem()
		end]]
		
		local minQuality = nil
		if tarotClothActive then
			minQuality = math.min(math.max(2, itemConfig:GetCollectible(requiredItem).Quality), 3)
		end
		local offeredItem = mod:GetDeathPoolItem(minQuality)
		
		table.insert(newDeals, {
			Offered = offeredItem,
			Required = requiredItem,
			--Pool = pool,
		})
	end
	
	data.Deals = newDeals
end

-- Initialize, or reload, the deals within a Death Deal room.
local function InitDeathDeals()
	local dealEntities = FindDeathDealEntities()
	if #dealEntities == 0 then
		lib.LogErr("No deal markers found in death deal room!")
		return
	end
	local statue = mod:FindReaperStatue()
	
	local data = mod:GetDeathDealData()
	
	if data.GenerateNewDeals then
		data.GenerateNewDeals = false
		GenerateNewDeals(#dealEntities)
		
		if not data.Deals or #data.Deals == 0 then
			for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE)) do
				if ent.SubType > 0 and ent:ToPickup().Price == 0 then
					return
				end
			end
			-- Give a free item
			local itemPos = lib.ZeroVector
			for i, entity in ipairs(dealEntities) do
				itemPos = itemPos + entity.Position
			end
			itemPos = itemPos / #dealEntities
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, mod:GetDeathPoolItem() or 0, itemPos, lib.ZeroVector, nil)
			return
		end
	end
	
	if data.Deals and #data.Deals > 0 then
		local deathDealDummy = Isaac.Spawn(mod.ENTITIES.DEAL_PARENT_DUMMY.ID, mod.ENTITIES.DEAL_PARENT_DUMMY.Var, mod.ENTITIES.DEAL_PARENT_DUMMY.Sub, lib.ZeroVector, lib.ZeroVector, nil)
		for i, entity in pairs(dealEntities) do
			local deal = data.Deals[i]
			if deal then
				mod:InitDeathDeal(entity, deal.Required, deal.Offered, deal.Pool, deathDealDummy)
				entity:GetData().deathDeal = deal
			else
				lib.LogErr("No corresponding Death Deal for deal entity.")
				entity:Remove()
			end
			
			local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, entity.Position - Vector(0, 10), lib.ZeroVector, nil)
			poof.Color = Color(0,0,0,0.5)
			poof.DepthOffset = 25
		end
	end
	
	--[[for _, entity in pairs(dealEntities) do
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, entity.Position - Vector(0, 10), lib.ZeroVector, nil)
		poof.Color = Color(0,0,0,0.5)
		poof.DepthOffset = 25
	end]]
end

function mod:RefreshDeals()
	local data = mod:GetDeathDealData()
	if game:GetRoom():GetFrameCount() > 10 and mod:IsDeathDealRoom() and data.GenerateNewDeals then
		InitDeathDeals()
		data.GenerateNewDeals = false
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.RefreshDeals)

function mod:ReaperStatueInit(entity)
	if entity.SubType == mod.ENTITIES.REAPER_STATUE.Sub then
		entity:GetSprite():Stop()
		entity:GetSprite():SetFrame(game:GetRoom():GetDecorationSeed() % 4)
		Isaac.GridSpawn(GridEntityType.GRID_WALL, 0, entity.Position, true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.ReaperStatueInit, mod.ENTITIES.REAPER_STATUE.Var)

-- Initialize the death deal room when entered.
function mod:DeathRoom()
	local level = game:GetLevel()
	local room = game:GetRoom()
	local roomDesc = level:GetCurrentRoomDesc()
	local roomData = roomDesc.Data
	
	if mod:IsDeathDealRoom() then
		game:ShowHallucination(100, roomData.Subtype)
		
		if room:IsFirstVisit() then
			for i=0, Random() % 6 do
				Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.WISP, 0, room:GetRandomPosition(10), lib.ZeroVector, nil)
			end
		end
		
		InitDeathDeals()
		
		lib.RefreshGrids()
		if lib.InStageApiFloor() then
			lib.ScheduleForUpdate(lib.RefreshGrids)
		end
	end
	
	deathTeleFrames = nil
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.DeathRoom)

function mod:DeathDealDealParentDummy(dummy)
	if dummy.SubType ~= mod.ENTITIES.DEAL_PARENT_DUMMY.Sub then return end
	
	if #Isaac.FindByType(mod.ENTITIES.DEATH_DEAL.ID, mod.ENTITIES.DEATH_DEAL.Var, mod.ENTITIES.DEATH_DEAL.Sub) == 0 then
		dummy:Remove()
	end
	
	--[[local room = game:GetRoom()
	
	-- Keep doors closed.
	for i=0, 7 do
		local door = room:GetDoor(i)
		if door then
			if door:IsOpen() then
				door:Close(true)
				door:Bar()
			elseif door:CanBlowOpen() then
				door:TryBlowOpen(true, Isaac.GetPlayer(0))
			end
		end
	end
	room:KeepDoorsClosed()]]
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.DeathDealDealParentDummy, mod.ENTITIES.DEAL_PARENT_DUMMY.Var)

function mod:XiiiPlayerDamage(player, damage, damageFlags, damageSourceRef)
	if deathTeleFrames and deathTeleFrames > 0 then
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.XiiiPlayerDamage, EntityType.ENTITY_PLAYER)

local function ShouldUseAltSkin(entity, backdrop)
	return not entity:GetData().samaelChangedSprite and (
		(mod:IsDeathDealRoom() and game:GetLevel():GetCurrentRoomDesc().Data.Subtype == backdrop)
		or (mod:IsFragmentRoom() and game:GetRoom():GetBackdropType() == backdrop))
end

function mod:DeathDealCarpet(entity)
	local data = entity:GetData()
	if ShouldUseAltSkin(entity, BackdropType.BARREN) then
		entity:GetSprite():ReplaceSpritesheet(0, "gfx/effects/isaaccarpet_barren.png")
		entity:GetSprite():LoadGraphics()
		data.samaelChangedSprite = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.DeathDealCarpet, EffectVariant.ISAACS_CARPET)

function mod:DeathDealBed(entity)
	local data = entity:GetData()
	if ShouldUseAltSkin(entity, BackdropType.BARREN) then
		entity:GetSprite():ReplaceSpritesheet(0, "gfx/items/pick ups/isaacbed_barren.png")
		entity:GetSprite():LoadGraphics()
		data.samaelChangedSprite = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.DeathDealBed, PickupVariant.PICKUP_BED)

function mod:DeathDealDirt(entity)
	local data = entity:GetData()
	if ShouldUseAltSkin(entity, BackdropType.DARKROOM) then
		entity:GetSprite():ReplaceSpritesheet(0, "gfx/effects/dirtpatch_darkroom.png")
		entity:GetSprite():LoadGraphics()
		data.samaelChangedSprite = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.DeathDealDirt, EffectVariant.DIRT_PATCH)
