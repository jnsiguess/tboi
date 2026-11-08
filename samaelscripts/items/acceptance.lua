local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local ACCEPTANCE = mod.ITEMS.ACCEPTANCE
local DENIAL = mod.ITEMS.DENIAL

local SPIRIT_OF_ACCEPTANCE = mod.ENTITIES.SPIRIT_OF_ACCEPTANCE.Var
local ACCEPTANCE_LIGHT = mod.ENTITIES.LIGHT_FROM_ABOVE.Var

local kAcceptanceBonusItemChance = 0.5

local spiritOfAcceptance

-- These items will be forcibly removed and rerolled by Acceptance, unless you have Denial.
local AcceptanceBlacklist = {
	CollectibleType.COLLECTIBLE_THERES_OPTIONS,
	CollectibleType.COLLECTIBLE_MORE_OPTIONS,
	CollectibleType.COLLECTIBLE_OPTIONS,
	CollectibleType.COLLECTIBLE_GUPPYS_EYE,
}

local function GetAcceptanceCount(overriddenByDenial)
	local count = 0
	
	for _, player in pairs(lib.GetPlayers()) do
		if overriddenByDenial and lib.HasItem(player, DENIAL) then
			-- Never hide items with Denial active.
			return 0
		end
		count = count + player:GetCollectibleNum(ACCEPTANCE)
	end
	
	return count
end

-- Only try to add Curse of the Blind 5 times per room at most, to avoid infinite loops in case any
-- other mods try to aggressively remove Curse of the Blind from the player.
local kMaxCurseAttempts = 5

local addedCurseOfBlindThisRoom = false
local addedCurseOfBlindAt
local curseAttempts = 0

function mod:AcceptanceNewRoom()
	addedCurseOfBlindThisRoom = false
	addedCurseOfBlindAt = nil
	curseAttempts = 0
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.AcceptanceNewRoom)

local function ShouldAddCurseOfBlind()
	return curseAttempts < kMaxCurseAttempts and game:GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND == 0
end

function mod:AcceptancePlayerUpdate(player)
	if not lib.HasItem(player, ACCEPTANCE) then return end
	
	-- Add curse of the blind.
	if not lib.HasItem(player, DENIAL) and ShouldAddCurseOfBlind() then
		game:GetLevel():AddCurse(LevelCurse.CURSE_OF_BLIND)
		addedCurseOfBlindThisRoom = true
		addedCurseOfBlindAt = game:GetFrameCount()
		curseAttempts = curseAttempts + 1
	end
	
	-- Grant a bonus item on first pickup.
	if not mod:GetAllRunData().acceptanceBonusItemGranted then
		mod:SpawnBonusItem(ItemPoolType.POOL_TREASURE, game:GetSeeds():GetStartSeed(), player.Position)
		mod:GetAllRunData().acceptanceBonusItemGranted = true
	end
	
	mod:AcceptanceCheckForbiddenItems(player)
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.AcceptancePlayerUpdate)

function mod:AcceptanceCache(player)
	if lib.HasItem(player, ACCEPTANCE) then
		player.Luck = player.Luck + 1
	end
	
	local pData = mod:GetPersistentPlayerData(player)
	if pData.acceptanceRockLuck then
		player.Luck = player.Luck + pData.acceptanceRockLuck
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.AcceptanceCache, CacheFlag.CACHE_LUCK)

-- Forces items to be blind question marks while Acceptance is active, even if Curse of the Blind is somehow removed.
--[[function mod:AcceptanceHideItems(pickup)
	if pickup.SubType < 1 or pickup.Touched then return end
	
	local data = pickup:GetData()
	
	if (ShouldAddCurseOfBlind() or (addedCurseOfBlindAt and game:GetFrameCount() - addedCurseOfBlindAt < 5 and addedCurseOfBlindAt ~= data.hiddenByAcceptanceAt))
			and GetAcceptanceCount(true) > 0 then
		-- Hide the item.
		pickup:GetSprite():ReplaceSpritesheet(1, "gfx/items/collectibles/questionmark.png")
		pickup:GetSprite():LoadGraphics()
		-- Letting our friend EID know that this item is now a question mark ahead of time. :)
		-- This helps avoids some lag with stuff like glitched crown, since identifying that an item is
		-- hidden as a question mark is a slow and expensive operation.
		pickup:GetData()["EID_IsAltChoice"] = true
		data.hiddenByAcceptanceAt = addedCurseOfBlindAt or game:GetFrameCount()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.AcceptanceHideItems, PickupVariant.PICKUP_COLLECTIBLE)]]

