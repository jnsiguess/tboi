local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local DENIAL = mod.ITEMS.DENIAL
local DENIAL_DICE = mod.ITEMS.DENIAL_DICE
local EFFIGY_OF_DENIAL = SamaelMod.ITEMS.EFFIGY_OF_DENIAL

local SPIRIT_OF_DENIAL = mod.ENTITIES.SPIRIT_OF_DENIAL.Var

local spiritOfDenial

function mod:SpawnDenialDice(pos)
	local dice = Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, DENIAL_DICE, pos, lib.ZeroVector, nil):ToPickup()
	
	if dice.Variant ~= PickupVariant.PICKUP_TAROTCARD or dice.SubType ~= DENIAL_DICE then
		dice:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, DENIAL_DICE, true, true, true)
	end
	
	mod:DenialDiceUpdate(dice)
	dice:Update()
	
	if not dice or not dice:Exists() or dice.Type ~= EntityType.ENTITY_PICKUP then
		-- Who ate my fucking dice.
		return
	end
	
	if dice.Variant ~= PickupVariant.PICKUP_TAROTCARD or dice.SubType ~= DENIAL_DICE then
		dice:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, DENIAL_DICE, true, true, true)
	end
end

function mod:SpiritOfDenialInit(entity)
	local data = entity:GetData()
	
	entity.SpriteOffset = Vector(0, -20)
	data.denialTargetOffset = Vector(0, -8)
	
	entity.Color = Color(1,1,1,0)
	data.denialTargetAlpha = 1.0
	
	entity:GetSprite():Play("Denial", true)
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.SpiritOfDenialInit, SPIRIT_OF_DENIAL)

function mod:SpiritOfDenialUpdate(entity)
	local data = entity:GetData()
	local sprite = entity:GetSprite()
	
	if sprite:GetAnimation() ~= "Denial" then
		sprite:Play("Denial", true)
	end
	if entity.FrameCount < 20 then
		sprite:SetFrame(0)
	end
	if sprite:GetOverlayAnimation() ~= "Body" then
		sprite:PlayOverlay("Body", true)
		sprite:SetOverlayRenderPriority(true)
	end
	if sprite:IsFinished("Denial") then
		entity:Remove()
		spiritOfDenial = nil
		return
	end
	if sprite:IsEventTriggered("Drop") then
		mod:SpawnDenialDice(entity.Position + Vector(0,1))
		data.denialTargetOffset = Vector(0, -20)
		data.denialTargetAlpha = 0
	end
	
	entity.SpriteOffset = lib.Lerp(entity.SpriteOffset, data.denialTargetOffset, 0.1)
	entity.Color = Color(1,1,1, lib.Lerp(entity.Color.A, data.denialTargetAlpha, 0.1))
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SpiritOfDenialUpdate, SPIRIT_OF_DENIAL)

function mod:DenialCache(player)
	if lib.HasItem(player, DENIAL) then
		player.Luck = player.Luck - 2 -- balanced
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.DenialCache, CacheFlag.CACHE_LUCK)

-- Only remove Curse of the Blind 5 times per room at most, to avoid infinite loops in case any
-- other mods try to aggressively force Curse of the Blind on the player.
local kMaxRemoveCurseAttepts = 5
local removeCurseofBlindAttempts = 0
function mod:DenialRemoveCurseOfBlind(player)
	local roomFrame = game:GetRoom():GetFrameCount()
	
	if roomFrame == 1 then
		removeCurseofBlindAttempts = 0
	end
	
	if lib.HasItem(player, DENIAL) and roomFrame >= 1 and game:GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND ~= 0 and removeCurseofBlindAttempts < kMaxRemoveCurseAttepts then
		game:GetLevel():RemoveCurses(LevelCurse.CURSE_OF_BLIND)
		removeCurseofBlindAttempts = removeCurseofBlindAttempts + 1
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.DenialRemoveCurseOfBlind)

-- Force items to never be blind question marks by reloading their graphics after init/reroll.
-- Since Denial also removes Curse of the Blind, this will probably only come into play for alt path item rooms.
function mod:DenialRevealBlindItems(pickup)
	local data = pickup:GetData()
	local id = pickup.SubType
	
	if not data.denialRevealed then
		data.denialRevealed = true
		-- See if any players have Denial.
		for _, player in pairs(lib.GetPlayers()) do
			if lib.HasItem(player, DENIAL) then
				-- Reload the item's sprite to make sure it's not hidden.
				lib.ReloadItemSprite(pickup)
				return
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.DenialRevealBlindItems, PickupVariant.PICKUP_COLLECTIBLE)

