local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local BARGAINING = mod.ITEMS.BARGAINING
local BARGAINING_CHIP = mod.ITEMS.BARGAINING_CHIP
local BARGAINING_FOSSIL = mod.ITEMS.BARGAINING_FOSSIL

local SPIRIT_OF_BARGAINING = mod.ENTITIES.SPIRIT_OF_BARGAINING
local SPIRIT_OF_BARGAINING_EFFECT = mod.ENTITIES.SPIRIT_OF_BARGAINING_EFFECT.Var

local function GetFloorData()
	return mod:GetFloorData("Bargaining")
end
local function GetRunData()
	return mod:GetRunData("Bargaining")
end
local function GetRoomData()
	return mod:GetCurrentRoomData("Bargaining")
end

local bargainingRNG
local function GetBargainingRNG()
	if not bargainingRNG then
		bargainingRNG = RNG()
		local seed = GetRunData().RngSeed or game:GetSeeds():GetStartSeed()
		bargainingRNG:SetSeed(seed, 1)
	end
	GetRunData().RngSeed = bargainingRNG:GetSeed()
	return bargainingRNG
end

local cachedSpiritOfBargaining
local function FindSpiritOfBargaining()
	if cachedSpiritOfBargaining and cachedSpiritOfBargaining:Exists() then
		return cachedSpiritOfBargaining
	end
	
	for _, ent in pairs(Isaac.FindByType(SPIRIT_OF_BARGAINING.ID, SPIRIT_OF_BARGAINING.Var)) do
		cachedSpiritOfBargaining = ent
		return ent
	end
end

local GlitchItems = {
	CollectibleType.COLLECTIBLE_MISSING_NO,
	CollectibleType.COLLECTIBLE_GB_BUG,
	CollectibleType.COLLECTIBLE_UNDEFINED,
	CollectibleType.COLLECTIBLE_GLITCHED_CROWN,
	CollectibleType.COLLECTIBLE_DATAMINER,
	CollectibleType.COLLECTIBLE_TMTRAINER,
}

local GlitchTrinkets = {
	TrinketType.TRINKET_ERROR,
	TrinketType.TRINKET_M,
}

local function IsGlitchItem(variant, subType)
	if variant == PickupVariant.PICKUP_COLLECTIBLE then
		for _, id in pairs(GlitchItems) do
			if subType == id then return true end
		end
	end
	if variant == PickupVariant.PICKUP_TRINKET then
		for _, id in pairs(GlitchTrinkets) do
			if subType == id then return true end
		end
	end
end

--------------------------------------------------
---- DEAL COSTS/PRICES
--------------------------------------------------

local function InitDealPriceSprite(anim)
	local sprite = Sprite()
	sprite:Load("gfx/samael_entities/bargaining/deal_price.anm2", true)
	sprite:SetAnimation(anim)
	sprite:SetFrame(0)
	return sprite
end

local function InitBasicDealPriceSprite(cost)
	local absoluteCost = math.abs(cost)
	local sprite = InitDealPriceSprite("NumbersWhite")
	local tensFrame = math.floor(absoluteCost * 0.1)
	if tensFrame <= 0 then
		tensFrame = 12  -- Invisible frame
	end
	sprite:SetLayerFrame(0, tensFrame)
	sprite:SetLayerFrame(1, absoluteCost % 10)
	sprite:SetLayerFrame(2, 10) -- Defaults to cents symbol
	if cost < -9 then
		sprite:SetLayerFrame(4, 2)
	elseif cost < 0 then
		sprite:SetLayerFrame(4, 1)
	end
	return sprite
end

local DEAL_COSTS = {
	COINS = {
		CanAfford = function(player, cost) return player:GetNumCoins() >= cost end,
		Pay = function(player, cost) player:AddCoins(-cost) end,
		InitSprites = function(cost) return InitBasicDealPriceSprite(cost) end,
		GetNumHeld = function(player) return player:GetNumCoins() end,
		Value = 1,
		ValueDeterioratesUntilQuantity = 75,
	},
	KEYS = {
		CanAfford = function(player, cost) return player:GetNumKeys() >= cost end,
		Pay = function(player, cost) player:AddKeys(-cost) end,
		InitSprites = function(cost)
			local sprite = InitBasicDealPriceSprite(cost)
			sprite:SetLayerFrame(2, 14)
			return sprite
		end,
		GetNumHeld = function(player) return player:GetNumKeys() end,
		Value = 5,
		ValueDeterioratesUntilQuantity = 20,
	},
	BOMBS = {
		CanAfford = function(player, cost) return player:GetNumBombs() >= cost end,
		Pay = function(player, cost) player:AddBombs(-cost) end,
		InitSprites = function(cost)
			local sprite = InitBasicDealPriceSprite(cost)
			sprite:SetLayerFrame(2, 13)
			return sprite
		end,
		GetNumHeld = function(player) return player:GetNumBombs() end,
		Value = 5,
		ValueDeterioratesUntilQuantity = 20,
	},
	HEARTS = {
		CanAfford = function(player, cost)
			local spendableHeartContainers = math.floor(player:GetMaxHearts()*0.5 + player:GetBoneHearts())
			return spendableHeartContainers > cost or (spendableHeartContainers == cost and player:GetSoulHearts() > 0)
		end,
		Pay = function(player, cost) player:AddMaxHearts(-cost*2) end,
		InitSprites = function(cost)
			local sprite = InitDealPriceSprite("Hearts")
			sprite:SetLayerFrame(1, math.min(math.max(0, cost-1), 2))
			return sprite
		end,
		GetNumHeld = function(player, offerInfo)
			local spendableHeartContainers = math.floor(player:GetMaxHearts()*0.5 + player:GetBoneHearts())
			if spendableHeartContainers > 0 and player:GetSoulHearts() == 0 then
				spendableHeartContainers = spendableHeartContainers - 1  -- Don't ask for all of a player's hearts.
			end
			local maximum = 2
			if offerInfo.Name == "ITEM" or offerInfo.Special then
				maximum = 3
			end
			return math.min(spendableHeartContainers, maximum)
		end,
		Value = 12.5,
	},
	SOUL_HEARTS = {
		CanAfford = function(player, cost)
			local fullSoulHearts = math.floor(player:GetSoulHearts() * 0.5)
			return fullSoulHearts > cost or (fullSoulHearts == cost and player:GetHearts() + player:GetBoneHearts() > 0)
		end,
		Pay = function(player, cost) player:AddSoulHearts(-cost*2) end,
		InitSprites = function(cost)
			local sprite = InitDealPriceSprite("SoulHearts")
			sprite:SetLayerFrame(1, math.min(math.max(0, cost-1), 3))
			return sprite
		end,
		GetNumHeld = function(player, offerInfo)
			local soulHearts = player:GetSoulHearts()
			if soulHearts > 0 and player:GetHearts() + player:GetBoneHearts() == 0 then
				soulHearts = soulHearts - 1  -- Don't ask for all of a player's hearts.
			end
			local fullSoulHearts = math.floor(soulHearts * 0.5)
			local maximum = 3
			if offerInfo.Name == "ITEM" or offerInfo.Special then
				maximum = 4
			end
			return math.min(fullSoulHearts, maximum)
		end,
		Value = 5,
		ValueIncreasesBy = 1.2,
	},
}

local function RequireItem(itemType)
	local item = Isaac.GetItemConfig():GetCollectible(itemType)
	if item then
		return {
			Type = itemType,
			CanAfford = function(player) return lib.HasItem(player, itemType, true) end,
			Pay = function(player) return player:RemoveCollectible(itemType, true) end,
			InitSprites = function()
				local sprite = InitDealPriceSprite("Item")
				local gfx = item.GfxFileName
				sprite:ReplaceSpritesheet(3, gfx)
				sprite:ReplaceSpritesheet(5, gfx)
				sprite:LoadGraphics()
				return sprite
			end,
			GetNumHeld = function(player)
				return player:GetCollectibleNum(itemType)
			end,
			Value = 10 + 5 * item.Quality,
		}
	end
end