---------- CODE FOR SPAWNING ADDITIONAL ITEMS ----------

-- All the below local variables are reset at the end of every update.
-- I have a Process outlined below for identifying newly-spawned items from an item pool.
local itemSpawnedFromPool = {}
local itemSpawnedWithSeed = {}
local seenOptionsPickupIndexes = {}
local bonusItemsToSpawn = {}
local tryToSpawnItems = false
local spawningItems = false
-- Table of non-collectible pickups, for use in identifying when a non-collectible pickup morphs
-- into a collectible (IE, when you get an item from a chest).
local nonCollectiblePickups = {}

function mod:IsMorphedPickup(collectible)
	local pickup = lib.GetOrInit(nonCollectiblePickups, collectible.Position.X)[collectible.Position.Y]
	return (pickup and pickup.Variant == PickupVariant.PICKUP_COLLECTIBLE and pickup.InitSeed == collectible.InitSeed)
end

function mod:PopulateNonCollectiblePickups(pickup)
	if pickup.Variant ~= PickupVariant.PICKUP_COLLECTIBLE and pickup.Position.X == pickup.Position.X and pickup.Position.Y == pickup.Position.Y then
		lib.GetOrInit(nonCollectiblePickups, pickup.Position.X)[pickup.Position.Y] = pickup
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.PopulateNonCollectiblePickups)

-- When the game chooses a collectible from an item pool, keep track of that this happened this frame.
function mod:AcceptancePostChooseItem(itemType, itemPool, decrease, seed)
	if not spawningItems and decrease and itemPool >= 0 then
		itemSpawnedFromPool[itemType] = itemPool
	end
end
mod:AddCallback(ModCallbacks.MC_POST_GET_COLLECTIBLE, mod.AcceptancePostChooseItem)

-- When an item pedestal is about to spawn, keep track of the fact we're spawning an item pedestal with this seed.
function mod:AcceptancePreItemSpawn(entityType, entityVariant, entitySubType, pos, vel, spawner, seed)
	if not spawningItems and entityType == EntityType.ENTITY_PICKUP and entityVariant == PickupVariant.PICKUP_COLLECTIBLE then
		itemSpawnedWithSeed[seed] = true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN, mod.AcceptancePreItemSpawn)

-- Post-init for an item pedestal, if we've previously noticed both MC_POST_GET_COLLECTIBLE and
-- MC_PRE_ENTITY_SPAWN for this same item (same itemType that was chosen, and same entity InitSeed
-- respectively), then this is a newly-spawned item chosen from an item pool.
-- This entire process will circumvent hard-coded item drops not chosen from pools, AND the swapping
-- effect from Tainted Isaac, Soul of Isaac, etc, which notably also triggers MC_POST_PICKUP_INIT.
function mod:AcceptancePostItemInit(pickup)
	local itemType = pickup.SubType
	
	if GetAcceptanceCount() > 0 and not spawningItems and itemSpawnedFromPool[itemType] and itemSpawnedFromPool[itemType] >= 0 and (itemSpawnedWithSeed[pickup.InitSeed] or mod:IsMorphedPickup(pickup)) then
		local pool = itemSpawnedFromPool[itemType]
		if not bonusItemsToSpawn[pool] then
			bonusItemsToSpawn[pool] = {}
		end
		-- Queue up that we want to spawn an item from this pool at this position.
		-- We can't spawn the item right now because FindFreePickupSpawnPosition won't work properly.
		-- (It would place the new item on the exact same spot as the current one, probably because its
		-- MC_POST_PICKUP_INIT hasn't finished yet.)
		bonusItemsToSpawn[pool][pickup.InitSeed] = pickup
		tryToSpawnItems = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.AcceptancePostItemInit, PickupVariant.PICKUP_COLLECTIBLE)

