local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local BAG_O_BONES = mod.ENTITIES.BAG_O_BONES.Var

local kPickupSpawnSpeed = 3

-- Chance that a Bag o' Bones can drop from skulls (if it didn't drop anything else).
local kBagOfBonesSkullSpawnChance = 0.05

-- Chance for Bag o' Bones to replace a sack.
local kBagOfBonesSackReplacementChance = 0.03

-- Chance for a Bag o' Bones to come from a dirt pile.
local kBagOfBonesDigUpChance = 0.5

-- Chances for bags to drop from chests.
local ChestDropChance = {
	[PickupVariant.PICKUP_CHEST] = 0.02,
	[PickupVariant.PICKUP_BOMBCHEST] = 0.1,
	[PickupVariant.PICKUP_SPIKEDCHEST] = 0.02,
	[PickupVariant.PICKUP_OLDCHEST] = 0.25,
	[PickupVariant.PICKUP_HAUNTEDCHEST] = 0.33,
	[PickupVariant.PICKUP_LOCKEDCHEST] = 0.02,
}

-- Sack replacement.
function mod:BagofBonesReplacement(eType, eVariant, eSubType, pos, vel, spawner, seed)
	if eType == EntityType.ENTITY_PICKUP and eVariant == PickupVariant.PICKUP_GRAB_BAG and eSubType == 0 then
		local rng = RNG()
		rng:SetSeed(seed, 1)
		if rng:RandomFloat() <= kBagOfBonesSackReplacementChance then
			return {EntityType.ENTITY_PICKUP, BAG_O_BONES, 0, seed}
		end
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN, mod.BagofBonesReplacement)

-- Adding to chest loot.
function mod:BagOfBonesFromChest(pickup)
	-- Only if unlocked/enabled.
	if mod.ContentManager:EntityLockedOrDisabled(EntityType.ENTITY_PICKUP, BAG_O_BONES) then return end
	
	if ChestDropChance[pickup.Variant] and ChestDropChance[pickup.Variant] > 0 then
		local data = pickup:GetData()
		if data.samaelChestState == ChestSubType.CHEST_CLOSED and pickup.SubType == ChestSubType.CHEST_OPENED then
			local rng = RNG()
			rng:SetSeed(pickup.DropSeed, 1)
			if rng:RandomFloat() <= ChestDropChance[pickup.Variant] then
				game:Spawn(EntityType.ENTITY_PICKUP, BAG_O_BONES, pickup.Position, RandomVector()*kPickupSpawnSpeed, nil, 0, pickup.DropSeed)
			end
		end
		data.samaelChestState = pickup.SubType
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.BagOfBonesFromChest)

function mod:BagOfBonesDirtPile(dirt)
	local anim = dirt:GetSprite():GetAnimation()
	if anim == "DugUp" and dirt:GetData().samaelDirtLastAnim == "Idle" then
		local rng = RNG()
		rng:SetSeed(dirt.InitSeed, 1)
		if rng:RandomFloat() <= kBagOfBonesDigUpChance then
			game:Spawn(EntityType.ENTITY_PICKUP, BAG_O_BONES, dirt.Position, RandomVector()*kPickupSpawnSpeed, nil, 0, dirt.InitSeed)
		end
	end
	dirt:GetData().samaelDirtLastAnim = anim
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.BagOfBonesDirtPile, EffectVariant.DIRT_PATCH)

-- Trinkets that may be spawned.
local BoneBagTrinkets = {
	-- Bone trinkets.
	TrinketType.TRINKET_POLISHED_BONE,
	TrinketType.TRINKET_HOLLOW_HEART,
	TrinketType.TRINKET_CURSED_SKULL,
	TrinketType.TRINKET_WISH_BONE,
	TrinketType.TRINKET_FINGER_BONE,
	TrinketType.TRINKET_RIB_OF_GREED,
	TrinketType.TRINKET_BLACK_TOOTH,
	-- Other stuff I still think is reasonable.
	TrinketType.TRINKET_KIDS_DRAWING,
	TrinketType.TRINKET_MYSTERIOUS_PAPER,
	TrinketType.TRINKET_MISSING_POSTER,
	TrinketType.TRINKET_CRACKED_DICE,
	TrinketType.TRINKET_FADED_POLAROID,
	TrinketType.TRINKET_WOODEN_CROSS,
	TrinketType.TRINKET_MISSING_PAGE,
}