local function RequireRandomItem(player)
	if player:GetData().MaliceMinion then return end
	
	local itemConfig = Isaac.GetItemConfig()
	local playersItems = {}
	local hasItems = false
	
	for i=1, itemConfig:GetCollectibles().Size-1 do
		local item = itemConfig:GetCollectible(i)
		if item and i ~= BARGAINING and not item:HasTags(ItemConfig.TAG_QUEST) and lib.HasItem(player, i, true)
				and i ~= CollectibleType.COLLECTIBLE_BOOSTER_PACK
				and i ~= CollectibleType.COLLECTIBLE_BUDDY_IN_A_BOX
				and player:GetActiveItem(ActiveSlot.SLOT_POCKET) ~= i
				and player:GetActiveItem(ActiveSlot.SLOT_POCKET2) ~= i then
			table.insert(playersItems, i)
			hasItems = true
		end
	end
	
	if not hasItems then return end
	
	local item = playersItems[GetBargainingRNG():RandomInt(#playersItems)+1]
	if item then
		return RequireItem(item)
	end
end

local function RequireTrinket(trinketType)
	if trinketType <= 0 or trinketType == TrinketType.TRINKET_TICK then return end
	
	local trinketInfo = Isaac.GetItemConfig():GetTrinket(trinketType)
	
	if trinketType > 0 and trinketInfo then
		return {
			Type = trinketType,
			CanAfford = function(player) return player:HasTrinket(trinketType) end,
			Pay = function(player) return player:TryRemoveTrinket(trinketType) end,
			InitSprites = function()
				local sprite = InitDealPriceSprite("Item")
				local gfx = trinketInfo.GfxFileName
				sprite:ReplaceSpritesheet(3, gfx)
				sprite:ReplaceSpritesheet(5, gfx)
				sprite:LoadGraphics()
				return sprite
			end,
			GetNumHeld = function()
				return 1
			end,
			Value = 7
		}
	end
end

local function RequireRandomTrinket(player)
	local trinket1 = player:GetTrinket(0)
	local trinket2 = player:GetTrinket(1)
	local pickFirstTrinket = GetBargainingRNG():RandomInt(2) == 0
	
	if pickFirstTrinket and trinket1 > 0 then
		return RequireTrinket(trinket1)
	elseif trinket2 > 0 then
		return RequireTrinket(trinket2)
	end
end

--------------------------------------------------
---- DEAL OFFERS/PRODUCTS
--------------------------------------------------

local DealOfferAnimOverrides = {}
DealOfferAnimOverrides["Collect"] = "Idle"
DealOfferAnimOverrides["Appear"] = "Idle"

local DealOfferAnimDontPlay = {}
DealOfferAnimDontPlay["WalkHori"] = true

local function InitDealOfferSprite(anm2, overlay)
	local sprite = Sprite()
	sprite:Load(anm2, true)
	local anim = sprite:GetDefaultAnimation()
	
	if DealOfferAnimOverrides[anim] then
		anim = DealOfferAnimOverrides[anim]
	end
	
	sprite:Play(anim, true)
	
	if overlay and overlay ~= "" then
		sprite:PlayOverlay(overlay, true)
	end
	
	if DealOfferAnimDontPlay[anim] then
		sprite.PlaybackSpeed = 0
	else
		sprite.PlaybackSpeed = 0.5
	end
	return sprite
end

local function InitDealOfferSpriteForEntity(eType, eVariant, eSubType)
	local entity = lib.TestSpawn(eType, eVariant, eSubType, false, true, true)
	local anm2 = entity:GetSprite():GetFilename()
	local overlay = entity:GetSprite():GetOverlayAnimation()
	entity:Remove()
	return InitDealOfferSprite(anm2, overlay)
end

local function SimplePayout(eType, eVariant, eSubType, origin, amount)
	for i=0, amount-1 do
		local angle = GetBargainingRNG():RandomInt(360)
		local vel = lib.NormalVector:Rotated(angle):Resized(0 + 0.5*i)
		local pos = origin
		if eType == EntityType.ENTITY_PICKUP and eVariant == PickupVariant.PICKUP_COLLECTIBLE then
			pos = Isaac.GetFreeNearPosition(origin, 1)
		end
		local thing = Isaac.Spawn(eType, eVariant, eSubType, pos + vel, vel, nil)
		thing:Update()
		if thing:ToPickup() and (thing.Type ~= eType or thing.Variant ~= eVariant or thing.SubType ~= eSubType) then
			thing:ToPickup():Morph(eType, eVariant, eSubType, true, true, true)
		end
	end
end

local function SimpleOffer(eType, eVariant, eSubType, anm2)
	eVariant = eVariant or 0
	eSubType = eSubType or 0
	
	local tab = {
		Type = eType,
		Variant = eVariant,
		SubType = eSubType,
		PayOut = function(self, pos, amount)
			if self.Type == EntityType.ENTITY_PICKUP then
				if self.Variant == PickupVariant.PICKUP_COLLECTIBLE then
					game:GetItemPool():RemoveCollectible(self.SubType)
				elseif self.Variant == PickupVariant.PICKUP_TRINKET then
					game:GetItemPool():RemoveTrinket(self.SubType)
				end
			end
			SimplePayout(self.Type, self.Variant, self.SubType, pos, amount)
		end,
	}
	
	if anm2 then
		tab.Sprite = InitDealOfferSprite(anm2)
	else
		tab.Sprite = InitDealOfferSpriteForEntity(eType, eVariant, eSubType)
	end
	
	return tab
end

local function OfferPickup(pickupVariant, pickupSubType)
	local eType, eVariant, eSubType, changed = lib.TestSpawn(EntityType.ENTITY_PICKUP, pickupVariant, pickupSubType or 1)
	if eType == EntityType.ENTITY_PICKUP and changed then
		pickupVariant = eVariant
		pickupSubType = eSubType
	end
	return SimpleOffer(EntityType.ENTITY_PICKUP, pickupVariant, pickupSubType or 1)
end

local function TryChooseItem(pool)
	local itemType = game:GetItemPool():GetCollectible(pool, true, GetBargainingRNG():Next())
	local eType, eVariant, eSubType = lib.TestSpawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, itemType)
	if eType == EntityType.ENTITY_PICKUP and eVariant == PickupVariant.PICKUP_COLLECTIBLE then
		return eSubType
	end
end

local function ChooseItem(overridePool)
	local pool = overridePool or lib.GetCurrentRoomItemPool()
	local itemType
	
	local attempts = 0
	while not itemType and attempts < 20 do
		itemType = TryChooseItem(pool)
		attempts = attempts + 1
	end
	if attempts >= 20 then
		lib.LogErr("Bargaining failed to pick an item to offer.")
	end
	if not itemType or itemType < 1 then
		itemType = CollectibleType.COLLECTIBLE_POOP
	end
	
	return itemType
end

local function OfferItem(itemType, hidden)
	if not itemType or itemType < 1 then
		itemType = ChooseItem()
	end
	local offer = SimpleOffer(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, itemType, "gfx/005.100_collectible.anm2")
	local gfx = "gfx/items/collectibles/questionmark.png"
	if not hidden then
		gfx = Isaac.GetItemConfig():GetCollectible(itemType).GfxFileName
	end
	offer.Sprite:ReplaceSpritesheet(1, gfx)
	offer.Sprite:LoadGraphics()
	offer.Sprite:Play("ShopIdle")
	offer.Hidden = hidden
	return offer
end

local function TryChooseTrinket()
	local trinketType = game:GetItemPool():GetTrinket()
	local eType, eVariant, eSubType = lib.TestSpawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, trinketType)
	if eType == EntityType.ENTITY_PICKUP and eVariant == PickupVariant.PICKUP_TRINKET then
		return eSubType
	end
end

local function ChooseTrinket()
	local trinketType
	
	local attempts = 0
	while not trinketType and attempts < 20 do
		trinketType = TryChooseTrinket()
		attempts = attempts + 1
	end
	if attempts >= 20 then
		lib.LogErr("Bargaining failed to pick a trinket to offer.")
	end
	if not trinketType or trinketType < 1 then
		trinketType = TrinketType.TRINKET_PETRIFIED_POOP
	end
	
	return trinketType
end

local function OfferTrinket(trinketType)
	if not trinketType or trinketType < 1 then
		trinketType = ChooseTrinket()
	end
	local offer = SimpleOffer(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, trinketType, "gfx/005.350_trinket.anm2")
	local gfx = Isaac.GetItemConfig():GetTrinket(trinketType).GfxFileName
	offer.Sprite:ReplaceSpritesheet(0, gfx)
	offer.Sprite:LoadGraphics()
	return offer
end

local function ServiceSprite(anim)
	local sprite = Sprite()
	sprite:Load("gfx/samael_entities/bargaining/services.anm2", true)
	--sprite:Play(anim, true)
	sprite:SetAnimation(anim)
	sprite:SetFrame(0)
	sprite.Offset = Vector(3, -14)
	return sprite
end

local ItemValueOverride = {}
ItemValueOverride[CollectibleType.COLLECTIBLE_PLAN_C] = -10
ItemValueOverride[CollectibleType.COLLECTIBLE_CURSED_EYE] = -10
ItemValueOverride[CollectibleType.COLLECTIBLE_ISAACS_HEART] = -10
ItemValueOverride[CollectibleType.COLLECTIBLE_BUM_FRIEND] = -5
ItemValueOverride[CollectibleType.COLLECTIBLE_BROWN_NUGGET] = -1
ItemValueOverride[CollectibleType.COLLECTIBLE_POOP] = 1
ItemValueOverride[CollectibleType.COLLECTIBLE_CLICKER] = 1