---------- HANDLING SHOP/DEAL ITEMS ----------

local buyableItems = {}

function mod:AcceptanceBuyableItemUpdate(pickup)
	if GetAcceptanceCount() > 0 and pickup.Price ~= 0 then
		local tab = lib.GetOrInit(buyableItems, pickup.InitSeed)
		tab.Ref = pickup
		tab.Type = pickup.SubType
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.AcceptanceBuyableItemUpdate, PickupVariant.PICKUP_COLLECTIBLE)

function mod:AcceptanceBuyableItemReset()
	buyableItems = {}
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.AcceptanceBuyableItemReset)

local function AnyPlayerIsHoldingUpItem(collectibleType)
	for _, player in pairs(lib.GetPlayers()) do
		if player:IsHoldingItem() and player.QueuedItem and player.QueuedItem.Item and player.QueuedItem.Item.ID == collectibleType then
			return true
		end
	end
end

---------- ACTUALLY SPAWNING BONUS ITEMS ----------

function mod:SpawnBonusItem(pool, seed, pos)
	spawningItems = true
	
	local newItem = game:GetItemPool():GetCollectible(pool, true, seed)
	if newItem > 0 then
		local spawnPos = game:GetRoom():FindFreePickupSpawnPosition(pos)
		local pickup = Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, newItem, spawnPos, lib.ZeroVector, nil)
		pickup:SetColor(Color(1,1,1,1,1,1,1), 20, 1, true, true)
		
		-- The spirit itself
		if not spiritOfAcceptance or not spiritOfAcceptance:Exists() then
			spiritOfAcceptance = Isaac.Spawn(EntityType.ENTITY_EFFECT, SPIRIT_OF_ACCEPTANCE, 0, spawnPos + Vector(0,5), lib.ZeroVector, nil):ToEffect()
		end	
		
		-- Light from above
		Isaac.Spawn(EntityType.ENTITY_EFFECT, ACCEPTANCE_LIGHT, 0, spawnPos, lib.ZeroVector, pickup):ToEffect()
		
		-- Sounds
		sfxManager:Play(SoundEffect.SOUND_THUMBSUP)
		sfxManager:Play(SoundEffect.SOUND_HOLY, 1, 0, false, 1.1)
		
		if pickup:HasEntityFlags(EntityFlag.FLAG_ITEM_SHOULD_DUPLICATE) then
			pickup:ClearEntityFlags(EntityFlag.FLAG_ITEM_SHOULD_DUPLICATE)
		end
		pickup:GetData().DamoclesDuplicate = true
	end
	
	spawningItems = false
end

-- Perform any item spawns that were queued up by the above processes.
-- The `spawningItems` bit prevents any of the above processes from triggering again this update.
function mod:AcceptancePostUpdate()
	local numAcceptance = GetAcceptanceCount()
	
	if numAcceptance > 0 then
		-- Bonus items for spawned items.
		if tryToSpawnItems then
			for pool, tab in pairs(bonusItemsToSpawn) do
				for seed, parentPickup in pairs(tab) do
					-- Only spawn one item at most from a choice group.
					local validOptionsIndex = (parentPickup.OptionsPickupIndex == 0 or not seenOptionsPickupIndexes[parentPickup.OptionsPickupIndex])
					local rng = RNG()
					rng:SetSeed(seed, 1)
					if parentPickup.Price == 0 and validOptionsIndex then
						for i=1, numAcceptance do
							if rng:RandomFloat() < kAcceptanceBonusItemChance then
								mod:SpawnBonusItem(pool, seed, parentPickup.Position)
								seenOptionsPickupIndexes[parentPickup.OptionsPickupIndex] = true
							end
						end
					end
				end
			end
		end
		
		-- Bonus items for purchased items.
		for initSeed, tab in pairs(buyableItems) do
			local pickup = tab.Ref
			local itemType = tab.Type
			if not pickup or not pickup:Exists() or pickup.Variant ~= PickupVariant.PICKUP_COLLECTIBLE or initSeed ~= pickup.InitSeed or pickup.Price == 0 then
				local rng = RNG()
				rng:SetSeed(initSeed, 1)
				if AnyPlayerIsHoldingUpItem(itemType) then
					for i=1, numAcceptance do
						if rng:RandomFloat() < kAcceptanceBonusItemChance then
							-- Lazily re-using item pool info I'm tracking in `denial.lua`.
							local itemPoolInfo = lib.GetOrInit(mod:GetFloorData("DenialItemPools"), ""..game:GetLevel():GetCurrentRoomDesc().ListIndex)
							local pool = itemPoolInfo[""..pickup.InitSeed] or lib.GetCurrentRoomItemPool()
							mod:SpawnBonusItem(pool, pickup.InitSeed, pickup.Position)
						end
					end
				end
				buyableItems[initSeed] = nil
			end
		end
	end
	
	itemSpawnedFromPool = {}
	itemSpawnedWithSeed = {}
	seenOptionsPickupIndexes = {}
	bonusItemsToSpawn = {}
	tryToSpawnItems = false
	nonCollectiblePickups = {}
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AcceptancePostUpdate)