---------- GENERAL PICKUP REROLLING FUNCTIONALITY ----------

local function GetDenialItemPoolInfo()
	return lib.GetOrInit(mod:GetFloorData("DenialItemPools"), ""..game:GetLevel():GetCurrentRoomDesc().ListIndex)
end

-- Rerolls an item pedestal.
function mod:RerollItem(pickup)
	pickup = pickup:ToPickup()
	if pickup and pickup.SubType > 0 then
		local roomPool = lib.GetCurrentRoomItemPool()
		local pool = GetDenialItemPoolInfo()[""..pickup.InitSeed] or roomPool
		local itemType = game:GetItemPool():GetCollectible(pool, true, pickup.InitSeed)
		
		-- Morph will remove the Soul of Isaac effect.
		-- Items like the D6 already do this.
		-- However this does negate Tainted Isaac's passive.
		pickup:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, itemType, true, true, true)
		
		-- Spawning a new pickup preserves Tainted Isaac's passive, but resets the pedestal sprite.
		-- Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, itemType, pickup.Position, lib.ZeroVector, nil)
		-- pickup:Remove()
	end
end

local kMaxRerollAttempts = 10

-- Rerolls a coin/key/bomb/etc. Will try not to reroll into the same one.
function mod:RerollToAnyPickup(pickup)
	if not pickup:ToPickup() then
		lib.LogErr("Failed to reroll, not a pickup: " .. pickup.Type ..".".. pickup.Variant ..".".. pickup.SubType)
	end
	pickup = pickup:ToPickup()
	
	local mode = 2  -- No collectibles.
	if pickup.Price ~= 0 then  -- Is a devil deal or shop item.
		mode = 1  -- No chests or collectibles.
	end
	
	local attempts = 0
	local	newPickup
	while attempts < kMaxRerollAttempts and (not newPickup or (newPickup.Variant == pickup.Variant and newPickup.SubType == pickup.SubType)) do
		if newPickup then newPickup:Remove() end
		newPickup = Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, mode, lib.ZeroVector, lib.ZeroVector, nil):ToPickup()
		newPickup.Timeout = 1
		attempts = attempts + 1
	end
	if newPickup then
		pickup:Morph(EntityType.ENTITY_PICKUP, newPickup.Variant, newPickup.SubType, true, false, false)
		newPickup:Remove()
	else
		lib.LogErr("Failed to reroll pickup with Variant " .. pickup.Variant)
	end
end

-- Rerolls primarily troll bombs to pickups, but works on any placed bomb.
function mod:RerollBombDrop(bomb)
	Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, 2, bomb.Position, bomb.Velocity, nil)
	bomb:Remove()
end

-- Rerolls a pill. Horse Pills will stay horse pills.
function mod:RerollPill(pill)
	if not pill:ToPickup() or pill.Variant ~= PickupVariant.PICKUP_PILL then
		lib.LogErr("Failed to reroll, not a pill: " .. pill.Type ..".".. pill.Variant ..".".. pill.SubType)
	end
	pill = pill:ToPickup()
	
	local pillColor = pill.SubType
	local isHorsePill = pillColor > PillColor.PILL_GIANT_FLAG
	
	if isHorsePill then
		pillColor = pillColor - PillColor.PILL_GIANT_FLAG
	end
	
	local attempts = 0
	local newPillColor = pillColor
	while attempts < kMaxRerollAttempts and (not newPillColor or newPillColor == pillColor) do
		newPillColor = game:GetItemPool():GetPill(pill.InitSeed + attempts)
		if newPillColor and newPillColor > PillColor.PILL_GIANT_FLAG then
			newPillColor = newPillColor - PillColor.PILL_GIANT_FLAG
		end
		attempts = attempts + 1
	end
	
	if newPillColor then
		if isHorsePill then
			newPillColor = newPillColor + PillColor.PILL_GIANT_FLAG
		end
		pill:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_PILL, newPillColor, true, false, true)
	else
		lib.LogErr("Failed to reroll pill.")
	end
end

-- Rerolls a card into another card. Includes runes, soul stones and objects.
function mod:RerollCard(card)
	if not card:ToPickup() or card.Variant ~= PickupVariant.PICKUP_TAROTCARD then
		lib.LogErr("Failed to reroll, not a card: " .. card.Type ..".".. card.Variant ..".".. card.SubType)
	end
	card = card:ToPickup()
	
	local attempts = 0
	local newCard = card.SubType
	while attempts < kMaxRerollAttempts and (not newCard or newCard == card.SubType) do
		newCard = game:GetItemPool():GetCard(card.InitSeed + attempts, true, true, false)
		attempts = attempts + 1
	end
	
	if newCard then
		card:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, newCard, true, false, true)
	else
		lib.LogErr("Failed to reroll card.")
	end