local DEAL_OFFERS = {
	COINS = {
		Value = 1,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_COIN, subType) end,
		GetPriority = function(self, player, roomData)
			local data = GetFloorData()
			local numCoins = player:GetNumCoins()
			local mightWantShopMoney = numCoins < roomData.mostExpensiveShopPrice --numCoins < 15 and data.enteredShopThisFloor
			local mightWantSlotsMoney = numCoins < 5 and roomData.slotsInRoom
			local hasMoneyItem = lib.HasItem(player, CollectibleType.COLLECTIBLE_MONEY_EQUALS_POWER)
					or lib.HasItem(player, CollectibleType.COLLECTIBLE_MONEY_IS_POWER)
					or lib.HasItem(player, CollectibleType.COLLECTIBLE_GREEDS_GULLET)
			if mightWantShopMoney or mightWantSlotsMoney or hasMoneyItem then
				return 2
			end
			if numCoins > 30 then
				return 0
			end
			return 1
		end,
		GetMinOffer = function(player, roomData)
			local numCoins = player:GetNumCoins()
			if roomData.mostExpensiveShopPrice and roomData.mostExpensiveShopPrice > numCoins then
				return roomData.mostExpensiveShopPrice - numCoins
			end
			return 1
		end,
		GetMaxOffer = function(player)
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_DEEP_POCKETS) then
				return 999 - player:GetNumCoins()
			end
			return 99 - player:GetNumCoins()
		end,
	},
	KEYS = {
		Value = 5,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_KEY, subType) end,
		GetPriority = function(self, player, roomData)
			local data = GetFloorData()
			local numKeys = player:GetNumKeys()
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_PAY_TO_PLAY) then
				return -1
			end
			if player:HasGoldenKey() or (numKeys > 0 and roomData.keysNeeded == 0) then
				return 0
			end
			if numKeys == 0 and not data.enteredTreasureRoomThisFloor then
				if data.seenTreasureRoomThisFloor then
					return 3
				else
					return 2
				end
			end
			if roomData.keysNeeded > numKeys or (
					numKeys == 0 and not data.enteredShopThisFloor and data.seenShopThisFloor
					and player:GetNumCoins() >= 15 and not player:HasTrinket(TrinketType.TRINKET_STORE_KEY)) then
				return 2
			end
			return 1
		end,
		GetMaxOffer = function(player)
			return 99 - player:GetNumKeys()
		end,
	},
	BOMBS = {
		Value = 5,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_BOMB, subType) end,
		GetPriority = function(self, player, roomData)
			local data = GetFloorData()
			local numBombs = player:GetNumBombs()
			if (numBombs > 0 and roomData.bombsNeeded == 0) or player:HasGoldenBomb() then
				return 0
			end
			local hasAccessToExplosions = player:HasWeaponType(WeaponType.WEAPON_BOMBS)
					or player:HasWeaponType(WeaponType.WEAPON_ROCKETS)
					or (player:HasWeaponType(WeaponType.WEAPON_TEARS) and lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC))
			if hasAccessToExplosions then return -1 end
			for i=0, 2 do
				if player:GetActiveCharge(i) >= 2 then
					local item = player:GetActiveItem(i)
					hasAccessToExplosions = hasAccessToExplosions
							or item == CollectibleType.COLLECTIBLE_BOBS_ROTTEN_HEAD
							or item == CollectibleType.COLLECTIBLE_MR_MEGA
							or item == CollectibleType.COLLECTIBLE_DOCTORS_REMOTE
				end
			end
			if roomData.bombsNeeded > numBombs and not hasAccessToExplosions then
				return 3
			end
			if numBombs == 0 then
				return 2
			end
			return 1
		end,
		GetMaxOffer = function(player)
			return 99 - player:GetNumBombs()
		end,
	},
	HEARTS = {
		Value = 2.5,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_HEART, HeartSubType.HEART_FULL) end,
		GetPriority = function(self, player, roomData)
			local numRedHearts = player:GetHearts()
			local maxRedHearts = player:GetEffectiveMaxHearts()
			local totalHealth = numRedHearts + player:GetSoulHearts() + player:GetBoneHearts()
			local emptyRedHearts = maxRedHearts - numRedHearts
			
			local mightSpendHearts = roomData.mightWantHearts and maxRedHearts >= 4
			
			if (emptyRedHearts >= 4 and totalHealth <= 4) or mightSpendHearts then
				return 2
			elseif player:GetPlayerType() == PlayerType.PLAYER_BETHANY_B then
				if player:GetBloodCharge() < 6 then
					return 2
				end
				return 1
			elseif not player:CanPickRedHearts() or (emptyRedHearts <= 1 or maxRedHearts == 0) then
				return -1
			end
			return 1
		end,
		GetMaxOffer = function(player, roomData)
			if player:GetPlayerType() == PlayerType.PLAYER_BETHANY_B then
				return 6
			end
			local maxHeartsOffer = math.ceil((player:GetEffectiveMaxHearts() - player:GetHearts()) * 0.5)
			if roomData.mightWantHearts then
				maxHeartsOffer = maxHeartsOffer + 3
			end
			return maxHeartsOffer
		end,
	},
	SOUL_HEARTS = {
		Value = 5,
		ChooseType = function(self)
			if GetBargainingRNG():RandomFloat() < 0.15 then
				self.Type = HeartSubType.HEART_BLACK
			else
				self.Type = HeartSubType.HEART_SOUL
			end
		end,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_HEART, self.Type) end,
		GetPriority = function(self, player, roomData)
			if not player:CanPickSoulHearts() or player:GetPlayerType() == PlayerType.PLAYER_THELOST or player:GetPlayerType() == PlayerType.PLAYER_THELOST_B then
				return -1
			end
			local freeSpace = 24 - player:GetMaxHearts() - player:GetBoneHearts()*2 - player:GetSoulHearts() - player:GetBrokenHearts()*2
			if (player:GetSoulHearts() == 0 and freeSpace > 6) or (roomData.soulHeartsNeeded*2 > player:GetSoulHearts()) then
				return 2
			elseif freeSpace > 6 then
				return 1
			end
			return 0
		end,
		GetMaxOffer = function(player)
			local freeSpace = 24 - player:GetMaxHearts() - player:GetBoneHearts()*2 - player:GetSoulHearts() - player:GetBrokenHearts()*2
			return math.min(math.ceil(freeSpace * 0.5), 3)
		end,
	},
	GACHA = {
		Chance = 0.2,
		NoHearts = true,
		NoItems = true,
		ChooseType = function(self)
			if GetBargainingRNG():RandomInt(2) == 0 then
				self.Type = CollectibleType.COLLECTIBLE_BOOSTER_PACK
			else
				self.Type = CollectibleType.COLLECTIBLE_BUDDY_IN_A_BOX
			end
		end,
		CalcValue = function(self)
			self.Value = 10
		end,
		IsItem = true,
		Init = function(self, subType) return OfferItem(self.Type or CollectibleType.COLLECTIBLE_BOOSTER_PACK) end,
		GetPriority = function(self, self, player)
			if self.Type == CollectibleType.COLLECTIBLE_BOOSTER_PACK
					and lib.HasItem(player, CollectibleType.COLLECTIBLE_LITTLE_BAGGY)
					and not lib.HasItem(player, CollectibleType.COLLECTIBLE_STARTER_DECK) then
				return -1
			end
			return 1
		end,
		GetMaxOffer = function(player) return 1 end,
	},
	PILLS = {
		NoHearts = true,
		NoItems = true,
		ChooseType = function(self)
			self.Type = game:GetItemPool():GetPill(GetBargainingRNG():Next())
		end,
		CalcValue = function(self)
			self.Value = 3
			if self.Type then
				if self.Type == PillColor.PILL_GOLD + PillColor.PILL_GIANT_FLAG then
					self.Value = 15
				elseif self.Type == PillColor.PILL_GOLD or self.Type > PillColor.PILL_GIANT_FLAG then
					self.Value = 10
				end
			end
		end,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_PILL, subType or self.Type) end,
		GetPriority = function(self, player)
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_PHD) or lib.HasItem(player, CollectibleType.COLLECTIBLE_FALSE_PHD)
					or lib.HasItem(player, CollectibleType.COLLECTIBLE_VIRGO) or lib.HasItem(player, CollectibleType.COLLECTIBLE_LUCKY_FOOT) then
				return 1
			end
			return 0
		end,
		GetMaxOffer = function(player) return 3 end,
	},
	BATTERIES = {
		NoHearts = true,
		NoItems = true,
		CalcValue = function(self)
			self.Value = 3 + GetBargainingRNG():RandomInt(3)
		end,
		Init = function(self, subType) return OfferPickup(PickupVariant.PICKUP_LIL_BATTERY, subType or 2) end,
		GetPriority = function(self, player)
			if player:NeedsCharge(0) or player:NeedsCharge(1) then
				return 1
			end
			return -1
		end,
		GetMaxOffer = function(player) return 6 end,
	},
	ITEM = {
		ChooseType = function(self)
			self.Type = ChooseItem()
		end,
		CalcValue = function(self)
			local config = Isaac.GetItemConfig()
			if self.Type and self.Type > 0 and config:GetCollectible(self.Type) then
				if ItemValueOverride[self.Type] then
					self.Value = ItemValueOverride[self.Type]
				else
					self.Value = 5 + 5 * config:GetCollectible(self.Type).Quality
				end
			else
				self.Value = 15
			end
		end,
		DecideHidden = function(self)
			local hidden = false
			if game:GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND ~= 0 or GetBargainingRNG():RandomFloat() < 0.10 then
				hidden = true
			end
			self.Hidden = hidden
		end,
		IsItem = true,
		Init = function(self, itemType)
			return OfferItem(self.Type or itemType, self.Hidden)
		end,
		GetPriority = function()
			return 1
		end,
		GetMaxOffer = function()
			return 1
		end,
	},
	TRINKET = {
		GetChance = function()
			if not (FiendFolio and FiendFolio.GolemExists()) then
				return 0.5
			end
		end,
		ChooseType = function(self)
			self.Type = ChooseTrinket()
		end,
		CalcValue = function(self)
			if self.Type and self.Type == TrinketType.TRINKET_TICK then
				self.Value = -5
			else
				self.Value = 5 + GetBargainingRNG():RandomInt(3)
			end
			if self.Type and lib.IsGoldenTrinket(self.Type) then
				self.Value = self.Value * 2
			end
		end,
		Init = function(self, trinketType)
			return OfferTrinket(self.Type or trinketType)
		end,
		GetPriority = function()
			if FiendFolio and FiendFolio.GolemExists() then
				return 2
			end
			return 1
		end,
		GetMaxOffer = function()
			return 1
		end,
	},
	DUPLICATE = {
		CalcValue = function(self)
			self.Value = 20 + 10 * GetBargainingRNG():RandomInt(2)
		end,
		Special = true,
		Chance = 0.15,
		PayOut = function(self, player)
			lib.ScheduleForUpdate(function()
				Isaac.GetPlayer():UseActiveItem(CollectibleType.COLLECTIBLE_DIPLOPIA, UseFlag.USE_NOANIM)
			end)
		end,
		Sprite = ServiceSprite("Diplopia"),
		GetPriority = function(self, player, roomData)
			if roomData.hasItemPedestal then
				return 3
			end
			return -1
		end,
		GetMaxOffer = function()
			return 1
		end,
	},
	REROLL_ITEMS = {
		CalcValue = function(self, roomData)
			self.Value = 12 + 8 * GetBargainingRNG():RandomInt(2)
		end,
		Special = true,
		PayOut = function(self, player)
			player:UseActiveItem(CollectibleType.COLLECTIBLE_D6, UseFlag.USE_NOANIM)
		end,
		Sprite = ServiceSprite("D6"),
		GetPriority = function(self, player, roomData)
			if roomData.hasItemPedestal or roomData.hasActiveItemPedestal then
				return 3
			end
			return -1
		end,
		GetMaxOffer = function()
			return 1
		end,
	},
	REROLL_PICKUPS = {
		CalcValue = function(self, roomData)
			self.Value = 10
		end,
		Special = true,
		PayOut = function(self, player)
			player:UseActiveItem(CollectibleType.COLLECTIBLE_D20, UseFlag.USE_NOANIM)
		end,
		Sprite = ServiceSprite("D20"),
		GetPriority = function(self, player, roomData)
			if roomData.numPickups >= 10 then
				return 2
			elseif roomData.numPickups >= 5 then
				return 1
			end
			return -1
		end,
		GetMaxOffer = function()
			return 1
		end,
	},
	REROLL_SELF = {
		Value = 15,
		Special = true,
		Chance = 0.25,
		PayOut = function(self, player)
			player:UseActiveItem(CollectibleType.COLLECTIBLE_D4, UseFlag.USE_NOANIM)
		end,
		Sprite = ServiceSprite("D4"),
		GetPriority = function(self, player)
			if player:GetCollectibleCount() > 6 then
				return 1
			end
			return -1
		end,
		GetMaxOffer = function()
			return 1
		end,
	},
}

--------------------------------------------------
---- DESIRE SENSORS
--------------------------------------------------

function mod:BargainingNewRoomChecks()
	local room = game:GetRoom()
	local data = GetFloorData()
	if room:GetType() == RoomType.ROOM_SHOP then
		data.enteredShopThisFloor = true
		data.seenShopThisFloor = true
	end
	if room:GetType() == RoomType.ROOM_SHOP then
		data.enteredTreasureRoomThisFloor = true
		data.seenTreasureRoomThisFloor = true
	end
	for i=0, DoorSlot.NUM_DOOR_SLOTS-1 do
		local door = room:GetDoor(i)
		if door then
			if door.TargetRoomType == RoomType.ROOM_SHOP and door:IsLocked() then
				data.seenShopThisFloor = true
			elseif door.TargetRoomType == RoomType.ROOM_TREASURE and door:IsLocked() then
				data.seenTreasureRoomThisFloor = true
			end
		end
	end
	local spirit = FindSpiritOfBargaining()
	if spirit then
		spirit:Update()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.BargainingNewRoomChecks)