---------- CODE FOR REROLLING BLACKLISTED ITEMS ----------

local droppedItemEffect = Isaac.GetEntityVariantByName("Samael Dropped Item")
function mod:AcceptanceCheckForbiddenItems(player)
	if not lib.HasItem(player, ACCEPTANCE) or lib.HasItem(player, DENIAL) then return end
	
	local removedItems = false
	
	for _, blacklistedItem in pairs(AcceptanceBlacklist) do
		if lib.HasItem(player, blacklistedItem, true) then
			while player:GetCollectibleNum(blacklistedItem, true) > 0 do
				player:RemoveCollectible(blacklistedItem, true)
				local vel = RandomVector()*5
				
				local droppedItem = Isaac.Spawn(EntityType.ENTITY_EFFECT, droppedItemEffect, 0, player.Position, vel, player)
				droppedItem.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_GROUND
				droppedItem:AddEntityFlags(EntityFlag.FLAG_PERSISTENT)
				droppedItem.Parent = player
				
				local item = Isaac.GetItemConfig():GetCollectible(blacklistedItem)
				if not item then return end
				local gfx = item.GfxFileName
				if gfx == "" then return end
				droppedItem:GetSprite():ReplaceSpritesheet(0, gfx)
				droppedItem:GetSprite():LoadGraphics()
				droppedItem:GetSprite():Play("Appear", true)
				
				removedItems = true
			end
		end
	end
	
	if removedItems then
		player:AnimateSad()
	end
end

function mod:AcceptanceDroppedItemUpdate(effect)
	effect.Velocity = lib.Lerp(effect.Velocity, lib.ZeroVector, 0.03)
	if effect.FrameCount > 40 then
		local pos = game:GetRoom():FindFreePickupSpawnPosition(effect.Position)
		local player = (effect.Parent or effect.SpawnerEntity or Isaac.GetPlayer(0)):ToPlayer()
		if not player then player = Isaac.GetPlayer(0) end
		mod:SpawnBonusItem(lib.GetCurrentRoomItemPool(), player:GetCollectibleRNG(ACCEPTANCE):Next(), pos)
		effect:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.AcceptanceDroppedItemUpdate, droppedItemEffect)

---------- SPIRIT OF ACCEPTANCE ----------

function mod:SpiritOfAcceptance(entity)
	local sprite = entity:GetSprite()
	
	if sprite:IsFinished("Appear") then
		sprite:Play("Start", true)
	elseif sprite:IsFinished("Start") then
		sprite:Play("End", true)
	elseif sprite:IsFinished("End") then
		entity:Remove()
	end
	
	local targetOffset = -1 * entity.FrameCount
	entity.SpriteOffset = Vector(0, lib.Lerp(entity.SpriteOffset.Y, targetOffset, 0.5))
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SpiritOfAcceptance, SPIRIT_OF_ACCEPTANCE)

---------- CODE FOR LIGHT BEAM VISUAL ----------

