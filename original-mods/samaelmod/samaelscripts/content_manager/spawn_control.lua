return function(ContentManager)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local lib = ContentManager.Lib
local game = Game()

----------------------------------------
-- Items/Trinkets

local function ItemRNG()
	local seed = ContentManager.MiscSaveData.GetItemSeed or game:GetSeeds():GetStartSeed()
	local rng = RNG()
	rng:SetSeed(seed, 35)
	ContentManager.MiscSaveData.GetItemSeed = rng:Next()
	return rng
end

-- Remove any locked items from the pools.
function ContentManager:RemoveLockedItemsFromPool()
	local itemPool = game:GetItemPool()
	
	for key, itemData in pairs(ContentManager.CATALOG[ContentManager.CLASS.ITEM]) do
		if ContentManager:ItemLockedOrDisabled(key) then
			itemPool:RemoveCollectible(itemData.ID)
		end
	end
	
	for key, itemData in pairs(ContentManager.CATALOG[ContentManager.CLASS.TRINKET]) do
		if ContentManager:TrinketLockedOrDisabled(key) then
			itemPool:RemoveTrinket(itemData.ID)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, ContentManager.RemoveLockedItemsFromPool)

-- Prevent the game from choosing a locked item.
function ContentManager:OnGetCollectible(item, pool, decrease)
	if ContentManager:ItemLockedOrDisabled(item) then
		game:GetItemPool():RemoveCollectible(item)
		return game:GetItemPool():GetCollectible(pool, decrease, ItemRNG():Next())
	end
end
mod:AddCallback(ModCallbacks.MC_POST_GET_COLLECTIBLE, ContentManager.OnGetCollectible)

-- Prevent the game from choosing a locked trinket.
function ContentManager:OnGetTrinket(trinket)
	if ContentManager:TrinketLockedOrDisabled(trinket) then
		game:GetItemPool():RemoveTrinket(trinket)
		return game:GetItemPool():GetTrinket()
	end
end
mod:AddCallback(ModCallbacks.MC_GET_TRINKET, ContentManager.OnGetTrinket)

-- If FiendFolio is installed, add any info on custom Golem Rocks.
function ContentManager:AddGolemRocks()
	if not FiendFolio then return end
	
	for _, data in pairs(ContentManager.CATALOG[ContentManager.CLASS.TRINKET]) do
		if data.GolemRock then
			FiendFolio.RockTrinkets[data.ID] = data.GolemRock.Rarity or 0
			if data.GolemRock.FossilCrushEffect then
				FiendFolio.FossilBreakEffects[data.ID] = data.GolemRock.FossilCrushEffect
			end
			if data.GolemRock.DelayedEncyFunc then
				data.GolemRock.DelayedEncyFunc()
				data.GolemRock.DelayedEncyFunc = nil
			end
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_GAME_STARTED, CallbackPriority.EARLY, ContentManager.AddGolemRocks)

----------------------------------------
-- Cards

local function CardRNG()
	local seed = ContentManager.MiscSaveData.GetCardSeed or game:GetSeeds():GetStartSeed()
	local rng = RNG()
	rng:SetSeed(seed, 35)
	ContentManager.MiscSaveData.GetCardSeed = rng:Next()
	return rng
end

-- Prevent the game from choosing a locked card.
function ContentManager:OnGetCard(_, card, includePlayingCards, includeRunes, onlyRunes)
	local cardData = ContentManager:GetCard(card)
	
	if not cardData then return end
	
	local weight = cardData.CardWeight
	if ContentManager:CardLockedOrDisabled(card) or (weight and weight < 1.0 and (weight == 0 or CardRNG():RandomFloat() > weight)) then
		return game:GetItemPool():GetCard(CardRNG():Next(), includePlayingCards, includeRunes, onlyRunes)
	end
	
	if cardData.CardReplacement and not ContentManager:CardLockedOrDisabled(cardData.CardReplacement) and CardRNG():RandomFloat() <= cardData.CardReplacementChance then
		return cardData.CardReplacement
	end