local function GetRoomDesireData()
	local room = game:GetRoom()
	
	local roomData = {}
	roomData.keysNeeded = 0
	roomData.bombsNeeded = 0
	roomData.hasItemPedestal = false
	roomData.hasActiveItemPedestal = false
	roomData.numPickups = 0
	roomData.slotsInRoom = false
	roomData.mostExpensiveShopPrice = 0
	roomData.soulHeartsNeeded = 0
	roomData.mightWantHearts = false
	
	-- Pickups / chests
	for _, pickup in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP)) do
		pickup = pickup:ToPickup()
		if (pickup.Variant == PickupVariant.PICKUP_ETERNALCHEST
				or pickup.Variant == PickupVariant.PICKUP_OLDCHEST
				or pickup.Variant == PickupVariant.PICKUP_LOCKEDCHEST
				or pickup.Variant == PickupVariant.PICKUP_MOMSCHEST)
				and pickup.SubType == ChestSubType.CHEST_CLOSED then
			roomData.keysNeeded = roomData.keysNeeded + 1
		elseif pickup.Variant == PickupVariant.PICKUP_MEGACHEST and pickup.SubType == ChestSubType.CHEST_CLOSED then
			roomData.keysNeeded = roomData.keysNeeded + 3
		elseif pickup.Variant == PickupVariant.PICKUP_BOMBCHEST and pickup.SubType == ChestSubType.CHEST_CLOSED then
			roomData.bombsNeeded = roomData.bombsNeeded + 1
		end
		if pickup.Variant == PickupVariant.PICKUP_COLLECTIBLE then
			if pickup.SubType > 0 then
				local item = Isaac.GetItemConfig():GetCollectible(pickup.SubType)
				if item and item.Type == ItemType.ITEM_ACTIVE then
					roomData.hasActiveItemPedestal = true
				else
					roomData.hasItemPedestal = true
				end
			end
		elseif pickup:CanReroll() and not (lib.IsVanillaChest(pickup) and pickup.SubType == ChestSubType.CHEST_OPENED) then
			roomData.numPickups = roomData.numPickups + 1
		end
		if pickup.Price > roomData.mostExpensiveShopPrice then
			roomData.mostExpensiveShopPrice = pickup.Price
		elseif pickup.Price == PickupPrice.PRICE_THREE_SOULHEARTS then
			roomData.soulHeartsNeeded = 3
		elseif pickup.Price == PickupPrice.PRICE_ONE_HEART_AND_TWO_SOULHEARTS then
			roomData.soulHeartsNeeded = math.max(roomData.soulHeartsNeeded, 2)
		end
	end
	
	-- Slot machines / beggars
	for _, slot in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT)) do
		if slot.Variant == 3 or slot.Variant == 4 or slot.Variant == 10 or slot.Variant == 11 or slot.Variant == 13 or slot.Variant == 16 then
			roomData.slotsInRoom = true
		elseif slot.Variant == 9 then
			roomData.bombsNeeded = roomData.bombsNeeded + 1
		elseif slot.Variant == 7 then
			roomData.keysNeeded = roomData.keysNeeded + 1
		elseif slot.Variant == 2 or slot.Variant == 5 or slot.Variant == 15 or slot.Variant == 17 then
			roomData.mightWantHearts = true
		end
	end
	
	-- Doors
	for i=0, DoorSlot.NUM_DOOR_SLOTS-1 do
		local door = room:GetDoor(i)
		if door then
			local variant = door:GetVariant()
			if variant == DoorVariant.DOOR_LOCKED then
				roomData.keysNeeded = roomData.keysNeeded + 1
			elseif variant == DoorVariant.DOOR_LOCKED_DOUBLE then
				roomData.keysNeeded = roomData.keysNeeded + 2
			elseif variant == DoorVariant.DOOR_LOCKED_CRACKED then
				roomData.bombsNeeded = roomData.bombsNeeded + 1
			end
		end
	end
	
	-- Grid Entities
	local foundGoldRocks = false
	
	for i=0, room:GetGridSize() do
		local gridEntity = room:GetGridEntity(i)
		if gridEntity then
			if gridEntity:GetType() == GridEntityType.GRID_LOCK then
				roomData.keysNeeded = roomData.keysNeeded + 1
			elseif gridEntity:GetType() == GridEntityType.GRID_ROCK_GOLD then
				foundGoldRocks = true
			elseif gridEntity:GetType() == GridEntityType.GRID_ROCKT or gridEntity:GetType() == GridEntityType.GRID_ROCK_SS then
				roomData.bombsNeeded = roomData.bombsNeeded + 1
			end
		end
	end
	
	if foundGoldRocks and roomData.bombsNeeded == 0 then
		roomData.bombsNeeded = 1
	end
	
	return roomData
end

--------------------------------------------------
---- DEAL GENERATION
--------------------------------------------------

local function GetAffordablePriceRange(player, costInfo, offerInfo, priority)
	local numHeld = costInfo.GetNumHeld(player, offerInfo)
	if numHeld == 0 then return end
	local minValue = costInfo.Value
	if costInfo.ValueDeterioratesUntilQuantity then
		local kMin = 0.5
		minValue = math.max(lib.Lerp(costInfo.Value, kMin, (numHeld-1) / costInfo.ValueDeterioratesUntilQuantity), kMin)
	end
	if costInfo.ValueIncreasesBy then
		minValue = minValue + (costInfo.ValueIncreasesBy*(numHeld-1)*(numHeld)*0.5)/numHeld
	end
	local maxValue = numHeld * minValue
	-- Ease up on the prices for priority sales, somewhat, if the player can't afford them otherwise.
	if priority > 1 and offerInfo.Value > maxValue and maxValue*2 >= offerInfo.Value then
		minValue = offerInfo.Value / numHeld
		maxValue = offerInfo.Value
	end
	if numHeld >= 1 then return {Min = minValue, Max = maxValue} end
end

