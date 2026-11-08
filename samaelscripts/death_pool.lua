local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

mod.DeathPoolName = "SAMAEL_DEATH"

if Encyclopedia then
	mod.DeathPoolID = Encyclopedia.GetItemPoolIdByName(mod.DeathPoolName)
	
	local poolSprite = Encyclopedia.RegisterSprite("gfx/ui/samael_death_pool.anm2", "Idle", 0)
	Encyclopedia.AddItemPoolSprite(mod.DeathPoolID, poolSprite)
end

local DeathPoolTemplate = {
	[CollectibleType.COLLECTIBLE_DEATHS_TOUCH] = 1.0,
	[CollectibleType.COLLECTIBLE_DEATHS_LIST] = 1.0,
	[CollectibleType.COLLECTIBLE_NECRONOMICON] = 1.0,
	[CollectibleType.COLLECTIBLE_HOURGLASS] = 1.0,
	[CollectibleType.COLLECTIBLE_ISAACS_TOMB] = 1.0,
	[CollectibleType.COLLECTIBLE_DECAP_ATTACK] = 1.0,
	[CollectibleType.COLLECTIBLE_FATE] = 1.0,
	[CollectibleType.COLLECTIBLE_FATES_REWARD] = 1.0,
	[CollectibleType.COLLECTIBLE_LIL_CHEST] = 1.0,
	[CollectibleType.COLLECTIBLE_CEREMONIAL_ROBES] = 1.0,
	
	[CollectibleType.COLLECTIBLE_DUALITY] = 1.0,
	[CollectibleType.COLLECTIBLE_EDENS_BLESSING] = 1.0,
	[CollectibleType.COLLECTIBLE_EDENS_SOUL] = 1.0,
	
	[CollectibleType.COLLECTIBLE_SWORN_PROTECTOR] = 1.0,
	[CollectibleType.COLLECTIBLE_SERAPHIM] = 1.0,
	[CollectibleType.COLLECTIBLE_HALO] = 1.0,
	[CollectibleType.COLLECTIBLE_CIRCLE_OF_PROTECTION] = 1.0,
	
	[CollectibleType.COLLECTIBLE_PURGATORY] = 1.0,
	[CollectibleType.COLLECTIBLE_VADE_RETRO] = 1.0,
	[CollectibleType.COLLECTIBLE_SPIRIT_SHACKLES] = 1.0,
	[CollectibleType.COLLECTIBLE_ASTRAL_PROJECTION] = 1.0,
	[CollectibleType.COLLECTIBLE_HUNGRY_SOUL] = 1.0,
	[CollectibleType.COLLECTIBLE_JAR_OF_WISPS] = 1.0,
	[CollectibleType.COLLECTIBLE_VENGEFUL_SPIRIT] = 1.0,
	[CollectibleType.COLLECTIBLE_LOST_SOUL] = 1.0,
	[CollectibleType.COLLECTIBLE_GHOST_BABY] = 1.0,
	
	[CollectibleType.COLLECTIBLE_BONE_SPURS] = 1.0,
	[CollectibleType.COLLECTIBLE_DRY_BABY] = 1.0,
	[CollectibleType.COLLECTIBLE_COMPOUND_FRACTURE] = 1.0,
	[CollectibleType.COLLECTIBLE_BOOK_OF_THE_DEAD] = 1.0,
	[CollectibleType.COLLECTIBLE_JAW_BONE] = 1.0,
	[CollectibleType.COLLECTIBLE_BRITTLE_BONES] = 1.0,
	[CollectibleType.COLLECTIBLE_MARROW] = 1.0,
	
	[CollectibleType.COLLECTIBLE_CLEAR_RUNE] = 1.0,
	
	[CollectibleType.COLLECTIBLE_DIVORCE_PAPERS] = 1.0,
	[CollectibleType.COLLECTIBLE_DADS_RING] = 1.0,
	[CollectibleType.COLLECTIBLE_DADS_KEY] = 1.0,
	[CollectibleType.COLLECTIBLE_DADS_LOST_COIN] = 1.0,
	[CollectibleType.COLLECTIBLE_MOMS_RING] = 1.0,
	[CollectibleType.COLLECTIBLE_MOMS_BOX] = 1.0,
	[CollectibleType.COLLECTIBLE_TORN_PHOTO] = 1.0,
	[CollectibleType.COLLECTIBLE_RED_KEY] = 1.0,
	
	[mod.ITEMS.DENIAL] = 0.4,
	[mod.ITEMS.ANGER] = 1.0,
	[mod.ITEMS.BARGAINING] = 1.0,
	[mod.ITEMS.DEPRESSION] = 1.0,
	[mod.ITEMS.ACCEPTANCE] = 0.4,
	[mod.ITEMS.THANATOPHOBIA] = 1.0,
	[mod.ITEMS.THANATOPHILIA] = 1.0,
	[mod.ITEMS.PUNISHMENT_OF_THE_GRAVE] = 1.0,
	[mod.ITEMS.REMEMBRANCE_OF_THE_FORGOTTEN] = 1.0,
	[mod.ITEMS.REMEMBRANCE_OF_DEATH] = 1.0,
	[mod.ITEMS.REAPER_BUM] = 1.0,
	[mod.ITEMS.MALAKH_MOT] = 0.4,
	[mod.ITEMS.MEMENTO_MORI] = 0.4,
	[mod.ITEMS.THANATOS] = 1.0,
	[mod.ITEMS.JAR_OF_SCYTHES] = 1.0,
	[mod.ITEMS.TRUMPET_OF_WOE] = 0.4,
}