local ModTrinkets = {
	"Calcium Penny",
}
for _, name in pairs(ModTrinkets) do
	local id = Isaac.GetTrinketIdByName(name)
	if id >= TrinketType.NUM_TRINKETS then
		table.insert(BoneBagTrinkets, id)
	end
end

-- Weighted payouts, aside from trinkets.
local BoneBagPayoutsNoTrinket = {
	BONE_ORBITAL = {
		Weight = 3,
		Spawn = function(bag, rng, player)
			local vel = RandomVector()*kPickupSpawnSpeed*2
			local bone = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FamiliarVariant.BONE_ORBITAL, 0, bag.Position, vel, player)
			bone:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			bone.Parent = player
			bone:GetData().samaelBagOfBonesInit = true
			bone:GetData().samaelBagOfBonesVel = vel
		end,
	},
	BONE_HEART = {
		Weight = 2,
		Spawn = function(bag, rng, player)
			local vel = RandomVector()*kPickupSpawnSpeed
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_BONE, bag.Position, vel, nil)
		end,
	},
	RUNE = {
		Weight = 2,
		Spawn = function(bag, rng, player)
			local vel = RandomVector()*kPickupSpawnSpeed
			local rune = game:GetItemPool():GetCard(rng:Next(), false, true, true)
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, rune, bag.Position, vel, nil)
		end,
	},
	FRIEND = {
		Weight = 2,
		Spawn = function(bag, rng, player)
			-- Pull from Thanatophilia's code to spawn a random bony friend.
			mod:SpawnBoneFriend(player, bag, rng, true)
		end,
	},
}

-- Weighted payouts, including trinkets.
local AllBoneBagPayouts = lib.ShallowCopy(BoneBagPayoutsNoTrinket)
AllBoneBagPayouts.TRINKET = {
	Weight = 1,
	Spawn = function(bag, rng, player)
		local trinket
		
		if FiendFolio then
			local isAnyGolem, isMixedGolem = FiendFolio.GolemExists()
			if isAnyGolem or (isMixedGolem and rng:RandomInt(3) ~= 0) then
				trinket = FiendFolio.GetGolemTrinket(nil, "Fossil", true)
			end
		end
		
		if not trinket then
			lib.Shuffle(BoneBagTrinkets, rng)
			for _, id in pairs(BoneBagTrinkets) do
				if game:GetItemPool():RemoveTrinket(id) then
					trinket = id
					break
				end
			end
		end
		
		if trinket then
			local vel = RandomVector()*kPickupSpawnSpeed
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, trinket, bag.Position, vel, nil)
			return trinket
		end
		
		-- No valid trinkets available. Spawn something else.
		return false
	end,
}

--[[mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	print(lib.PickRandom({
		{Hello=1, Weight = 3},
		{Hello=2, Weight = 2},
		{Hello=3, Weight = 2},
	}, Isaac.GetPlayer():GetCollectibleRNG(1)).Hello)
end)]]

-- Spawn payouts.
local function BagOBonesPayout(bag, player)
	lib.BoneGibsBurst(bag.Position)
	bag:GetSprite():Play("Collect", true)
	
	bag:GetData().samaelPickupFixedPos = bag.Position
	bag.Velocity = lib.ZeroVector
	
	local rng = bag:GetDropRNG()
	
	-- Choose which payout to give. If Trinkets are chosen and they fail to spawn, try again without trinkets.
	if lib.PickRandom(AllBoneBagPayouts, rng).Spawn(bag, rng, player) == false then
		lib.PickRandom(BoneBagPayoutsNoTrinket, rng).Spawn(bag, rng, player)
	end
	
	for i=0, rng:RandomInt(3) do
		AllBoneBagPayouts.BONE_ORBITAL.Spawn(bag, rng, player)
	end
end