local function ChooseDeals(player, customSettings)
	local roomData = GetRoomDesireData()
	
	local selectedDeals = {}
	local offers = {}
	
	-- Initialize the list of potential offers.
	-- Offers are divided up by their "priority", which is higher if the player might desire it.
	for dealName, offerInfo in pairs(DEAL_OFFERS) do
		local skip = customSettings and (
			(customSettings.NO_ITEMS and offerInfo.IsItem)
			or (customSettings.NO_SERVICES and offerInfo.Special)
			or (customSettings.BLACKLIST and customSettings.BLACKLIST[dealName])
			or (customSettings.WHITELIST and not customSettings.WHITELIST[dealName]))
		
		local chance = offerInfo.Chance or (offerInfo.GetChance and offerInfo:GetChance())
		if not skip and (not chance or GetBargainingRNG():RandomFloat() < chance) then
			local priority = offerInfo:GetPriority(player, roomData)
			offerInfo.Name = dealName
			-- Pick the item/trinket/etc to offer, if applicable.
			if offerInfo.ChooseType then
				offerInfo:ChooseType()
			end
			-- Calculate the value of this offer, if needed.
			-- (For example, item value varies based on quality.)
			if offerInfo.CalcValue then
				offerInfo:CalcValue()
			end
			if offerInfo.DecideHidden then
				offerInfo:DecideHidden()
			end
			--lib.Log(dealName .. " has priority " .. priority)
			table.insert(lib.GetOrInit(offers, priority), offerInfo)
		end
	end
	
	-- Iterate through the potential offers, starting with the highest priority.
	for i=5,0,-1 do
		if offers[i] then
			-- Shuffle the order of the offers of this priority before iterating over them.
			lib.Shuffle(offers[i])
			for priority, offerInfo in pairs(offers[i]) do
				-- Chosen costs for this offer that the player can afford.
				local possibleCosts = {}
				-- Cost types that are eligible for this offer.
				local possibleCostTypes
				
				if offerInfo.Value <= 0 then
					-- Always use coins for non-positive values.
					table.insert(possibleCosts, {Quantity=1, CostType=DEAL_COSTS.COINS, CostAmount=offerInfo.Value})
				else
					-- Always ask for Store Credit / Your Soul as the cost if held.
					if player:HasTrinket(TrinketType.TRINKET_STORE_CREDIT) then
						possibleCostTypes = possibleCostTypes or {}
						possibleCostTypes["TRINKET"] = RequireTrinket(TrinketType.TRINKET_STORE_CREDIT)
						possibleCostTypes["TRINKET"].Value = 10 + GetBargainingRNG():RandomInt(16)
					end
					if player:HasTrinket(TrinketType.TRINKET_YOUR_SOUL) then
						possibleCostTypes = possibleCostTypes or {}
						possibleCostTypes["TRINKET"] = RequireTrinket(TrinketType.TRINKET_YOUR_SOUL)
						possibleCostTypes["TRINKET"].Value = 25
					end
					if not possibleCostTypes then
						possibleCostTypes = lib.ShallowCopy(DEAL_COSTS)
						-- Pick a random item/trinket from the player and add that as a possible cost for this offer.
						if not offerInfo.NoItems then
							possibleCostTypes["ITEM"] = RequireRandomItem(player)
						end
						possibleCostTypes["TRINKET"] = RequireRandomTrinket(player)
					end
					
					if offerInfo.NoHearts or player:GetPlayerType() == PlayerType.PLAYER_KEEPER or player:GetPlayerType() == PlayerType.PLAYER_KEEPER_B then
						possibleCostTypes["HEARTS"] = nil
					end
					
					-- Consider each possible cost type.
					for costName, costInfo in pairs(possibleCostTypes) do
						if costName ~= offerInfo.Name then
							local range = GetAffordablePriceRange(player, costInfo, offerInfo, priority)
							if range then
								local canAffordOne = range.Max >= offerInfo.Value
								if canAffordOne then
									local maxAffordable = math.max(math.floor(range.Max / offerInfo.Value), 1)
									local minQuantity = math.ceil((range.Min / offerInfo.Value) * 0.5)
									if offerInfo.GetMinOffer then
										minQuantity = math.max(minQuantity, offerInfo.GetMinOffer(player, roomData))
									end
									local quantity = math.max(minQuantity, GetBargainingRNG():RandomInt(maxAffordable)+1)
									quantity = math.min(quantity, offerInfo.GetMaxOffer(player, roomData))
									local calculatedCost = math.min(math.ceil((offerInfo.Value * quantity) / range.Min), costInfo.GetNumHeld(player, offerInfo))
									if costInfo.CanAfford(player, calculatedCost) then
										table.insert(possibleCosts, {
											OfferQuantity = quantity,
											CostName = costName,
											CostType = costInfo.Type,
											CostQuantity = calculatedCost,
										})
									end
								end
							end
						end
					end
				end
				
				-- If at least one valid, affordable cost was found, make a deal.
				if #possibleCosts > 0 then
					--[[local offer
					if offerInfo.Special then
						deal = offerInfo
					else
						deal = offerInfo:Init()
					end]]
					local tab = possibleCosts[GetBargainingRNG():RandomInt(#possibleCosts)+1]
					local deal = {
						Offer = {
							Name = offerInfo.Name,
							Type = offerInfo.Type,
							Value = offerInfo.Value,
							Quantity = tab.OfferQuantity or 1,
							Hidden = offerInfo.Hidden,
						},
						Cost = {
							Name = tab.CostName,
							Type = tab.CostType,
							Quantity = tab.CostQuantity,
						}
					}
					
					--deal.CostSprite = deal.CostType.InitSprites(deal.CostAmount)
					
					table.insert(selectedDeals, deal)
					if #selectedDeals == 3 then
						return selectedDeals
					end
				end
			end
		end
	end
	return selectedDeals
end

local function SpawnDeal(pos, deal, parent)
	local dealEntity = lib.Spawn(mod.ENTITIES.BARGAINING_DEAL, pos, lib.ZeroVector, parent):ToNPC()
	dealEntity.Parent = parent
	dealEntity.DepthOffset = 1
	mod:InitDeal(dealEntity, deal)
	return dealEntity
end

local function RefreshDeal(deal)
	if not deal or not deal.CostAmount then return end
	deal.CostSprite = deal.CostType.InitSprites(deal.CostAmount)
end

local BAD_OFFERS = {
	{Name="ITEM", Type = CollectibleType.COLLECTIBLE_PLAN_C},
	{Name="ITEM", Type = CollectibleType.COLLECTIBLE_POOP},
	{Name="ENTITY", Type = EntityType.ENTITY_PROJECTILE},
	{Name="ENTITY", Type = EntityType.ENTITY_SPIDER},
	{Name="ENTITY", Type = EntityType.ENTITY_GAPER},
	{Name="ENTITY", Type = EntityType.ENTITY_BOMB, Variant = BombVariant.BOMB_TROLL},
}

local DEAL_OFFSETS = {
	Vector(45, 12),
	Vector(0, 41),
	Vector(-46, 12),
}

local function GenerateBargainingDeals(spirit, player, reloadingExistingDeals)
	if not spirit or not player or not player:ToPlayer() then
		lib.LogErr("Error initializing Bargaining deals.")
	end
	
	local data = spirit:GetData()
	local pos = spirit.Position - Vector(0, 10)
	
	local deals = GetRoomData().Deals or {}
	if #deals == 0 and not data.bargainingIsMad then
		deals = ChooseDeals(player)
	end
	
	-- "Bad deals" if bargaining is mad or the player can't afford anything.
	if #deals == 0 then
		data.bargainingState = "MAD"
		GetRoomData().Mad = true
	else
		GetRoomData().Mad = false
	end
	while #deals < 3 do
		local badOffer = lib.PickRandom(BAD_OFFERS, GetBargainingRNG())
		if badOffer.Name == "ENTITY" then
			badOffer.Quantity = 3 + (GetBargainingRNG():Next() % 19)
		end
		table.insert(deals, {
			Offer = badOffer,
			Cost = {
				Name = "COINS",
				Quantity = (GetBargainingRNG():Next() % 11) * (-1),
			}
		})
	end
	
	if spirit:GetData().bargainingDeals then
		for _, entity in pairs(spirit:GetData().bargainingDeals) do
			entity:Remove()
		end
	end
	spirit:GetData().bargainingDeals = {}
	local dealEntities = spirit:GetData().bargainingDeals
	
	GetRoomData().Deals = deals
	
	if not reloadingExistingDeals then
		sfxManager:Play(SoundEffect.SOUND_SUMMONSOUND, 1, 0, false, 1.1)
	end
	
	for i=1,3 do
		local deal = deals[i]
		if deal then
			local spawnPos = pos + DEAL_OFFSETS[i]
			local dealEntity = SpawnDeal(spawnPos, deal, spirit, i)
			if not reloadingExistingDeals then
				Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, spawnPos, lib.ZeroVector, dealEntity)
			end
			table.insert(dealEntities, dealEntity)
		end
	end
end

--------------------------------------------------
---- FINDING A SPAWN POSITION FOR THE SPIRIT
--------------------------------------------------

local function CheckTile(x, y, tab)
	local room = game:GetRoom()
	
	local memo = lib.GetOrInit(tab, x)[y]
	if memo ~= nil then
		return memo
	end
	
	local gridId = y * room:GetGridWidth() + x
	local gridPos = room:GetGridPosition(gridId)
	--local freePos = room:FindFreeTilePosition(gridPos, 1)
	local freePos = room:FindFreePickupSpawnPosition(gridPos, 0, true, false)
	local isFree = (gridPos.X == freePos.X and gridPos.Y == freePos.Y)
	tab[x][y] = isFree
	return isFree
end

local kPreferredPlayerDist = 75

local function PickBestSpawnPos(posCandidates)
	for i=4,0,-1 do
		if posCandidates[i] then
			local best
			for _, tab in pairs(posCandidates[i]) do
				if not best or (tab.Dist < best.Dist and not (tab.Dist < kPreferredPlayerDist and best.Dist >= kPreferredPlayerDist)) then
					best = tab
				end
			end
			if best then return best.Pos end
		end
	end
end

local function FindBargainingSpawnPos(startPos)
	local room = game:GetRoom()
	
	local slots = Isaac.FindByType(EntityType.ENTITY_SLOT)
	
	local posCandidates = {}
	
	local tab = {}
	for y = 1, room:GetGridHeight() - 2 do
		for x = 1, room:GetGridWidth() - 2 do
			local gridId = y * room:GetGridWidth() + x
			local pos = room:GetGridPosition(gridId)
			
			local valid = CheckTile(x, y, tab) and CheckTile(x-1, y, tab) and CheckTile(x+1, y, tab)
					and CheckTile(x, y+1, tab) and CheckTile(x-1, y+1, tab) and CheckTile(x+1, y+1, tab)
					and room:IsPositionInRoom(pos, 0)
			
			if valid then
				local extraSpace = y > 0 and y < room:GetGridHeight() - 2 and x > 1 and x < room:GetGridWidth() - 2
					and CheckTile(x-2, y, tab) and CheckTile(x+2, y, tab) and CheckTile(x-2, y+1, tab) and CheckTile(x+2, y+1, tab)
					and CheckTile(x-1, y-1, tab) and CheckTile(x, y-1, tab) and CheckTile(x+1, y-1, tab)
					and CheckTile(x-1, y+2, tab) and CheckTile(x, y+2, tab) and CheckTile(x+1, y+2, tab)
				
				local dist = pos:Distance(startPos)
				
				local nearby = Isaac.FindInRadius(pos, 70, EntityPartition.PICKUP | EntityPartition.PLAYER)
				
				local priority = 0
				
				if #nearby == 0 then
					local slotNearby = false
					for _, slot in pairs(slots) do
						if not slotNearby and slot.Position:Distance(pos) < 70 then
							slotNearby = true
						end
					end
					if not slotNearby then
						priority = priority + 2
					end
				end
				if extraSpace then
					priority = priority + 1
				end
				if dist >= kPreferredPlayerDist then
					priority = priority + 1
				end
				
				table.insert(lib.GetOrInit(posCandidates, priority), {Dist=dist, Pos=pos})
			end
		end
	end
	
	return PickBestSpawnPos(posCandidates) or startPos
end

--------------------------------------------------
---- MISC BARGAINING MANAGEMENT
--------------------------------------------------

function mod:BargainingItemUse(_, player, useFlags)
	GetRoomData().Deals = {}
	GetRoomData().Mad = false
	
	local spiritOfBargaining = FindSpiritOfBargaining()
	if spiritOfBargaining then
		GenerateBargainingDeals(spiritOfBargaining, player)
	else
		local pos = FindBargainingSpawnPos(player.Position)
		spiritOfBargaining = Isaac.Spawn(SPIRIT_OF_BARGAINING.ID, SPIRIT_OF_BARGAINING.Var, 0, pos, lib.ZeroVector, player)
	end
end
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.BargainingItemUse, BARGAINING_CHIP)

local spiritOfBargainingEffect = nil

function mod:SpawnBargainingChips()
	local level = game:GetLevel()
	local room = game:GetRoom()
	local roomType = room:GetType() 
	local roomData = lib.GetOrInit(mod:GetFloorData("BargainingChip"), ""..game:GetLevel():GetCurrentRoomDesc().ListIndex)
	
	local isStartingRoom = level:GetCurrentRoomIndex() == level:GetStartingRoomIndex()
	
	if not (roomType == RoomType.ROOM_SHOP or roomType == RoomType.ROOM_TREASURE or roomType == RoomType.ROOM_BOSS
			or roomType == RoomType.ROOM_ARCADE or roomType == RoomType.ROOM_SECRET or roomType == RoomType.ROOM_CHEST
			or roomType == RoomType.ROOM_BLACK_MARKET or roomType == RoomType.ROOM_PLANETARIUM or isStartingRoom) then
		return
	end
	
	local player
	local num = 0
	for _, p in pairs(lib.GetPlayers()) do
		num = num + p:GetCollectibleNum(BARGAINING)
		if not player then
			player = p
		end
	end
	if (roomData.numChipsSpawned or 0) < num then
		for i=(roomData.numChipsSpawned or 0), num-1 do
			--local prepos = room:GetRandomPosition(10)
			--local prepos = Isaac.GetFreeNearPosition(player.Position, 10)
			local prepos = player.Position + Vector(150, 0):Rotated(GetBargainingRNG():RandomInt(360))
			local pos = room:FindFreePickupSpawnPosition(prepos, 15)
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, BARGAINING_CHIP, pos, lib.ZeroVector, nil)
			if not spiritOfBargainingEffect or not spiritOfBargainingEffect:Exists() then
				spiritOfBargainingEffect = Isaac.Spawn(EntityType.ENTITY_EFFECT, SPIRIT_OF_BARGAINING_EFFECT, 0, pos, lib.ZeroVector, nil)
			end
		end
		roomData.numChipsSpawned = num
	end
end

local checkedBargainingChipsThisRoom = false