end
mod:AddCallback(ModCallbacks.MC_GET_CARD, ContentManager.OnGetCard)

-- Booster Pack doesn't trigger MC_GET_CARD, so we have to handle that seperately.

-- The last frame we detected a player obtaining a new copy of Booster Pack.
local boosterPackDetectedAt = nil

local boosterPackCounts = {}

-- Detect when a player obtains a new copy of Booster Pack.
-- Booster Pack doesn't trigger MC_GET_CARD.
function ContentManager:CountBoosterPacks()
	for i=0, game:GetNumPlayers()-1 do
		local player = Isaac.GetPlayer(i)
		
		local prev = boosterPackCounts[i] or 0
		local new = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BOOSTER_PACK, true)
		
		if new > prev then
			boosterPackDetectedAt = game:GetFrameCount()
		end
		
		boosterPackCounts[i] = new
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, ContentManager.CountBoosterPacks)

-- Anti-recursion bit.
local fixingBoosterPackSpawn = false

-- On pickup init, if a player was detected obtaining a new copy of Booster Pack within the last frame, check if the card should be replaced.
-- Normally replacements are done on MC_GET_CARD. Booster Pack is the only exception, because it doesn't trigger that callback for some reason.
function ContentManager:OnPickupInit(pickup)
	if fixingBoosterPackSpawn or pickup:GetSprite():GetAnimation() ~= "Appear" then return end
	
	-- The card spawns from Booster Pack can occur prior to us detecting the Booster Pack in MC_POST_PEFFECT_UPDATE.
	-- So check all the players here, too.
	ContentManager:CountBoosterPacks()

	-- The frame we last detected Booster Pack shouldn't ever be higher than the current frame.
	-- If this happens, some cards probably spawned prior to MC_POST_GAME_STARTED, and we didn't reset `boosterPackDetectedAt` yet.
	if boosterPackDetectedAt and boosterPackDetectedAt > game:GetFrameCount() then
		boosterPackDetectedAt = nil
	end

	if boosterPackDetectedAt and game:GetFrameCount() - boosterPackDetectedAt <= 1 then
		-- This is probably a Booster Pack spawn.
		local replacement = ContentManager:OnGetCard(nil, pickup.SubType, true, false, false)
		if replacement then
			fixingBoosterPackSpawn = true
			pickup:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, replacement, true, true, false)
			fixingBoosterPackSpawn = false
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, ContentManager.OnPickupInit, PickupVariant.PICKUP_TAROTCARD)

-- Register object mimic charges for Fiend Folio's "Perfectly Generic Object", if applicable.
function ContentManager:AddObjectMimicCharges()
	if FiendFolio then
		for _, data in pairs(ContentManager.CATALOG[ContentManager.CLASS.CARD]) do
			if data.CardType == ItemConfig.CARDTYPE_SPECIAL_OBJECT then
				FiendFolio.PocketObjectMimicCharges[data.ID] = data.MimicCharge
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, ContentManager.AddObjectMimicCharges)

-- Fixing object pickup sound.
local sfx = SFXManager()
local fixObjectSound = false

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if fixObjectSound then
		fixObjectSound = false
		if sfx:IsPlaying(SoundEffect.SOUND_BOOK_PAGE_TURN_12) then
			sfx:Play(SoundEffect.SOUND_SHELLGAME, 1, 0, false, 1)
			sfx:Stop(SoundEffect.SOUND_BOOK_PAGE_TURN_12)
		end
	end
end)

-- Triggers the above sound replacement.
mod:AddCallback(ModCallbacks.MC_PRE_PICKUP_COLLISION, function(_, pickup, collider)
	local data = ContentManager:GetCard(pickup.SubType)
	if collider and collider.Type == EntityType.ENTITY_PLAYER
			and data and data.CardType == ItemConfig.CARDTYPE_SPECIAL_OBJECT
			and collider:ToPlayer():CanPickupItem() and not collider:ToPlayer():IsHoldingItem() then
		fixObjectSound = true
	end
end, PickupVariant.PICKUP_TAROTCARD)