-- Call this during or before MC_POST_GAME_STARTED.
-- Pass a table with the item ID as the key and the weight as the value.
-- See below for an example.
function mod:AddToDeathPool(items)
	for itemID, weight in pairs(items) do
		local conf = Isaac.GetItemConfig():GetCollectible(itemID)
		if conf and conf:IsCollectible() then
			DeathPoolTemplate[itemID] = weight
		end
	end
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
	if FiendFolio then
		mod:AddToDeathPool({
			[FiendFolio.ITEM.COLLECTIBLE.SPARE_RIBS] = 1.0,
			[FiendFolio.ITEM.COLLECTIBLE.BLACK_MOON] = 1.0,
			[FiendFolio.ITEM.COLLECTIBLE.DADS_WALLET] = 1.0,
		})
	end
	if Retribution then
		mod:AddToDeathPool({
			[Retribution.Item.HEEL_SPUR] = 1.0,
			[Retribution.Item.DOMINICUS] = 1.0,
			[Retribution.Item.RICKETS] = 1.0,
			[Retribution.Item.ROLL_FILM] = 1.0,
		})
	end
end)

local function GetDeathPoolData()
	return mod:GetRunData("DEATH_POOL")
end

local function GetDeathPoolSeed()
	local data = GetDeathPoolData()
	local rng = RNG()
	rng:SetSeed(data.DeathPoolSeed or game:GetSeeds():GetStartSeed(), 35)
	data.DeathPoolSeed = rng:Next()
	return data.DeathPoolSeed
end

function mod:InitDeathPool()
	local data = GetDeathPoolData()
	data.DeathPool = {}
	
	for itemID, weight in pairs(DeathPoolTemplate) do
		table.insert(data.DeathPool, {
			ID = itemID,
			Weight = weight,
		})
	end
end

local function TryPickItem(minQuality, noActives)
	local data = GetDeathPoolData()
	local itemPool = game:GetItemPool()
	
	if not data.DeathPool then
		mod:InitDeathPool()
	end
	
	local filteredPool = {}
	
	for _, poolItem in pairs(data.DeathPool) do
		local configEntry = Isaac.GetItemConfig():GetCollectible(poolItem.ID)
		if configEntry and configEntry:IsAvailable()
				and not (noActives and configEntry.Type == ItemType.ITEM_ACTIVE)
				and not (minQuality and configEntry.Quality < minQuality) then
			table.insert(filteredPool, poolItem)
		end
	end
	
	local choice
	
	repeat
		choice = lib.PickRandom(filteredPool, GetDeathPoolSeed())
		if choice then
			choice.Weight = 0
		end
	until (not choice or itemPool:RemoveCollectible(choice.ID))
	
	if choice then
		return choice.ID
	end
end

function mod:GetDeathPoolItem(minQuality, noActives)
	lib.Log("Going to pick an item from the Death pool...")
	
	local noActives = noActives or false
	local someoneHasChaos = false
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(TrinketType.TRINKET_NO) then noActives = true end
		if player:HasCollectible(CollectibleType.COLLECTIBLE_CHAOS) then someoneHasChaos = true end
	end
	
	if not someoneHasChaos then
		local data = GetDeathPoolData()
		
		lib.Log("Picking from the custom Death pool now...")
		local item = TryPickItem(minQuality, noActives)
		if item then
			lib.Log("Success! Death pool picked: " .. item)
			return item
		end
		
		if not data.poolExhausted then
			lib.Log("Death pool seems to be empty. Will try resetting it ONCE.")
			mod:InitDeathPool()
			item = TryPickItem(minQuality, noActives)
			data.poolExhausted = true
			if item then
				lib.Log("Success! Death pool picked: " .. item)
				return item
			end
		end
	end
	
	if someoneHasChaos then
		lib.Log("Chaos is active.")
	else
		lib.Log("Death pool is exhausted.")
	end
	
	local itemPool = game:GetItemPool()
	local chosenItem
	local pickAttempts = 0
	
	while pickAttempts < 50 and (not chosenItem or (minQuality and Isaac.GetItemConfig():GetCollectible(chosenItem).Quality < minQuality)) do
		local seed = GetDeathPoolSeed()
		local backupPool
		
		if seed % 2 == 0 then
			if game:IsGreedMode() then
				backupPool = ItemPoolType.POOL_GREED_ANGEL
			else
				backupPool = ItemPoolType.POOL_ANGEL
			end
		else
			if game:IsGreedMode() then
				backupPool = ItemPoolType.POOL_GREED_DEVIL
			else
				backupPool = ItemPoolType.POOL_DEVIL
			end
		end
		
		chosenItem = itemPool:GetCollectible(backupPool, false, seed)
		pickAttempts = pickAttempts + 1
	end
	
	if chosenItem then
		lib.Log("Death pool picked: " .. chosenItem)
	else
		lib.LogErr("Death pool failed to pick an item.")
		chosenItem = CollectibleType.COLLECTIBLE_DEATHS_TOUCH
	end
	
	itemPool:RemoveCollectible(chosenItem)
	
	return chosenItem
end

function mod:DeathPoolOverride(pool, decrease, seed)
	if mod.InitFinished and (pool == ItemPoolType.POOL_TREASURE or pool == ItemPoolType.POOL_GREED_TREASURE) then
		if mod:IsDeathDealRoom() or mod:IsFragmentRoom() then
			return mod:GetDeathPoolItem()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_GET_COLLECTIBLE, mod.DeathPoolOverride)