end

-- Rerolls a trinket. Golden status will be maintained.
function mod:RerollTrinket(trinket)
	if not trinket:ToPickup() or trinket.Variant ~= PickupVariant.PICKUP_TRINKET then
		lib.LogErr("Failed to reroll, not a trinket: " .. trinket.Type ..".".. trinket.Variant ..".".. trinket.SubType)
	end
	trinket = trinket:ToPickup()
	
	local newTrinket = game:GetItemPool():GetTrinket()
	
	if FiendFolio then
		local isAnyGolem, isMixedGolem = FiendFolio.GolemExists()
		if isAnyGolem and not isMixedGolem then
			newTrinket = FiendFolio.GetRandomGolemTrinket(newTrinket)
		end
	end
	
	if not newTrinket then
		lib.LogErr("Failed to reroll trinket.")
	end
	
	if lib.IsGoldenTrinket(trinket.SubType) and not lib.IsGoldenTrinket(newTrinket) then
		newTrinket = newTrinket | TrinketType.TRINKET_GOLDEN_FLAG
	end
	
	trinket:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, newTrinket, true, false, true)
end

---------- SACKS AND SIMILAR THINGIES ----------

local SACKS = {
	{ Variant = PickupVariant.PICKUP_GRAB_BAG, SubType = SackSubType.SACK_NORMAL },
	{ Variant = PickupVariant.PICKUP_GRAB_BAG, SubType = SackSubType.SACK_BLACK },
	{ Variant = mod.ENTITIES.BAG_O_BONES.Var },
}