-- Detects if The Forgotten's club is touching a bag.
-- Credit to Piber20 for this function.
function mod:GetBoneSwingPickupPlayer(pickup)
	--try to get a player from bone club swings
	if pickup:IsShopItem() then return end
	
	for _, knife in pairs(Isaac.FindByType(EntityType.ENTITY_KNIFE, -1, 4, false, false)) do
		if knife.FrameCount > 0 and knife.Parent then
			local parent = knife.Parent
			if parent:ToPlayer() then
				local player = parent:ToPlayer()
				
				--find the center of the swing object
				knife = knife:ToKnife()
				local position = knife.Position
				local scale = 30
				if knife.Variant == 2 then --knife + bone
					scale = 42
				end
				scale = scale * knife.SpriteScale.X
				local offset = Vector(scale,0)
				offset = offset:Rotated(knife.Rotation)
				position = position + offset
				
				--do player checks
				if (position - pickup.Position):Length() < pickup.Size + scale and (not pickup:GetSprite():IsPlaying("Collect")) then --check if the player is touching it
					return player
				end
			end
		end
	end
end

-- Detect when the player touches a bag.
function mod:BagOBonesCollision(bag, collider)
	local player = collider:ToPlayer()
	if player and bag:IsShopItem() and bag.Price > player:GetNumCoins() then
		return true
	end
	
	if bag:GetSprite():GetAnimation() == "Collect" then
		return true
	end
	
	if collider and player then
		if bag:IsShopItem() then
			if bag.Price == PickupPrice.PRICE_SPIKES then
				local tookDamage = player:TakeDamage(2, DamageFlag.DAMAGE_SPIKES | DamageFlag.DAMAGE_NO_PENALTIES, EntityRef(nil), 30)
				if not tookDamage then
					return true
				end
			elseif bag.Price > 0 then
				player:AddCoins(-1 * bag.Price)
			end
			bag.Price = 0
		end
		BagOBonesPayout(bag, player)
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_PICKUP_COLLISION, mod.BagOBonesCollision, BAG_O_BONES)

-- Post-update function (handles animations and some other stuff).
function mod:BagOBonesUpdate(bag)
	local sprite = bag:GetSprite()
	
	if sprite:IsFinished("Collect") then
		bag:Remove()
		return
	end
	
	if sprite:GetAnimation() == "Collect" then
		mod:BagOBonesStopMoving(bag)
	elseif sprite:IsPlaying("Appear") and sprite:IsEventTriggered("DropSound") then
		sfxManager:Play(SoundEffect.SOUND_FETUS_JUMP)
	end
	
	if sprite:GetAnimation() == "Idle" or sprite:WasEventTriggered("DropSound") then
		local meleePickupPlayer = mod:GetBoneSwingPickupPlayer(bag)
		if meleePickupPlayer then
			BagOBonesPayout(bag, meleePickupPlayer)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.BagOBonesUpdate, BAG_O_BONES)

-- Freeze the bag in place when its being opened.
function mod:BagOBonesStopMoving(bag)
	local data = bag:GetData()
	
	if bag:GetSprite():GetAnimation() == "Collect" then
		if data.samaelPickupFixedPos then
			bag.Position = data.samaelPickupFixedPos
		end
		bag.Velocity = lib.ZeroVector
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_RENDER, mod.BagOBonesStopMoving, BAG_O_BONES)

-- When bone orbitals spawn from the bag, have them slide a little bit before snapping to the player.
function mod:BoneOrbitalUpdate(entity)
	local data = entity:GetData()
	
	if data.samaelBagOfBonesFixOffset then
		entity.PositionOffset = lib.Lerp(entity.PositionOffset, lib.ZeroVector, 0.1)
		if entity.FrameCount - data.samaelBagOfBonesFixOffset > 20 then
			data.samaelBagOfBonesFixOffset = nil
			entity.PositionOffset = lib.ZeroVector
		end
	end
	
	if data.samaelBagOfBonesInit then
		if game:GetRoom():GetFrameCount() <= 1 then
			data.samaelBagOfBonesInit = false
			entity.PositionOffset = lib.ZeroVector
			return
		end
		if data.samaelBagOfBonesVel then
			entity.Velocity = data.samaelBagOfBonesVel
			data.samaelBagOfBonesVel = data.samaelBagOfBonesVel * 0.9
		end
		local sprite = entity:GetSprite()
		if sprite:GetFrame() >= 5 then
			sprite:SetFrame(5)
		end
		local t = 10
		local x = math.min(entity.FrameCount, t)
		local offset = math.sin(math.pi * 1.5 * x / t) * -12
		entity.PositionOffset = Vector(0, offset)
		if entity.FrameCount > 20 then
			data.samaelBagOfBonesInit = false
			data.samaelBagOfBonesFixOffset = entity.FrameCount
		end
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.BoneOrbitalUpdate, FamiliarVariant.BONE_ORBITAL)