function mod:AcceptanceLightInit(effect)
	local c = Color(1,1,1,1)
	c:SetColorize(1,1,1,1)
	effect.Color = c
	if effect.SpawnerEntity then
		effect:FollowParent(effect.SpawnerEntity)
	end
	effect:GetSprite().PlaybackSpeed = 2
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.AcceptanceLightInit, ACCEPTANCE_LIGHT)

function mod:AcceptanceLight(effect)
	local sprite = effect:GetSprite()
	if sprite:GetAnimation() ~= "Disappear" and effect.FrameCount > 30 then
		sprite.PlaybackSpeed = 1
		sprite:Play("Disappear", true)
	end
	if sprite:IsFinished("Disappear") then
		effect:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.AcceptanceLight, ACCEPTANCE_LIGHT)

---------- EFFIGY OF ACCEPTANCE (ROCK) ----------

local EFFIGY_OF_ACCEPTANCE = mod.ITEMS.EFFIGY_OF_ACCEPTANCE

local questionMarkSprite = Sprite()
questionMarkSprite:Load("gfx/005.100_collectible.anm2",true)
questionMarkSprite:ReplaceSpritesheet(1,"gfx/items/collectibles/questionmark.png")
questionMarkSprite:LoadGraphics()

-- Code for identifying a question mark collectible taken from EID.
local function IsUnknownItem(pickup)
	if pickup.Type ~= EntityType.ENTITY_PICKUP or pickup.Variant ~= PickupVariant.PICKUP_COLLECTIBLE then return false end
	
	if pickup.Touched then return false end
	
	for _, player in pairs(lib.GetPlayers()) do
		if lib.HasItem(player, mod.ITEMS.DENIAL) then
			return false
		end
	end
	
	if game:GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND ~= 0 then
		return true
	end
	
	local entitySprite = pickup:GetSprite()
	
	questionMarkSprite:SetFrame(entitySprite:GetAnimation(), entitySprite:GetFrame())
	
	for i = -1,1,1 do
		for j = -40,10,3 do
			local qcolor = questionMarkSprite:GetTexel(Vector(i,j),lib.ZeroVector,1,1)
			local ecolor = entitySprite:GetTexel(Vector(i,j),lib.ZeroVector,1,1)
			if qcolor.Red ~= ecolor.Red or qcolor.Green ~= ecolor.Green or qcolor.Blue ~= ecolor.Blue then
				-- It doesn't match the question mark sprite.
				return false
			end
		end
	end
	
	return true
end

local checkOptionsGroups = false

function mod:AcceptanceRockItemInit()
	checkOptionsGroups = true
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.AcceptanceRockItemInit, PickupVariant.PICKUP_COLLECTIBLE)

local function SomeoneHasAcceptanceRock()
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(EFFIGY_OF_ACCEPTANCE) then
			return true
		end
	end
end

function mod:AcceptanceRockCheckOptionsGroups()
	if not checkOptionsGroups then return end
	
	checkOptionsGroups = false
	
	if not SomeoneHasAcceptanceRock() then return end
	
	local optionsPedestals = {}
	
	for _, item in ipairs(Isaac.FindByType(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE)) do
		item = item:ToPickup()
		if item.SubType > 0 and item.OptionsPickupIndex ~= 0 then
			if not optionsPedestals[item.OptionsPickupIndex] then
				optionsPedestals[item.OptionsPickupIndex] = {}
			end
			table.insert(optionsPedestals[item.OptionsPickupIndex], item)
		end
	end
	
	for idx, list in pairs(optionsPedestals) do
		local groupAlreadyHasHiddenItem = false
		for _, item in ipairs(list) do
			if IsUnknownItem(item) then
				groupAlreadyHasHiddenItem = true
				break
			end
		end
		
		if not groupAlreadyHasHiddenItem and list[2] then
			lib.HideItemSprite(list[2])
			list[2]:GetData().hiddenByAcceptanceRock = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AcceptanceRockCheckOptionsGroups)
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.AcceptanceRockCheckOptionsGroups)

local pedestalCollisions = {}