function mod:RerollSack(sack)
	if not sack:ToPickup() then
		lib.LogErr("Failed to reroll sack, not a pickup: " .. sack.Type ..".".. sack.Variant ..".".. sack.SubType)
	end
	sack = sack:ToPickup()
	local rng = sack:GetDropRNG()
	
	local options = {}
	
	for _, tab in pairs(SACKS) do
		if sack.Variant ~= tab.Variant or sack.SubType ~= (tab.SubType or 0) then
			table.insert(options, tab)
		end
	end
	
	local choice = options[rng:RandomInt(#options)+1]
	
	if not choice then
		lib.LogErr("Failed to reroll sack, for some reason!")
		return
	end
	
	sack:Morph(EntityType.ENTITY_PICKUP, choice.Variant, choice.SubType or 0, true, false, false)
end

---------- REROLLING CHESTS FUNCTIONALITY ----------

-- Tables to store which chests can be chosen when rerolling a chest.
local BASIC_CHESTS = {
	PickupVariant.PICKUP_CHEST,
	PickupVariant.PICKUP_BOMBCHEST,
	PickupVariant.PICKUP_SPIKEDCHEST,
	PickupVariant.PICKUP_LOCKEDCHEST,
	PickupVariant.PICKUP_REDCHEST,
}
local RARE_CHESTS = {
	PickupVariant.PICKUP_ETERNALCHEST,
}

-- Try to restrict some of these chests from appearing before being unlocked.
local SpecialChestsInitialized = false
function mod:DenialSpecialChestsInit()
	if SpecialChestsInitialized then return end
	
	local unlocks = mod:VanillaUnlocks()
	if unlocks.WoodenChest then
		table.insert(BASIC_CHESTS, PickupVariant.PICKUP_WOODENCHEST)
	end
	if unlocks.MegaChest then
		table.insert(RARE_CHESTS, PickupVariant.PICKUP_MEGACHEST)
	end
	if unlocks.RedKey then
		table.insert(RARE_CHESTS, PickupVariant.PICKUP_MOMSCHEST)
	end
	if unlocks.IsaacsTomb then
		table.insert(RARE_CHESTS, PickupVariant.PICKUP_OLDCHEST)
	end
	
	SpecialChestsInitialized = true
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.DenialSpecialChestsInit)

-- Pick a random, different chest from the provided options.
local function PickNewChest(current, options)
	local rng = current:GetDropRNG()
	
	local choice = current.Variant
	local attempts = 0
	while attempts < kMaxRerollAttempts and (not choice or choice == current.Variant) do
		choice = options[rng:RandomInt(#options)+1]
		attempts = attempts + 1
	end
	return choice or current.Variant
end

local kRareChestChance = 0.01
-- Rerolls a chest.
function mod:RerollChest(chest)
	local rng = chest:GetDropRNG()
	
	local newVariant = 50
	if rng:RandomFloat() <= kRareChestChance then
		newVariant = PickNewChest(chest, RARE_CHESTS)
	else
		newVariant = PickNewChest(chest, BASIC_CHESTS)
	end
	
	Isaac.Spawn(EntityType.ENTITY_PICKUP, newVariant, 0, chest.Position, chest.Velocity, chest.SpawnerEntity)
	chest:Remove()
end

---------- REROLLING SLOTS FUNCTIONALITY ----------

local SLOT_VARIANTS = {
	1, -- Slot Machine
	2, -- Blood Donation Machine
	3, -- Fortune Telling Machine
	4, -- Beggar
	5, -- Devil Beggar
	6, -- Shell Game
	7, -- Key Beggar
	8, -- Donation Machine
	9, -- Bomb Beggar
	10, -- Restock Machine
	12, -- Mom's Dresser
	13, -- Battery Beggar
}

-- Try to restrict some slot machines from appearing unless unlocked.
local SlotMachinesInitialized = false
function mod:DenialInitSlots()
	if SlotMachinesInitialized then return end
	
	local unlocks = mod:VanillaUnlocks()
	if unlocks.HellGame then
		table.insert(SLOT_VARIANTS, 15)
	end
	if unlocks.CraneGame then
		table.insert(SLOT_VARIANTS, 16)
	end
	if unlocks.Confessional then
		table.insert(SLOT_VARIANTS, 17)
	end
	if unlocks.RottenBeggar then
		table.insert(SLOT_VARIANTS, 18)
	end
	
	SlotMachinesInitialized = true
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.DenialInitSlots)

-- Reroll a slot into another random slot.
function mod:RerollSlot(slot)
	local rng = slot:GetDropRNG()
	
	local choice = slot.Variant
	local attempts = 0
	while attempts < kMaxRerollAttempts and (not choice or choice == slot.Variant) do
		choice = SLOT_VARIANTS[rng:RandomInt(#SLOT_VARIANTS)+1]
		attempts = attempts + 1
	end
	
	if choice and choice ~= slot.Variant then
		Isaac.Spawn(EntityType.ENTITY_SLOT, choice, 0, slot.Position, slot.Velocity, slot.SpawnerEntity)
		slot:Remove()
	end
end

---------- GENERAL REROLL FUNCTIONALITY ----------

-- Table for storing what function to call to reroll a specific entitiy.
local REROLL_FUNC = {}

local function AddRerollFunc(eType, eVariant, eSubType, func)
	lib.GetOrInit(REROLL_FUNC, eType, eVariant or -1)[eSubType or -1] = func
end

AddRerollFunc(mod.ENTITIES.BARGAINING_DEAL.ID, mod.ENTITIES.BARGAINING_DEAL.Var, -1, function(_, deal) mod:RerollBargainingDeal(deal) end)
AddRerollFunc(EntityType.ENTITY_BOMBDROP, -1, -1, mod.RerollBombDrop)

local function AddPickupRerollFunc(pickupVariant, func, subType)
	AddRerollFunc(EntityType.ENTITY_PICKUP, pickupVariant, subType or -1, func)
end

AddPickupRerollFunc(-1, mod.RerollToAnyPickup)
AddPickupRerollFunc(PickupVariant.PICKUP_COLLECTIBLE, mod.RerollItem)
AddPickupRerollFunc(PickupVariant.PICKUP_PILL, mod.RerollPill)
AddPickupRerollFunc(PickupVariant.PICKUP_TAROTCARD, mod.RerollCard)
AddPickupRerollFunc(PickupVariant.PICKUP_TRINKET, mod.RerollTrinket)

for variant, _ in pairs(lib.VanillaChestVariants) do
	AddPickupRerollFunc(variant, mod.RerollChest, ChestSubType.CHEST_CLOSED)
end

for _, tab in pairs(SACKS) do
	AddPickupRerollFunc(tab.Variant, mod.RerollSack)
end

local function AddSlotRerollFunc(slotVariant, func)
	AddRerollFunc(EntityType.ENTITY_SLOT, slotVariant, -1, func)
end

for variant, _ in pairs(lib.VanillaSlotVariants) do
	AddSlotRerollFunc(variant, mod.RerollSlot)
end

AddPickupRerollFunc(PickupVariant.PICKUP_TROPHY, false)
AddPickupRerollFunc(PickupVariant.PICKUP_BIGCHEST, false)

local function GetRerollFunc(eType, eVariant, eSubType)
	local tab = REROLL_FUNC[eType]
	if not tab then return end
	tab = tab[eVariant] or tab[-1]
	if not tab then return end
	return tab[eSubType] or tab[-1]
end

local function Reroll(entity)
	local rerollFunc = GetRerollFunc(entity.Type, entity.Variant, entity.SubType)
	if rerollFunc then
		local pos = entity.Position
		rerollFunc(_, entity)
		sfxManager:Play(SoundEffect.SOUND_SLOTSPAWN)
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, pos, lib.ZeroVector, nil)
		if entity:ToPickup() then
			entity:ToPickup().Timeout = -1
		end
	else
		lib.LogErr("No reroll function for entity: " .. entity.Type ..".".. entity.Variant ..".".. entity.SubType)
	end
end

---------- MOD COMPATABILITY ----------

function mod:AddChests(chests, allowRerollInto, rare)
	for _, chest in pairs(chests) do
		local name, eType, eVariant, eSubType
		if type(chest) == "string" then
			name = chest
			eType = Isaac.GetEntityTypeByName(name)
			eVariant = Isaac.GetEntityVariantByName(name)
		elseif type(chest) == "number" then
			eType = EntityType.ENTITY_PICKUP
			eVariant = chest
		elseif type(chest) == "table" then
			eType = EntityType.ENTITY_PICKUP
			eVariant = chest[1]
			eSubType = chest[2]
		end
		if eVariant and eVariant > 0 and eType == EntityType.ENTITY_PICKUP then
			if allowRerollInto ~= false then
				table.insert(rare and RARE_CHESTS or BASIC_CHESTS, eVariant)
			end
			AddPickupRerollFunc(eVariant, mod.RerollChest, eSubType or 0)
		else
			lib.LogErr("Failed to add modded chest: " .. (name or eVariant or "(Unknown)"))
		end
	end
end

function mod:AddSacks(sacks, allowRerollInto)
	for _, sack in pairs(sacks) do
		local name, eType, eVariant, eSubType
		if type(sack) == "string" then
			name = sack
			eType = Isaac.GetEntityTypeByName(name)
			eVariant = Isaac.GetEntityVariantByName(name)
		elseif type(sack) == "number" then
			eType = EntityType.ENTITY_PICKUP
			eVariant = sack
		elseif type(sack) == "table" then
			eType = EntityType.ENTITY_PICKUP
			eVariant = sack[1]
			eSubType = sack[2]
		end
		if eVariant and eVariant > 0 and eType == EntityType.ENTITY_PICKUP then
			if allowRerollInto ~= false then
				table.insert(SACKS, { Variant = eVariant, SubType = (eSubType or 0) })
			end
			AddPickupRerollFunc(eVariant, mod.RerollSack, eSubType or -1)
		else
			lib.LogErr("Failed to add modded sack: " .. (name or eVariant or "(Unknown)"))
		end
	end
end

function mod:AddSlots(slots, allowRerollInto)
	for _, slot in pairs(slots) do
		local name, eType, eVariant
		if type(slot) == "string" then
			name = slot
			eType = Isaac.GetEntityTypeByName(name)
			eVariant = Isaac.GetEntityVariantByName(name)
		elseif type(slot) == "number" then
			eType = EntityType.ENTITY_SLOT
			eVariant = slot
		end
		if eVariant and eVariant >= 0 and eType == EntityType.ENTITY_SLOT then
			if allowRerollInto ~= false then
				table.insert(SLOT_VARIANTS, eVariant)
			end
			AddSlotRerollFunc(eVariant, mod.RerollSlot)
		else
			lib.LogErr("Failed to add modded slot: " .. (name or eVariant or "(Unknown)"))
		end
	end
end

local ModdedStuffInitialized = false
function mod:DenialInitModdedStuff()
	if ModdedStuffInitialized then return end
	
	local data = mod:GetPersistentData("Denial")
	
	mod:AddSlots({mod.ENTITIES.FERRYMAN.Var}, not mod.ContentManager:EntityLockedOrDisabled(EntityType.ENTITY_SLOT, mod.ENTITIES.FERRYMAN.Var))
	
	if RepentancePlusMod then
		mod:AddChests({"Scarlet Chest", "Flesh Chest", "Black Chest"})
		mod:AddSlots({"Stargazer"})
	end
	if CadaverRNG then
		mod:AddChests({"Rotten Chest"})
	end
	if CCO and CCO.JOB_MOD then
		mod:AddSlots({"Praying Altar"}, data.JobPrayingAltarUnlocked)
	end
	if FiendFolio then
		mod:AddChests({"Shop Chest", "Dire Chest", "Glass Chest"})
		mod:AddChests({"Reheated Chest"}, false)
		mod:AddSlots({
			"Poker Table", "Blacksmith", "Zodiac Beggar", "Robot Teller", "Evil Beggar", "Fake Beggar",
			"Cell Game", "Hug Beggar", "Cosplay Beggar", "Golden Slot Machine"})
		mod:AddSlots({"Dealer", "Phone Booth", "Jukebox"}, false)
		mod:AddSacks({{666, 0}, {666, 10}, {666, 11}})
	end
	
	ModdedStuffInitialized = true
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.DenialInitModdedStuff)

function mod:DenialJobPrayingAltarCheck(eType, eVariant, eSubType)
	local data = mod:GetPersistentData("Denial")
	
	if CCO and CCO.JOB_MOD and eType == EntityType.ENTITY_SLOT and eVariant == Isaac.GetEntityVariantByName("Praying Altar") then
		if eSubType == 1 then
			data.JobPrayingAltarUnlocked = false
		else
			data.JobPrayingAltarUnlocked = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN, mod.DenialJobPrayingAltarCheck)

---------- DENIAL DICE SPAWNING MECHANICS ----------

local function GetDenialDiceRoomData()
	return lib.GetOrInit(mod:GetFloorData("DenialDice"), ""..game:GetLevel():GetCurrentRoomDesc().ListIndex)
end

function mod:ConsumeDenialDice()
	local data = GetDenialDiceRoomData()
	data.numDiceUsed = (data.numDiceUsed or 0) + 1
end

-- For the "Effigy of Denial" rock trinket.
function mod:DenialOnRoomClear()
	local roomData = GetDenialDiceRoomData()
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(EFFIGY_OF_DENIAL) then
			local power = FiendFolio and FiendFolio.GetGolemTrinketPower(player, EFFIGY_OF_DENIAL) or player:GetTrinketMultiplier(EFFIGY_OF_DENIAL)
			local chance = 0.25 * power
			if player:GetTrinketRNG(EFFIGY_OF_DENIAL):RandomFloat() <= chance then
				roomData.bonusDice = (roomData.bonusDice or 0) + 1
			end
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, CallbackPriority.LATE, mod.DenialOnRoomClear)