function mod:CheckBargainingChips()
	local room = game:GetRoom()
	
	if room:GetFrameCount() == 1 then
		checkedBargainingChipsThisRoom = false
	end
	
	if room:IsClear() and not checkedBargainingChipsThisRoom then
		mod:SpawnBargainingChips()
	end
	
	GetRoomData().LastUpdate = game:GetFrameCount()
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.CheckBargainingChips)

function mod:PlayerCheckBargainingChips(player)
	local data = player:GetData()
	
	local numSpiritOfBargainingHeld = player:GetCollectibleNum(BARGAINING)
	
	if numSpiritOfBargainingHeld > (data.numSpiritOfBargainingHeld or 0) then
		mod:SpawnBargainingChips()
	end
	
	data.numSpiritOfBargainingHeld = numSpiritOfBargainingHeld
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.PlayerCheckBargainingChips)

function mod:SpiritOfBargainingEffectInit(eff)
	local sprite = eff:GetSprite()
	if eff.SubType == 2 then
		sprite:Play("Fall", true)
		eff.DepthOffset = -255
		eff.SpriteOffset = Vector(0, 5)
	elseif eff.SubType == 1 then
		sprite:Play("Leave", true)
	else
		sprite:Play("Give", true)
		eff.SpriteOffset = Vector(5, -25)
		eff.Color = Color(1,1,1,0.5)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.SpiritOfBargainingEffectInit, SPIRIT_OF_BARGAINING_EFFECT)

function mod:SpiritOfBargainingEffect(eff)
	local sprite = eff:GetSprite()
	if sprite:IsFinished("Give") or sprite:IsFinished("Leave") then
		eff:Remove()
	elseif sprite:IsFinished("Fall") then
		sprite:Play("Unroll", true)
	end
	
	if not eff.Parent then
		eff.Parent = FindSpiritOfBargaining()
	end
	
	if eff.SubType == 2 and (not eff.Parent or not eff.Parent:Exists()) then
		eff:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SpiritOfBargainingEffect, SPIRIT_OF_BARGAINING_EFFECT)

--------------------------------------------------
---- DEAL UPDATE CODE
--------------------------------------------------

local function PayoutDeal(deal, player, isDuplication, isFreeDeathDeal)
	if not deal --[[or not deal.Parent]] then return end
	
	if deal.SubType == mod.ENTITIES.STANDALONE_DEAL.Sub then
		local sd = GetRoomData().StandaloneDeals
		if sd then
			sd[""..deal.InitSeed] = nil
		end
	end
	
	local data = deal:GetData()
	local dealData = data.bargainingDealData
	
	if not dealData then return end
	
	if dealData.Offer.Special then
		dealData.Offer:PayOut(player)
	else
		dealData.Offer:PayOut(deal.Position, dealData.OfferQuantity)
	end
	
	if isDuplication then return end
	
	player.ItemHoldCooldown = 60
	
	if dealData.IsDeathDeal and not isFreeDeathDeal then
		mod:DeathDealTaken(player, dealData.Cost.Type)
	elseif deal.SubType == mod.ENTITIES.BARGAINING_DEAL.Sub then
		GetRoomData().Deals = nil
		-- Replace with exit animation
		sfxManager:Play(SoundEffect.SOUND_SLOTSPAWN, 1, 0, false, 1.1)
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, deal.Parent.Position, lib.ZeroVector, nil)
		Isaac.Spawn(EntityType.ENTITY_EFFECT, SPIRIT_OF_BARGAINING_EFFECT, 1, deal.Parent.Position, lib.ZeroVector, nil)
	end
	
	if deal.Parent then
		deal.Parent:Remove()
	else
		sfxManager:Play(SoundEffect.SOUND_SLOTSPAWN, 1, 0, false, 1.1)
		deal:Remove()
	end
end

function mod:InitDeal(dealEntity, dealInfo)
	local data = dealEntity:GetData()
	data.bargainingDealInfo = dealInfo
	
	local tab = {}
	
	local offerName = dealInfo.Offer.Name
	local offer
	if offerName == "ENTITY" then
		offer = SimpleOffer(dealInfo.Offer.Type, dealInfo.Offer.Variant, dealInfo.Offer.SubType)
	else
		offer = DEAL_OFFERS[offerName]
	end
	offer.Type = dealInfo.Offer.Type
	offer.Value = dealInfo.Offer.Value
	offer.Hidden = dealInfo.Offer.Hidden
	if offer.Special or offerName == "ENTITY" then
		tab.Offer = offer
	else
		tab.Offer = offer:Init()
	end
	tab.Offer.ItemPoolOverride = dealInfo.ItemPoolOverride
	tab.OfferQuantity = dealInfo.Offer.Quantity or 1
	
	local cost
	local costName = dealInfo.Cost.Name
	if costName == "ITEM" then
		cost = RequireItem(dealInfo.Cost.Type)
	elseif costName == "TRINKET" then
		cost = RequireTrinket(dealInfo.Cost.Type)
	elseif DEAL_COSTS[costName] then
		cost = DEAL_COSTS[costName]
	else
		lib.LogErr("Deal init error: Unknown cost `" .. (costName or "<nil>") .. "` for offer `" .. offerName .. "`.")
		return
	end
	tab.Cost = cost
	tab.CostQuantity = dealInfo.Cost.Quantity or 1
	tab.CostSprite = cost.InitSprites(tab.CostQuantity)
	
	tab.IsDeathDeal = dealInfo.IsDeathDeal
	
	data.bargainingDealData = tab
	
	data["EID_Description"] = nil
	
	if EID and tab.Offer.Type and not tab.Offer.Hidden and EID:getDescriptionData(tab.Offer.Type, tab.Offer.Variant or 0, tab.Offer.SubType or 0) then
		local eidData = EID:getDescriptionObj(tab.Offer.Type, tab.Offer.Variant or 0, tab.Offer.SubType or 0)
		local name = eidData.Name
		local desc = eidData.Description
		if name and desc then
			data["EID_Description"] = {
				Name = name,
				Description = desc,
			}
		end
	end
end

if EID then
	local function EidModifierCondition(descObj)
		return descObj.ObjType == mod.ENTITIES.BARGAINING_DEAL.ID and descObj.ObjVariant == mod.ENTITIES.BARGAINING_DEAL.Var
	end
	local function EidModifierCallback(descObj)
		if descObj.Entity then
			local data = descObj.Entity:GetData().bargainingDealData
			if data then
				return EID:getDescriptionObj(data.Offer.Type, data.Offer.Variant, data.Offer.SubType)
			end
		end
	end
	EID:addDescriptionModifier("SAMAEL_DEALS", EidModifierCondition, EidModifierCallback)
end

local function FixDealEntity(deal)
	deal.DepthOffset = 1
	deal.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ENEMIES
	deal.GridCollisionClass = 0
	deal:AddEntityFlags(EntityFlag.FLAG_NO_TARGET)
	
	local room = game:GetRoom()
	local index = room:GetGridIndex(deal.Position)
	if room:GetGridPath(index) < 900 then
		room:SetGridPath(index, 900)
	end
end

function mod:BargainingDealInit(deal)
	if deal.Variant ~= mod.ENTITIES.BARGAINING_DEAL.Var then return end
	deal:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	FixDealEntity(deal)
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.BargainingDealInit, mod.ENTITIES.BARGAINING_DEAL.ID)

function mod:BargainingDealUpdate(deal)
	if deal.Variant ~= mod.ENTITIES.BARGAINING_DEAL.Var then return end
	
	FixDealEntity(deal)
	
	if deal.FrameCount == 0 then return end
	
	local isStandaloneDeal = deal.SubType == mod.ENTITIES.STANDALONE_DEAL.Sub
	local isBargainingDeal = deal.SubType == mod.ENTITIES.BARGAINING_DEAL.Sub
	
	local spiritOfBargaining = FindSpiritOfBargaining()
	
	if not isStandaloneDeal and (not deal.Parent or not deal.Parent:Exists() or (isBargainingDeal and not spiritOfBargaining)) then
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, deal.Position, lib.ZeroVector, deal)
		deal:Remove()
		return
	end
	
	local roomData = GetRoomData()
	local data = deal:GetData()
	
	if not data.bargainingDealData then
		if isStandaloneDeal then
			local generatedDeals = ChooseDeals(lib.PickRandom(lib.GetPlayers(), deal:GetDropRNG()))
			local dealInfo = lib.PickRandom(generatedDeals, deal:GetDropRNG())
			if not dealInfo then
				deal:Remove()
				return
			end
			mod:InitDeal(deal, dealInfo)
		else
			lib.LogErr("Removing deal with no data")
			deal:Remove()
			return
		end
	end
	
	if data.bargainingDealInfo and isStandaloneDeal then
		if not roomData.StandaloneDeals then
			roomData.StandaloneDeals = {}
		end
		roomData.StandaloneDeals[""..deal.InitSeed] = {
			DATA = data.bargainingDealInfo,
			FRAME = game:GetFrameCount(),
			POSX = deal.Position.X,
			POSY = deal.Position.Y,
		}
	end
	
	local dealData = data.bargainingDealData
	
	if not data.TargetPosition then
		data.TargetPosition = deal.Position
	end
	deal.Position = data.TargetPosition
	deal.Velocity = lib.ZeroVector
	
	if isBargainingDeal and not dealData.Hidden and dealData.Offer.Type == EntityType.ENTITY_PICKUP
			and IsGlitchItem(dealData.Offer.Variant, dealData.Offer.SubType) then
		if not data.originalCost then
			data.originalCost = dealData.CostQuantity
		end
		local p0 = Isaac.GetPlayer(0) -- Fallback
		local player = (spiritOfBargaining.SpawnerEntity or p0):ToPlayer() or p0
		dealData.CostQuantity = GetBargainingRNG():RandomInt(math.min(data.originalCost * 2, dealData.Cost.GetNumHeld(player, dealData))+1)
		dealData.CostSprite = dealData.Cost.InitSprites(dealData.CostQuantity)
	end
	
	for _, pickup in pairs(Isaac.FindInRadius(deal.Position, deal.Size, EntityPartition.PICKUP)) do
		pickup = pickup:ToPickup()
		if pickup then
			local pushDir = (pickup.Position - deal.Position):Normalized()
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
	
	if deal.FrameCount < 30 then return end
	
	for i, player in pairs(lib.GetPlayers()) do
		local dist = deal.Position:Distance(player.Position)
		if dist < deal.Size and dealData.Cost.CanAfford(player, dealData.CostQuantity) then
			dealData.Cost.Pay(player, dealData.CostQuantity)
			PayoutDeal(deal, player)
			return
		elseif isBargainingDeal and dist < deal.Size*2.5 and deal.Parent:GetData().bargainingState ~= "MAD"
				and deal.Parent:GetData().bargainingState ~= "ANNOYED" and dealData.Cost.CanAfford(player, dealData.CostQuantity) then
			deal.Parent:GetData().bargainingState = "EXCITED"
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.BargainingDealUpdate, mod.ENTITIES.BARGAINING_DEAL.ID)

mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, function(_, ent, collider)
	if ent.Variant == mod.ENTITIES.BARGAINING_DEAL.Var then
		return true
	end
end, mod.ENTITIES.BARGAINING_DEAL.ID)

local function dRender(sprite, screenPos, color)
	if sprite then
		sprite.Color = color
		sprite:Render(screenPos, lib.ZeroVector, lib.ZeroVector)
		if not game:IsPaused() then
			sprite:Update()
		end
	end
end

local font = Font()
font:Load("font/pftempestasevencondensed.fnt")

function mod:BargainingDealRenderPrice(deal)
	if deal.Variant ~= mod.ENTITIES.BARGAINING_DEAL.Var then return end
	
	local data = deal:GetData()
	local dealData = data.bargainingDealData
	
	if not dealData or game:GetRoom():GetRenderMode() == RenderMode.RENDER_WATER_REFLECT then return end
	
	local screenPos = Isaac.WorldToScreen(deal.Position)
	
	dRender(dealData.CostSprite, screenPos, deal.Color)
	dRender(dealData.Offer.Sprite, screenPos, deal.Color)
	
	if dealData.OfferQuantity and dealData.OfferQuantity > 1 then
		font:DrawString("x" .. dealData.OfferQuantity, screenPos.X + 4, screenPos.Y - 10, KColor(1,1,1,1,0,0,0),0,true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_RENDER, mod.BargainingDealRenderPrice, mod.ENTITIES.BARGAINING_DEAL.ID)

--------------------------------------------------
---- S.o.B. UPDATE
--------------------------------------------------

function mod:SpiritOfBargainingInit(entity)
	if entity.Variant ~= SPIRIT_OF_BARGAINING.Var then return end
	
	if FindSpiritOfBargaining() then
		entity:Remove()
		return
	end
	
	if GetRoomData().Mad then
		GetRoomData().Mad = false
		entity:Remove()
		return
	end
	
	for _, eff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, SPIRIT_OF_BARGAINING_EFFECT)) do
		eff:Remove()
	end
	
	entity:GetData().bargainingRug = Isaac.Spawn(EntityType.ENTITY_EFFECT, SPIRIT_OF_BARGAINING_EFFECT, 2, entity.Position, lib.ZeroVector, nil)
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.SpiritOfBargainingInit, SPIRIT_OF_BARGAINING.ID)

function mod:SpiritOfBargainingUpdate(entity)
	if entity.Variant ~= SPIRIT_OF_BARGAINING.Var then return end
	
	entity.DepthOffset = 1
	
	local data = entity:GetData()
	local runData = GetRunData()
	local sprite = entity:GetSprite()
	local reloadExistingDeals = false
	
	if not data.Initialized then
		local existingDeals = GetRoomData().Deals
		
		if existingDeals and #existingDeals > 0 then
			sprite:Play("Appear", true)
			sprite:SetLastFrame()
			data.bargainingState = "NORMAL"
			reloadExistingDeals = true
			if data.bargainingRug then
				data.bargainingRug:GetSprite():Play("Idle", true)
			end
		elseif runData.wasBombed then
			sprite:Play("AppearRude", true)
			data.bargainingIsMad = true
			runData.wasBombed = false
			data.bargainingState = "MAD"
		else
			sprite:Play("Appear", true)
			data.bargainingState = "NORMAL"
		end
		
		data.Initialized = true
	end
	
	if not data.dealsSpawned and (reloadExistingDeals or sprite:IsEventTriggered("Spawn")) then
		local player = (entity.SpawnerEntity or Isaac.GetPlayer(0)):ToPlayer()
		GenerateBargainingDeals(entity, player, reloadExistingDeals)
		data.dealsSpawned = true
	end
	
	if sprite:IsFinished("Appear") or sprite:IsFinished("AppearRude") then
		sprite:SetFrame("Idle", 0)
		if data.bargainingIsMad then
			sprite:SetLayerFrame(1, 1)
			sprite:SetLayerFrame(3, 1)
		end
		sprite:SetOverlayRenderPriority(true)
		sprite:PlayOverlay("Body", true)
	end
	
	if sprite:GetAnimation() == "Idle" then
		if data.bargainingState ~= "ANNOYED" then
			if data.bargainingBomb and not data.bargainingBomb:Exists() then
				data.bargainingBomb = nil
				data.bargainingState = "NORMAL"
			end
			if data.bargainingBomb then
				data.bargainingState = "DISAPPOINTED"
			else
				local nearbyPlayers = Isaac.FindInRadius(entity.Position, 125, EntityPartition.PLAYER)
				if data.bargainingState == "EXCITED" and #nearbyPlayers == 0 then
					data.bargainingState = "DISAPPOINTED"
				end
				if data.bargainingState == "DISAPPOINTED" and #nearbyPlayers > 0 then
					data.bargainingState = "NORMAL"
				end
			end
		end
		
		if data.bargainingState == "MAD" then
			sprite:SetLayerFrame(2, 1)
			sprite:SetLayerFrame(1, 1)
			sprite:SetLayerFrame(3, 1)
		elseif data.bargainingState == "NORMAL" or (data.bargainingState == "EXCITED" and entity.FrameCount % 2 == 0) then
			sprite:SetLayerFrame(2, 0)
		elseif data.bargainingState == "EXCITED" then
			sprite:SetLayerFrame(2, 4)
		elseif data.bargainingState == "DISAPPOINTED" or data.bargainingState == "ANNOYED" then
			sprite:SetLayerFrame(2, 2)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SpiritOfBargainingUpdate, SPIRIT_OF_BARGAINING.ID)

function mod:SpiritOfBargainingDamage(tookDamage, damage, damageFlags, damageSourceRef)
	if tookDamage.Variant == SPIRIT_OF_BARGAINING.Var then
		if damageFlags & DamageFlag.DAMAGE_EXPLOSION == 0 then
			-- Not an explosion.
			return false
		end
		if damageSourceRef.Type == EntityType.ENTITY_PLAYER or damageSourceRef.SpawnerType == EntityType.ENTITY_PLAYER then
			GetRunData().wasBombed = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.SpiritOfBargainingDamage, SPIRIT_OF_BARGAINING.ID)

function mod:SpiritOfBargainingDie(entity)
	if entity.Variant ~= SPIRIT_OF_BARGAINING.Var then return end
	
	if GetBargainingRNG():RandomInt(2) == 0 then
		for i=1,2 do
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COIN, CoinSubType.COIN_PENNY, entity.Position, Vector(2,0):Rotated(GetBargainingRNG():RandomInt(360)), nil)
		end
	else
		for i=1,2 do
			EntityNPC.ThrowSpider(entity.Position, entity, entity.Position + Vector(35,0):Rotated(GetBargainingRNG():RandomInt(360)), false, -25)
		end
	end
	
	mod.ContentManager:GrantCustomAchievement(mod.ACHIEVEMENTS.KILL_BARGAINING)
	
	entity:Remove()
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.SpiritOfBargainingDie, SPIRIT_OF_BARGAINING.ID)

function mod:SpiritOfBargainingBomb(bomb)
	local nearby = Isaac.FindInRadius(bomb.Position, 100, EntityPartition.ENEMY)
	for _, entity in pairs(nearby) do
		if entity.Type == SPIRIT_OF_BARGAINING.ID and entity.Variant == SPIRIT_OF_BARGAINING.Var then
			entity:GetData().bargainingBomb = bomb
			return
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.SpiritOfBargainingBomb)

--------------------------------------------------
---- RESPONDING TO THE PLAYER USING ITEMS
--------------------------------------------------

function mod:BargainingFreePayout(player, isDuplication)
	local spiritOfBargaining = FindSpiritOfBargaining()
	if not player or not spiritOfBargaining then return false end
	local data = spiritOfBargaining:GetData()
	if not data.bargainingDeals then return false end
	
	local choice = GetBargainingRNG():RandomInt(3) + 1
	local chosenDeal = data.bargainingDeals[choice]
	
	PayoutDeal(chosenDeal, player, isDuplication)
	return true
end

function mod:DeathDealFreePayout(player, isDuplication)
	local deathDeals = {}
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.DEATH_DEAL.ID, mod.ENTITIES.DEATH_DEAL.Var, mod.ENTITIES.DEATH_DEAL.Sub)) do
		table.insert(deathDeals, deal)
	end
	if #deathDeals == 0 then return end
	local chosenDeal = lib.PickRandom(deathDeals, player:GetCardRNG(mod.ITEMS.XIII))
	if chosenDeal then
		PayoutDeal(chosenDeal, player, isDuplication, true)
	end
end

local couponGiveFreeDeal = false

function mod:BargainingCouponPre(_, _, player)
	couponGiveFreeDeal = false
	
	for _, pickup in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP)) do
		local price = pickup:ToPickup().Price
		if price ~= 0 and price ~= PickupPrice.PRICE_FREE then
			return
		end
	end
	
	couponGiveFreeDeal = true
end
mod:AddPriorityCallback(ModCallbacks.MC_PRE_USE_ITEM, -1, mod.BargainingCouponPre, CollectibleType.COLLECTIBLE_COUPON)

function mod:BargainingCoupon(_, rng, player)
	if not couponGiveFreeDeal then return end
	
	couponGiveFreeDeal = false
	
	if mod:BargainingFreePayout(player) then return end
	
	local deals = Isaac.FindByType(mod.ENTITIES.STANDALONE_DEAL.ID, mod.ENTITIES.STANDALONE_DEAL.Var, mod.ENTITIES.STANDALONE_DEAL.Sub)
	
	if #deals == 0 then return end
	
	local chosenDeal = lib.PickRandom(deals, rng)
	if chosenDeal then
		PayoutDeal(chosenDeal, player)
		return
	end
	
	mod:DeathDealFreePayout(player)
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.BargainingCoupon, CollectibleType.COLLECTIBLE_COUPON)