local function OnItemPedestalCollsion(pedestal, player)
	if pedestal.SubType > 0 and not pedestal.Touched then
		if pedestalCollisions[pedestal.InitSeed] then
			pedestalCollisions[pedestal.InitSeed].Frame = game:GetFrameCount()
		elseif IsUnknownItem(pedestal) then
			pedestalCollisions[pedestal.InitSeed] = {
				ItemID = pedestal.SubType,
				Frame = game:GetFrameCount(),
				Position = Vector(pedestal.Position.X, pedestal.Position.Y),
			}
		else
			return
		end
		if pedestal:GetData().hiddenByAcceptanceRock and not pedestal:GetData().acceptanceRockHideAtEndOfFrame then
			-- Un-hide the item until the end of the frame.
			-- This makes it so its not a question mark when picked up.
			lib.ReloadItemSprite(pedestal)
			pedestal:GetData().acceptanceRockHideAtEndOfFrame = true
		end
	end
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	for _, pedestal in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE)) do
		if pedestal.SubType > 0 and pedestal:GetData().acceptanceRockHideAtEndOfFrame then
			-- If somehow the player didn't actually pick up a hidden item they touched, re-hide it.
			pedestal:GetData().acceptanceRockHideAtEndOfFrame = nil
			lib.HideItemSprite(pedestal)
		end
	end
end)

function mod:AcceptanceRockCollision(player, collider)
	if collider and collider.Type == EntityType.ENTITY_PICKUP and collider.Variant == PickupVariant.PICKUP_COLLECTIBLE and collider.SubType > 0 then
		OnItemPedestalCollsion(collider:ToPickup(), player)
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_PLAYER_COLLISION, mod.AcceptanceRockCollision)

function mod:AcceptanceRockPlayerUpdate(player)
	local isHeld = player:HasTrinket(EFFIGY_OF_ACCEPTANCE)
	local stageType = game:GetLevel():GetStageType()
	local isAltPath = (stageType == StageType.STAGETYPE_REPENTANCE or stageType == StageType.STAGETYPE_REPENTANCE_B)
	mod.HiddenItemManager:CheckStack(player, CollectibleType.COLLECTIBLE_THERES_OPTIONS, isHeld and 1 or 0)
	mod.HiddenItemManager:CheckStack(player, CollectibleType.COLLECTIBLE_MORE_OPTIONS, (isHeld and not isAltPath) and 1 or 0)
	
	if isHeld and player:IsHoldingItem() and player.QueuedItem and player.QueuedItem.Item then
		for key, tab in pairs(pedestalCollisions) do
			if tab.ItemID == player.QueuedItem.Item.ID then
				-- Player just grabbed a new hidden item~!
				local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, ACCEPTANCE_LIGHT, 0, player.Position, lib.ZeroVector, player):ToEffect()
				
				local power = FiendFolio and FiendFolio.GetGolemTrinketPower(player, EFFIGY_OF_ACCEPTANCE) or player:GetTrinketMultiplier(EFFIGY_OF_ACCEPTANCE)
				
				local heartsToSpawn = 1 + math.floor(power)
				local bonusChance = power % 1
				
				if player:GetTrinketRNG(EFFIGY_OF_ACCEPTANCE):RandomFloat() <= bonusChance then
					heartsToSpawn = heartsToSpawn + 1
				end
				
				while heartsToSpawn > 0 do
					local heartSubType = (heartsToSpawn == 1) and HeartSubType.HEART_HALF_SOUL or HeartSubType.HEART_SOUL
					Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, heartSubType, player.Position, RandomVector()*1.5, nil)
					heartsToSpawn = heartsToSpawn - 2
				end
				
				local data = mod:GetPersistentPlayerData(player)
				data.acceptanceRockLuck = (data.acceptanceRockLuck or 0) + 0.5 * power
				player:AddCacheFlags(CacheFlag.CACHE_LUCK)
				player:EvaluateItems()
				sfxManager:Play(SoundEffect.SOUND_THUMBSUP)
				
				pedestalCollisions[key] = nil
				break
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.AcceptanceRockPlayerUpdate)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	local currentFrame = game:GetFrameCount()
	
	local toRemove = {}
	for key, tab in pairs(pedestalCollisions) do
		if currentFrame - tab.Frame > 1 then
			table.insert(toRemove, key)
		end
	end
	for _, k in pairs(toRemove) do
		pedestalCollisions[k] = nil
	end
end)