function mod.DenialFossilCrushEffect(player, spawner)
	local roomData = GetDenialDiceRoomData()
	roomData.bonusDice = (roomData.bonusDice or 0) + 3
end

if FiendFolio then
	function mod:EffigyOfDenialGrind(grinder)
		if grinder:GetSprite():IsEventTriggered('Prize') and grinder:GetData().GrindingTrinket == EFFIGY_OF_DENIAL then
			local roomData = GetDenialDiceRoomData()
			roomData.bonusDice = (roomData.bonusDice or 0) + 3
		end
	end
	mod:AddPostSlotUpdateFunc(mod.EffigyOfDenialGrind, 1020)
end

function mod:HandleDenialDiceSpawning()
	local room = game:GetRoom()
	local roomData = GetDenialDiceRoomData()
	local roomFrame = room:GetFrameCount()
	
	if roomFrame == 0 then
		return
	elseif roomFrame == 1 or not roomData.numDiceSpawned then
		roomData.numDiceSpawned = roomData.numDiceUsed or 0
	end
	
	if not room:IsClear() then return end
	
	local firstPlayer
	local maxDenialDice = roomData.bonusDice or 0
	local realDenial = false
	
	for _, player in pairs(lib.GetPlayers()) do
		if not firstPlayer and (lib.HasItem(player, DENIAL) or player:HasTrinket(EFFIGY_OF_DENIAL)) then
			firstPlayer = player
		end
		realDenial = realDenial or lib.HasItem(player, DENIAL)
		maxDenialDice = maxDenialDice + player:GetCollectibleNum(DENIAL)
	end
	
	-- When the room is clear, spawn Denial Dice as long as it hasn't been used up for this room already.
	while roomData.numDiceSpawned < maxDenialDice do
		local pos = room:FindFreePickupSpawnPosition((spiritOfDenial or firstPlayer or Isaac.GetPlayer()).Position, 40)
		if realDenial and (not spiritOfDenial or not spiritOfDenial:Exists()) then
			spiritOfDenial = Isaac.Spawn(EntityType.ENTITY_EFFECT, SPIRIT_OF_DENIAL, 0, pos, lib.ZeroVector, nil)
		else
			mod:SpawnDenialDice(pos)
		end
		roomData.numDiceSpawned = roomData.numDiceSpawned + 1
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.HandleDenialDiceSpawning)