function mod:BargainingD1(_, rng, player)
	if #Isaac.FindByType(EntityType.ENTITY_PICKUP) > 0 then return end
	
	local deals = {}
	
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.BARGAINING_DEAL.ID, mod.ENTITIES.BARGAINING_DEAL.Var)) do
		local dealData = deal:GetData().bargainingDealData
		if dealData and dealData.Offer.Type == EntityType.ENTITY_PICKUP and dealData.Offer.Variant ~= PickupVariant.PICKUP_COLLECTIBLE then
			table.insert(deals, deal)
		end
	end
	
	if #deals == 0 then return end
	
	local chosenDeal = lib.PickRandom(deals, rng)
	if chosenDeal then
		PayoutDeal(chosenDeal, player, true)
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.BargainingD1, CollectibleType.COLLECTIBLE_D1)

function mod:BargainingCreditCard(_, player)
	mod:BargainingFreePayout(player)
	
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.STANDALONE_DEAL.ID, mod.ENTITIES.STANDALONE_DEAL.Var, mod.ENTITIES.STANDALONE_DEAL.Sub)) do
		PayoutDeal(deal, player)
	end
	
	mod:DeathDealFreePayout(player)
end
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.BargainingCreditCard, Card.CARD_CREDIT)

function mod:BargainingDiplopia(_, rng, player)
	mod:BargainingFreePayout(player, true)
	
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.STANDALONE_DEAL.ID, mod.ENTITIES.STANDALONE_DEAL.Var, mod.ENTITIES.STANDALONE_DEAL.Sub)) do
		PayoutDeal(deal, player, true)
	end
	
	mod:DeathDealFreePayout(player, true)
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.BargainingDiplopia, CollectibleType.COLLECTIBLE_DIPLOPIA)

local catchTrollBombs = false

function mod:RerollBargainingDeal(deal, player, activeItemID)
	local dealInfo = deal:GetData().bargainingDealInfo
	local dealData = deal:GetData().bargainingDealData
	
	if not dealData or not dealData.Offer or dealData.Offer.Type ~= EntityType.ENTITY_PICKUP then
		lib.LogErr("Failed to reroll deal - missing data.")
		return
	end
	
	local offer = dealData.Offer
	
	if offer.Variant == PickupVariant.PICKUP_COLLECTIBLE then
		local original = offer.SubType
		local newItem
		if FiendFolio and activeItemID == FiendFolio.ITEM.COLLECTIBLE.LOADED_D6 then
			local candidateItems = FiendFolio:getLoadedDiceCandidateItems(player)
			local roll = GetBargainingRNG():RandomInt(#candidateItems)+1
			newItem = candidateItems[roll]
			if newItem == original and #candidateItems > 0 then
				roll = (roll == #candidateItems) and 1 or (roll+1)
				newItem = candidateItems[roll]
			end
		else
			newItem = ChooseItem(offer.ItemPoolOverride)
		end
		if newItem == original then
			newItem = ChooseItem(offer.ItemPoolOverride)
		end
		
		dealInfo.Offer.Type = newItem
		if deal:GetData().deathDeal then
			deal:GetData().deathDeal.Offered = newItem
		end
	else
		local eType, eVariant, eSubType = lib.TestSpawn(EntityType.ENTITY_PICKUP, 0, 2)
		
		dealData.Variant = eVariant
		dealData.SubType = eSubType
		
		dealInfo.Offer = {
			Name = "ENTITY",
			Type = eType,
			Variant = eVariant,
			SubType = eSubType,
			Quantity = dealInfo.Offer.Quantity,
		}
	end
	
	mod:InitDeal(deal, dealInfo)
	Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, deal.Position, lib.ZeroVector, deal)
	
	local spiritOfBargaining = FindSpiritOfBargaining()
	if spiritOfBargaining then
		spiritOfBargaining:GetData().bargainingState = "ANNOYED"
	end
	
	for _, bomb in pairs(Isaac.FindByType(EntityType.ENTITY_BOMB)) do
		if bomb.FrameCount == 0 then
			bomb:Remove()
		end
	end
end

function mod:BargainingReroll(activeItemID, _, player)
	local rerollItems = activeItemID == CollectibleType.COLLECTIBLE_D6
	local rerollPickups = activeItemID == CollectibleType.COLLECTIBLE_D20
	
	if FiendFolio then
		rerollItems = rerollItems or (activeItemID == FiendFolio.ITEM.COLLECTIBLE.LOADED_D6)
	end
	
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.BARGAINING_DEAL.ID, mod.ENTITIES.BARGAINING_DEAL.Var)) do
		local dealData = deal:GetData().bargainingDealData
		if dealData and dealData.Offer.Type == EntityType.ENTITY_PICKUP then
			local isItem = dealData.Offer.Variant == PickupVariant.PICKUP_COLLECTIBLE
			if (rerollItems and isItem) or (rerollPickups and not isItem) then
				mod:RerollBargainingDeal(deal, player, activeItemID)
			end
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.BargainingReroll, CollectibleType.COLLECTIBLE_D20)
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.BargainingReroll, CollectibleType.COLLECTIBLE_D6)
if FiendFolio and FiendFolio.ITEM.COLLECTIBLE.LOADED_D6 then
	mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, CallbackPriority.EARLY, mod.BargainingReroll, FiendFolio.ITEM.COLLECTIBLE.LOADED_D6)
end

local function ValidSpindownItem(id)
	local configEntry = Isaac.GetItemConfig():GetCollectible(id)
	if configEntry and not configEntry.Hidden then
		return true
	end
	return false
end

function mod:BargainingSpindown(_, _, player)
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.BARGAINING_DEAL.ID, mod.ENTITIES.BARGAINING_DEAL.Var)) do
		local dealData = deal:GetData().bargainingDealData
		
		if dealData and dealData.Offer.Type == EntityType.ENTITY_PICKUP and dealData.Offer.Variant == PickupVariant.PICKUP_COLLECTIBLE then
			local dealInfo = deal:GetData().bargainingDealInfo
			
			local offer = dealData.Offer
			
			local original = offer.SubType
			local newItem = original - 1
			
			while newItem > 0 and not ValidSpindownItem(newItem) do
				newItem = newItem - 1
			end
			
			if newItem == 0 then
				deal:Remove()
			else
				dealInfo.Offer.Type = newItem
				if deal:GetData().deathDeal then
					deal:GetData().deathDeal.Offered = newItem
				end
				
				mod:InitDeal(deal, dealInfo)
				Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, deal.Position, lib.ZeroVector, deal)
				
				local spiritOfBargaining = FindSpiritOfBargaining()
				if spiritOfBargaining then
					spiritOfBargaining:GetData().bargainingState = "ANNOYED"
				end
			end
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.BargainingSpindown, CollectibleType.COLLECTIBLE_SPINDOWN_DICE)

--------------------------------------------------
---- DEATH DEAL SUPPORT
--------------------------------------------------

function mod:InitDeathDeal(entity, requiredItem, offeredItem, itemPool, parent)
	mod:InitDeal(entity, {
		Offer = {
			Name = "ITEM",
			Type = offeredItem,
			ItemPoolOverride = itemPool,
			Hidden = game:GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND ~= 0,
		},
		Cost = {
			Name = "ITEM",
			Type = requiredItem
		},
		IsDeathDeal = true,
	})
	
	entity.Parent = parent
	entity.DepthOffset = 1
end

--------------------------------------------------
---- Bargaining Fossil
--------------------------------------------------

function mod:RespawnStandaloneDeals()
	local level = game:GetLevel()
	if level:GetCurrentRoomIndex() == level:GetStartingRoomIndex() and game:GetRoom():IsFirstVisit() then return end
	
	local roomData = GetRoomData()
	local standaloneDeals = roomData.StandaloneDeals or {}
	
	local spawnedDeals = {}
	
	for _, tab in pairs(standaloneDeals) do
		if roomData.LastUpdate and roomData.LastUpdate - tab.FRAME < 2 then
			local dealEntity = lib.Spawn(mod.ENTITIES.STANDALONE_DEAL, Vector(tab.POSX, tab.POSY), lib.ZeroVector, nil):ToNPC()
			mod:InitDeal(dealEntity, tab.DATA)
		end
	end
	
	roomData.StandaloneDeals = {}
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.RespawnStandaloneDeals)

function mod.BargainingFossilCrushEffect(player, spawner)
	for i=1, 2 do
		local pos = FindBargainingSpawnPos(spawner.Position)
		local dealEntity = lib.Spawn(mod.ENTITIES.STANDALONE_DEAL, pos, lib.ZeroVector, nil):ToNPC()
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, pos, lib.ZeroVector, dealEntity)
		
		local generatedDeals = ChooseDeals(player, {WHITELIST={TRINKET=true}})
		if generatedDeals[1] then
			mod:InitDeal(dealEntity, generatedDeals[1])
		end
	end
end

function mod:BargainingFossilNewRoom()
	local level = game:GetLevel()
	if game:GetRoom():GetFrameCount() ~= 1 or level:GetCurrentRoomIndex() ~= level:GetStartingRoomIndex() then return end
	
	local data = GetFloorData()
	if data.SpawnedFossilDeals then return end
	
	local playersWithFossil = {}
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(BARGAINING_FOSSIL) then
			table.insert(playersWithFossil, player)
			break
		end
	end
	if #playersWithFossil == 0 then return end
	
	data.SpawnedFossilDeals = true
	
	local rng = playersWithFossil[1]:GetTrinketRNG(BARGAINING_FOSSIL)
	
	local totalPower = 0
	for _, player in pairs(playersWithFossil) do
		totalPower = totalPower + (FiendFolio and FiendFolio.GetGolemTrinketPower(player, BARGAINING_FOSSIL) or player:GetTrinketMultiplier(BARGAINING_FOSSIL))
	end
	local extraChance = totalPower % 1
	local numToSpawn = 1 + math.floor(totalPower)
	
	if extraChance > 0 and rng:RandomFloat() <= extraChance then
		numToSpawn = numToSpawn + 1
	end
	
	for i=1, numToSpawn do
		local pos = FindBargainingSpawnPos(game:GetRoom():GetCenterPos())
		local dealEntity = lib.Spawn(mod.ENTITIES.STANDALONE_DEAL, pos, lib.ZeroVector, nil):ToNPC()
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, pos, lib.ZeroVector, dealEntity)
		
		local generatedDeals = ChooseDeals(lib.PickRandom(lib.GetPlayers(), rng), {NO_ITEMS=true, NO_SERVICES=true})
		local dealInfo = lib.PickRandom(generatedDeals, rng)
		if dealInfo then
			mod:InitDeal(dealEntity, dealInfo)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.BargainingFossilNewRoom)