-- All BackdropTypes where Skulls are the alt rock type.
local IsSkullBackdrop = {}
IsSkullBackdrop[BackdropType.DEPTHS] = true
IsSkullBackdrop[BackdropType.NECROPOLIS] = true
IsSkullBackdrop[BackdropType.DANK_DEPTHS] = true
IsSkullBackdrop[BackdropType.SHEOL] = true
IsSkullBackdrop[BackdropType.SACRIFICE] = true
IsSkullBackdrop[BackdropType.MAUSOLEUM] = true
IsSkullBackdrop[BackdropType.MAUSOLEUM_ENTRANCE] = true
IsSkullBackdrop[BackdropType.MAUSOLEUM2] = true
IsSkullBackdrop[BackdropType.MAUSOLEUM3] = true
IsSkullBackdrop[BackdropType.MAUSOLEUM4] = true
IsSkullBackdrop[BackdropType.GEHENNA] = true

-- Stores the skulls in the current room.
local skulls = {}

-- Find all the skulls in the room, if any.
function mod:FindSkulls()
	local room = game:GetRoom()
	
	skulls = {}
	
	-- Don't bother with any of this stuff if Bag o' Bones is locked.
	if mod.ContentManager:EntityLockedOrDisabled(EntityType.ENTITY_PICKUP, BAG_O_BONES) then return end
	
	if not IsSkullBackdrop[room:GetBackdropType()] then return end
	
	for i=0, room:GetGridSize() do
		local gridEntity = room:GetGridEntity(i)
		if gridEntity and gridEntity:GetType() == GridEntityType.GRID_ROCK_ALT and gridEntity.State ~= 2 then
			table.insert(skulls, i)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.FindSkulls)
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.FindSkulls, CollectibleType.COLLECTIBLE_D12)

-- Holds pickups or hosts that were initialized during the current frame.
-- For checking if a destroyed skull dropped something else.
local possibleSkullDrops = {}

function mod:CheckPossibleSkullDrops(pickup)
	if not lib.IsEmpty(skulls) then
		table.insert(possibleSkullDrops, pickup.Position)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.CheckPossibleSkullDrops, EntityType.ENTITY_HOST)
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.CheckPossibleSkullDrops)

-- Repeatedly check every skull, to detect when it is destroyed, and to check if it dropped something else.
function mod:CheckSkulls()
	local room = game:GetRoom()
	
	if IsSkullBackdrop[room:GetBackdropType()] then
		for key, gridId in pairs(skulls) do
			local gridEntity = room:GetGridEntity(gridId)
			if gridEntity and gridEntity:GetType() == GridEntityType.GRID_ROCK_ALT then
				if gridEntity.State == 2 then
					-- Skull has been destroyed since the last update.
					local allowSpawn = true
					for _, spawnedPos in pairs(possibleSkullDrops) do
						if spawnedPos:Distance(gridEntity.Position) < 1 then
							-- Something else spawned at this position in this same frame - the skull most likely dropped something.
							allowSpawn = false
						end
					end
					if allowSpawn then
						local rng = RNG()
						rng:SetSeed(gridEntity.Desc.SpawnSeed, 1)
						if rng:RandomFloat() <= kBagOfBonesSkullSpawnChance then
							-- Drop a bag o' bones.
							game:Spawn(EntityType.ENTITY_PICKUP, BAG_O_BONES, gridEntity.Position, lib.ZeroVector, nil, 0, gridEntity.Desc.SpawnSeed)
						end
					end
					skulls[key] = nil
				end
			else
				skulls[key] = nil
			end
		end
	end
	
	possibleSkullDrops = {}
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.CheckSkulls)