---------- DENIAL DICE CONSUMABLE FUNCTIONALITY ----------

-- Returns true if the given entity is a valid reroll target.
local function IsValidDenialTarget(target)
	if target:GetSprite():IsPlaying("Collect") or target:GetSprite():IsPlaying("PlayerPickupSparkle") then return false end
	
	local eType = target.Type
	local eVariant = target.Variant
	local eSubType = target.SubType
	
	local isEmptyPedestal = (
			eType == EntityType.ENTITY_PICKUP
			and eVariant == PickupVariant.PICKUP_COLLECTIBLE
			and eSubType < 1)
	
	local isMomsBed = (
			eType == EntityType.ENTITY_PICKUP
			and eVariant == PickupVariant.PICKUP_BED
			and eSubType == 10)
	
	return target:Exists() and not isEmptyPedestal and not isMomsBed and GetRerollFunc(eType, eVariant, eSubType)
end

-- Buffer to add a bit of extra minimum distance to decide when to switch targets to something closer.
local kDenialTargetDistanceBuffer = 0

local function ConsiderNewDenialTarget(player, newTarget)
	local pData = player:GetData()
	
	local isCurrentTarget = pData.denialTarget and pData.denialTarget.InitSeed == newTarget.InitSeed
	
	if not IsValidDenialTarget(newTarget) then
		if isCurrentTarget then
			-- Current target is no longer valid.
			pData.denialTarget = nil
		end
		return
	end
	
	if not pData.denialTarget then
		pData.denialTarget = newTarget
	elseif not isCurrentTarget then
		local targetDist = player.Position:Distance(pData.denialTarget.Position)
		local newTargetDist = player.Position:Distance(newTarget.Position)
		if newTargetDist + kDenialTargetDistanceBuffer < targetDist then
			pData.denialTarget = newTarget
		end
	end