----------------------------------------
-- Entities

-- If a locked entity would spawn, replace it with its designated replacement.
-- If no replacement is specified, spawn a tiny fly to try to be harmless.
local function CheckLockedEntitySpawn(eType, eVariant, eSubType)
	local key = ContentManager.EntityKey(eType, eVariant, eSubType)
	if ContentManager:EntityLockedOrDisabled(key) then
		local data = ContentManager.CATALOG[ContentManager.CLASS.ENTITY][key]
		if data.Unlockable and data.Unlockable.ReplacementInfo then
			local replacement = data.Unlockable.ReplacementInfo
			return {
				replacement.Type,
				replacement.Variant,
				replacement.SubType,
			}
		else
			return {EntityType.ENTITY_EFFECT, EffectVariant.TINY_FLY, 0}
		end
	end
end

function ContentManager:PreEntitySpawn(eType, eVariant, eSubType)
	if not game:IsPaused() then
		return CheckLockedEntitySpawn(eType, eVariant, eSubType)
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN, ContentManager.PreEntitySpawn)

function ContentManager:PreRoomEntitySpawn(eType, eVariant, eSubType)
	return CheckLockedEntitySpawn(eType, eVariant, eSubType)
end
mod:AddCallback(ModCallbacks.MC_PRE_ROOM_ENTITY_SPAWN, ContentManager.PreRoomEntitySpawn)

----------------------------------------
-- Pills

-- For a locked pill, pick a random replacement pill effect that is not already in the pool.
-- This isn't perfect, as the replacment isn't technically in the pool, still.
-- Also, if another mod did something similar to this, could potentially end up with seemingly
-- two of the same pill in the pool.
local antiPillRecursion = false
function ContentManager:GetPillEffect(pillEffect, pillColor)
	if antiPillRecursion then return end 
	
	if ContentManager:PillLockedOrDisabled(pillEffect) then
		local runSeed = game:GetSeeds():GetStartSeed()
		
		local lockedPillReplacementData = lib.GetSubTable(ContentManager.MiscSaveData, "PillReplacement")
		if lockedPillReplacementData.RunSeed ~= runSeed then
			lockedPillReplacementData.RunSeed = runSeed
			lockedPillReplacementData.ReplacementCache = {}
			lockedPillReplacementData.UsedReplacements = {}
		end
		local cache = lockedPillReplacementData.ReplacementCache
		local used = lockedPillReplacementData.UsedReplacements
		
		-- If we've already chosen a replacement for this pill, just keep using that.
		if cache[pillEffect] then
			return cache[pillEffect]
		end
		
		local itemPool = game:GetItemPool()
		
		-- Get all the pill effects currently in the pool.
		local pillEffectInPool = {}
		antiPillRecursion = true
		for color=PillColor.PILL_BLUE_BLUE, PillColor.PILL_WHITE_YELLOW do
			local eff = itemPool:GetPillEffect(color)
			pillEffectInPool[eff] = true
		end
		antiPillRecursion = false
		
		-- Find all of the pill effects that are NOT in the current pool.
		local replacementCandidates = {}
		for eff=PillEffect.PILLEFFECT_BAD_GAS, PillEffect.PILLEFFECT_EXPERIMENTAL do
			if not pillEffectInPool[eff] and not used[replacement] then
				table.insert(replacementCandidates, eff)
			end
		end
		
		-- Pick a replacement PillEffect based on the run seed & this PillEffect's ID.
		local replacement = replacementCandidates[((runSeed + pillEffect) % #replacementCandidates) + 1]
		
		if replacement then
			-- Store this replacement to avoid changing it later.
			cache[pillEffect] = replacement
			used[replacement] = true
			return replacement
		else
			lib.LogErr("Failed to choose replacement for locked pill.")
			return PillEffect.PILLEFFECT_BAD_GAS
		end
	end
end
mod:AddCallback(ModCallbacks.MC_GET_PILL_EFFECT, ContentManager.GetPillEffect)

----------------------------------------------------------------------------------------------------
end