end

-- Look in the room to find the nearest valid target for the Denial Dice.
local function FindDenialTarget(player)
	for _, pickup in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP, -1, -1, true, false)) do
		ConsiderNewDenialTarget(player, pickup)
	end
	for _, slot in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT, -1, -1, true, false)) do
		ConsiderNewDenialTarget(player, slot)
	end
	for _, bomb in pairs(Isaac.FindByType(EntityType.ENTITY_BOMBDROP, -1, -1, true, false)) do
		ConsiderNewDenialTarget(player, bomb)
	end
	for _, deal in pairs(Isaac.FindByType(mod.ENTITIES.BARGAINING_DEAL.ID, mod.ENTITIES.BARGAINING_DEAL.Var, -1, true, false)) do
		local dealData = deal:GetData().bargainingDealData
		if dealData and dealData.Offer.Type == EntityType.ENTITY_PICKUP then
			ConsiderNewDenialTarget(player, deal)
		end
	end
end

function mod:DenialUpdate(player)
	local pData = player:GetData()
	
	if game:GetRoom():GetFrameCount() == 0 then
		-- If the player changes rooms while holding a Denial Dice, remove it.
		-- Denial Dice dropped during room frame 0 will get removed during their init.
		for i=3,0,-1 do
			if player:GetCard(i) == DENIAL_DICE then
				player:DropPocketItem(i, lib.ZeroVector)
			end
		end
	end
	
	local hasDenialDice = player:GetCard(0) == DENIAL_DICE or player:GetCard(1) == DENIAL_DICE
	
	if pData.denialTarget and not IsValidDenialTarget(pData.denialTarget) then
		pData.denialTarget = nil
	end
	
	if hasDenialDice then
		-- Continually find the closest valid reroll target, but not every single frame.
		if not pData.denialTarget or player.FrameCount % 6 == 0 then
			FindDenialTarget(player)
		end
	elseif pData.denialTarget then
		pData.denialTarget = nil
	end
	
	-- Highlight the current target.
	if pData.denialTarget then
		if not pData.denialCursor then
			pData.denialCursor = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.TARGET, 0, player.Position, lib.ZeroVector, player)
			local c = Color(0.33, 0.33, 0.9, 1)
			pData.denialCursor.Color = c
			pData.denialCursor:GetData().isSpiritOfDenialCursor = true
		end
		pData.denialCursor.Target = pData.denialTarget
		pData.denialCursor.Position = pData.denialTarget.Position
		pData.denialCursor.DepthOffset = pData.denialTarget.DepthOffset - 50
		pData.denialCursor.SpriteScale = Vector(0.1, 0.1) * pData.denialTarget.Size * pData.denialTarget.SizeMulti
		
		if pData.denialTarget.FrameCount % 20 == 0 then
			local c1 = pData.denialTarget.Color
			c1:SetColorize(2, 2, 3, 1)
			pData.denialTarget:SetColor(c1, 20, 1, true, true)
			
			local c2 = pData.denialCursor.Color
			c2:SetColorize(3, 3, 4, 1)
			pData.denialCursor:SetColor(c2, 20, 1, true, true)
		end
	elseif pData.denialCursor then
		pData.denialCursor:Remove()
		pData.denialCursor = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.DenialUpdate)

local denialDicePos = {}

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	denialDicePos = {}
end)

function mod:DenialDiceUpdate(dice)
	if dice.SubType ~= DENIAL_DICE then
		return
	end
	if game:GetRoom():GetFrameCount() == 0 then
		-- Delete self on new room.
		-- Not really needed due to the Timeout.
		dice:Remove()
	end
	dice.Timeout = 5
	
	lib.GetOrInit(denialDicePos, dice.Position.X)[dice.Position.Y] = game:GetFrameCount()
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.DenialDiceUpdate, PickupVariant.PICKUP_TAROTCARD)

function mod:DenialDiceInit(pickup)
	if pickup.Variant == PickupVariant.PICKUP_TAROTCARD and pickup.SubType == DENIAL_DICE then
		mod:DenialDiceUpdate(pickup)
		return
	end
	local diceFrame = lib.GetOrInit(denialDicePos, pickup.Position.X)[pickup.Position.Y]
	if diceFrame and game:GetFrameCount() - diceFrame <= 2 then
		-- Most likely pickup was rerolled from a Denial Dice. Remove the Timeout on first update.
		pickup:GetData().samaelClearTimeout = true
		
		-- Also, consider that rerolled dice to have been consumed.
		mod:ConsumeDenialDice()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.DenialDiceInit)

mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, function(_, pickup)
	if pickup:GetData().samaelClearTimeout then
		pickup:GetData().samaelClearTimeout = nil
		pickup.Timeout = -1
	end
end)

-- Reroll the targeted entity.
function mod:DenialDice(_, player, useFlags)
	local pData = player:GetData()
	
	if not pData.denialTarget or not IsValidDenialTarget(pData.denialTarget) then
		pData.denialTarget = nil
		FindDenialTarget(player)
	end
	
	if pData.denialTarget then
		Reroll(pData.denialTarget)
		mod:ConsumeDenialDice()
	end
end
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.DenialDice, DENIAL_DICE)

---------- TRACKING WHICH ITEM POOL A PEDESTAL CAME FROM ----------

local itemSpawnedFromPool = {}
local itemSpawnedWithSeed = {}

-- Detect when an item is chosen from a pool.
function mod:DenialPostChooseItem(itemType, itemPool, decrease, seed)
	if decrease and itemPool >= 0 and itemPool ~= lib.GetCurrentRoomItemPool() then
		itemSpawnedFromPool[itemType] = itemPool
		--lib.Log("Chose item " .. Isaac:GetItemConfig():GetCollectible(itemType).Name .. " from pool: " .. itemPool, true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_GET_COLLECTIBLE, mod.DenialPostChooseItem)

-- Detect when an item is about to spawn.
function mod:DenialPreItemSpawn(entityType, entityVariant, entitySubType, pos, vel, spawner, seed)
	if entityType == EntityType.ENTITY_PICKUP and entityVariant == PickupVariant.PICKUP_COLLECTIBLE then
		itemSpawnedWithSeed[seed] = true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN, mod.DenialPreItemSpawn)

function mod:DenialPostItemInit(pickup)
	local itemType = pickup.SubType
	
	if itemSpawnedFromPool[itemType] then
		-- Newly spawned pedestal, or otherwise if we have no pool saved for this item in this room yet.
		if itemSpawnedWithSeed[pickup.InitSeed] or not GetDenialItemPoolInfo()[""..pickup.InitSeed] then
			GetDenialItemPoolInfo()[""..pickup.InitSeed] = itemSpawnedFromPool[itemType]
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.DenialPostItemInit, PickupVariant.PICKUP_COLLECTIBLE)

function mod:DenialPostUpdate()
	itemSpawnedFromPool = {}
	itemSpawnedWithSeed = {}
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.DenialPostUpdate)
