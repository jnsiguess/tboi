-- Contains code for Samael as a character. Tainted Samael shares much of this basic functionality.
-- This file is the original Samael main.lua from 2017 and has thus been modified many times over the years.
-- The code in this file is in a very messy state and I do not reccomend trying to read it.
-- If you have questions about how things work you can just ask me directly.
--
-- I do not reccomend trying to reference this code to try to make your own melee character!
-- If you'd like you can reach out to me for help, but you shouldn't try to make a melee character as your very first mod.

local mod = SamaelMod
local lib = mod.Lib

local game = mod.Game
local sfxManager = mod.SfxManager

-- References
local kScytheId = Isaac.GetEntityTypeByName("Samael Scythe") -- Entity ID of the scythe weapon entity
local kScytheBladeId = Isaac.GetEntityTypeByName("Samael Scythe Blade") -- Entity ID of the scythe blade, for visual overlay
local kScytheTearVariant = Isaac.GetEntityVariantByName("Samael Scythe Projectile") -- Entity variant number of the scythe projectile
local kBrimstoneHoleId = Isaac.GetEntityTypeByName("Samael Brimstone Hole")
local kScytheHitboxType = 617 -- EntityKnife subtype of the scythe's hitbox entity.
local kScytheKnifeType = 618 -- EntityKnife subtype for a thrown scythe-knife (Mom's knife synergy).
local kLudoScytheType = 619 -- EntityKnife subtype for a controlled (Ludovidico) projectile.
local kGhostScythe = Isaac.GetEntityVariantByName("Samael Ghost Scythe")  -- Effect variant of a ghost scythe that does a single swing
local kSamaelPocketActive = mod.ITEMS.MALAKH_MOT
local kWraithEffect = Isaac.GetEntityVariantByName("Samael Wraith") -- Entity for rendering wraith form

-- Static variables (can be used as tweaks/settings)
local kWraithModeDuration = 80 -- Duration of Wraith Mode
local kWraithModeEndIFrames = 30 -- Additional iframes at the end of Wraith Mode
local kWraithModeFireDelayMult = 0.51
local kWraithModeScytheDamageMult = 1.0

local kScytheMeleeDamageMult = 1.0 --Scythe damage = damage stat * 2 * this
local kScytheProjectileDamageMult = 1.0 --Scythe projectile damage = damage stat * this
local kScytheChargeTimeMin = 3 --Minimum number of frames to charge a projectile
local kScytheChargeTimeMid = 30 --Charge time at default fire delay (10)
local kScytheChargeTimeMax = 60 --Not the maximum charge time, but rather the point where charge time is equal to MaxFireDelay (I think...)
local kScytheMinSwingDelay = 2 -- Minimum frames between scythe swing
local kScytheMaxSwingDelay = 50 -- Maximum frames between scythe swings
local kScytheKnockbackStrength = 10 --How much of a knockback effect the scythe has
local kMaxScythes = 8 -- Max # of "multishot" scythes that can be held.
local kBrimstoneTime = 30 -- How long Samael's brimstone lasers last.
local kScytheKnifeHomingRange = 100 -- Max range at which homing scythes can choose targets

local kBaseRange = 260
local kScytheMaxSize = 2.0
local kScytheMinSize = 0.75
local kScytheRangeForMaxSize = 1000
local kScytheRangeForMinSize = 160

--------------------------------------------------
---- HELPER FUNCTIONS
--------------------------------------------------

-- Returns true if the player is doing an animation that shouldn't force a charge attack to get
-- released. This is because during some of these animations, player:GetFireDirection() == -1.
local function PlayerCantAttackDuringAnim(player)
	local anims = {
			"Pickup", "Hit", "Sad", "Happy", "Jump", "LiftItem", "HideItem", "UseItem", "FallIn",
			"JumpOut", "PickupWalkDown", "PickupWalkLeft", "PickupWalkUp", "PickupWalkRight"}
	for i,anim in ipairs(anims) do
		if lib.CurrentAnimIs(player:GetSprite(), anim) then
			return true
		end
	end
	return false
end

local function GetWraithCharge(player)
	return mod:GetPersistentPlayerData(player).wraithCharge or 0
end

local function SetWraithCharge(player, x)
	mod:GetPersistentPlayerData(player).wraithCharge = x
end

local function AddWraithCharge(player, x)
	local data = mod:GetPersistentPlayerData(player)
	data.wraithCharge = (data.wraithCharge or 0) + x
end

--------------------------------------------------
---- Some AB+ ""Fixes""
--------------------------------------------------

-- AB+ reroll ""fix"".
function mod:AbPostReroll()
	for _, player in pairs(lib.GetPlayers()) do
		if lib.IsSamael(player) then
			mod:UpdateCostumes(player, true)
			
			if not lib.HasItem(player, CollectibleType.COLLECTIBLE_SCHOOLBAG) then
				player:AddCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG, 0, false)
				player:RemoveCostume(Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG))
			end
			
			if not lib.HasItem(player, kSamaelPocketActive) then
				player:SwapActiveItems()
				player:AddCollectible(kSamaelPocketActive, 0, false)
				player:SwapActiveItems()
			end
		end
	end
end

-- AB+ ""fix"" for other reroll methods or the player otherwise loses Malakh Mot.
function mod:AbSpawnPocketActiveInStartingRoomIfLost()
	for _, player in pairs(lib.GetPlayers()) do
		if lib.IsSamael(player) and not lib.HasItem(player, kSamaelPocketActive) then
			local room = game:GetLevel():GetCurrentRoom()
			local pos = room:GetClampedPosition (room:GetTopLeftPos(), 20)
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, kSamaelPocketActive, pos, lib.ZeroVector, nil)
			break
		end
	end
end

-- Don't allow the player to pick up batteries for Malakh Mot.
function mod:AbBatteryPickup(battery, entity)
	local player = entity:ToPlayer()
	if player and lib.IsSamael(player) and player:GetActiveItem() == kSamaelPocketActive and player:GetActiveCharge() < 100 then
		return true
	end
end

if not REPENTANCE then
	mod:AddCallback(ModCallbacks.MC_USE_ITEM, mod.AbPostReroll, CollectibleType.COLLECTIBLE_D4)
	mod:AddCallback(ModCallbacks.MC_USE_ITEM, mod.AbPostReroll, CollectibleType.COLLECTIBLE_D100)
	mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, mod.AbSpawnPocketActiveInStartingRoomIfLost)
	mod:AddCallback(ModCallbacks.MC_PRE_PICKUP_COLLISION, mod.AbBatteryPickup, PickupVariant.PICKUP_LIL_BATTERY)
end

--------------------------------------------------
---- PLAYER INIT/UPDATE
--------------------------------------------------

local function InitializeScytheData(player)
	local playerData = player:GetData()
	
	playerData.samaelScytheType = "default"
	playerData.samaelScytheState = 0
	playerData.samaelScytheScale = 1
	playerData.samaelScytheFlipped = false
	--playerData.samaelScytheRenderDirection = Direction.DOWN
	playerData.samaelScytheLastCardinalDirection = Direction.DOWN
	playerData.samaelScytheLastVectorDirection = lib.NormalVector
	playerData.samaelScytheCharge = 0
	playerData.samaelScytheMaxCharge = mod:calcChargeTime(player)
	playerData.samaelScytheSwingCooldown = 0
	playerData.samaelScytheCurrentOffset = lib.ZeroVector
	playerData.samaelScytheTargetOffset = lib.ZeroVector
	playerData.samaelScytheTargetRot = 0
	playerData.enemiesHitThisSwing = 0
	playerData.samaelEpiphoraCounter = 0
	playerData.samaelPencilCounter = 0
	playerData.neptunusCharge = 0
	playerData.swordFireDelay = 0

	playerData.numScythes = 1
end

function mod:GiveSamaelPocketActive(player)
	if not REPENTANCE then return end
	
	if lib.IsSamael(player) and not lib.IsTaintedSamael(player) and player:GetActiveItem(ActiveSlot.SLOT_POCKET) == 0 then
		player:SetPocketActiveItem(kSamaelPocketActive)
	end
	
	if player:GetActiveItem(ActiveSlot.SLOT_POCKET) == kSamaelPocketActive then
		if player:GetActiveItem(ActiveSlot.SLOT_PRIMARY) == kSamaelPocketActive then
			player:RemoveCollectible(kSamaelPocketActive, true, ActiveSlot.SLOT_PRIMARY)
		end
		if player:GetActiveItem(ActiveSlot.SLOT_SECONDARY) == kSamaelPocketActive then
			player:RemoveCollectible(kSamaelPocketActive, true, ActiveSlot.SLOT_SECONDARY)
		end
	end
end

function mod:SamaelPocketActiveCheck()
	for _, player in pairs(lib.GetPlayers()) do
		mod:GiveSamaelPocketActive(player)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, mod.SamaelPocketActiveCheck)
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.SamaelPocketActiveCheck)

function mod:InitializeSamael(player)
	if not lib.IsSamael(player) then return end
	
	local playerData = player:GetData()
	
	if REPENTANCE then
		lib.ScheduleForUpdate(function()
			mod:SamaelPocketActiveCheck()
		end, 0, nil, true)
	else
		-- No pocket actives in AB+, so we make do.
		if not lib.HasItem(player, CollectibleType.COLLECTIBLE_SCHOOLBAG) then
			player:AddCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG, 0, false)
			player:RemoveCostume(Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG))
		end
		if not lib.HasItem(player, kSamaelPocketActive) and (player:GetActiveItem() == 0 or player.SecondaryActiveItem == 0) then
			player:AddCollectible(kSamaelPocketActive, 0, false)
		end
	end

	-- Add Costumes
	mod:UpdateCostumes(player, true)

	if not GetWraithCharge(player) then
		SetWraithCharge(player, 0)
	end
	playerData.wraithTime = 0
	playerData.wraithActive = false
	playerData.wraithCooldown = 0
	
	playerData.samaelInitialized = true
	
	InitializeScytheData(player)
	
	mod:CheckPlayerAnm2(player)
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_INIT, mod.InitializeSamael)

function mod:CheckPlayerAnm2(player)
	if player:IsCoopGhost() then return end
	
	local level = game:GetLevel()
	
	local pSprite = player:GetSprite()
	
	local roomData = level:GetCurrentRoomDesc().Data
	
	local altTag = ""
	if REPENTANCE and lib.IsInMineshaft() then
		altTag = "_alt"
	end
	
	local defaultanm2 = "gfx/001.000_player.anm2"
	local targetAnm2 = defaultanm2
	
	local samaelAnm2 = "gfx/characters/samael/samael" .. altTag .. ".anm2"
	local taintedSamaelAnm2 = "gfx/characters/samael_b/samael_b" .. altTag .. ".anm2"
	local samaelChallengeAnm2 = "gfx/characters/samael_b/samael_i.anm2"
	
	if lib.IsChallengeSamael(player) then
		targetAnm2 = samaelChallengeAnm2
	elseif lib.IsTaintedSamael(player) then
		targetAnm2 = taintedSamaelAnm2
	elseif lib.IsSamael(player) then
		targetAnm2 = samaelAnm2
	end
	
	local currentAnm2 = pSprite:GetFilename()
	
	if (lib.IsSamael(player) and (forceReload or currentAnm2 ~= targetAnm2)) or (not lib.IsSamael(player) and (currentAnm2 == samaelAnm2 or currentAnm2 == taintedSamaelAnm2 or currentAnm2 == samaelChallengeAnm2)) then
		local anim = pSprite:GetAnimation()
		pSprite:Load(targetAnm2, true)
		pSprite:Play(anim, true)
	end
end

--------------------------------------------------------------------------------
-- SAMAEL'S FEATHER (RETRIBUTION EASTER EGG)

local function SpawnSamaelBaby(player, altSprite, forceSpawn)
	if not CiiruleanItems or not CiiruleanItems.SAMAEL_BABY then return end
	
	for _, ent in pairs(Isaac.FindByType(CiiruleanItems.SAMAEL_BABY.ID, CiiruleanItems.SAMAEL_BABY.VARIANT, CiiruleanItems.SAMAEL_BABY.SUBTYPE)) do
		if ent:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) and not ent:GetData().isSamaelsSamaelBaby and not ent:GetData().isThanatophiliaMinion then
			ent:GetData().isSamaelsSamaelBaby = true
			return ent
		end
	end
	
	local pData = player:GetData()
	
	if pData.samaelBabySpawnCooldown and not forceSpawn then
		pData.samaelBabySpawnCooldown = pData.samaelBabySpawnCooldown - 1
		if pData.samaelBabySpawnCooldown > 0 then
			return nil
		end
	end
	
	local pos = game:GetRoom():FindFreePickupSpawnPosition(player.Position, 30, true, false)
	local angel = Isaac.Spawn(CiiruleanItems.SAMAEL_BABY.ID, CiiruleanItems.SAMAEL_BABY.VARIANT, CiiruleanItems.SAMAEL_BABY.SUBTYPE, pos, lib.ZeroVector, player)
	angel:AddCharmed(EntityRef(player), -1)
	angel:GetData().isSamaelsSamaelBaby = true
	
	if altSprite then
		local sprite = angel:GetSprite()
		sprite:ReplaceSpritesheet(0, "gfx/bosses/retribution/samael/monster_babyskeltal_x.png")
		sprite:LoadGraphics()
	end
	
	pData.samaelBabySpawnCooldown = 120
	
	return angel
end

local function HandleSamaelFeather(player)
	if not player:HasTrinket(mod.ITEMS.SAMAELS_FEATHER) then return end
	
	if player:GetTrinket(0) == mod.ITEMS.SAMAELS_FEATHER or player:GetTrinket(0) == mod.ITEMS.SAMAELS_FEATHER then
		player:TryRemoveTrinket(mod.ITEMS.SAMAELS_FEATHER)
		mod:AddSmeltedTrinket(player, mod.ITEMS.SAMAELS_FEATHER)
		
		sfxManager:Play(SoundEffect.SOUND_THUMBSUP)
		sfxManager:Play(SoundEffect.SOUND_HOLY, 1, 0, false, 0.85)
		player:SetColor(Color(1,1,1,1,1,1,1), 20, 1, true, true)
		
		for i=1, 14 do
			local bubbleTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HAEMO_TRAIL, 617, player.Position - Vector(0, player.Size), RandomVector()*20, pickup):ToEffect()
			bubbleTrail.SpriteScale = Vector(0.4, 0.4)
			local n = 0.85
			bubbleTrail.Color = Color(n,n,n, 0.75)
			bubbleTrail:GetSprite():Play("Poof2", true)
			bubbleTrail:GetSprite().PlaybackSpeed = 0.4
			bubbleTrail.Target = player
		end
	end
	
	if not CiiruleanItems then return end
	
	local queuedItem = player.QueuedItem and player.QueuedItem.Item
	
	if queuedItem and queuedItem:IsTrinket() and queuedItem.ID == mod.ITEMS.SAMAELS_FEATHER then return end
	
	local pData = player:GetData()
	
	local forceSpawn = false
	if not pData.spawnedSamaelBabies then
		forceSpawn = true
		pData.spawnedSamaelBabies = true
	end
	
	if not pData.samaelBaby1 or not pData.samaelBaby1:Exists() then
		pData.samaelBaby1 = SpawnSamaelBaby(player, false, forceSpawn)
	end
	if not pData.samaelBaby2 or not pData.samaelBaby2:Exists() then
		pData.samaelBaby2 = SpawnSamaelBaby(player, true, forceSpawn)
	end
end

mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
	if CiiruleanItems and CiiruleanItems.SAMAEL and npc.Type == CiiruleanItems.SAMAEL.ID and npc.Variant == CiiruleanItems.SAMAEL.VARIANT then
		if npc:IsDead() and not npc:GetData().spawnedSamaelsFeather then
			npc:GetData().spawnedSamaelsFeather = true
			local foundSamael = false
			local foundFeather = #Isaac.FindByType(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, mod.ITEMS.SAMAELS_FEATHER) > 0
			for _, player in pairs(lib.GetPlayers()) do
				if lib.IsSamael(player) then
					foundSamael = true
				end
				if player:HasTrinket(mod.ITEMS.SAMAELS_FEATHER) then
					foundFeather = true
				end
			end
			if foundSamael and not foundFeather then
				Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, mod.ITEMS.SAMAELS_FEATHER, npc.Position, Vector(-3, 2), npc)
			end
		end
	end
end)

local SAMAEL_FEATHER_ANM2 = "gfx/items/trinkets/samael/samaels_feather.anm2"

mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, function(_, pickup)
	if pickup.SubType ~= mod.ITEMS.SAMAELS_FEATHER then return end
	
	local sprite = pickup:GetSprite()
	if sprite:GetFilename() ~= SAMAEL_FEATHER_ANM2 then
		for _, player in pairs(lib.GetPlayers()) do
			if lib.IsSamael(player) then
				local anim = sprite:GetAnimation()
				sprite:Load(SAMAEL_FEATHER_ANM2, true)
				sprite:Play(anim, true)
				break
			end
		end
	end
	
	if pickup.FrameCount % 4 == 0 then
		local pos = pickup.Position - Vector(0, 10)
		for _, player in pairs(lib.GetPlayers()) do
			if lib.IsSamael(player) then
				local bubbleTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HAEMO_TRAIL, 617, pos, RandomVector()*4, pickup):ToEffect()
				bubbleTrail.SpriteScale = Vector(0.4, 0.4)
				local n = 0.85
				bubbleTrail.Color = Color(n,n,n, 0.85)
				bubbleTrail:GetSprite():Play("Poof2", true)
				bubbleTrail:GetSprite().PlaybackSpeed = 0.4
				bubbleTrail.Target = player
			end
		end
	end
end, PickupVariant.PICKUP_TRINKET)

mod:AddCallback(ModCallbacks.MC_POST_PROJECTILE_INIT, function(_, projectile)
	if CiiruleanItems and CiiruleanItems.SAMAEL_BABY and projectile.SpawnerEntity
			and projectile.SpawnerEntity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)
			and (projectile.SpawnerEntity:GetData().isSamaelsSamaelBaby or projectile.SpawnerEntity:GetData().isThanatophiliaMinion)
			and projectile.SpawnerType == CiiruleanItems.SAMAEL_BABY.ID
			and projectile.SpawnerVariant == CiiruleanItems.SAMAEL_BABY.VARIANT
			and projectile.SpawnerEntity.SubType == CiiruleanItems.SAMAEL_BABY.SUBTYPE then
		projectile.Variant = ProjectileVariant.PROJECTILE_BONE
		
		local sprite = projectile:GetSprite()
		sprite:Load("gfx/grey_bone_projectile.anm2", true)
		sprite:LoadGraphics()
	end
end, ProjectileVariant.PROJECTILE_NORMAL)

--------------------------------------------------------------------------------

function mod:PostPlayerUpdate(player)
	local playerData = player:GetData()

	if REPENTANCE then
		mod:CheckForSpecialInteractions(player)
	end
	
	--mod:CheckCanShoot(player)
	
	--local roomName = game:GetLevel():GetCurrentRoomDesc().Data.Name
	--local isMineshaft = roomName:find("^Mineshaft") ~= nil or roomName:find("^Knife Piece Room") ~= nil
	
	if lib.IsSamael(player) then --If the player is Samael
		if not playerData.samaelInitialized then
			mod:InitializeSamael(player)
		end
		if not playerData.isSamael then
			mod:UpdateCostumes(player, true)
			playerData.isSamael = true -- In case of character-changing effects like clicker.
		end
		--mod:CheckPlayerAnm2(player)

		local level = game:GetLevel()
		local room = level:GetCurrentRoom()

		if not REPENTANCE then
			player.FireDelay = player.MaxFireDelay
			
			if not lib.HasItem(player, CollectibleType.COLLECTIBLE_SCHOOLBAG) then
				player:AddCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG, 0, false)
				player:RemoveCostume(Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG))
				
				if not lib.HasItem(player, kSamaelPocketActive) then
					player:SwapActiveItems()
					player:AddCollectible(kSamaelPocketActive, 0, false)
					player:SwapActiveItems()
				end
			end
		end
		
		--[[if not lib.IsTaintedSamael(player) then
			mod:WraithModeHandler(player)
		end]]
		
		if not playerData.samaelScythe or not playerData.samaelScythe:Exists() then
			mod:SpawnScythe(player)
			mod:UpdateScytheType(player)
			mod:UpdateNumScythes(player)
		end
		
		-- At the start of a new room
		if room:GetFrameCount() == 1 or player.FrameCount == 1 then
			player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
			playerData.astralProjected = nil
		end
		
		-- Force the scythe to visually update during Astral Projection's time shenanigans.
		if REPENTANCE and player:GetEffects():HasCollectibleEffect(CollectibleType.COLLECTIBLE_ASTRAL_PROJECTION) then
			if not playerData.astralProjected then
				playerData.astralProjected = game:GetFrameCount()
			end
			if playerData.astralProjected > game:GetFrameCount() - 125 then
				playerData.samaelScythe:GetSprite():Update()
			end
		end
		
		if playerData.swordFireDelay or 0 > 0 then
			playerData.swordFireDelay = playerData.swordFireDelay - 1
		end
		
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_DEAD_TOOTH) and player:GetAimDirection():Length() > 0.2 then
			if not playerData.samaelDeadTooth or not playerData.samaelDeadTooth:Exists() then
				playerData.samaelDeadTooth = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.FART_RING, 0, player.Position, lib.ZeroVector, player):ToEffect()
				playerData.samaelDeadTooth:FollowParent(player)
				playerData.samaelDeadTooth.ParentOffset = Vector(0, -20)
				playerData.samaelDeadTooth.Scale = 0.4
			end
			playerData.samaelDeadTooth.Timeout = 10
		end
		
		HandleSamaelFeather(player, forceSpawn)
		
		local shouldHaveBottle = TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.THE_BOTTLE) and not mod:HasThrownKnifeScythes(player) and not playerData.mementoMoriActive
		local hasBottle = playerData.samaelFollowBottle and playerData.samaelFollowBottle:Exists()
		
		if shouldHaveBottle and not hasBottle then
			--[[local pos = player.Position
			if playerData.samaelLastProjectileDirection then
				pos = pos + playerData.samaelLastProjectileDirection:Resized(50)
			end]]
			local pos = player.Position + Vector(50, 0)
			local bottle = Isaac.Spawn(EntityType.ENTITY_KNIFE, 0, 3, pos, lib.ZeroVector, player):ToKnife()
			bottle.Parent = player
			if playerData.samaelBottleBroken then
				bottle:GetData().BottleRoomIndex = game:GetLevel():GetCurrentRoomIndex()
				bottle:GetData().BrokenBefore = true
				bottle:Update()
				bottle:GetSprite():ReplaceSpritesheet(0, "gfx/projectiles/knife_bottle_broken.png")
			else
				bottle:GetSprite():ReplaceSpritesheet(0, "gfx/projectiles/knife_bottle.png")
			end
			bottle:GetSprite():LoadGraphics()
			bottle:Update()
			playerData.samaelFollowBottle = bottle
		elseif not shouldHaveBottle and hasBottle then
			playerData.samaelFollowBottle:Remove()
			playerData.samaelFollowBottle = nil
		end
		
		--local isIllusionModIllusion = playerData.IllusionMod and playerData.IllusionMod.IsIllusion
		
		--[[if lib.IsTaintedSamael(player) then
			if player:GetSprite():IsPlaying("Death") or player:GetSprite():IsPlaying("Hit")
		end]]
		
		-- Custom death animation
		--[[if player:GetSprite():IsPlaying("Death") then
			Isaac.DebugString("hi")
			player:GetSprite():Play("TrueDeath", true)
		end]]
		--[[if player:GetSprite():IsPlaying("Death") and false then --When player dies
			if not playerData.samaelDying then
				local special = Isaac.Spawn(kSpecialAnimEntity, 0, 0, player.Position, lib.ZeroVector, player):ToNPC() --Spawn the special animations entity
				special.Parent = player
				if isIllusionModIllusion then
					special:GetData().isIllusion = true
				end
				special:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
				special:GetSprite():Play("Death", 1) --Play custom death animation
				special.CanShutDoors = false
				playerData.samaelDying = true --Set dying flag
			end
			player.Visible = false
		elseif playerData.samaelDying then --If the player is not dying, and the dying flag is on
			playerData.samaelDying = false --Turn the flag off
			if not isIllusionModIllusion then
				player.Visible = true
			end
		end]]
	else -- This player is not Samael.
		if playerData.isSamael then -- This player WAS Samael...
			-- Make sure certain things do not persist outside of Samael
			--[[if not player:GetSprite():IsPlaying("Death") and playerData.samaelDying then
				playerData.samaelDying = false --Turn the flag off
				player.Visible = true
			end]]
			
			mod:UpdateCostumes(player, false)
			mod:CheckPlayerAnm2(player)
			
			playerData.isSamael = false
		end
		
		if player:GetActiveItem(ActiveSlot.SLOT_POCKET) == kSamaelPocketActive then
			player:RemoveCollectible(kSamaelPocketActive, true, ActiveSlot.SLOT_POCKET)
		end
	end
	
	mod:WraithModeHandler(player)
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.PostPlayerUpdate)

function mod:SamaelNewRoom()
	for _, player in pairs(lib.GetPlayers()) do
		local playerData = player:GetData()
		if lib.IsSamael(player) then
			if not playerData.samaelScythe or not playerData.samaelScythe:Exists() then
				mod:SpawnScythe(player)
				mod:UpdateScytheType(player)
				mod:UpdateNumScythes(player)
			end
			mod:CheckPlayerAnm2(player)
			playerData.hideScythe = false
			playerData.samaelBottleBroken = false
		elseif playerData.isSamael then
			mod:CheckPlayerAnm2(player)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.SamaelNewRoom)

local didSamaelSpecialFade

function mod:SamaelDeathHandler(player)
	if not lib.IsSamael(player) then return end
	
	local sprite = player:GetSprite()
	
	if sprite:IsPlaying("Death") then
		if sprite:IsEventTriggered("SamaelBlood") then
			Isaac.Spawn(1000, SoundEffect.SOUND_MEATY_DEATHS, 0, player.Position, lib.ZeroVector, player)
			sfxManager:Play(SoundEffect.SOUND_DEATH_BURST_LARGE, 1, 0, false, 1)
			lib.BoneGibsBurst(player.Position)
		elseif sprite:IsEventTriggered("SamaelFade") and didSamaelSpecialFade ~= game:GetFrameCount() then
			mod:FadeSamaelSpecial(player)
			sfxManager:Play(SoundEffect.SOUND_BEAST_GHOST_RISE, 1.0, 0, false, 1.0)
			--[[lib.ScheduleForUpdate(function()
				sfxManager:Play(160, 2.0, 0, false, 0.5)
			end, 20)]]
			didSamaelSpecialFade = game:GetFrameCount()
		elseif sprite:IsEventTriggered("SamaelDeath") then
			local pitchMult = 1 / lib.Lerp(player.SpriteScale.X, 1.0, 0.5)
			sfxManager:Play(SoundEffect.SOUND_DEATH_DIES, 0.8, 0, false, 1.15 * pitchMult)
		elseif sprite:IsEventTriggered("SamaelBones") then
			lib.BoneGibsBurst(player.Position)
		end
	elseif sprite:IsPlaying("LostDeath") and sprite:GetFrame() == 0 then
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 10, player.Position, lib.ZeroVector, nil)
		poof:GetSprite():SetFrame(1)
		
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 5, player.Position, lib.ZeroVector, nil):ToEffect()
		local sprite = eff:GetSprite()
		sprite:ReplaceSpritesheet(0, "gfx/effects/samael_effect_010_poof02_bloodcloud.png")
		sprite:LoadGraphics()
		eff.Color = Color(1,1,1,0.7)
		sprite.Scale = Vector(0.5, 0.6) * player.SpriteScale.X
		eff:GetData().mementoMoriFade = true
		sfxManager:Play(162, 1, 0, false, 2.5)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.SamaelDeathHandler)

-- Body that shows up when you touch white fire
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, function(_, body)
	local sprite = body:GetSprite()
	local samael = string.find(sprite:GetFilename(), "samael.anm2")
	local tSamael = string.find(sprite:GetFilename(), "samael_b.anm2")
	if (samael or tSamael) and sprite:GetAnimation() ~= "FakeDeath" then
		if tSamael then
			mod:TaintedSamaelHurtSound(body.SpawnerEntity and body.SpawnerEntity:ToPlayer())
		end
		sprite:Play("FakeDeath", true)
	end
end, EffectVariant.DEVIL)

--------------------------------------------------
---- COSTUMES
--------------------------------------------------

local function CustomCostume(name, allowNormal, allowTainted, removeOriginal, lowPriority)
	if allowNormal == nil then
		allowNormal = true
	end
	if allowTainted == nil then
		allowTainted = true
	end
	if removeOriginal == nil then
		removeOriginal = false
	end
	if lowPriority == nil then
		lowPriority = false
	end
	
	return {
		Name = name,
		AllowNormal = allowNormal,
		AllowTainted = allowTainted,
		RemoveOriginal = removeOriginal,
		LowPriority = lowPriority,
	}
end

local function Costume(name, removeOriginal)
	return CustomCostume(name, true, true, removeOriginal, false)
end

local function LowPriorityCostume(name, removeOriginal)
	return CustomCostume(name, true, true, removeOriginal, true)
end

local function ACostume(name, removeOriginal)
	return CustomCostume(name, true, false, removeOriginal, false)
end

local function BCostume(name, removeOriginal)
	return CustomCostume(name, false, true, removeOriginal, false)
end

local SamaelCostumes = {}
SamaelCostumes[CollectibleType.COLLECTIBLE_SACK_HEAD] = {Costume("sackhead")}
SamaelCostumes[CollectibleType.COLLECTIBLE_CONE_HEAD] = {Costume("conehead")}
SamaelCostumes[CollectibleType.COLLECTIBLE_HABIT] = {Costume("habit", true)}
SamaelCostumes[CollectibleType.COLLECTIBLE_BRIMSTONE] = {Costume("brimstone")}
SamaelCostumes[CollectibleType.COLLECTIBLE_20_20] = {Costume("2020")}
SamaelCostumes[CollectibleType.COLLECTIBLE_THE_WIZ] = {Costume("wiz", true)}
SamaelCostumes[CollectibleType.COLLECTIBLE_TECHNOLOGY] = {Costume("tech")}
SamaelCostumes[CollectibleType.COLLECTIBLE_TECHNOLOGY_2] = {Costume("tech2")}
SamaelCostumes[CollectibleType.COLLECTIBLE_TECH_X] = {Costume("techx", true)}
SamaelCostumes[CollectibleType.COLLECTIBLE_8_INCH_NAILS] = {Costume("nails", true)}
SamaelCostumes[CollectibleType.COLLECTIBLE_WHORE_OF_BABYLON] = {Costume("wob")}
SamaelCostumes[CollectibleType.COLLECTIBLE_INFAMY] = {Costume("infamy")}
SamaelCostumes[CollectibleType.COLLECTIBLE_MAGGYS_BOW] = {Costume("bow")}
SamaelCostumes[CollectibleType.COLLECTIBLE_PAGEANT_BOY] = {Costume("pageant", true)}
SamaelCostumes[CollectibleType.COLLECTIBLE_BLACK_CANDLE] = {Costume("candle")}
SamaelCostumes[CollectibleType.COLLECTIBLE_MAGIC_8_BALL] = {LowPriorityCostume("8ball")}
SamaelCostumes[CollectibleType.COLLECTIBLE_SPOON_BENDER] = {Costume("spoon_bender")}

SamaelCostumes[CollectibleType.COLLECTIBLE_GIMPY] = {ACostume("gimpy", true)}
SamaelCostumes[CollectibleType.COLLECTIBLE_HOLY_GRAIL] = {ACostume("angel")}

if REPENTANCE then
	SamaelCostumes[CollectibleType.COLLECTIBLE_MONEY_EQUALS_POWER] = {Costume("moneypower")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_CARD_READING] = {LowPriorityCostume("cardreading")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_VENUS] = {Costume("venus"), BCostume("venus_head")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_JUPITER] = {ACostume("hidecloak")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_CHEMICAL_PEEL] = {BCostume("skull")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_SULFURIC_ACID] = {BCostume("acid", true)}
	SamaelCostumes[CollectibleType.COLLECTIBLE_INTRUDER] = {BCostume("intruder")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_TERRA] = {BCostume("terra")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_URANUS] = {BCostume("uranus", true)}
	SamaelCostumes[CollectibleType.COLLECTIBLE_SAUSAGE] = {BCostume("sosig")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_CHAOS] = {BCostume("chaos")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_ROID_RAGE] = {BCostume("roids")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_KNOCKOUT_DROPS] = {BCostume("knockout")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_MEAT] = {BCostume("meat", true)}
	SamaelCostumes[CollectibleType.COLLECTIBLE_GODS_FLESH] = {BCostume("godsflesh")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_CRICKETS_HEAD] = {BCostume("maxshead")}
	SamaelCostumes[CollectibleType.COLLECTIBLE_CAT_O_NINE_TAILS] = {BCostume("catoninetails", true)}
	SamaelCostumes[CollectibleType.COLLECTIBLE_ROTTEN_TOMATO] = {BCostume("tomato")}
else
	SamaelCostumes[CollectibleType.COLLECTIBLE_MONEY_IS_POWER] = {Costume("moneypower")}
end

if FiendFolio then
	SamaelCostumes[FiendFolio.ITEM.COLLECTIBLE.COOL_SUNGLASSES] = {Costume("cool_sunglasses", true)}
	SamaelCostumes[FiendFolio.ITEM.COLLECTIBLE.DEVILS_ABACUS] = {Costume("devilsabacus", false)}
	SamaelCostumes[FiendFolio.ITEM.COLLECTIBLE.EMOJI_GLASSES] = {Costume("emoji_glasses", true)}
	SamaelCostumes[FiendFolio.ITEM.COLLECTIBLE.BIRTHDAY_GIFT] = {Costume("birthday", true)}
	SamaelCostumes[FiendFolio.ITEM.COLLECTIBLE.EXCELSIOR] = {BCostume("excelsior", true)}
end
SamaelCostumes[-1] = nil

local SamaelCostumeBlacklist = {
	CollectibleType.COLLECTIBLE_STYE,
	CollectibleType.COLLECTIBLE_TOUGH_LOVE,
	CollectibleType.COLLECTIBLE_SQUEEZY,
	CollectibleType.COLLECTIBLE_TORN_PHOTO,
	CollectibleType.COLLECTIBLE_CURSE_OF_THE_TOWER,
	CollectibleType.COLLECTIBLE_SULFURIC_ACID,
	CollectibleType.COLLECTIBLE_IT_HURTS,
	CollectibleType.COLLECTIBLE_EYE_DROPS,
	CollectibleType.COLLECTIBLE_TOOTH_PICKS,
	CollectibleType.COLLECTIBLE_MOMS_PERFUME,
	CollectibleType.COLLECTIBLE_EPIPHORA,
	CollectibleType.COLLECTIBLE_DADS_LOST_COIN,
	CollectibleType.COLLECTIBLE_GLAUCOMA,
	CollectibleType.COLLECTIBLE_4_5_VOLT,
	CollectibleType.COLLECTIBLE_C_SECTION,
	CollectibleType.COLLECTIBLE_MUTANT_SPIDER,
}

if TaintedCollectibles then
	table.insert(SamaelCostumeBlacklist, TaintedCollectibles.SPIDER_FREAK)
end

local function RemoveCostume(player, collectibleType)
	local itemConfig = Isaac.GetItemConfig():GetCollectible(collectibleType)
	player:RemoveCostume(itemConfig)
end

function mod:ActuallyRemoveCostumes(player)
	local playerData = player:GetData()
	if playerData.SamaelRemoveCostumes and #playerData.SamaelRemoveCostumes > 0 then
		for _, collectibleType in pairs(playerData.SamaelRemoveCostumes) do
			RemoveCostume(player, collectibleType)
		end
		playerData.SamaelRemoveCostumes = {}
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, mod.ActuallyRemoveCostumes)

local function QueueRemoveCostume(player, collectibleType)
	RemoveCostume(player, collectibleType)
	local playerData = player:GetData()
	playerData.SamaelRemoveCostumes = playerData.SamaelRemoveCostumes or {}
	table.insert(playerData.SamaelRemoveCostumes, collectibleType)
end

local kSamaelBaseCostume = Isaac.GetCostumeIdByPath("gfx/characters/samael/samael_costume.anm2")
local kTaintedSamaelBaseCostume = Isaac.GetCostumeIdByPath("gfx/characters/samael_b/samael_b_costume.anm2")

function mod:UpdateCostumes(player, addBaseCostumes)
	local playerData = player:GetData()
	
	if not playerData.samaelCostumesAdded then
		playerData.samaelCostumesAdded = {}
	end
	
	Isaac.DebugString("[Samael] Refreshing Costumes...")
	
	if lib.IsSamael(player) then
		if addBaseCostumes then
			-- Try to remove costumes before adding them.
			-- This is to suppress the "Adding costume which was added before" messages in the console.
			if lib.IsTaintedSamael(player) then
				player:TryRemoveNullCostume(kTaintedSamaelBaseCostume)
				player:AddNullCostume(kTaintedSamaelBaseCostume)
			else
				player:TryRemoveNullCostume(kSamaelBaseCostume)
				player:AddNullCostume(kSamaelBaseCostume)
			end
		end
	else
		player:TryRemoveNullCostume(kSamaelBaseCostume)
		player:TryRemoveNullCostume(kTaintedSamaelBaseCostume)
	end
	
	for collectibleType, costumes in pairs(SamaelCostumes) do
		for _, costume in pairs(costumes) do
			local name = costume.Name
			local path = "gfx/characters/samael_" .. name .. ".anm2"
			local costumeId = Isaac.GetCostumeIdByPath(path)
			
			if costumeId > -1 then
				local shouldHaveCostume =
						lib.IsSamael(player) and lib.HasItem(player, collectibleType) and (
							(lib.IsTaintedSamael(player) and costume.AllowTainted)
							or (not lib.IsTaintedSamael(player) and costume.AllowNormal)
						)
				local key = ""..collectibleType.."."..costumeId
				local alreadyAdded = playerData.samaelCostumesAdded[key]
				
				-- Add the costume if the player should have it.
				if shouldHaveCostume and not alreadyAdded then
					player:TryRemoveNullCostume(costumeId)
					player:AddNullCostume(costumeId)
					if costume.RemoveOriginal then
						QueueRemoveCostume(player, collectibleType)
					end
					playerData.samaelCostumesAdded[key] = true
				end
				
				-- Remove the costume if the player should not have it.
				if not shouldHaveCostume and alreadyAdded then
					player:TryRemoveNullCostume(costumeId)
					playerData.samaelCostumesAdded[key] = nil
				end
			end
		end
	end
	
	if lib.IsTaintedSamael(player) then
		for _, collectibleType in pairs(SamaelCostumeBlacklist) do
			if lib.HasItem(player, collectibleType) then
				QueueRemoveCostume(player, collectibleType)
			end
		end
	end
	
	local addWings = false
	
	if lib.IsSamael(player) then
		local angelCostume = Isaac.GetCostumeIdByPath("gfx/characters/samael_angel.anm2")
		local realAngelCostume = Isaac.GetCostumeIdByPath("gfx/characters/samael_realangel.anm2")
		local improvedCostume = Isaac.GetCostumeIdByPath("gfx/characters/samael_improved.anm2")
		player:TryRemoveNullCostume(angelCostume)
		player:TryRemoveNullCostume(realAngelCostume)
		player:TryRemoveNullCostume(improvedCostume)
		
		local hasFeather = player:HasTrinket(mod.ITEMS.SAMAELS_FEATHER)
		local hasSamaelBirthright = not lib.IsTaintedSamael(player) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT)
		local hasAngel = player:HasPlayerForm(PlayerForm.PLAYERFORM_ANGEL) or lib.HasItem(player, CollectibleType.COLLECTIBLE_REVELATION)
		
		if hasFeather then
			player:AddNullCostume(realAngelCostume)
		elseif hasAngel then
			player:AddNullCostume(angelCostume)
		end
		
		addWings = hasFeather or hasSamaelBirthright
		
		if lib.IsChallengeSamael(player) then
			player:AddNullCostume(improvedCostume)
			addWings = true
		end
	end
	
	if addWings then
		player:AddCostume(Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_REVELATION), false)
		playerData.addedSamaelWingCostume = true
	elseif playerData.addedSamaelWingCostume then
		player:RemoveCostume(Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_REVELATION))
		playerData.addedSamaelWingCostume = false
	end
	
	--game:GetItemPool():ForceAddPillEffect(PillEffect.PILLEFFECT_I_FOUND_PILLS)
	
	--[[if lib.IsTaintedSamael(player) and player:GetEffects():HasNullEffect(NullItemID.ID_I_FOUND_HORSE_PILLS) then
		local costume = Isaac.GetCostumeIdByPath("gfx/characters/samael_megapills.anm2")
		player:TryRemoveNullCostume(costume)
		player:AddNullCostume(costume)
	end]]
	
	--[[if lib.HasItem(player, CollectibleType.COLLECTIBLE_JUPITER) then
		player:TryRemoveNullCostume(kSamaelCloakId)
	end]]
	
	Isaac.DebugString("[Samael] Done refreshing costumes.")
end

function mod:PostReroll(_, _, player)
	if lib.IsSamael(player) then
		mod:UpdateCostumes(player, true)
	end
end
if REPENTANCE then
	--mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.PostReroll, CollectibleType.COLLECTIBLE_CLICKER)
	mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.PostReroll, CollectibleType.COLLECTIBLE_D4)
	mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.PostReroll, CollectibleType.COLLECTIBLE_D100)
end

local kGnawedLeafCostume = Isaac.GetCostumeIdByPath("gfx/characters/samael_gnawedleaf.anm2")

function mod:MaybeUpdateCostumes(player)
	local pData = player:GetData()
	
	if lib.IsSamael(player) then
		local itemCount = player:GetCollectibleCount()
		local isCoopGhost = player:IsCoopGhost()
		local hasFeather = player:HasTrinket(mod.ITEMS.SAMAELS_FEATHER)
		
		if not isCoopGhost and pData.samaelIsCoopGhost then
			mod:UpdateCostumes(player, true)
		elseif itemCount ~= pData.samaelItemCount or hasFeather ~= pData.samaelHasFeather then
			mod:UpdateCostumes(player, false)
		end
		
		pData.samaelItemCount = itemCount
		pData.samaelIsCoopGhost = isCoopGhost
		pData.samaelHasFeather = hasFeather
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.MaybeUpdateCostumes)

--------------------------------------------------
---- SCYTHE FUNCTIONALITY
--------------------------------------------------

-- Spawn Samael's Scythe
function mod:SpawnScythe(player, childIndex)
	local playerData = player:GetData()
	local scythe = Isaac.Spawn(kScytheId, childIndex or 0, 0, player.Position, lib.ZeroVector, player):ToNPC()
	scythe.Parent = player
	scythe.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
	scythe:ClearEntityFlags(EntityFlag.FLAG_APPEAR) --Skip spawning animations
	scythe:AddEntityFlags(EntityFlag.FLAG_NO_STATUS_EFFECTS | EntityFlag.FLAG_PERSISTENT)
	scythe.CanShutDoors = false --Its not an enemy
	scythe:GetSprite().Rotation = playerData.samaelScytheTargetRot or 0
	
	if not childIndex or childIndex == 0 then -- Master Scythe
		if playerData.samaelScytheState ~= 1 then
			playerData.samaelScytheState = 0
		end
		playerData.samaelScythe = scythe
		playerData.hideScythe = false
		scythe:GetData().rng = RNG()
		scythe:GetData().rng:SetSeed(scythe.InitSeed, 35)
	end
	
	local scytheBlade = Isaac.Spawn(kScytheBladeId, 0, 0, player.Position, lib.ZeroVector, player):ToNPC()
	scytheBlade.Parent = scythe
	scytheBlade.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
	scytheBlade:ClearEntityFlags(EntityFlag.FLAG_APPEAR) --Skip spawning animations
	scytheBlade:AddEntityFlags(EntityFlag.FLAG_NO_STATUS_EFFECTS | EntityFlag.FLAG_PERSISTENT)
	scytheBlade.CanShutDoors = false --Its not an enemy
	
	scythe:GetData().samaelScytheBlade = scytheBlade
	
	mod:LoadScytheGraphics(scythe:GetSprite(), scytheBlade:GetSprite(), playerData.samaelScytheType)
	
	scythe:Update()
	scytheBlade:Update()
	
	return scythe
end

-- Update the correct number of visual scythes.
function mod:UpdateNumScythes(player, reloadGraphics)
	local playerData = player:GetData()
	--[[local scythe = playerData.samaelScythe
	
	local num2020 = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_20_20)
	local numInnerEye = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_INNER_EYE)
	local numSpider = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MUTANT_SPIDER)
	local numWiz = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_THE_WIZ)

	local numScythes = 1 + 1*num2020 + 2*numInnerEye + 3*numSpider + 1*numWiz
	
	local maximum = kMaxScythes
	if playerData.samaelScytheType == "cannon" then
		maximum = 5
	end
	
	-- i=0 would be the master scythe here, so we can skip it
	for i=1,maximum-1 do
		if numScythes > i then
			if playerData["childScythe" .. i] == nil or not playerData["childScythe" .. i]:Exists() then
				playerData["childScythe" .. i] = mod:SpawnScythe(player, i)
				playerData["childScythe" .. i].DepthOffset = -i
			end
		elseif playerData["childScythe" .. i] ~= nil and playerData["childScythe" .. i]:Exists() then
			playerData["childScythe" .. i]:Remove()
		end
	end
	
	playerData.numScythes = math.min(numScythes, kMaxScythes)]]
	
	local numScythes = lib.GetNumProjectiles(player, true)
	local maximum = kMaxScythes
	if playerData.samaelScytheType == "cannon" then
		maximum = 5
	end
	numScythes = math.min(numScythes, maximum)
	
	-- i=0 would be the master scythe here, so we can skip it
	for i=1,kMaxScythes-1 do
		if numScythes > i then
			if playerData["childScythe" .. i] == nil or not playerData["childScythe" .. i]:Exists() then
				playerData["childScythe" .. i] = mod:SpawnScythe(player, i)
				playerData["childScythe" .. i].DepthOffset = -i
			end
		elseif playerData["childScythe" .. i] ~= nil and playerData["childScythe" .. i]:Exists() then
			playerData["childScythe" .. i]:Remove()
		end
	end
	
	playerData.numScythes = numScythes
end

-- Determine the type of scythe the player should have (default, brimstone, etc)
function mod:UpdateScytheType(player)
	local playerData = player:GetData()
	local newScytheType = "default"
	
	playerData.samaelScytheShouldColorHandle = false

	if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) then
		newScytheType = "sword"
	elseif not lib.IsTaintedSamael(player) and (player:HasWeaponType(WeaponType.WEAPON_BOMBS)
			or player:HasWeaponType(WeaponType.WEAPON_ROCKETS)
			or (player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) and lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS))) then
		newScytheType = "cannon"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
		newScytheType = "brimstone"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS)
			or (TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.LIL_SLUGGER)) then
		newScytheType = "saw"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_TERRA) then
		newScytheType = "rock"
		playerData.samaelScytheShouldColorHandle = true
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_URANUS) then
		newScytheType = "ice"
		playerData.samaelScytheShouldColorHandle = true
	elseif lib.HasItem(player, Isaac.GetItemIdByName("GMO Corn")) or lib.HasItem(player, Isaac.GetItemIdByName("Horncob")) then
		newScytheType = "corn"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_HEAD_OF_THE_KEEPER)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_GREED)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_MIDAS_TOUCH) then
		newScytheType = "gold"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_2)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_5)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_X)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_ZERO)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_JACOBS_LADDER) then
		newScytheType = "tech"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_SPIDER_BITE)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_SPIDERBABY)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_INFESTATION)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_INFESTATION_2)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_HIVE_MIND)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_BURSTING_SACK)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_PARASITOID)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_SWARM)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_MULLIGAN) then
		newScytheType = "bug"
	end
	
	if newScytheType ~= playerData.samaelScytheType then
		playerData.samaelScytheType = newScytheType
		mod:ReloadAllScytheGraphics(player)
	end
end

-- Loads the correct graphics for the scythe.
function mod:LoadScytheGraphics(scytheSprite, bladeSprite, scytheType)
	local animFile = "gfx/samael_scythe.anm2"
	
	if scytheType == "cannon" then
		animFile = "gfx/samael_scythe_cannon.anm2"
	elseif scytheType ~= "default" and scytheType ~= "gold" then
		animFile = "gfx/samael_scythe_simple.anm2"
	end
	
	if scytheSprite:GetFilename() ~= animFile then
		scytheSprite:Load(animFile, true)
		bladeSprite:Load(animFile, true)
	end
	
	if scytheType == "default" then
		scytheSprite:ReplaceSpritesheet(0, "gfx/samael_scythe/scythe.png")
		bladeSprite:ReplaceSpritesheet(0, "gfx/samael_scythe/scythe_blade.png")
	elseif scytheType == "gold" then
		scytheSprite:ReplaceSpritesheet(0, "gfx/samael_scythe/gold.png")
		bladeSprite:ReplaceSpritesheet(0, "gfx/samael_scythe/gold_blade.png")
	elseif scytheType == "cannon" then
		scytheSprite:ReplaceSpritesheet(0, "gfx/samael_scythe/cannon.png")
		bladeSprite:ReplaceSpritesheet(0, "gfx/samael_scythe/cannon_blade.png")
	else
		local gfx = "gfx/samael_scythe/" .. scytheType .. ".png"
		scytheSprite:ReplaceSpritesheet(1, gfx)
		scytheSprite:ReplaceSpritesheet(2, "gfx/samael_null.png")
		scytheSprite:ReplaceSpritesheet(3, "gfx/samael_null.png")
		bladeSprite:ReplaceSpritesheet(1, "gfx/samael_null.png")
		bladeSprite:ReplaceSpritesheet(2, gfx)
		bladeSprite:ReplaceSpritesheet(3, gfx)
	end
	
	scytheSprite:LoadGraphics()
	bladeSprite:LoadGraphics()
end

-- Reloads the graphics for all scythes currently out (master + all children)
function mod:ReloadAllScytheGraphics(player)
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	local scytheBlade = scythe:GetData().samaelScytheBlade
	
	local scytheType = playerData.samaelScytheType
	
	local scytheSprite = scythe:GetSprite()
	local bladeSprite = scytheBlade:GetSprite()
	
	mod:LoadScytheGraphics(scytheSprite, bladeSprite, scytheType)
	scythe:GetData().samaelScytheType = scytheType
	
	for i=1,kMaxScythes-1 do
		if playerData["childScythe" .. i] and playerData["childScythe" .. i]:Exists() then
			local childScytheSprite = playerData["childScythe" .. i]:GetSprite()
			local childBladeSprite = playerData["childScythe" .. i]:GetData().samaelScytheBlade:GetSprite()
			mod:LoadScytheGraphics(childScytheSprite, childBladeSprite, scytheType)
			playerData["childScythe" .. i]:GetData().samaelScytheType = scytheType
		end
	end
end

-- Helper function to choose the right animation for the scythe.
function mod:AnimateScythe(sprite, scale, anim, play, frame)
	local animPrefix = ""
	if play then
		if sprite:GetAnimation() ~= (animPrefix .. anim) then
			sprite:Play(animPrefix .. anim, true)
		end
	else
		if frame ~= nil then
			sprite:SetFrame(animPrefix .. anim, frame)
		else
			sprite:SetFrame(animPrefix .. anim, 0)
		end
	end
end

function mod:RadialOnSwingEffects(player)
	local playerData = player:GetData()
	
	-- Immaculate Heart
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_IMMACULATE_HEART) then
		local projVel = lib.DirectionalVector(playerData.samaelScytheLastCardinalDirection, player.ShotSpeed * 7)
		local tear = mod:FireSingleProjectile(player, player.Position, projVel, 0, 1.0, false, true)
		tear.Scale = tear.Scale * 0.666
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear:SetColor(lib.NewColor(1, 1, 1, 1, 0.1, 0.1, 0.1), -1, 1, false, false)
		end
		if not lib.HasItem(player, CollectibleType.COLLECTIBLE_TRACTOR_BEAM) then
			lib.AddTearFlag(tear, TearFlags.TEAR_SPECTRAL)
			tear.FallingAcceleration = -0.01
			tear.FallingSpeed = -6.66
			tear:GetData().samaelScytheOrbitStartDir = projVel
		end
	end
	
	--Godhead light ring
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_GODHEAD) then
		local god = player:SpawnMawOfVoid(20):ToLaser()
		god.Radius = 25
		god:GetData().isSamaelLaser = true
		god:GetData().isSamaelGodHead = true
		god:GetData().targetWidth = 6
		god.DepthOffset = -100
		sfxManager:Stop(426)
		sfxManager:Play(SoundEffect.SOUND_REDLIGHTNING_ZAP, 1, 0, false, 1.3)
		god.CollisionDamage = player.Damage*0.2
		god:SetBlackHpDropChance(0)
		local sprite = god:GetSprite()
		sprite:Load("gfx/007.008_light ring.anm2", true)
		sprite:Play("LargeRedLaser", true)
		god:Update()
	end
	
	-- Technology Items
	local laserCount = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_TECHNOLOGY)
			+ player:GetCollectibleNum(CollectibleType.COLLECTIBLE_TECHNOLOGY_2)
			+ player:GetCollectibleNum(CollectibleType.COLLECTIBLE_TECH_5)
			+ player:GetCollectibleNum(CollectibleType.COLLECTIBLE_TECH_X)
	
	if Retribution and Retribution.ITEMS then
		laserCount = laserCount + player:GetCollectibleNum(Retribution.ITEMS.TECHNOLOGY_OMICRON)
	end
	
	for i=0, laserCount-1 do
		local radius = 60
		local x = math.floor(i * 0.5)
		if i % 2 == 0 then
			radius = radius * (0.9^(i+1)) * playerData.samaelScytheScale
		else
			radius = radius * (1.05^(i+1)) * playerData.samaelScytheScale
		end
		
		local laser = mod:AddLaserRing(player, nil, radius)
		
		if playerData.samaelScytheState == 4 then
			laser.ParentOffset = Vector(0, 15)
		end
		
		laser:Update()
	end
	
	if lib.IsTaintedSamael(player) and lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_SORE) then
		local rng = playerData.samaelScythe:GetData().rng
		for i=0, rng:RandomInt(3) do
			local vel = Vector(0, player.ShotSpeed * 10):Rotated(rng:RandomInt(360))
			local tear = player:FireTear(player.Position, vel, false, false, false, player, 1.0):ToTear()
		end
	end
	
	if REPENTANCE then
		if TaintedTreasure then
			if lib.HasItem(player, TaintedCollectibles.RAW_SOYLENT) then
				TaintedTreasure:RawSoylentOnFireClub(player, playerData.samaelScythe)
			end
		end
	end
	
	if FiendFolio and player:HasCollectible(FiendFolio.ITEM.COLLECTIBLE.DEVILS_ABACUS) then
		FiendFolio:devilsAbacusPostFireTear(player, nil, nil, player:GetData())
	end
end

function mod:DirectionalOnSwingEffects(player)
	local playerData = player:GetData()
	local rng = playerData.samaelScythe:GetData().rng
	
	local dir = playerData.samaelScytheLastVectorDirection
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MONSTROS_LUNG) then
		local projVel = dir:Resized(player.ShotSpeed * 7)
		mod:FireProjectileGroup(player, 8, player.Position, projVel, 0.5, true)
	end
		
	-- Lead Pencil Synergy
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_LEAD_PENCIL) then
		playerData.samaelPencilCounter = playerData.samaelPencilCounter + 1
		if playerData.samaelPencilCounter >= 10 then
			local projVel = dir:Resized(player.ShotSpeed * 7)
			mod:FireProjectileGroup(player, 12, player.Position, projVel, 1.0, true)
			playerData.samaelPencilCounter = 0
		end
	end
		
	-- Large zit
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_LARGE_ZIT) and rng:RandomFloat() < 0.2 then
		player:DoZitEffect(dir)
	end
		
	-- Flames
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_GHOST_PEPPER) and rng:RandomFloat() < lib.GetActivationChance(0.0833, player.Luck, 11, true) then
		local projVel = dir:Resized(13)
		local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLUE_FLAME, 0, player.Position, projVel, player):ToEffect()
		flame:SetDamageSource(EntityType.ENTITY_PLAYER)
		flame.LifeSpan = 60
		flame.Timeout = 60
		flame.State = 1
		flame.CollisionDamage = player.Damage * 6
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRDS_EYE) and rng:RandomFloat() < lib.GetActivationChance(0.0833, player.Luck, 11, true) then
		local projVel = dir:Resized(13)
		local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.RED_CANDLE_FLAME, 0, player.Position, projVel, player):ToEffect()
		flame:SetDamageSource(EntityType.ENTITY_PLAYER)
		flame.CollisionDamage = player.Damage * 4
	end
	
	-- Mom's Wig
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_WIG) and rng:RandomFloat() < lib.GetActivationChance(0.1, player.Luck, 10, true) then
		if player:GetNumBlueSpiders() < 5 then
			player:ThrowBlueSpider(player.Position, player.Position + RandomVector() * 50)
		end
	end
	
	if REPENTANCE and FiendFolio then
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_PEPPERMINT) then
			local projVel = Vector(10, 0):Rotated(rng:RandomInt(360))
			FiendFolio:firePeppermint(player, player.Position, projVel)
		end
		if lib.HasItem(player, FiendFolio.ITEM.COLLECTIBLE.BEE_SKIN) then
			local tear = player:FireTear(player.Position, dir, false, true, true)
			tear:ChangeVariant(0)
			for k,v in pairs(tear:GetData()) do tear:GetData()[k] = nil end
			tear:Remove()
		end
		if lib.HasItem(player, FiendFolio.ITEM.COLLECTIBLE.DADS_POSTICHE) then
			FiendFolio.trySpawnPosticheSkuzz(player, dir, rng)
		end
	end
	
	mod:MaybeSpawnEvilEye(player, dir, rng)
end

-- SPINNN
function mod:StartScytheSpin(player)
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	
	playerData.samaelScytheState = 4
	playerData.samaelScytheSpinRot = 0
	playerData.samaelScytheSpinStart = playerData.samaelScytheTargetRot - 40
	playerData.samaelScytheCharge = 0
	scythe.SpriteRotation = playerData.samaelScytheSpinStart
	--mod:AnimateScythe(scythe:GetSprite(), playerData.samaelScytheScale, "Spin", false)
	scythe:GetSprite():Play("Spin", true)
end

-- Update function for the main Scythe entity. Handles most of the Scythe's behaviour.
function mod:MasterScytheUpdate(scythe)
	if scythe.SubType >= 1 then
		if not scythe:GetSprite():IsPlaying("Swing") or scythe:GetSprite():IsFinished("Swing") then
			scythe:GetSprite():Play("Swing", true)
		end
		return nil
	end
	
	local player = scythe.Parent:ToPlayer()
	local playerData = player:GetData()
	
	if not playerData.samaelScythe or playerData.samaelScythe.InitSeed ~= scythe.InitSeed then
		scythe:Remove()
		return
	end
	
	local rng = scythe:GetData().rng

	--if playerData.samaelDying then return end

	local sprite = scythe:GetSprite() --The Scythe's sprite
	local headDirection = player:GetHeadDirection()
	local aimDirection = player:GetAimDirection()
	local isAiming = aimDirection:Length() > 0.2
	local isReleased = not isAiming and not game:IsPaused()
	local projVel = lib.ZeroVector --For storing the proper velocity of a projectile (calculated later)
	local proj = nil --For storing a projectile when fired
	
	local hasSpiritSword = REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD)
	local hasSoyMilk = lib.HasItem(player, CollectibleType.COLLECTIBLE_SOY_MILK) or lib.HasItem(player, CollectibleType.COLLECTIBLE_ALMOND_MILK)
	
	-- Update the size of the scythe.
	local rangeAffectsScytheScale = mod:GetOptions().scytheRangeSize ~= false
	playerData.samaelScytheScale = 1.0
	
	if rangeAffectsScytheScale then
		local range = player.TearRange
		if range >= kBaseRange then
			if range >= 500 and FiendFolio and FiendFolio.HasTrinityWorm(player) then
				range = range - (500 * FiendFolio.GetTrinityWormMultiplier(player))
			end
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_MY_REFLECTION) then
				range = math.max((range - 50) / 1.5, kBaseRange)
			end
			playerData.samaelScytheScale = math.min(lib.Lerp(1.0, kScytheMaxSize, (range - kBaseRange) / (kScytheRangeForMaxSize - kBaseRange)), kScytheMaxSize)
		else
			playerData.samaelScytheScale = math.max(lib.Lerp(kScytheMinSize, 1.0, (range - kScytheRangeForMinSize) / (kBaseRange - kScytheRangeForMinSize)), kScytheMinSize)
		end
	end
	
	local sizeMod = 0
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_PUPULA_DUPLEX) then
		sizeMod = sizeMod + 0.1
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_DEAD_ONION) then
		if rangeAffectsScytheScale then
			sizeMod = sizeMod + 0.25
		else
			sizeMod = sizeMod + 0.2
		end
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TROPICAMIDE) then
		if rangeAffectsScytheScale then
			sizeMod = sizeMod + 0.05
		else
			sizeMod = sizeMod + 0.2
		end
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_POLYPHEMUS) then
		sizeMod = sizeMod + 0.25
	end
	if Retribution and player:HasCollectible(Retribution.ITEMS.SNAKE_OIL) then
		sizeMod = sizeMod + 0.25
	end
	playerData.samaelScytheScale = playerData.samaelScytheScale + sizeMod
	
	if player:IsCoopGhost() then
		playerData.samaelScytheScale = playerData.samaelScytheScale * 0.75
	end
	
	if hasSoyMilk or (not rangeAffectsScytheScale and lib.HasItem(player, CollectibleType.COLLECTIBLE_NUMBER_ONE)) then
		playerData.samaelScytheScale = playerData.samaelScytheScale - 0.25
	end
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) and playerData.samaelScytheMaxCharge then
		local maxChoco = math.min(playerData.samaelScytheScale + 0.5, playerData.samaelScytheScale * 1.5) - playerData.samaelScytheScale
		local charge = math.min((playerData.samaelScytheCharge or 0) / playerData.samaelScytheMaxCharge, 1.0)
		playerData.chocoMilkTargetScale = lib.Lerp(0, maxChoco, charge)
		local n = 0.05
		if playerData.samaelSwingingAutomatically and isAiming then
			n = 0.01
		elseif (playerData.chocoMilkScale or 0) < playerData.chocoMilkTargetScale then
			n = 0.15
		end
		playerData.chocoMilkScale = lib.Lerp(playerData.chocoMilkScale or 0, playerData.chocoMilkTargetScale, n)
		playerData.samaelScytheScale = playerData.samaelScytheScale + playerData.chocoMilkScale
	end
	
	if lib.HasItemEffect(player, CollectibleType.COLLECTIBLE_MEGA_MUSH) then
		playerData.samaelScytheScale = 2
	end
	
	if (playerData.samaelScytheSwingCooldown or 0) > 0 then --Decrement the swingdelay (if it exists)
		playerData.samaelScytheSwingCooldown = playerData.samaelScytheSwingCooldown - 1
	end
	
	-- Neptunus
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_NEPTUNUS) then
		if playerData.samaelScytheState == 0 and playerData.samaelScytheSwingCooldown == 0 and isReleased then
			playerData.neptunusCharge = playerData.neptunusCharge + 2
		else
			playerData.neptunusCharge = playerData.neptunusCharge - 1.5
		end
	else
		playerData.neptunusCharge = 0
	end
	playerData.neptunusCharge = math.min(math.max(0, playerData.neptunusCharge), 100)

	if mod:ShouldHideScythe(player) then
		playerData.samaelScytheSwingCooldown = 4
	end
	
	--READY ATTACK: When the player is holding down a fire direction, and the scythe is not on cooldown
	if not player:IsDead() and isAiming and playerData.samaelScytheSwingCooldown == 0 and playerData.samaelScytheState ~= 4 and not player:IsHoldingItem() then
		if sprite:GetAnimation() ~= "ChargeStart" and sprite:GetAnimation() ~= "Charge"
				and not (playerData.samaelScytheType == "cannon" and (sprite:IsPlaying("Fire") or sprite:IsPlaying("SmallFire"))) then
			--mod:AnimateScythe(sprite, playerData.samaelScytheScale, "Charge", true) -- Hold up the scythe
			sprite:Play("ChargeStart", true)
		end
		
		if sprite:IsFinished("ChargeStart") then
			sprite:Play("Charge", true)
		end
		
		if playerData.samaelScytheState == 2 then --If previous attack was interrupted (due to fast attack rate)
			-- Flip scythe from left to right with each swing.
			if playerData.samaelScytheType ~= "cannon" then
				playerData.samaelScytheFlipped = not playerData.samaelScytheFlipped
			end
			
			mod:DeadEyeHandler(player)
		end

		playerData.samaelScytheState = 1 --Scythe is ready, or charging
		playerData.enemiesHitThisSwing = 0
		
		mod:UpdateScytheTargetRotation(player, true)
	end
	
	-- Decide if the scythe should be swung, or if a charged attack should be used.
	
	local swingAutomatically = false -- If true, the scythe swings without needing to let go.
	local fireAutomatically = false -- If true, charged attacks go off without needing to let go.
	
	-- Item/Weapon checks that set these booleans are done in order of increasing priority.
	-- IE, Technology Lasers can fire automatically without swinging. Soy Milk will override this to
	-- force auto swinging as well, and chocolate milk will override this to force you to release
	-- in order to fire lasers. That is why this block is checked before the others.
	if player:HasWeaponType(WeaponType.WEAPON_LASER) then
		swingAutomatically = false
		fireAutomatically = true
	end
	
	if hasSoyMilk then
		swingAutomatically = true
		fireAutomatically = true
	end
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_ANTI_GRAVITY) then
		fireAutomatically = true
	end
	
	-- Chocolate milk overrides lower priority cases where firing would be automatic, requiring you to
	-- release to fire.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
		fireAutomatically = false
	end
	
	-- Marked overrides everything else even chocolate milk to require automatic firing.
	-- This also indirectly disables partial charging.
	-- Occult Brimstone/Technology is functionally the same as Marked.
	local hasOccultMark = lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_THE_OCCULT)
			and (player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) or player:HasWeaponType(WeaponType.WEAPON_LASER))
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED) or hasOccultMark then
		swingAutomatically = true
		fireAutomatically = true
	end
	
	-- Dr Fetus / Epic Fetus
	if playerData.samaelScytheType == "cannon" then
		swingAutomatically = false
		fireAutomatically = true
	end
	
	-- Some items allow you to fire projectiles even at partial charge.
	local allowPartialCharge = not fireAutomatically and (
			lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK)
			or player:HasWeaponType(WeaponType.WEAPON_KNIFE)
			or player:HasWeaponType(WeaponType.WEAPON_TECH_X))
	
	-- Ludo doesn't really work at all with any of these features.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE)
			and playerData.samaelScytheType ~= "cannon"
			and not hasSpiritSword then
		swingAutomatically = false
		fireAutomatically = false
		allowPartialCharge = false
	end
	
	local disableCharge = false
	if playerData.wraithActive then
		fireAutomatically = false
		swingAutomatically = not lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) or hasSoyMilk
		disableCharge = not lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) and not hasSpiritSword
	elseif lib.IsTaintedSamael(player) then
		fireAutomatically = false
		swingAutomatically = not ((lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) or hasSpiritSword) and not hasSoyMilk)
		disableCharge = not (lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) and not hasSoyMilk) and not hasSpiritSword
	end
	
	-- Charge up the scythe, unless Neptunus is active.
	if playerData.neptunusCharge > 0 then
		swingAutomatically = true
	elseif not disableCharge and not mod:ShouldHideScythe(player)
			and not (player:HasWeaponType(WeaponType.WEAPON_KNIFE) and mod:HasThrownKnifeScythes(player)) and (
			playerData.samaelScytheState == 1 or (
			swingAutomatically and (playerData.samaelScytheState == 2 or playerData.samaelScytheSwingCooldown > 0))) then
		local increase = playerData.wraithActive and 2 or 1
		playerData.samaelScytheCharge = playerData.samaelScytheCharge + increase
		playerData.lastScytheCharge = playerData.samaelScytheCharge
	end
	
	if player:IsDead() then
		playerData.samaelScytheCharge = 0
	end
	
	playerData.samaelSwingingAutomatically = swingAutomatically
	
	-- TRUE if no shooting input is pressed, but the player is forced to attack.
	-- For kidney stone.
	local mightNeedToFireKidneyStone = lib.HasItem(player, CollectibleType.COLLECTIBLE_KIDNEY_STONE)
			and isAiming and not player:AreOpposingShootDirectionsPressed()
			and player:GetShootingInput().X == 0 and player:GetShootingInput().Y == 0
	
	local shouldSwingScythe = playerData.samaelScytheState == 1 and (isReleased or swingAutomatically or mightNeedToFireKidneyStone)
	
	local chargeLevel = math.min(playerData.samaelScytheCharge / playerData.samaelScytheMaxCharge, 1.0)
	local sufficientCharge =
			(playerData.samaelScytheCharge >= playerData.samaelScytheMaxCharge)
			or (allowPartialCharge and (chargeLevel >= 0.25))
	
	playerData.samaelScytheCanFireAutomatically = fireAutomatically
	
	local shouldFireProjectile = sufficientCharge and (isReleased or fireAutomatically or mightNeedToFireKidneyStone)
			and not (player:HasWeaponType(WeaponType.WEAPON_KNIFE) and mod:HasThrownKnifeScythes(player))
	
	if REVEL and REVEL.ITEM.BURNBUSH:PlayerHasCollectible(player) then
		playerData.DisableBurningBush = not (
			(playerData.samaelScytheCharge >= playerData.samaelScytheMaxCharge)
			or (allowPartialCharge and (chargeLevel > 0.5))
			or fireAutomatically
		)
		if player:HasWeaponType(WeaponType.WEAPON_TEARS) then
			shouldFireProjectile = false
		end
	end
	
	-- The Brimstone WeaponType cannot fire projectiles without swinging the scythe, due to the animation.
	if playerData.samaelScytheType ~= "cannon" and player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) and shouldFireProjectile and not shouldSwingScythe then
		shouldFireProjectile = false
	end
	
	-- Tech.5
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_5) and playerData.samaelScytheState == 1
			and ((sufficientCharge and not shouldFireProjectile) or fireAutomatically)
			and scythe.FrameCount % 4 == 0 and rng:RandomFloat() < 0.167 then
		player:FireTechLaser(player.Position, LaserOffset.LASER_TECH5_OFFSET, playerData.samaelScytheLastVectorDirection, false, true, player, chargeLevel)
	end
	
	-- Cannon+Marked autoswing
	if playerData.samaelScytheType == "cannon" and lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED)
			and (sprite:IsPlaying("Fire") or sprite:IsPlaying("SmallFire")) and sprite:GetFrame() >= 6 then
		shouldSwingScythe = true
	end
	
	-- Don't release charge during animations like holding up an item.
	if player:IsDead() or PlayerCantAttackDuringAnim(player) or player:IsHoldingItem() then
		shouldFireProjectile = false
		shouldSwingScythe = false
	end
	
	if lib.IsTaintedSamael(player) then
		if shouldSwingScythe and lib.IsInMineshaft() then
			-- Prevent stupid softlocks in the mines escape sequence.
			shouldFireProjectile = true
		elseif not hasSpiritSword then
			shouldFireProjectile = false
		end
	end
	
	if hasSoyMilk and hasSpiritSword then
		shouldFireProjectile = sufficientCharge and isReleased
	end
	
	-- CHARGED ATTACK EVENT (PROJECTILE) -------------------
	if shouldFireProjectile then
		if hasSpiritSword then
			mod:StartScytheSpin(player)
			sfxManager:Play(538, 0.8, 0, false, 1.0)
			if not lib.IsTaintedSamael(player) then
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
					mod:SpawnBrimstoneHole(player, 1, chargeLevel, Direction.UP)
					mod:SpawnBrimstoneHole(player, 1, chargeLevel, Direction.DOWN)
					mod:SpawnBrimstoneHole(player, 1, chargeLevel, Direction.LEFT)
					mod:SpawnBrimstoneHole(player, 1, chargeLevel, Direction.RIGHT)
				else
					mod:FireProjectiles(player, playerData.samaelScytheLastCardinalDirection, playerData.samaelScytheLastVectorDirection, 1.0)
					playerData.swordFireDelay = player.MaxFireDelay * 2
				end
			end
			shouldSwingScythe = false
		elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE) and playerData.samaelScytheType ~= "cannon" then
			scythe:PlaySound(SoundEffect.SOUND_FETUS_JUMP, 1.75, 0, false, 1.2) --Play swinging sound
			mod:SpawnLudoScythes(player)
			playerData.hideScythe = true
		elseif player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) and playerData.samaelScytheType ~= "cannon" then
			mod:SpawnBrimstoneHole(player, 1, chargeLevel)
		else
			playerData.samaelLastProjectileDirection = playerData.samaelScytheLastVectorDirection
			mod:FireProjectiles(player, playerData.samaelScytheLastCardinalDirection, playerData.samaelScytheLastVectorDirection, chargeLevel)
			
			if playerData.samaelScytheType == "cannon" then
				--mod:AnimateScythe(sprite, playerData.samaelScytheScale, "Fire", true)
				sprite:Play("Fire", true)
			end
		end
	
		playerData.samaelScytheCharge = 0 --Reset charge
	end
	
	local isMementoMoriSwing = false
	
	if playerData.mementoMoriForceScytheSwing then
		shouldSwingScythe = true
		mod:UpdateScytheTargetRotation(player, true)
		playerData.mementoMoriForceScytheSwing = nil
		playerData.samaelScytheDelaySwing = 5
		scythe.SpriteRotation = playerData.samaelScytheTargetRot
		isMementoMoriSwing = true
	end
	
	-- SCYTHE SWING EVENT -------------------
	if shouldSwingScythe then
		mod:RadialOnSwingEffects(player)
		mod:DirectionalOnSwingEffects(player)
		
		-- Spirit sword projectile when at full health.
		if hasSpiritSword and not lib.IsTaintedSamael(player)
				and not lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE)
				and player:HasFullHeartsAndSoulHearts() and playerData.swordFireDelay <= 0 then
			mod:FireProjectiles(player, playerData.samaelScytheLastCardinalDirection, playerData.samaelScytheLastVectorDirection, 1.0)
			playerData.swordFireDelay = player.MaxFireDelay * 3
		end
	
		if mod:ShouldHideScythe(player) then
			playerData.samaelScytheState = 0
		else
			--mod:AnimateScythe(sprite, playerData.samaelScytheScale, "Swing", true)
			sprite:Play("Swing", true)
			if isMementoMoriSwing then
				sprite:SetFrame(1)
			end
			playerData.samaelScytheSwingCooldown = mod:calcSwingDelay(player) --Set new swing delay
			scythe:PlaySound(SoundEffect.SOUND_FETUS_JUMP, 1.75, 0, false, 1.2) --Play swinging sound
			
			if not swingAutomatically then
				playerData.samaelScytheCharge = 0 --Reset charge
			end
				
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
				mod:SpawnBrimstoneHole(player, 0, 0)
			end
			
			playerData.samaelScytheState = 2
		end
		
		-- Epiphora
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIPHORA) and not swingAutomatically and not lib.IsTaintedSamael(player) then
			if playerData.samaelLastEpiphoraDirection == playerData.samaelScytheLastCardinalDirection then
				playerData.samaelEpiphoraCounter = playerData.samaelEpiphoraCounter + 1 --Add to counter
			else
				playerData.samaelEpiphoraCounter = 0
			end
			playerData.samaelLastEpiphoraDirection = playerData.samaelScytheLastCardinalDirection
			if not REPENTANCE then
				player:AddCacheFlags(CacheFlag.CACHE_FIREDELAY)
				player:EvaluateItems()
			end
		elseif playerData.samaelEpiphoraCounter and playerData.samaelEpiphoraCounter > 0 then
			playerData.samaelEpiphoraCounter = 0
			if not REPENTANCE then
				player:AddCacheFlags(CacheFlag.CACHE_FIREDELAY)
				player:EvaluateItems()
			end
		end
	end
	
	--CURRENTLY SWINGING THE SCYTHE
	if playerData.samaelScytheState == 2 then
		if playerData.numScythes >= sprite:GetFrame() + 1 then
			local hitbox = mod:SpawnScytheHitbox(player, scythe, sprite:GetFrame() + 1)
			
			if sprite:GetFrame() == 0 then
				mod:IsaacsTearsFix(player)
				-- Try to behead shopkeepers once per swing.
				local touchedEntities = Isaac.FindInRadius(hitbox.Position, hitbox.Size)
				for _, entity in ipairs(touchedEntities) do
					if entity.Type == EntityType.ENTITY_SHOPKEEPER then
						mod:TryDecapitateShopkeeper(entity, rng)
					elseif entity.Type == EntityType.ENTITY_PICKUP and mod:GetOptions().scythePickup ~= false then
						mod:ScythePickupCollision(player, entity)
					elseif entity.Type == EntityType.ENTITY_BIG_BONY and entity.Variant == 10 then
						entity:TakeDamage(player.Damage, 0, EntityRef(player), 0)
						entity.Velocity = (entity.Position - player.Position):Resized(entity.Velocity:Length())
						sfxManager:Play(SoundEffect.SOUND_BONE_BOUNCE)
					end
				end
				
				if player:HasPlayerForm(PlayerForm.PLAYERFORM_BABY) then
					mod:SpawnLokiScythe(player, 45, 0.5)
					mod:SpawnLokiScythe(player, -45, 0.5)
				end
				
				-- Loki's Horn / Mom's Eye
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_LOKIS_HORNS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_EYE) then
					local triggerChance = lib.GetActivationChance(0.25, player.Luck, 15)
					if lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_EYE) then
						triggerChance = lib.GetActivationChance(0.5, player.Luck, 5)
					end
					
					if rng:RandomFloat() <= triggerChance then
						if lib.HasItem(player, CollectibleType.COLLECTIBLE_LOKIS_HORNS) then
							mod:SpawnLokiScythe(player, 90)
							mod:SpawnLokiScythe(player, 270)
						end
						mod:SpawnLokiScythe(player, 180)
					end
				end
				-- (Loki's Horn / Mom's Eye End)
			end
		end
		
		if sprite:GetAnimation() == "Swing" and playerData.samaelScytheDelaySwing and sprite:IsEventTriggered("SwingEnd") then
			if playerData.samaelScytheDelaySwing <= 0 then
				local frame = sprite:GetFrame()
				sprite:Play("Swing", true)
				sprite:SetFrame(frame)
				playerData.samaelScytheDelaySwing = nil
			else
				sprite:Stop()
				playerData.samaelScytheDelaySwing = playerData.samaelScytheDelaySwing - 1
			end
		elseif sprite:IsFinished("Swing") then
			playerData.samaelScytheState = 0
			-- Flip scythe from left to right with each swing.
			if playerData.samaelScytheType ~= "cannon" then
				playerData.samaelScytheFlipped = not playerData.samaelScytheFlipped
			end
			mod:DeadEyeHandler(player)
		end
		
		-- Mysterious liquid synergy.
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_MYSTERIOUS_LIQUID) then
			local numPuddles = 4
			if playerData.samaelScytheScale >= 1.4 then
				numPuddles = 5
			end
			local arc = 90
			for i=0, numPuddles-1 do
				if i == sprite:GetFrame() then
					local angle = ( (arc/(numPuddles-1))*i ) - (arc/2)
					if playerData.samaelScytheFlipped then
						angle = angle * -1
					end
					local offset = lib.DirectionalVector(playerData.samaelScytheLastCardinalDirection, 55 * playerData.samaelScytheScale):Rotated(angle)
					local pos = scythe.Position:__add(offset)
					local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PLAYER_CREEP_GREEN, 0, pos, lib.ZeroVector, player, 0, 0):ToEffect()
					creep.CollisionDamage = math.min(player.Damage * 0.1, 1)
					creep:SetDamageSource(EntityType.ENTITY_PLAYER)
					sfxManager:Play(258, 0.5, 0, false, 0.5 + i % 0.417)
				end
			end
		end
	end
	
	scythe.RenderZOffset = 0
	
	-- SPIN
	local spinningToWin = player:GetEffects():HasNullEffect(NullItemID.ID_SPIN_TO_WIN)
	if spinningToWin and playerData.samaelScytheState ~= 4 then
		mod:StartScytheSpin(player)
	end
	if playerData.samaelScytheState == 4 then
		if playerData.samaelScytheSpinRot == 0 then
			if not playerData.samaelScytheLastCardinalDirection or playerData.samaelScytheLastCardinalDirection == -1 then
				playerData.samaelScytheLastCardinalDirection = Direction.UP
			end
			mod:RadialOnSwingEffects(player)
		end
		if playerData.samaelScytheSpinRot <= 40*(playerData.numScythes-1) then
			mod:SpawnScytheHitbox(player, scythe, (playerData.samaelScytheSpinRot / 40) + 1, true)
		end
		playerData.samaelScytheTargetRot = playerData.samaelScytheSpinStart + playerData.samaelScytheSpinRot
		playerData.samaelScytheSpinRot = playerData.samaelScytheSpinRot + 40
		playerData.samaelScytheTargetOffset = Vector(0, -10)
		playerData.samaelScytheLastVectorDirection = Vector.FromAngle(playerData.samaelScytheSpinRot)
		mod:DirectionalOnSwingEffects(player)
		
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_MYSTERIOUS_LIQUID) then
			local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PLAYER_CREEP_GREEN, 0,
					player.Position:__add(Vector(60, 0):Rotated(playerData.samaelScytheSpinRot)), lib.ZeroVector, player, 0, 0):ToEffect()
			creep.CollisionDamage = math.min(player.Damage * 0.2, 1)
			creep:SetDamageSource(EntityType.ENTITY_PLAYER)
			sfxManager:Play(258, 0.5, 0, false, 0.5 + 0.4 * (playerData.samaelScytheSpinRot/360))
		end
		
		if playerData.samaelScytheSpinRot >= 360 then
			if spinningToWin then
				playerData.samaelScytheSpinRot = 0
			else
				playerData.samaelScytheState = 0
				scythe.SpriteRotation = scythe.SpriteRotation + 100
				mod:UpdateScytheTargetRotation(player)
				playerData.samaelScytheCurrentOffset = playerData.samaelScytheTargetOffset + Vector(10,0):Rotated(scythe.SpriteRotation-45)
				scythe.Position = player.Position + playerData.samaelScytheCurrentOffset
			end
		end
	elseif not mod:HasThrownKnifeScythes(player) or not mod:ShouldHideScythe(player) then
		mod:UpdateScytheTargetRotation(player)
	end
	
	if playerData.samaelScytheState == 0 then --When nothing else is going on, idle state
		--mod:AnimateScythe(sprite, playerData.samaelScytheScale, "Idle", true)
		if not sprite:IsPlaying("Idle") then
			sprite:Play("Idle", true)
		end
		if isReleased then
			playerData.samaelScytheCharge = 0
			playerData.lastScytheCharge = 0
		end
	end
	
	scythe.Scale = playerData.samaelScytheScale
	
	if playerData.samaelScytheState ~= playerData.samaelScythePrevState then
		playerData.samaelScytheStateFrames = 0
	end
	playerData.samaelScytheStateFrames = (playerData.samaelScytheStateFrames or 0) + 1
	playerData.samaelScythePrevState = playerData.samaelScytheState
end

-- Update function for "child" scythes: Purely visual scythes when holding multiple scythes.
function mod:ChildScytheUpdate(childScythe)
	if childScythe.Variant == 0 then return nil end
	
	local player = childScythe.Parent
	local playerData = player:GetData()
	
	if childScythe.FrameCount > 5 and (not playerData["childScythe" .. childScythe.Variant] or playerData["childScythe" .. childScythe.Variant].InitSeed ~= childScythe.InitSeed) then
		childScythe:Remove()
		return
	end
	
	local masterScythe = playerData.samaelScythe
	
	local sprite = childScythe:GetSprite()
	local parentSprite = masterScythe:GetSprite()
	
	if REPENTANCE then
		sprite:SetFrame(parentSprite:GetAnimation(), parentSprite:GetFrame())
	else
		sprite:SetFrame(lib.FindCurrentScytheAnim(parentSprite), parentSprite:GetFrame())
	end
	
	if playerData.samaelScytheState == 4 then
		sprite.FlipX = parentSprite.FlipX
		sprite.Rotation = parentSprite.Rotation - (360/playerData.numScythes)*childScythe.Variant
	elseif childScythe.Variant % 2 == 0 or playerData.samaelScytheType == "cannon" then
		-- Scythe flipping should match the parent.
		sprite.FlipX = parentSprite.FlipX
		sprite.Rotation = parentSprite.Rotation
	elseif childScythe.Variant % 2 == 1 then
		-- Scythe flipping should be inverse of the parent.
		sprite.FlipX = not parentSprite.FlipX
		sprite.Rotation = parentSprite.Rotation * (-1)
	end
	
	childScythe.Scale = masterScythe.Scale
end

function mod:Flash(entity, color)
	local intensityCap = 0.75
	local r = math.min(color.R, intensityCap)
	local g = math.min(color.G, intensityCap)
	local b = math.min(color.B, intensityCap)
	
	local scale = 0.30
	local a = math.sin(game:GetFrameCount() * 0.9)*scale + scale
	local c = entity.Color
	c:SetOffset(c.RO + r * a, c.GO + g * a, c.BO + b * a)
	entity:SetColor(c, -1, 1, false, false)
end

-- Plays a quick little effect to visualize the scythe breaking/disappearing.
-- Currently only really used as a compatibility patch with IllusionMod.
local function ScytheBreak(scythe)
	local color = scythe.Color
	if scythe:GetData().samaelScytheBlade then
		color = scythe:GetData().samaelScytheBlade.Color
	end

	local eff
	if scythe:GetData().samaelScytheType == "rock" then
		eff = Isaac.Spawn(1000, EffectVariant.ROCK_POOF, 0, scythe.Position, lib.ZeroVector, Isaac.GetPlayer(0)):ToEffect()
		eff.SpriteScale = scythe.SpriteScale * 1.75
		sfxManager:Play(487, 0.75, 0, false, 1.0)
		
		local rng = RNG()
		rng:SetSeed(scythe.InitSeed, 35)
		
		for i=0, 6 do
			local particle = Isaac.Spawn(1000, EffectVariant.TOOTH_PARTICLE, 1, scythe.Position, Vector(rng:RandomFloat()*6 - 3, rng:RandomFloat()*6 - 3), Isaac.GetPlayer(0)):ToEffect()
			particle.Color = lib.NewColor(0.55, 0.45, 0.45) * scythe.Color
		end
	else
		eff = Isaac.Spawn(1000, EffectVariant.SCYTHE_BREAK, 0, scythe.Position, lib.ZeroVector, Isaac.GetPlayer(0)):ToEffect()
		eff.SpriteScale = scythe.SpriteScale
		if scythe:GetData().samaelScytheType == "ice" then
			sfxManager:Play(SoundEffect.SOUND_FREEZE_SHATTER, 0.75, 0, false, 1.0)
		elseif REPENTANCE then
			sfxManager:Play(492, 0.5, 0, false, 0.75)
		else
			sfxManager:Play(138, 0.4, 0, false, 1.2)
		end
	end
	
	eff.Color = color
	eff:GetSprite():SetFrame(1)
	scythe:Remove()
end

local function CalcScythePosOffset(angle)
	local xOffset = -10 * math.sin(math.pi * angle / 180)
	local yOffset = 11 * math.cos(math.pi * angle / 180) - 9
	return Vector(xOffset, yOffset)
end

local HideScytheDuringAnimation = {
	TeleportUp = true,
	TeleportDown = true,
	Trapdoor = true,
	LightTravel = true,
}

local function ShouldHideScytheDueToAnimation(player)
	local anim = player:GetSprite():GetAnimation()
	local data = player:GetData()
	local buffer = data.samaelHideScytheBuffer
	local shouldHide = HideScytheDuringAnimation[anim]
	if (shouldHide or not game:IsPaused()) and anim ~= "Trapdoor" then
		data.samaelHideScytheBuffer = shouldHide
	end
	return buffer or shouldHide
end

local samaelUsedGenesis = false

function mod:ShouldHideScythe(player)
	local pData = player:GetData()
	if samaelUsedGenesis then
		if not game:IsPaused() then
			samaelUsedGenesis = false
		end
		return true
	end
	return pData.hideScythe or ShouldHideScytheDueToAnimation(player) or (
			mod:HasThrownKnifeScythes(player) and player:HasWeaponType(WeaponType.WEAPON_KNIFE) and not lib.IsTaintedSamael(player)
			and not (TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.THE_BOTTLE))
			and not (lib.HasItem(player, CollectibleType.COLLECTIBLE_SOY_MILK) or lib.HasItem(player, CollectibleType.COLLECTIBLE_ALMOND_MILK))
		)
end

if REPENTANCE then
	mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, function(_, _, _, player)
		samaelUsedGenesis = true
	end, CollectibleType.COLLECTIBLE_GENESIS)
end

function mod:HideScytheWhileTeleporting(scythe)
	local player = scythe.Parent:ToPlayer()
	local pData = player:GetData()
	
	if ShouldHideScytheDueToAnimation(player) then
		scythe.Color = lib.InvisibleColor
		scythe.SpriteRotation = 0
		pData.samaelScytheTargetRot = 0
		pData.samaelScytheTargetOffset = CalcScythePosOffset(0)
		pData.samaelScytheLastCardinalDirection = Direction.DOWN
		pData.samaelScytheLastVectorDirection = lib.DirectionalVector(Direction.DOWN)
		
		local blade = scythe:GetData().samaelScytheBlade
		if blade then
			blade.Color = lib.InvisibleColor
		end
		
		mod:UpdateScythePosition(player)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_RENDER, mod.HideScytheWhileTeleporting, kScytheId)

-- Calls the appropriate update function for the master/child scythes.
function mod:ScytheUpdate(scythe)
	if not scythe.Parent or not scythe.Parent:Exists() then
		ScytheBreak(scythe)
		return nil
	end

	local player = scythe.Parent:ToPlayer()
	local playerData = player:GetData()
	
	if not lib.IsSamael(player) and not playerData.wraithActive then
		scythe:Remove()
		return nil
	end

	if scythe.Variant == 0 then
		mod:MasterScytheUpdate(scythe)
	else
		mod:ChildScytheUpdate(scythe)
	end
	
	if mod:ShouldHideScythe(player) or (playerData.mementoMoriActive and not playerData.disjointedMementoMori) then
		scythe.Color = lib.InvisibleColor
	elseif playerData.samaelScytheShouldColorHandle then
		scythe.Color = playerData.samaelScytheColor or lib.NullColor
	else
		scythe.Color = lib.NullColor
	end
	
	local scytheBlade = scythe:GetData().samaelScytheBlade
	if scytheBlade then
		scytheBlade.Scale = scythe.Scale
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ScytheUpdate, kScytheId)

function mod:UpdateScytheTargetRotation(player, force)
	local playerData = player:GetData()
	
	if game:IsPaused() --[[or game:GetRoom():GetFrameCount() <= 5]] then return end
	
	if not lib.IsSamael(player) and not playerData.wraithActive then return end
	if playerData.samaelScytheState ~= 0 and playerData.samaelScytheState ~= 1 and not force then return end
	
	local scythe = playerData.samaelScythe
	
	if not scythe or not scythe:Exists() then
		return nil
	end
	
	if playerData.samaelScytheType == "cannon" then
		playerData.samaelScytheFlipped =
			(not playerData.samaelScytheFlipped and scythe.SpriteRotation > 0 and scythe.SpriteRotation < 180)
			or (playerData.samaelScytheFlipped and scythe.SpriteRotation < 0 and scythe.SpriteRotation > -180)
	end
	
	local sprite = scythe:GetSprite()
	
	local flipped = playerData.samaelScytheFlipped or false
	if sprite.FlipX ~= flipped then
		scythe.SpriteRotation = scythe.SpriteRotation * -1
	end
	sprite.FlipX = flipped
	
	local dir = nil
	
	if player:GetAimDirection():Length() > 0.2 then
		dir = player:GetAimDirection()
	elseif playerData.samaelScytheState == 1 then
		return
	elseif player:GetMovementVector():Length() > 0.2 then
		dir = player:GetMovementVector()
	elseif playerData.samaelScytheState == 0 then
		dir = Vector(0,1)
	end
	
	if playerData.mementoMoriForceScytheSwing then
		dir = playerData.mementoMoriForceScytheSwing
	end
	
	if dir then
		dir = dir:Rotated(-90)
		
		if not lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED)
				and not lib.HasItem(player, CollectibleType.COLLECTIBLE_ANALOG_STICK)
				and not player:HasWeaponType(WeaponType.WEAPON_KNIFE)
				and not playerData.mementoMoriForceScytheSwing then
			dir = lib.DirectionalVector(lib.GetDirectionFromVector(dir))
		end
		
		local currentAngle = dir:GetAngleDegrees()
		local rot = currentAngle
		if flipped then
			rot = rot * -1
		end
		scythe.SpriteRotation = scythe.SpriteRotation % 360
		if math.abs((scythe.SpriteRotation - 360) - rot) < math.abs(scythe.SpriteRotation - rot) then
			scythe.SpriteRotation = scythe.SpriteRotation - 360
		elseif math.abs((scythe.SpriteRotation + 360) - rot) < math.abs(scythe.SpriteRotation - rot) then
			scythe.SpriteRotation = scythe.SpriteRotation + 360
		end
		playerData.samaelScytheTargetRot = rot
		playerData.samaelScytheLastVectorDirection = lib.NormalVector:Rotated(currentAngle)
		playerData.samaelScytheLastCardinalDirection = lib.GetDirectionFromVector(playerData.samaelScytheLastVectorDirection)
		
		playerData.samaelScytheTargetOffset = CalcScythePosOffset(currentAngle)
	end
end

-- Puts the scythes in the correct position.
function mod:UpdateScythePosition(player)
	local playerData = player:GetData()
	
	if not lib.IsSamael(player) and not playerData.wraithActive then return end
	
	local scythe = playerData.samaelScythe
	
	if not scythe or not scythe:Exists() then
		return nil
	end
	
	local blade = scythe:GetData().samaelScytheBlade
	
	local lerpSpeed = 0.25
	if playerData.samaelScytheState > 1 then
		lerpSpeed = 0.5
	end
	
	local currentOffset = playerData.samaelScytheCurrentOffset or lib.ZeroVector
	local targetOffset = playerData.samaelScytheTargetOffset or lib.ZeroVector
	local newOffset = lib.Lerp(currentOffset, targetOffset, lerpSpeed)
	playerData.samaelScytheCurrentOffset = newOffset
	
	local pos = player.Position + newOffset
	local vel = lib.ZeroVector
	
	scythe.Position = pos
	scythe.Velocity = vel
	blade.Position = pos
	blade.Velocity = vel
	
	local rot = 0
	if scythe.FrameCount <= 1 or playerData.samaelScytheState == 4 then
		rot = playerData.samaelScytheTargetRot
	else
		rot = lib.Lerp(scythe.SpriteRotation, playerData.samaelScytheTargetRot or 0, lerpSpeed)
	end
	
	scythe.SpriteRotation = rot
	blade.SpriteRotation = rot
	
	local flipped = playerData.samaelScytheFlipped
	
	-- Child scythes
	for i=1,kMaxScythes-1 do
		local childScythe = playerData["childScythe" .. i]
		if childScythe and childScythe:Exists() then
			local posOffset = lib.DirectionalVector(playerData.samaelScytheLastCardinalDirection, math.floor(childScythe.Variant*0.5)*10):Rotated(180)
			if playerData.samaelScytheType == "cannon" then
				local modifier = 1
				if i % 2 == 0 then
					modifier = -1
				end
				posOffset = lib.DirectionalVector(playerData.samaelScytheLastCardinalDirection, math.ceil(childScythe.Variant*0.5)*10*modifier):Rotated(-90 - 20*modifier)
			end
			for _, entity in pairs({childScythe, childScythe:GetData().samaelScytheBlade}) do
				entity.Position = pos + posOffset
				entity.Velocity = vel
				local sprite = entity:GetSprite()
				if playerData.samaelScytheState == 4 then
					sprite.FlipX = flipped
					sprite.Rotation = rot - (360/playerData.numScythes)*childScythe.Variant
				elseif childScythe.Variant % 2 == 0 or playerData.samaelScytheType == "cannon" then
					-- Scythe flipping should match the parent.
					sprite.FlipX = flipped
					sprite.Rotation = rot
				elseif childScythe.Variant % 2 == 1 then
					-- Scythe flipping should be inverse of the parent.
					sprite.FlipX = not flipped
					sprite.Rotation = rot * (-1)
				end
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.UpdateScythePosition)

-- Update function for the scythe's blades.
function mod:ScytheBladeUpdate(blade)
	if not blade.Parent then
		blade:Remove()
		return nil
	end
	
	local scythe = blade.Parent:ToNPC()
	
	if not scythe.Parent or not scythe.Parent:Exists() then
		blade:Remove()
		return nil
	end
	
	local player = scythe.Parent:ToPlayer()
	local playerData = player:GetData()
	
	local scytheColor = player.TearColor
	local flashColor = player.TearColor
	
	local tearColorIsDefault = lib.SameColor(player.TearColor, lib.SamaelTearColor)
	
	if FiendFolio and lib.HasItem(player, FiendFolio.ITEM.COLLECTIBLE.TIME_ITSELF) then
		-- Multi-euclidean colorization code taken from FiendFolio/Erfly
		local s = math.sin(game:GetFrameCount()/20*math.pi)
		local multiEuclidColor = Color(-s*2, -s*2, -s*2, 1, (s+1)/2, (s+1)/2, (s+1)/2)
		multiEuclidColor:SetColorize(1, 1, 1, 1)
		scytheColor = multiEuclidColor
	elseif playerData.samaelScytheType == "gold" or playerData.samaelScytheType == "corn" then
		scytheColor = lib.NullColor
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE) then
		local speed = 25
		local i = (game:GetFrameCount() % speed) / speed
		local freq = 2 * math.pi
		local r = (math.sin(freq*i + 0) * 127 + 128) / 255
		local g = (math.sin(freq*i + 2) * 127 + 128) / 255
		local b = (math.sin(freq*i + 4) * 127 + 128) / 255

		if playerData.samaelScytheType == "brimstone" or playerData.samaelScytheType == "tech" then
			scytheColor = lib.NewColor(1,1,1,1,r*0.25,g*0.7,b*1.0)
		else
			scytheColor = lib.NewColor(r,g,b)
		end
		flashColor = scytheColor
	elseif playerData.samaelScytheType == "brimstone" or playerData.samaelScytheType == "tech" then
		scytheColor = player.LaserColor
		if tearColorIsDefault then
			flashColor = lib.NewColor(0.75, 0.1, 0.1)
		end
	elseif tearColorIsDefault then
		if (lib.HasItem(player, CollectibleType.COLLECTIBLE_BLOOD_CLOT) or lib.HasItem(player, CollectibleType.COLLECTIBLE_CHEMICAL_PEEL)) and playerData.samaelScytheFlipped then
			scytheColor = lib.BloodColor
		elseif REPENTANCE and FiendFolio and lib.HasItem(player, FiendFolio.ITEM.COLLECTIBLE.IMP_SODA) then
			scytheColor = Color(1.3, 1.3, 1.3, 1, 80/255, -120/255, 80/255)
		else
			scytheColor = lib.NullColor
		end
	end
	
	playerData.samaelScytheColor = scytheColor
	
	if mod:ShouldHideScythe(player) or (playerData.mementoMoriActive and not playerData.disjointedMementoMori) then
		blade.Color = lib.InvisibleColor
	else
		blade.Color = scytheColor
	end
	
	if lib.IsSamael(player) and playerData.samaelScytheCharge and playerData.samaelScytheCharge >= playerData.samaelScytheMaxCharge then
		mod:Flash(blade, flashColor)
		mod:Flash(scythe, flashColor)
	end
	
	local bladeSprite = blade:GetSprite()
	local handleSprite = scythe:GetSprite()
	bladeSprite.FlipX = handleSprite.FlipX
	bladeSprite.Rotation = handleSprite.Rotation
	
	if REPENTANCE then
		bladeSprite:SetFrame(handleSprite:GetAnimation(), handleSprite:GetFrame())
	else
		local anim = lib.FindCurrentScytheAnim(handleSprite)
		if anim then
			bladeSprite:SetFrame(anim, handleSprite:GetFrame())
		end
	end
	
	--blade.RenderZOffset = scythe.RenderZOffset + 1
	blade.DepthOffset = scythe.DepthOffset + 1
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ScytheBladeUpdate, kScytheBladeId)

-- Disable scythe reflections.
function mod:ScytheRender(scythe)
	local renderMode = game:GetRoom():GetRenderMode()
	
	if renderMode == RenderMode.RENDER_WATER_ABOVE then
		scythe.Visible = false
	else
		scythe.Visible = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_RENDER, mod.ScytheRender, kScytheId)
mod:AddCallback(ModCallbacks.MC_POST_NPC_RENDER, mod.ScytheRender, kScytheBladeId)

-- Calculate # of frames needed to charge up a projectile
function mod:calcChargeTime(player)
	local chargeTime = kScytheChargeTimeMax
	-- I'll be honest looking back at this now this seems overly complicated but the charge times
	-- generally seem to be fine so whatever I'll leave it like this.
	--"Calculate charge time (Parabola! Because why not)" - Original comment from 2017
	if player.MaxFireDelay < kScytheChargeTimeMax then
		local x = player.MaxFireDelay
		local min = kScytheChargeTimeMin
		local mid = kScytheChargeTimeMid
		local max = kScytheChargeTimeMax
		--Using formulas to fit a parabola to three points
		local a = (mid*max - min*(max-10) - max*10)/((-1)*max*max*10+100*max)
		local b = (mid-min-a*(100))/10
		--local c = min
		chargeTime = math.floor(a*x*x + b*x + min) -- y = ax^2 + bx + c
	end
	
	if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) then
		if not lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
			chargeTime = math.ceil(chargeTime * 0.5)
		end
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE)
			and player:GetData().samaelScytheType ~= "cannon" then
		chargeTime = math.max(chargeTime, kScytheChargeTimeMid*0.5)
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK)
			or player:HasWeaponType(WeaponType.WEAPON_TECH_X) then
		chargeTime = chargeTime * 2
	end
	
	--[[if player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) or player:HasWeaponType(WeaponType.WEAPON_BOMBS) or player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) then
		chargeTime = chargeTime * 2
	end]]
	
	if player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) then
		chargeTime = chargeTime * 2.5
	elseif player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) then
		chargeTime = chargeTime * 2
	elseif player:HasWeaponType(WeaponType.WEAPON_BOMBS) then
		chargeTime = chargeTime * 1.5
	end
	
	return chargeTime
end

-- Calculate delay between scythe swings
function mod:calcSwingDelay(player)
	if REPENTANCE and (player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) 
			or player:GetEffects():HasCollectibleEffect(CollectibleType.COLLECTIBLE_BERSERK)) then
		return kScytheMinSwingDelay
	end

	local playerData = player:GetData()
	local delay = player.MaxFireDelay
	
	if delay > kScytheMaxSwingDelay then
		delay = kScytheMaxSwingDelay
	end
	
	if delay > kScytheMinSwingDelay then
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_NEPTUNUS) and playerData.neptunusCharge > 0 then
			delay = delay * 0.5
		end
		if playerData.wraithActive and playerData.wraithCooldown == 0 then
			delay = lib.Lerp(delay, kScytheMinSwingDelay, kWraithModeFireDelayMult)
		end
	end
	
	return math.max(math.floor(delay), kScytheMinSwingDelay)
end

--------------------------------------------------------------------------------------------------
---- FORCE SAMAEL'S HEAD TO STAY FACING THE DIRECTION OF A SCYTHE SWING, FINALLY
--------------------------------------------------------------------------------------------------

local InputDirections = {}
InputDirections[ButtonAction.ACTION_SHOOTLEFT] = Direction.LEFT
InputDirections[ButtonAction.ACTION_SHOOTUP] = Direction.UP
InputDirections[ButtonAction.ACTION_SHOOTRIGHT] = Direction.RIGHT
InputDirections[ButtonAction.ACTION_SHOOTDOWN] = Direction.DOWN

function mod:ForceScytheHeadDirection(player, inputHook, buttonAction)
	if not InputDirections[buttonAction] or not player or not player:ToPlayer() then return end
	player = player:ToPlayer()
	local data = player:GetData()
	if not data.samaelScythe or not data.samaelScythe:Exists() or lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED) then return end
	
	local currentValue = Input.GetActionValue(buttonAction, player.ControllerIndex)
	
	if (data.samaelScytheState == 1 or data.samaelScytheState == 2) and currentValue <= 0.1 then
		local returnVal
		
		if InputDirections[buttonAction] == data.samaelScytheLastCardinalDirection then
			returnVal = 0.01
		else
			returnVal = 0.0
		end
		
		if inputHook == InputHook.IS_ACTION_PRESSED then
			return returnVal > 0
		end
		return returnVal
	end
end
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION, mod.ForceScytheHeadDirection, InputHook.IS_ACTION_PRESSED)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION, mod.ForceScytheHeadDirection, InputHook.GET_ACTION_VALUE)

--------------------------------------------------------------------------------------------------
---- PICKING UP STUFF WITH THE SCYTHE????
--------------------------------------------------------------------------------------------------

local ScythePickupCollisionBlacklist = {
	[PickupVariant.PICKUP_COLLECTIBLE] = true,
	[PickupVariant.PICKUP_SHOPITEM] = true,
	[PickupVariant.PICKUP_TROPHY] = true,
	[PickupVariant.PICKUP_BIGCHEST] = true,
	[PickupVariant.PICKUP_BED] = true,
	[PickupVariant.PICKUP_MEGACHEST] = true,
	[PickupVariant.PICKUP_THROWABLEBOMB] = true,
}

local function ScythePickupPush(player, pickup)
	if not pickup.Touched and pickup:GetSprite():GetAnimation() ~= "Collect" then
		pickup.Velocity = (pickup.Position - player.Position):Resized(5)
	end
end

local function ScythePickupSetHidden(pickup, hidden)
	if pickup.Variant == PickupVariant.PICKUP_TRINKET then
		pickup.Visible = not hidden
	else
		pickup:GetSprite().Color = hidden and lib.InvisibleColor or lib.NullColor
	end
end

local ScythePickups = {}

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	local toRemove = {}
	for hash, tab in pairs(ScythePickups) do
		tab.Countdown = tab.Countdown - 1
		if tab.Countdown <= 0 then
			table.insert(toRemove, hash)
		end
	end
	for _, hash in pairs(toRemove) do
		ScythePickups[hash] = nil
	end
end)

function mod:ScythePickupCollision(player, pickup)
	pickup = pickup:ToPickup()
	
	if not player or player.EntityCollisionClass == EntityCollisionClass.ENTCOLL_NONE
			or player.EntityCollisionClass == EntityCollisionClass.ENTCOLL_PLAYERONLY
			or not pickup or ScythePickupCollisionBlacklist[pickup.Variant] or pickup.Price ~= 0
			or (pickup:GetData().samaelPickupCooldown or 0) > 0 then
		return
	end
	
	if lib.IsVanillaChest(pickup) and pickup.SubType == ChestSubType.CHEST_OPENED then
		ScythePickupPush(player, pickup)
		return
	end
	
	-- TryOpenChest doesn't trigger challenge rooms and such.
	-- It also opens haunted chests without spawning the polty for some reason.
	--[[if mod:IsChest(pickup) then
		if ScytheOpenableChest[pickup.Variant] then
			pickup:TryOpenChest(player)
		else
			ScythePickupPush(player, pickup)
		end
		return
	end]]
	
	ScythePickups[GetPtrHash(pickup)] = {
		PickupPos = Vector(pickup.Position.X, pickup.Position.Y),
		PlayerPos = Vector(player.Position.X, player.Position.Y),
		Countdown = 2,
		Variant = pickup.Variant,
	}
	
	-- Mimic chests act weird, so forcibly convert them to spiked chests if they havent triggered yet.
	if pickup.Variant == PickupVariant.PICKUP_MIMICCHEST then
		pickup:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_SPIKEDCHEST, 0, false, true, false)
		pickup:GetSprite():SetLastFrame()
	end
	
	pickup:GetData().samaelPickupActualPos = pickup.Position
	pickup:GetData().samaelPickupCountDown = 2
	pickup:GetData().samaelPickupPlayer = player
	pickup.Position = player.Position
	ScythePickupSetHidden(pickup, true)
end

-- Extra protection to try to avoid haunted chests hurting Samael
function mod:SamaelHauntedChest(player, damage, damageFlags, damageSourceRef)
	if damageSourceRef.Type == EntityType.ENTITY_POLTY then
		for _, tab in pairs(ScythePickups) do
			if tab.Countdown > 0 and tab.Variant == PickupVariant.PICKUP_HAUNTEDCHEST then
				return false
			end
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.SamaelHauntedChest, EntityType.ENTITY_PLAYER)

function mod:ScythePickup(pickup)
	local data = pickup:GetData()
	
	if (data.samaelPickupCooldown or 0) > 0 then
		data.samaelPickupCooldown = data.samaelPickupCooldown - 1
	end
	
	if data.samaelPickupActualPos and (data.samaelPickupCountDown or 0) > 0 then
		data.samaelPickupCountDown = data.samaelPickupCountDown - 1
		if data.samaelPickupFixedPos then
			data.samaelPickupFixedPos = data.samaelPickupActualPos
		end
		if data.samaelPickupCountDown <= 0 then
			local pos = data.samaelPickupActualPos
			pickup.Position = pos
			pickup.TargetPosition = pos
			pickup:GetData().Position = pos
			
			ScythePickupSetHidden(pickup, false)
			
			ScythePickupPush(data.samaelPickupPlayer or Isaac.GetPlayer(0), pickup)
			
			data.samaelPickupActualPos = nil
			data.samaelPickupCountDown = nil
			data.samaelPickupPlayer = nil
			
			data.samaelPickupCooldown = 10
		else
			ScythePickupSetHidden(pickup, true)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.ScythePickup)

function mod:ScythePickupRender(pickup)
	local data = pickup:GetData()
	
	if data.samaelPickupActualPos and (data.samaelPickupCountDown or 0) > 0 then
		ScythePickupSetHidden(pickup, false)
		pickup:GetSprite():Render(Isaac.WorldToScreen(data.samaelPickupActualPos), lib.ZeroVector, lib.ZeroVector)
		ScythePickupSetHidden(pickup, true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_RENDER, mod.ScythePickupRender)

-- Makes sure Samael can't get shoved around by, say, a chest morphing into an item pedestal on top of them.
mod:AddCallback(ModCallbacks.MC_PRE_PLAYER_COLLISION, function(_, player, collider)
	if collider and collider:GetData().samaelPickupActualPos and (collider:GetData().samaelPickupCountDown or 0) > 0 then
		if player.Velocity:Length() < 0.01 then
			local currentPos = Vector(player.Position.X, player.Position.Y)
			lib.ScheduleForUpdate(function()
				player.Position = currentPos
				player.Velocity = lib.ZeroVector
			end)
		else
			local currentVel = Vector(player.Velocity.X, player.Velocity.Y)
			lib.ScheduleForUpdate(function()
				player.Velocity = currentVel
			end)
		end
	end
end)

-- Keeps items dropped by stuff like grab bags in the correct locations.
function mod:ScythePickupInit(pickup)
	if pickup.Type == EntityType.ENTITY_EFFECT and pickup.Variant == 1748 then return end
	
	if ScythePickups[GetPtrHash(pickup)] then
		-- Likely morphed pickup.
		local pos = ScythePickups[GetPtrHash(pickup)].PickupPos
		pickup.Position = pos
		pickup.TargetPosition = pos
		lib.ScheduleForUpdate(function()
			pickup.Position = pos
			pickup.TargetPosition = pos
		end)
		return
	end
	
	if pickup.SpawnerType == EntityType.ENTITY_PICKUP and pickup.SpawnerEntity and pickup.SpawnerEntity:GetData().samaelPickupActualPos then
		pickup.Position = pickup.SpawnerEntity:GetData().samaelPickupActualPos
		return
	end
	
	local foundPickupPos
	local foundPickupDist
	
	for _, tab in pairs(ScythePickups) do
		local dist = pickup.Position:Distance(tab.PlayerPos)
		if (not foundPickupDist or dist < foundPickupDist) and dist <= 5 then
			foundPickupPos = tab.PickupPos
			foundPickupDist = dist
		end
	end
	
	if foundPickupPos then
		pickup.Position = foundPickupPos
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.ScythePickupInit)
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, mod.ScythePickupInit)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.ScythePickupInit)
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.ScythePickupInit)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.ScythePickupInit, BombVariant.BOMB_TROLL)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.ScythePickupInit, BombVariant.BOMB_SUPERTROLL)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.ScythePickupInit, BombVariant.BOMB_GOLDENTROLL)

--------------------------------------------------
---- SCYTHE HITBOX
--------------------------------------------------

-- Calculate the correct position for an active hitbox.
function mod:GetHitboxPosOffset(player, dir)
	local playerData = player:GetData()
	
	if playerData.samaelScytheState == 4 then
		return lib.ZeroVector
	end
	
	dir = dir or playerData.samaelScytheLastVectorDirection
	local currentAngle = dir:Rotated(-90):GetAngleDegrees()
	local xOffset = -40 * math.sin(math.pi * currentAngle / 180) * playerData.samaelScytheScale
	local yOffset = 35 * math.cos(math.pi * currentAngle / 180) * playerData.samaelScytheScale
	return Vector(xOffset, yOffset)
end

-- Spawn a hitbox.
function mod:SpawnScytheHitbox(player, scythe, index, isSpinAttack)
	local playerData = player:GetData()
	
	if index and playerData["scytheHitbox" .. index] ~= nil then
		playerData["scytheHitbox" .. index]:Remove()
	end
	
	local hitbox = Isaac.Spawn(EntityType.ENTITY_KNIFE, 10, kScytheHitboxType, player.Position, lib.ZeroVector, player):ToKnife() --Spawn the hitbox
	hitbox:ClearEntityFlags(EntityFlag.FLAG_APPEAR) --Skip appear animations
	hitbox.SpawnerEntity = player
	hitbox.Parent = scythe
	hitbox.Target = hitbox -- :^)
	hitbox.Size = 42*playerData.samaelScytheScale
	--hitbox:SetColor(kUniqueKeyColor, -1, 1, false, false)
	hitbox.TearFlags = player.TearFlags

	hitbox:GetData().posOffset = mod:GetHitboxPosOffset(player)
	hitbox.Position = player.Position + hitbox:GetData().posOffset
	hitbox.Velocity = player.Velocity
	
	local damage = player.Damage * kScytheMeleeDamageMult
	
	if playerData.wraithActive then
		damage = damage * kWraithModeScytheDamageMult
	end
	
	if isSpinAttack then
		hitbox.Size = 75 * playerData.samaelScytheScale
		hitbox:GetData().isSpinAttack = true
	end
	
	if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) then
		if isSpinAttack then
			damage = (damage * 3) + 5
		else
			damage = (damage * 1.5) + 1.75
		end
	end
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) and not playerData.samaelSwingingAutomatically then
		damage = damage * 0.5 + damage * 3 * math.min((playerData.lastScytheCharge / playerData.samaelScytheMaxCharge), 1)
	end
	
	if playerData.samaelScytheType == "cannon" or lib.HasItem(player, CollectibleType.COLLECTIBLE_PROPTOSIS) then
		damage = damage * 1.5
	end
	
	local bloodClots = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BLOOD_CLOT)
	local chemPeels = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_CHEMICAL_PEEL)
	if (bloodClots > 0 or chemPeels > 0) and playerData.samaelScytheFlipped then
		damage = damage + bloodClots + chemPeels * 2
	end
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC) and player:HasWeaponType(WeaponType.WEAPON_TEARS) then
		damage = damage * 0.5
	end
	
	hitbox.CollisionDamage = damage
	
	if index then
		playerData["scytheHitbox" .. index] = hitbox
	end
	
	if Retribution then
		Retribution.CallCustomCallbacks(Retribution.Callback.ANY_WEAPON_FIRE, nil, player)
		Retribution.CallCustomCallbacks(Retribution.Callback.PRE_SWING_BONE_CLUB, nil, hitbox, player)
	end
	
	return hitbox
end

-- Callback function for the Scythe's hitbox (Its an invisible Knife entity)
function mod:ScytheHitboxUpdate(hitbox)
	-- Reset Variant to 0 (default Knife).
	-- Variant is changed to 10 (Spirit Sword) during entity collisions only.
	hitbox.Variant = 0

	local room = game:GetRoom()
	local player = hitbox.SpawnerEntity:ToPlayer()
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	hitbox.Parent = scythe
	local data = hitbox:GetData()
	
	local isSpinAttack = hitbox:GetData().isSpinAttack
	
	local parentIsGone = not scythe or not scythe:Exists() or not player or not player:Exists()
			--or not lib.IsSamael(player) or playerData.samaelDying
	local spinAttackOver = isSpinAttack and playerData.samaelScytheState ~= 4
	local swingOver = not isSpinAttack and scythe and (scythe:GetSprite():GetFrame() == 0
			or hitbox.FrameCount > 6 or scythe:GetSprite():GetFrame() > 6 or playerData.samaelScytheState ~= 2)
			--or playerData.samaelScytheLastCardinalDirection == Direction.NO_DIRECTION)

	if parentIsGone or spinAttackOver or swingOver then
		hitbox:Remove()
		return
	end
	
	if not hitbox:GetData().isMultiEuclidean then
		hitbox.Position = player.Position + (hitbox:GetData().posOffset or lib.ZeroVector)
		hitbox.Velocity = player.Velocity
	end
	
	if data.TaintedTechLaser then
		local laser = data.TaintedTechLaser
		laser.MaxDistance = hitbox.Size * 2
		laser.Angle = laser.Angle + (Random() % 120 - 60)
		laser.CollisionDamage = hitbox.CollisionDamage * 0.5
	end
	
	for _, gridEntity in pairs(lib.FindGridEntitiesInRadius(hitbox.Position, hitbox.Size)) do
		local gridIndex = gridEntity:GetGridIndex()
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_SULFURIC_ACID) or lib.HasItem(player, CollectibleType.COLLECTIBLE_TERRA)
				or lib.HasTearFlag(hitbox, TearFlags.TEAR_ACID) or lib.HasTearFlag(hitbox, TearFlags.TEAR_ROCK) then
			room:DamageGrid(gridIndex, 100, EntityRef(player))
			room:DestroyGrid(gridIndex, 0, nil, EntityRef(player))
		else
			room:DamageGrid(gridIndex, 1, EntityRef(player))
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.ScytheHitboxUpdate, kScytheHitboxType)

-- Damage callback for any hits that should charge the wraith meter.
function mod:ChargeUpWraith(tookDamage, damage, damageFlags, damageSourceRef)
	if damageSourceRef and damageSourceRef.Entity then
		local player = lib.FindPlayerSpawnerOrParent(damageSourceRef.Entity)
		
		if player then
			tookDamage:GetData().samaelLastPlayerDamageSource = EntityPtr(player)
		end
		
		if not player or not lib.HasItem(player, kSamaelPocketActive) or not tookDamage:IsVulnerableEnemy() then
			return
		end
		
		-- AB+ EntityRefs give incomplete info, so we'll need to find the full player entity.
		if not REPENTANCE then
			for _, p in pairs(lib.GetPlayers()) do
				if player.ControllerIndex == p.ControllerIndex then
					player = p
				end
			end
		end
		
		local playerData = player:GetData()
		
		if not playerData.wraithActive and not ((playerData.wraithCooldown or 0) > 0) then
			AddWraithCharge(player, damage)
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, mod.ChargeUpWraith)

local function AllowScytheHit(tookDamage, damageSourceRef)
	if not damageSourceRef or not damageSourceRef.Entity or not tookDamage:IsVulnerableEnemy() then
		return false
	end
	if damageSourceRef.Entity.SubType == kLudoScytheType then
		return false
	end
	if damageSourceRef.Entity.Type == EntityType.ENTITY_KNIFE and damageSourceRef.Entity.SubType == kScytheHitboxType then
		return true
	end
end

function mod:PreScytheHits(tookDamage, damage, damageFlags, damageSourceRef)
	if AllowScytheHit(tookDamage, damageSourceRef) then
		local hitbox
		if REPENTANCE then
			hitbox = damageSourceRef.Entity:ToKnife()
		else -- AB+ EntityRefs point to an incomplete entity but I can cheat by storing a proper ref in Target
			hitbox = damageSourceRef.Entity.Target:ToKnife()
		end
		local player = hitbox.SpawnerEntity:ToPlayer()
		local playerData = player:GetData()
		if not player:HasWeaponType(WeaponType.WEAPON_KNIFE) and playerData.samaelScytheType ~= "cannon"
				and playerData.samaelScytheType ~= "saw" and hitbox:GetData()[tookDamage.InitSeed] then
			-- This hitbox already hit this enemy and multihit isn't allowed.
			return false
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.PreScytheHits)

-- Damage callback for scythe melee hits.
function mod:ScytheHits(tookDamage, damage, damageFlags, damageSourceRef)
	if AllowScytheHit(tookDamage, damageSourceRef) then
		local hitbox
		if REPENTANCE then
			hitbox = damageSourceRef.Entity:ToKnife()
		else -- AB+ EntityRefs point to an incomplete entity but I can cheat by storing a proper ref in Target
			hitbox = damageSourceRef.Entity.Target:ToKnife()
		end
		local hitboxData = hitbox:GetData()
		
		local player = hitbox.SpawnerEntity:ToPlayer()
		local playerData = player:GetData()
		local scythe = playerData.samaelScythe
		local rng = scythe:GetData().rng
		tookDamage = tookDamage:ToNPC()
		
		hitbox.Parent = player
		
		hitboxData[tookDamage.InitSeed] = true
		
		-- Spawn rock shockwaves with terra.
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TERRA) and playerData.enemiesHitThisSwing == 0 then
			Isaac.Spawn(1000, 73, 1, hitbox.Position, lib.ZeroVector, player)
		end
		
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_CURSED_EYE) and rng:RandomInt(5) == 0 then
			lib.TeleportEnemy(player, tookDamage)
		end
		
		playerData.enemiesHitThisSwing = playerData.enemiesHitThisSwing + 1
		
		-- Apply hit sound and blood splatter.
		local blood = Isaac.Spawn(1000, 2, 0, lib.Lerp(hitbox.Position, tookDamage.Position, 0.75), lib.ZeroVector, tookDamage) --Blood effect
		blood:GetSprite().Color = tookDamage.SplatColor
		tookDamage:PlaySound(SoundEffect.SOUND_MEATY_DEATHS, 0.75, 0, false, 1.8) --Play hit sound
		
		-- HACK ALERT: Normally the HitBox is a standard Knife entity, Variant 0. Right before a hit is
		-- applied change it to Variant 10 (Spirit Sword) to use the Spirit Sword's on-hit interactions.
		-- While set to this Variant it doesn't seem to be capable of hitting enemies, (probably due to
		-- something in the Spirit Sword's code that only allows it to hit enemies during proper swings)
		-- so we'll change it back to 0 during the next standard Update.
		hitbox.Variant = 10
		
		-- Get the TearFlags and TearColor for as if Isaac had fired a tear. This allows us to get
		-- random effects like fruit cake.
		-- Fun fact, before I realized that this was what TearParams were for, I used to call FireTear,
		-- grab its TearFlags, and then immediately delete it.
		local tearParams = player:GetTearHitParams(WeaponType.WEAPON_TEARS, 1, 1, nil)
		
		-- Apply TearFlags. Also store the TearColor as it is sometimes useful.
		hitbox.TearFlags = tearParams.TearFlags
		hitbox.Color = tearParams.TearColor
		
		-- TEAR_GLITTER_BOMB flag has no effect for tears and knives, so I use it to help keep track of
		-- if a Tear was spawned directly via Samael's Scythe (parasite, compound fracture, etc).
		lib.AddTearFlag(hitbox, TearFlags.TEAR_GLITTER_BOMB)
		
		-- Serpents Kiss: Chance to drop black hearts when killing poisoned enemies with the scythe.
		if (lib.HasTearFlag(hitbox, TearFlags.TEAR_POISON) or tookDamage:HasEntityFlags(EntityFlag.FLAG_POISON))
				and lib.HasItem(player, CollectibleType.COLLECTIBLE_SERPENTS_KISS) then
			lib.AddTearFlag(hitbox, TearFlags.TEAR_BLACK_HP_DROP)
		end
		
		-- Eye of Greed / Midas Touch
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_GREED)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_MIDAS_TOUCH) then
			local chance = 0.1 * player:GetCollectibleNum(CollectibleType.COLLECTIBLE_EYE_OF_GREED)
					+ 0.05 * player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MIDAS_TOUCH)
			if rng:RandomFloat() <= chance then
				lib.AddTearFlag(hitbox, TearFlags.TEAR_MIDAS)
			end
		end
		
		-- Bleed
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_BACKSTABBER) then
			local chance = lib.GetCappedActivationChance(0.1, 1, player.Luck, 20, true)
			if rng:RandomFloat() <= chance then
				tookDamage:AddEntityFlags(EntityFlag.FLAG_BLEED_OUT)
			end
		end
		
		-- Tech Zero
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_ZERO) and not lib.IsTaintedSamael(player) then
			lib.AddTearFlag(hitbox, TearFlags.TEAR_JACOBS)
		end
		
		-- Disable the TEAR_KNOCKBACK flag and apply a bonus to the knockback manually if it was there.
		local knockbackBonus = 1
		if lib.HasTearFlag(hitbox, TearFlags.TEAR_KNOCKBACK) then knockbackBonus = 2 end
		lib.RemoveTearFlag(hitbox, TearFlags.TEAR_KNOCKBACK)
		
		if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) then
			knockbackBonus = knockbackBonus + 1
			if hitboxData.isSpinAttack then
				knockbackBonus = knockbackBonus + 1
			end
		end
		
		-- Apply knockback to enemy.
		local knockbackForce = tookDamage.Position:__sub(player.Position):Resized(kScytheKnockbackStrength*knockbackBonus)	--:Normalized() * (kScytheKnockbackStrength*knockbackBonus)
		tookDamage.Velocity = lib.Lerp(tookDamage.Velocity, knockbackForce, 0.75)
		
		-- Apply knockback to player.
		if REPENTANCE and hitboxData.isSpinAttack then
			player.Velocity = lib.Lerp(player.Velocity, knockbackForce * -1, 0.5)
		end
		
		-- Haemoclaria
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_HAEMOLACRIA) or lib.HasTearFlag(hitbox, TearFlags.TEAR_BURSTSPLIT) then
			lib.RemoveTearFlag(hitbox, TearFlags.TEAR_BURSTSPLIT)
			local pos = tookDamage.Position + (player.Position - tookDamage.Position):Resized(tookDamage.Size + 10)
			local tear = player:FireTear(pos, lib.ZeroVector, false, false, false)
			lib.RemoveTearFlag(tear, TearFlags.TEAR_QUADSPLIT)
			lib.RemoveTearFlag(tear, TearFlags.TEAR_SPLIT)
			lib.RemoveTearFlag(tear, TearFlags.TEAR_BONE)
			lib.RemoveTearFlag(tear, TearFlags.TEAR_EXPLOSIVE)
			tear.Height = 0
			tear:Die()
		end
		
		-- Cricket's Body
		local shouldFireSplitTears = false
		if REPENTANCE then
			shouldFireSplitTears = lib.HasTearFlag(hitbox, TearFlags.TEAR_QUADSPLIT)
					and not lib.HasTearFlag(hitbox, TearFlags.TEAR_SPLIT) and not lib.HasTearFlag(hitbox, TearFlags.TEAR_BONE)
		else
			shouldFireSplitTears = lib.HasTearFlag(hitbox, TearFlags.TEAR_QUADSPLIT)
					or lib.HasTearFlag(hitbox, TearFlags.TEAR_SPLIT) or lib.HasTearFlag(hitbox, TearFlags.TEAR_BONE)
		end
		if shouldFireSplitTears then
			local numTears = 4
			
			if not REPENTANCE and not lib.HasTearFlag(hitbox, TearFlags.TEAR_QUADSPLIT) then
				numTears = 0
				if lib.HasTearFlag(hitbox, TearFlags.TEAR_SPLIT) then
					numTears = 2
				end
				if lib.HasTearFlag(hitbox, TearFlags.TEAR_BONE) then
					numTears = 1 + rng:RandomInt(3)
				end
			end
			
			local origin = tookDamage.Position + (player.Position - tookDamage.Position):Resized(tookDamage.Size + 10)
			local speed = player.ShotSpeed * 10
			local vel = Vector(0, speed):Rotated(rng:RandomInt(360))
			
			for i=0, numTears-1 do
				local projVel = vel:Rotated((360/numTears)*i)
				local tear = player:FireTear(origin, projVel, false, false, false)
				tear.Scale = tear.Scale * 0.5
				tear.CollisionDamage = tear.CollisionDamage * 0.5
				lib.RemoveTearFlag(tear, TearFlags.TEAR_BURSTSPLIT)
				lib.RemoveTearFlag(tear, TearFlags.TEAR_QUADSPLIT)
				if REPENTANCE then
					lib.RemoveTearFlag(tear, TearFlags.TEAR_SPLIT)
					lib.RemoveTearFlag(tear, TearFlags.TEAR_BONE)
				end
			end
		end
		
		-- FIEND FOLIO's TIME ITSELF
		if tookDamage:GetData().FFMultiEuclideanDuration and tookDamage:GetData().FFMultiEuclideanDuration >= 0 then
			mod:SpawnMultiEuclideanScythe(player, tookDamage, hitbox)
		end
		
		-- With Ipecac, trigger explosions on scythe hits.
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC) then
			local explosionDamageMult = 2
			if not player:HasWeaponType(WeaponType.WEAPON_TEARS) then
				explosionDamageMult = 4
			end
			game:BombExplosionEffects(
					lib.Lerp(hitbox.Position, tookDamage.Position, 0.5),
					hitbox.CollisionDamage * explosionDamageMult,
					hitbox.TearFlags ~ TearFlags.TEAR_GLITTER_BOMB,
					hitbox.Color,
					player,
					playerData.samaelScytheScale
				)
		end
		
		mod:TriggerRetributionKnifeHitEffects(player, hitbox, tookDamage)
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.ScytheHits)

function mod:ScytheCollision(hitbox, collider)
	if collider and hitbox:GetData()[collider.InitSeed] then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_KNIFE_COLLISION, mod.ScytheCollision, kScytheHitboxType)

function mod:TriggerRetributionKnifeHitEffects(player, knife, collider)
	if not Retribution then return end
	
	local data = knife:GetData()
	data.retributionTearEffectEntityBlacklist = data.retributionTearEffectEntityBlacklist or {}
	
	for _, callbackData in pairs(Retribution.GetCustomCallbacks(Retribution.Callback.APPLY_TEARFLAG_EFFECT)) do
		if Retribution.HasRetributionTearFlags(knife, callbackData.Arg) then
			local newEntity = callbackData.Call(collider, player, knife)
			
			if newEntity then
				collider = newEntity
			end
		end
	end
	
	data.retributionTearEffectEntityBlacklist[collider.InitSeed] = true
end

--------------------------------------------------
---- "GHOST" SCYTHE HITBOX (MOM'S EYE / LOKI'S HORNS)
--------------------------------------------------

local kGhostScytheAlpha = 0.75

function mod:GhostScytheUpdate(effect)
	local data = effect:GetData()
	local sprite = effect:GetSprite()
	
	local a = data.alpha or kGhostScytheAlpha
	
	if sprite:IsFinished("Swing") or a < kGhostScytheAlpha then
		local targetRot = data.targetRot or effect.SpriteRotation + 5
		a = lib.Lerp(a, 0, 0.3)
		
		if a < 0.01 then
			effect:Remove()
			return
		end
		
		data.targetRot = targetRot
	end
	
	local c = effect.Color
	c:SetTint(c.R, c.G, c.B, a)
	effect.Color = c
	
	if sprite:IsEventTriggered("SwingEnd") and a == kGhostScytheAlpha then
		--sprite:Stop()
		a = kGhostScytheAlpha - 0.05
	end
	
	effect:GetData().alpha = a
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.GhostScytheUpdate, kGhostScythe)

function mod:SpawnLokiScythe(player, rotOffset, damageMult)
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	
	if not scythe or not scythe:Exists() then return end
	
	playerData.samaelGhostScythes = playerData.samaelGhostScythes or {}
	
	if playerData.samaelGhostScythes[rotOffset] and playerData.samaelGhostScythes[rotOffset]:Exists() then
		playerData.samaelGhostScythes[rotOffset]:Remove()
	end
	
	local hitbox = mod:SpawnScytheHitbox(player, scythe)
	hitbox:GetData().posOffset = mod:GetHitboxPosOffset(player, playerData.samaelScytheLastVectorDirection:Rotated(rotOffset))
	hitbox.Position = player.Position + hitbox:GetData().posOffset
	hitbox.CollisionDamage = hitbox.CollisionDamage * (damageMult or 1)
	
	local ghostScythe = Isaac.Spawn(EntityType.ENTITY_EFFECT, kGhostScythe, 0, player.Position, lib.ZeroVector, player):ToEffect()
	ghostScythe.Parent = player
	ghostScythe:FollowParent(player)
	ghostScythe.Target = hitbox
	playerData.samaelGhostScythes[rotOffset] = ghostScythe
	
	ghostScythe.SpriteScale = Vector(playerData.samaelScytheScale, playerData.samaelScytheScale)
	
	local c = player.TearColor
	c:SetTint(c.R, c.G, c.B, kGhostScytheAlpha)
	ghostScythe.Color = c
	
	local sprite = ghostScythe:GetSprite()
	sprite:Play("Swing", true)
	sprite.FlipX = playerData.samaelScytheFlipped
	sprite:Update()
	
	sprite.Rotation = playerData.samaelScytheTargetRot + rotOffset
	local offset = CalcScythePosOffset(sprite.Rotation)
	if sprite.FlipX then
		offset = Vector(offset.X * -1, offset.Y)
	end
	ghostScythe.ParentOffset = offset
end

function mod:SpawnMultiEuclideanScythe(player, target, originalHitbox)
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	
	if not scythe or not scythe:Exists() then return end
	
	local dir = (target.Position - player.Position):Normalized()
	local pos = target.Position + dir:Resized(target.Size)
	
	local hitbox = mod:SpawnScytheHitbox(player, scythe)
	hitbox:GetData().posOffset = mod:GetHitboxPosOffset(player, dir)
	hitbox.Position = pos + hitbox:GetData().posOffset
	hitbox:GetData()[target.InitSeed] = true
	for k, v in pairs(originalHitbox:GetData()) do
		if type(k) == "number" and type(v) == "boolean" and v == true then
			hitbox:GetData()[k] = true
		end
	end
	hitbox:GetData().isMultiEuclidean = true
	
	local ghostScythe = Isaac.Spawn(EntityType.ENTITY_EFFECT, kGhostScythe, 0, pos, lib.ZeroVector, player):ToEffect()
	ghostScythe.Parent = player
	ghostScythe.Target = hitbox
	
	ghostScythe.SpriteScale = Vector(playerData.samaelScytheScale, playerData.samaelScytheScale)
	
	local s = math.sin(0.5*math.pi)
	local multiEuclidColor = Color(-s*2, -s*2, -s*2, kGhostScytheAlpha, (s+1)/2, (s+1)/2, (s+1)/2)
	multiEuclidColor:SetColorize(1, 1, 1, 1)
	ghostScythe.Color = multiEuclidColor
	
	local sprite = ghostScythe:GetSprite()
	sprite:Play("Swing", true)
	sprite.FlipX = playerData.samaelScytheFlipped
	sprite:Update()
	
	local rot = dir:GetAngleDegrees() - 90
	if sprite.FlipX then
		rot = rot * -1
	end
	sprite.Rotation = rot
	
	local offset = CalcScythePosOffset(rot)
	if sprite.FlipX then
		offset = Vector(offset.X * -1, offset.Y)
	end
	ghostScythe.Position = ghostScythe.Position + offset
end

--------------------------------------------------
---- SPECIAL CO-OP INTERACTIONS / EASTER EGGS
--------------------------------------------------

-- Trigger the animation for a special interaction.
function mod:AnimateSpecialInteraction(samael, otherPlayer, text)
	game:GetHUD():ShowItemText(text, "", false)
	samael:AnimateHappy()
	otherPlayer:AnimateHappy()
	
	if samael.FrameCount == 1 then
		local posOption1 = otherPlayer.Position:__add(Vector(60, 0))
		local posOption2 = otherPlayer.Position:__add(Vector(-60, 0))
		if game:GetRoom():IsPositionInRoom(posOption1, 0) then
			samael.Position = posOption1
		else
			samael.Position = posOption2
		end
	elseif otherPlayer.FrameCount == 1 then
		local posOption1 = samael.Position:__add(Vector(60, 0))
		local posOption2 = samael.Position:__add(Vector(-60, 0))
		if game:GetRoom():IsPositionInRoom(posOption1, 0) then
			otherPlayer.Position = posOption1
		else
			otherPlayer.Position = posOption2
		end
	end
	
	samael:GetData()["samael" .. otherPlayer:GetName() .. "InteractionDone"] = true
end

-- Check if a special interaction has already been triggered.
function mod:InteractionHasBeenDone(samael, otherPlayer) 
	return samael:GetData()["samael" .. otherPlayer:GetName() .. "InteractionDone"] == true
end

-- Check if Samael has a valid special interaction with this player type.
function mod:TrySpecialInteraction(samael, otherPlayer)
	if not lib.IsSamael(samael) or lib.IsSamael(otherPlayer)
			or mod:InteractionHasBeenDone(samael, otherPlayer) then
		return false
	end
	
	local otherPlayerType = otherPlayer:GetPlayerType()
	
	local item1Pos = samael.Position:__add(Vector(30, 25))
	local item2Pos = otherPlayer.Position:__add(Vector(30, 25))
	
	if otherPlayerType == PlayerType.PLAYER_LILITH or otherPlayerType == PlayerType.PLAYER_LILITH_B then
		mod:AnimateSpecialInteraction(samael, otherPlayer, "Power Couple!")
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_BLACK, item1Pos, lib.ZeroVector, nil)
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_BLACK, item2Pos, lib.ZeroVector, nil)
		return true
	elseif otherPlayerType == PlayerType.PLAYER_SAMSON_B then
		mod:AnimateSpecialInteraction(samael, otherPlayer, "Reap and Tear!")
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_SCARED, item1Pos, lib.ZeroVector, nil)
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, Card.CARD_REVERSE_STRENGTH, item2Pos, lib.ZeroVector, nil)
		return true
	elseif otherPlayerType == PlayerType.PLAYER_THEFORGOTTEN or otherPlayerType == PlayerType.PLAYER_THEFORGOTTEN_B then
		mod:AnimateSpecialInteraction(samael, otherPlayer, "Me & my Psychopomp")
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, Card.CARD_DEATH, item1Pos, lib.ZeroVector, nil)
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, Card.CARD_REVERSE_DEATH, item2Pos, lib.ZeroVector, nil)
		return true
	elseif otherPlayer:GetName() == "Mei" or otherPlayer:GetName() == "Tainted Mei" then
		mod:AnimateSpecialInteraction(samael, otherPlayer, "2017 Classics!")
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COIN, CoinSubType.COIN_PENNY, item1Pos, lib.ZeroVector, nil)
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_HALF_SOUL, item2Pos, lib.ZeroVector, nil)
		return true
	elseif otherPlayerType == Isaac.GetPlayerTypeByName("Bertran") then
		mod:AnimateSpecialInteraction(samael, otherPlayer, "Heads will roll!")
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, Card.CARD_CHARIOT, item1Pos, lib.ZeroVector, nil)
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_BOMB, BombSubType.BOMB_NORMAL, item2Pos, lib.ZeroVector, nil)
		return true
	elseif otherPlayerType == Isaac.GetPlayerTypeByName("Fiend") then
		mod:AnimateSpecialInteraction(samael, otherPlayer, "Reaper's Harvest!")
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_BLACK, item1Pos, lib.ZeroVector, nil)
		Isaac.Spawn(5, 100, Isaac.GetItemIdByName("GMO Corn"), item2Pos, lib.ZeroVector, nil)
		return true
	end
	
	return false
end

-- Look for potential special interactions when a player first spawns.
function mod:CheckForSpecialInteractions(player)
	local playerData = player:GetData()

	local shouldCheckSpecialInteractions = player.FrameCount == 1
			and game:GetLevel():GetCurrentRoom():GetFrameCount() > 1
			and playerData.checkedSpecialSamaelInteractions ~= true
	
	if playerData.trySpecialSamaelInteractionsAgain == true and player:GetSprite():GetAnimation() ~= "Happy" then
		playerData.trySpecialSamaelInteractionsAgain = false
		shouldCheckSpecialInteractions = true
	end
	
	if shouldCheckSpecialInteractions then
		playerData.checkedSpecialSamaelInteractions = true
		-- If this is the first instance of this particular player type to spawn, check for a special interaction.
		local count = 0
		for _, otherPlayer in pairs(lib.GetPlayers()) do
			if player:GetName() == otherPlayer:GetName() and otherPlayer:GetPlayerType() ~= PlayerType.PLAYER_SAMSON then
				count = count + 1
			end
		end
		if count == 1 then
			local foundInteraction = false
			for _, otherPlayer in pairs(lib.GetPlayers()) do
				if lib.IsSamael(player) then
					if mod:TrySpecialInteraction(player, otherPlayer) then
						foundInteraction = true
						break
					end
				elseif lib.IsSamael(otherPlayer) then
					if mod:TrySpecialInteraction(otherPlayer, player) then
						foundInteraction = true
						break
					end
				end
			end
			if foundInteraction and lib.IsSamael(player) and game:GetNumPlayers() > 2 then
				-- An interaction was found and triggered, but there could be others.
				-- Check again after the animation is finished.
				playerData.trySpecialSamaelInteractionsAgain = true
			end
		end
	end
end

--------------------------------------------------
---- WRAITH MODE FUNCTIONALITY
--------------------------------------------------

function mod:GetWraithModeDuration(player)
	local dur = kWraithModeDuration
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) then
		dur = dur + 20
	end
	return dur
end

-- Trigger Wrath Mode
function mod:TriggerWraithMode(_, _, player, useFlags)
	local playerData = player:GetData()
	
	if game:GetLevel():GetCurrentRoom():GetFrameCount() == 0 then
		return {Discharge = false}
	end
	
	if player:IsHoldingItem() then
		player:PlayExtraAnimation("HideItem")
	end
	
	if not (REPENTANCE and (useFlags & UseFlag.USE_MIMIC == UseFlag.USE_MIMIC)) then
		SetWraithCharge(player, 0)
	end
	
	playerData.wraithActive = true
	playerData.wraithCooldown = 0
	
	if not lib.IsSamael(player) then
		InitializeScytheData(player)
		mod:SpawnScythe(player)
	end
	
	-- Pop certain effects when entering wraith mode.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_HOLY_LIGHT) then
		player:UseActiveItem(CollectibleType.COLLECTIBLE_CRACK_THE_SKY, false, false, false, false)
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_CURSE_OF_THE_TOWER) then
		player:UseActiveItem(CollectibleType.COLLECTIBLE_ANARCHIST_COOKBOOK, false, false, false, false)
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_VARICOSE_VEINS) then
		player:UseActiveItem(CollectibleType.COLLECTIBLE_TAMMYS_HEAD, false, false, false, false)
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MAW_OF_THE_VOID or CollectibleType.COLLECTIBLE_MAW_OF_VOID)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_ATHAME) then
		player:SpawnMawOfVoid(100)
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC) then
		local tearParams = player:GetTearHitParams(WeaponType.WEAPON_TEARS, 1, 1, nil)
		local damageMul = 10
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC) and player:HasWeaponType(WeaponType.WEAPON_TEARS) then
			damageMul = 1
		end
		game:BombExplosionEffects(
				player.Position,
				player.Damage * damageMul,
				tearParams.TearFlags,
				tearParams.TearColor,
				player,
				playerData.samaelScytheScale * 2
			)
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS) then
		for i=0, 6 do
			local target = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.TARGET, 0, game:GetRoom():GetRandomPosition(20), lib.ZeroVector, player):ToEffect()
			target.State = 1
			target.LifeSpan = 5 * (i+1)
			target.Timeout = 5 * (i+1)
		end
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BLACK_POWDER) then
		local pentagram = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PENTAGRAM_BLACKPOWDER, 0, player.Position, lib.ZeroVector, player):ToEffect()
		pentagram.State = 1
		pentagram.Size = 150
		pentagram.SpriteScale = Vector(0.75,0.75)
	end
	
	playerData.wraithTime = mod:GetWraithModeDuration(player)

	player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_PLAYERONLY
	sfxManager:Play(SoundEffect.SOUND_DEATH_CARD, 1, 0, false, 1.1)
	
	--Black poof effects
	local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, player.Position, lib.ZeroVector, player):ToEffect()
	poof:GetSprite().Color = lib.NewColor(0,0,0,0.66,0,0,0)
	poof:FollowParent(player)
	
	local puff = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, player.Position, lib.ZeroVector, player):ToEffect()
	puff.Color = lib.NewColor(0,0,0,0.75)
	puff.SpriteScale = Vector(1.1, 1.1)
	
	-- Draw the wraith sprite
	player.Visible = false -- Make the player invisible.
	local wraith = Isaac.Spawn(EntityType.ENTITY_EFFECT, kWraithEffect, 0, player.Position, lib.ZeroVector, player):ToEffect()
	if lib.IsSamael(player) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) then
		wraith:GetSprite():Load("gfx/samael_angel.anm2", true)
	end
	wraith.Parent = player
	wraith:FollowParent(player)
	wraith:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	wraith.SpriteScale = player.SpriteScale
	wraith.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
	wraith.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
	
	mod:WraithVisualUpdate(wraith)
	
	player:AddCacheFlags(CacheFlag.CACHE_SPEED)
	player:EvaluateItems()
end
if REPENTANCE then
	mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.TriggerWraithMode, kSamaelPocketActive)
end

-- Malakh Mot + Book of Virtues
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, function(_, fam)
	if fam.SubType == kSamaelPocketActive then
		local sprite = fam:GetSprite()
		sprite:ReplaceSpritesheet(0, "gfx/samael_entities/malakh_mot_wisp.png")
		sprite:LoadGraphics()
		local c = Color(0.9,0.9,1.0,1)
		c:SetColorize(1,0,1,1)
		sprite.Color = c
	end
end, FamiliarVariant.WISP)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_INIT, function(_, tear)
	local fam = tear.SpawnerEntity and tear.SpawnerEntity:ToFamiliar()
	if fam and fam.Variant == FamiliarVariant.WISP and fam.SubType == kSamaelPocketActive then
		tear:ChangeVariant(TearVariant.SCHYTHE)
	end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, function(_, tear)
	local fam = tear.SpawnerEntity and tear.SpawnerEntity:ToFamiliar()
	if fam and fam.Variant == FamiliarVariant.WISP and fam.SubType == kSamaelPocketActive and not tear:GetData().malakhMotWispInit then
		tear.CollisionDamage = tear.CollisionDamage + 1
		tear.Color = lib.NullColor
		tear:GetData().malakhMotWispInit = true
	end
end)

local AnimationTriggersWraithModeEnd = {
	["Trapdoor"] = true,
	["TeleportUp"] = true,
	["TeleportDown"] = true,
	["MinecartEnter"] = true,
	["LostDeath"] = true,
	["Death"] = true,
	["FakeDeath"] = true,
	["FallIn"] = true,
	["HoleDeath"] = true,
	["ForgottenDeath"] = true,
	["DeathTeleport"] = true,
	["LightTravel"] = true,
}

-- Wraith Mode Handler
function mod:WraithModeHandler(player)
	local playerData = player:GetData()
	local room = game:GetLevel():GetCurrentRoom()
	local newRoom = room:GetFrameCount() == 0
	
	if lib.HasItem(player, kSamaelPocketActive) then
		local wraithCharge = GetWraithCharge(player) or 0
		local maxWraithCharge = 60 + 45 * math.min(game:GetLevel():GetStage(), LevelStage.STAGE6)
		
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) then
			maxWraithCharge = math.ceil(maxWraithCharge * 0.8)
		end
		
		local currentWraithCharge = math.min(math.floor((wraithCharge / maxWraithCharge) * 100), 100)
		playerData.maxWraithCharge = maxWraithCharge
		
		local chargeToRender
		if playerData.wraithActive then
			-- Render the charge depleting while the effect is active.
			chargeToRender = math.min(math.floor(100 * (playerData.wraithTime or 0) / mod:GetWraithModeDuration(player)), 99)
		else
			-- Cap the visible charge at 95% until it reaches 100% to make it more obvious when its not full yet.
			chargeToRender = math.min(currentWraithCharge, 95)
		end
		
		if not playerData.wraithCooldown then
			playerData.wraithCooldown = 0
		end
		if not playerData.wraithTime then
			playerData.wraithTime = 0
		end
		
		if REPENTANCE then
			for _, slot in pairs({ActiveSlot.SLOT_PRIMARY, ActiveSlot.SLOT_SECONDARY, ActiveSlot.SLOT_POCKET}) do
				if player:GetActiveItem(slot) == kSamaelPocketActive then
					if currentWraithCharge >= 100 then
						player:FullCharge(slot, true)
					else
						player:SetActiveCharge(chargeToRender, slot)
					end
				end
			end
		else
			-- Handling charge for AB+
			local isPrimary = player:GetActiveItem() == kSamaelPocketActive
			local isSecondary = player.SecondaryActiveItem and player.SecondaryActiveItem.Item == kSamaelPocketActive
			if (isPrimary and player:GetActiveCharge() ~= currentWraithCharge) or (isSecondary and player.SecondaryActiveItem.Charge ~= currentWraithCharge) then
				if isSecondary then
					player:SwapActiveItems()
				end
				local otherActive = player.SecondaryActiveItem
				player:SetActiveCharge(chargeToRender)
				player.SecondaryActiveItem = otherActive
				-- Triggering wraith mode in AB+
				if player.ControlsEnabled and room:GetFrameCount() > 10 and wraithCharge >= maxWraithCharge 
						and Input.IsActionPressed(ButtonAction.ACTION_ITEM, player.ControllerIndex) then
					mod:TriggerWraithMode(nil, nil, player)
				end
				if isSecondary then
					player:SwapActiveItems()
				end
			end
		end
	end
	
	if playerData.wraithActive and (AnimationTriggersWraithModeEnd[player:GetSprite():GetAnimation()] or newRoom or playerData.doingIsaacKillAnimation) then --Stop wraith form early
		playerData.wraithActive = false
		player:AddCacheFlags(CacheFlag.CACHE_SPEED)
		player:EvaluateItems()
		playerData.wraithCooldown = 0
		playerData.wraithTime = 0
		if not playerData.doingIsaacKillAnimation then
			player.Visible = true
		end
		player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
		playerData.wraithActive = false
	end
	
	if (playerData.wraithCooldown or 0) > 0 then -- On cooldown after wraith form wears off (briefly flashing and still invulnerable)
		playerData.wraithCooldown = playerData.wraithCooldown - 1
		--[[if playerData.wraithCooldown % 4 == 0 then
			player:SetColor(lib.NewColor(0.3, 0.3, 0.3, 1, 0,0,0), 2, 999, false, false)
		end]]
		if playerData.wraithCooldown <= 0 then
			player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
			player:AddCacheFlags(CacheFlag.CACHE_SPEED)
			player:EvaluateItems()
		end
	elseif playerData.wraithActive then --Full wraith form is active
		if not lib.IsSamael(player) then
			player.FireDelay = player.MaxFireDelay
		end
		playerData.wraithTime = playerData.wraithTime - 1
		player.Visible = false
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.DARK_BALL_SMOKE_PARTICLE, 0, player.Position, lib.ZeroVector, player) -- Smoke trail
		player:SetMinDamageCooldown(kWraithModeEndIFrames)
		if playerData.wraithTime <= 0 then --When wraith time is over
			sfxManager:Play(316, 1.8, 0, false, 1.25)
			playerData.wraithActive = false
			playerData.wraithCooldown = kWraithModeEndIFrames
			player.Visible = true
			--Black poof effect
			local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, player.Position, lib.ZeroVector, player):ToEffect()
			poof:GetSprite().Color = lib.NewColor(0,0,0,0.66,0,0,0)
			poof:FollowParent(player)
		end
	end
end

function mod:WraithVisualUpdate(wraith)
	local sprite = wraith:GetSprite()
	local player = wraith.Parent:ToPlayer()
	local playerData = player:GetData()
	
	if wraith.SubType == 1 then
		if not (wraith:GetData().isaacBoss and wraith:GetData().isaacBoss:Exists()) and not lib.CurrentAnimIs(wraith:GetSprite(), "Special2") and not lib.CurrentAnimIs(wraith:GetSprite(), "Special2B") then
			if lib.IsTaintedSamael(player) then
				wraith:GetSprite():Play("Special2B", true)
			else
				wraith:GetSprite():Play("Special2", true)
			end
		end
		
		if wraith.FrameCount < 100 then
			player.ControlsEnabled = false
			player.Visible = false
		end
		
		if wraith:GetSprite():IsFinished("Special2") or wraith:GetSprite():IsFinished("Special2B") or wraith.FrameCount > 150 then
			player.Position = wraith.Position
			player.Visible = true
			player.ControlsEnabled = true
			if lib.IsTaintedSamael(player) then
				player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
				playerData.mementoMoriIFrames = 60
				playerData.samaelScytheFlipped = true
			else
				playerData.wraithCooldown = 30
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, player.Position, lib.ZeroVector, player):ToEffect()
				sfxManager:Play(316, 1.8, 0, false, 1.25)
				poof:GetSprite().Color = lib.NewColor(0,0,0,0.66,0,0,0)
				poof:FollowParent(player)
			end
			playerData.hideScythe = false
			playerData.doingIsaacKillAnimation = nil
			playerData.samaelScythe:Update()
			if mod:GetAllRunData().triggerFinalSequence then
				mod:GetAllRunData().startFinalSequence = true
				mod:FindGraveRoom()
			end
			wraith:Remove()
		end
		return
	end
	
	if not playerData.wraithActive or playerData.wraithCooldown > 0 then
		wraith:Remove()
		return
	end
	
	if AnimationTriggersWraithModeEnd[player:GetSprite():GetAnimation()] then
		mod:WraithModeHandler(player)
		wraith:Remove()
		return
	end
	
	player.Visible = false
	
	--wraith.Position = player.Position
	--wraith.Velocity = player.Velocity
	
	local currentAnim = sprite:GetAnimation()
	
	--[[if player:IsHoldingItem() or player:GetSprite():GetAnimation():find("PickupWalk") then
		if currentAnim ~= "LiftItem" and currentAnim ~= "HoldItem" then
			sprite:Play("LiftItem", true)
		elseif sprite:IsFinished("LiftItem") then
			sprite:Play("HoldItem", true)
		end
	elseif currentAnim == "LiftItem" or currentAnim == "HoldItem" then
		sprite:Play("PutAway", true)
	elseif not sprite:IsPlaying("PutAway") then]]
	
	local headDir = player:GetHeadDirection()
	if lib.IsSamael(player) and playerData.samaelScytheState ~= 0 then
		headDir = playerData.samaelScytheLastCardinalDirection
	end
	local headAnim = "Down"
	if headDir == Direction.UP then
		headAnim = "Up"
	elseif headDir == Direction.LEFT then
		headAnim = "Left"
	elseif headDir == Direction.RIGHT then
		headAnim = "Right"
	end
	
	local bodyDir = player:GetMovementDirection()
	local bodyAnim = "Down"
	if bodyDir == Direction.UP then
		bodyAnim = "Up"
	elseif bodyDir == Direction.LEFT then
		bodyAnim = "Left"
	elseif bodyDir == Direction.RIGHT then
		bodyAnim = "Right"
	end
	
	if sprite:GetFilename() == "gfx/samael_angel.anm2" then
		if not sprite:IsPlaying(bodyAnim) then
			sprite:Play(bodyAnim, true)
		end
		sprite:SetOverlayRenderPriority(bodyAnim == "Up")
		if (headAnim == "Left" and bodyAnim == "Right") or (headAnim == "Right" and bodyAnim == "Left") then
			headAnim = headAnim .. "2"
		end
		headAnim = "Head" .. headAnim
		if not sprite:IsOverlayPlaying(headAnim) then
			sprite:PlayOverlay(headAnim, true)
		end
	elseif not sprite:IsPlaying(headAnim) then
		sprite:Play(headAnim, true)
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, -1, mod.WraithVisualUpdate, kWraithEffect)

mod:AddPriorityCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, CallbackPriority.EARLY, function(_, player)
	local pData = player:GetData()
	
	-- FAIL SAFE
	if pData.doingIsaacKillAnimation and pData.doingIsaacKillAnimation > 0 then
		pData.doingIsaacKillAnimation = pData.doingIsaacKillAnimation - 1
		player:SetMinDamageCooldown(60)
		if pData.doingIsaacKillAnimation < 1 then
			player.Visible = true
			player.ControlsEnabled = true
			
			if lib.IsTaintedSamael(player) then
				player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
				pData.mementoMoriIFrames = 60
				pData.samaelScytheFlipped = true
			else
				pData.wraithCooldown = 30
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, player.Position, lib.ZeroVector, player):ToEffect()
				sfxManager:Play(316, 1.8, 0, false, 1.25)
				poof:GetSprite().Color = lib.NewColor(0,0,0,0.66,0,0,0)
				poof:FollowParent(player)
			end
			
			pData.hideScythe = false
			pData.doingIsaacKillAnimation = nil
			if pData.samaelScythe then
				pData.samaelScythe:Update()
			end
		end
	end
end)

function mod:WraithVisualRender(wraith)
	local sprite = wraith:GetSprite()
	local n = (20 * math.sin(math.pi * wraith.FrameCount / 8) + 20) / 255
	local color = lib.NewColor(1,1,1,1, n, 0, n)
	--local color = lib.NullColor
	if sprite:GetFilename() == "gfx/samael_angel.anm2" then
		local scale = Vector(sprite.Scale.X, sprite.Scale.Y)
		local anim = sprite:GetAnimation()
		local frame = sprite:GetFrame()
		sprite.Color = Color(0,0,0, 1, 0.5, 0, 0.5)
		sprite:SetFrame("Fire", wraith.FrameCount % 10)
		sprite:Render(Isaac.WorldToScreen(wraith.Position), lib.ZeroVector, lib.ZeroVector)
		sprite:Play(anim, true)
		sprite:SetFrame(frame)
		sprite.Scale = scale * (1 + (wraith.FrameCount % 10) / 40)
		sprite.Color = Color(0,0,0, 1 - (wraith.FrameCount % 10) / 9, 0.5, 0, 0.5)
		sprite:Render(Isaac.WorldToScreen(wraith.Position) + Vector(0, scale.Y), lib.ZeroVector, lib.ZeroVector)
		sprite.Color = color
		sprite.Scale = scale
		sprite:Render(Isaac.WorldToScreen(wraith.Position), lib.ZeroVector, lib.ZeroVector)
	else
		sprite.Color = color
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_RENDER, mod.WraithVisualRender, kWraithEffect)

-- Disable item use while in wraith mode.
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION, function(_, entity, hook, action)
	if entity and entity:ToPlayer() and entity:ToPlayer():GetData().wraithActive and (action == ButtonAction.ACTION_ITEM or action == ButtonAction.ACTION_PILLCARD) then
		return false
	end
end, InputHook.IS_ACTION_TRIGGERED)

-- Damage callback for the player taking damage
function mod:playerDamage(player, damage, damageFlags, damageSourceRef)
	player = player:ToPlayer()
	local playerData = player:GetData()
	
	local isSelfDamage = damageSourceRef.Type == EntityType.ENTITY_PLAYER
	local isRedHeartDamage = damageFlags & DamageFlag.DAMAGE_RED_HEARTS == DamageFlag.DAMAGE_RED_HEARTS
	local isExplosionDamage = damageFlags & DamageFlag.DAMAGE_EXPLOSION == DamageFlag.DAMAGE_EXPLOSION
	local isSpikesDamage = damageFlags & DamageFlag.DAMAGE_SPIKES == DamageFlag.DAMAGE_SPIKES
	local isRedPoopDamage = damageFlags & DamageFlag.DAMAGE_POOP == DamageFlag.DAMAGE_POOP
	local isCurseRoomDamage = damageFlags & DamageFlag.DAMAGE_CURSED_DOOR == DamageFlag.DAMAGE_CURSED_DOOR
	
	-- Resist damage from spiked chests when opening them with the scythe.
	if damageSourceRef.Type == EntityType.ENTITY_PICKUP and (damageSourceRef.Variant == PickupVariant.PICKUP_SPIKEDCHEST or damageSourceRef.Variant == PickupVariant.PICKUP_MIMICCHEST)
			and damageSourceRef.Entity and (damageSourceRef.Entity:GetData().samaelPickupCountDown or 0) > 0 then
			return false
	end
	
	--Resist all damage in wraith mode except for things like the IV bag or Razor
	if (playerData.wraithActive or (playerData.wraithCooldown or 0) > 0 or playerData.doingIsaacKillAnimation) and not isSelfDamage and not isRedHeartDamage then
		return false
	end
	
	-- Panic Button automatically triggers wraith mode before taking damage.
	if REPENTANCE and lib.HasItem(player, kSamaelPocketActive)
			and player:HasTrinket(TrinketType.TRINKET_PANIC_BUTTON)
			and GetWraithCharge(player) >= 100
			and not isSelfDamage and not isRedHeartDamage and not isRedPoopDamage
			and not isExplosionDamage and not isSpikesDamage and not isCurseRoomDamage then
		mod:TriggerWraithMode(nil, nil, player, 0)
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.playerDamage, EntityType.ENTITY_PLAYER)

-- Samael's familiars are invincible while he is.
function mod:SamaelFamiliarDamage(fam)
	if fam:GetData().isSamaelWisp then
		return false
	end
	local player = fam.Player
	if not player and fam.SpawnerEntity then
		player = fam.SpawnerEntity:ToPlayer() 
	end
	if not player and fam.Parent then
		player = fam.Parent:ToPlayer() 
	end
	if player and (player:GetData().wraithActive or (player:GetData().mementoMoriActive and not player:GetData().disjointedMementoMori)) then
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.SamaelFamiliarDamage, EntityType.ENTITY_FAMILIAR)

--------------------------------------------------
---- PROJECTILE-FIRING FUNCTIONALITY
--------------------------------------------------

-- Return the number of tears the player should fire.
-- Mostly tries to replicate the current functionalities of multishot/monstro's lung exactly.
function mod:getNumTears(player, noLung)
	local playerData = player:GetData()
	
	if player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) and playerData.samaelScytheType ~= "cannon" and not noLung then
		local num2020 = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_20_20)
		local numInnerEye = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_INNER_EYE)
		local numSpider = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MUTANT_SPIDER)
		local numWiz = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_THE_WIZ)
		local numTears = 2 + 12*player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MONSTROS_LUNG)
		if num2020		 > 0 then numTears = numTears + 3 + 2*(num2020		-1) end
		if numInnerEye > 0 then numTears = numTears + 5 + 3*(numInnerEye-1) end
		if numSpider	 > 0 then numTears = numTears + 7 + 5*(numSpider	-1) end
		if numWiz			 > 0 then numTears = numTears + 3 + 2*(numWiz			-1) end
		return math.min(numTears, 50*(numWiz+1))
	else
		local numTears = lib.GetNumProjectiles(player)
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_KNIFE) and player:HasWeaponType(WeaponType.WEAPON_KNIFE) then
			numTears = numTears + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MOMS_KNIFE) - 1
		end
		if playerData.samaelScytheType == "cannon" then
			local numDrFetus = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_DR_FETUS)
			local numEpicFetus = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_EPIC_FETUS)
			numTears = numTears + math.max(numDrFetus + numEpicFetus - 1, 0)
			if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_ROCKETS) then
				numTears = numTears + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_ROCKET_IN_A_JAR)
			end
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_MONSTROS_LUNG) then
				numTears = numTears + 5
			end
		end
		return numTears
	end
end

function mod:UpdateProjectileSprite(tear, visualType)
	local tearSprite = tear:GetSprite()
	tearSprite:ReplaceSpritesheet(1, "gfx/effects/samael scythe/" .. visualType .. ".png")
	tearSprite:ReplaceSpritesheet(2, "gfx/effects/samael scythe/" .. visualType .. "_blade.png")
	tearSprite:LoadGraphics()
	tearSprite:Play("Knife", true)
	
	if lib.SameColor(tear.Color, lib.SamaelTearColor) then
		tear:SetColor(lib.NullColor, -1, 1, false, false)
	end
			
	tear:GetData().visualType = visualType
end

function mod:BombUpdate(bomb)
	if bomb:GetData().samaelForcedRocket then
		local targetVel = bomb:GetData().samaelRocketTargetVel
		local startVel = targetVel * 0.25
		local framesUntilFullSpeed = 15
		local counter = bomb.FrameCount - (bomb:GetData().samaelRocketStartFrame or 0)
		local percent = math.min(counter / framesUntilFullSpeed, 1.0)
		bomb.Velocity = lib.Lerp(startVel, targetVel, percent)
		bomb.SpriteRotation = targetVel:GetAngleDegrees()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_BOMB_UPDATE, mod.BombUpdate, BombVariant.BOMB_ROCKET)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_UPDATE, mod.BombUpdate, BombVariant.BOMB_ROCKET_GIGA)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_RENDER, mod.BombUpdate, BombVariant.BOMB_ROCKET)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_RENDER, mod.BombUpdate, BombVariant.BOMB_ROCKET_GIGA)

local function LoadThrownScytheSprite(player, sprite, bladeOnly)
	local pData = player:GetData()
	
	sprite:Load("gfx/samael_scythe_simple.anm2", false)
	if bladeOnly then
		if pData.samaelScytheType ~= "default" then
			sprite:ReplaceSpritesheet(2, "gfx/samael_scythe/" .. pData.samaelScytheType .. ".png")
			sprite:ReplaceSpritesheet(3, "gfx/samael_scythe/" .. pData.samaelScytheType .. ".png")
		end
		sprite:ReplaceSpritesheet(1, "gfx/samael_null.png")
		sprite:ReplaceSpritesheet(4, "gfx/samael_null.png")
	elseif pData.samaelScytheType ~= "default" then
		for i=1, 4 do
			sprite:ReplaceSpritesheet(i, "gfx/samael_scythe/" .. pData.samaelScytheType .. ".png")
		end
	end
	sprite:LoadGraphics()
	sprite:Play("Thrown2", true)
end

function mod:ThrowKnifeScythe(player, vel, charge)
	local playerData = player:GetData()
	
	local angle = playerData.samaelKnifeLaser.AngleDegrees - vel:GetAngleDegrees()
	
	local knife = player:FireKnife(player, 0, false, kScytheKnifeType, 0):ToKnife()
	local kData = knife:GetData()
	kData.samaelScytheKnifeAngle = angle or 0 --projVel:GetAngleDegrees() % 90
	kData.samaelScytheKnifeRange = player.TearRange
	kData.samaelScytheKnifeSpeed = player.ShotSpeed * 10
	kData.samaelScytheKnifeCharge = charge or 1.0
	knife.Charge = kData.samaelScytheKnifeCharge
	kData.samaelScytheKnifeStartVel = vel
	kData.isSamaelKnifeScythe = true
	if TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.THE_BOTTLE) then
		kData.samaelBottle = true
	end
	
	playerData.thrownKnifeScythes[knife.InitSeed] = knife
	
	--[[local path, pathLength = mod:GetScytheKnifePath(player)
	kData.samaelScytheKnifePath = path
	kData.samaelScytheKnifePathLength = pathLength]]
	
	--[[local knifeSprite = knife:GetSprite()
	knifeSprite:Load("gfx/samael_scythe_projectile.anm2", true)
	knifeSprite:ReplaceSpritesheet(1, "gfx/effects/samael scythe/" .. playerData.samaelScytheType .. ".png")
	knifeSprite:ReplaceSpritesheet(2, "gfx/effects/samael scythe/" .. playerData.samaelScytheType .. "_blade.png")
	knifeSprite:LoadGraphics()
	knifeSprite:Play("Knife", true)]]
	
	local knifeSprite = knife:GetSprite()
	if kData.samaelBottle then
		if playerData.samaelBottleBroken then
			kData.BottleRoomIndex = game:GetLevel():GetCurrentRoomIndex()
			kData.BrokenBefore = true
			knife:Update()
			knifeSprite:ReplaceSpritesheet(0, "gfx/projectiles/knife_bottle_broken.png")
		else
			knifeSprite:ReplaceSpritesheet(0, "gfx/projectiles/knife_bottle.png")
		end
		knifeSprite:LoadGraphics()
		knife.Color = lib.NullColor
	else
		LoadThrownScytheSprite(player, knifeSprite)
		
		knifeSprite.FlipX = playerData.samaelScytheFlipped
		knife.Size = 35 * playerData.samaelScytheScale
		knife.SpriteScale = Vector(playerData.samaelScytheScale, playerData.samaelScytheScale)
		knife.SpriteOffset = Vector(0, -10)
		knife.PositionOffset = lib.ZeroVector
		
		if playerData.samaelScytheShouldColorHandle then
			knife.Color = playerData.samaelScytheColor or lib.NullColor
		else
			local sprite = Sprite()
			LoadThrownScytheSprite(player, sprite, true)
			sprite.Color = playerData.samaelScytheColor or lib.NullColor
			sprite.Scale = knifeSprite.Scale
			sprite.FlipX = knifeSprite.FlipX
			sprite.Offset = knife.SpriteOffset
			kData.samaelScytheKnifeBladeSprite = sprite
			knife.Color = lib.NullColor
		end
	end
	
	-- Laser rings around knives
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_X)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE)
			or lib.HasItem(player, lib.Access(Retribution, "ITEMS", "TECHNOLOGY_OMICRON")) then
		local laser = mod:AddLaserRing(player, knife, 50 * playerData.samaelScytheScale)
		laser.ParentOffset = knife.SpriteOffset
		laser:SetTimeout(-1)
	end
	
	--[[if bloodClotOrChem then
		knife.Color = lib.BloodColor
	elseif lib.SameColor(player.TearColor, lib.SamaelTearColor) then
		knife.Color = lib.NullColor
	end]]
	
	return knife
end

mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, function(_, tear)
	if TaintedTears and tear.Variant == TaintedTears.SAWBLADE and lib.IsSamael(tear.SpawnerEntity) and not tear:GetData().samaelCheckedTearColor then
		tear:GetData().samaelCheckedTearColor = true
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
	end
end)

-- Fire a single projectile (tear, knife, bomb, etc).
function mod:FireSingleProjectile(player, projOrigin, projVel, angleOffset, chargeLevel, isPencilTear, forceTear)
	if isPencilTear then
		forceTear = true
	end
	
	projVel = projVel:Rotated(angleOffset)

	local playerData = player:GetData()
	
	local damageBonus = 0
	
	local bloodClots = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BLOOD_CLOT)
	local chemPeels = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_CHEMICAL_PEEL)
	local bloodClotOrChem = (bloodClots > 0 or chemPeels > 0) and playerData.samaelBloodClotFlag
	if bloodClotOrChem then
		damageBonus = bloodClots + chemPeels * 2
	end
	
	local damageMult = chargeLevel
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
		damageMult = damageMult * 3
	end
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_2) then
		laser = player:FireTechLaser(player.Position, LaserOffset.LASER_TECH2_OFFSET, projVel, false, true, player, damageMult)
		laser.CollisionDamage = laser.CollisionDamage + damageBonus
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_5) then
		laser = player:FireTechLaser(player.Position, LaserOffset.LASER_TECH5_OFFSET, projVel, false, true, player, damageMult)
		laser.CollisionDamage = laser.CollisionDamage + damageBonus
	end
	
	local fireBomb = playerData.samaelScytheType == "cannon" and not forceTear
	
	local taintedSawblade
	if not fireBomb and TaintedTreasure and TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.LIL_SLUGGER) then
		taintedSawblade = TaintedTreasure:FireSawblade(player, projVel, nil, damageMult, projOrigin)
		--[[if lib.SameColor(taintedSawblade.Color, lib.SamaelTearColor) then
			taintedSawblade.Color = lib.NullColor
		end]]
		taintedSawblade.Position = projOrigin
	end
	
	--[[if player:HasWeaponType(WeaponType.WEAPON_FETUS) and not forceTear then
		local fetus = Isaac.Spawn(EntityType.ENTITY_TEAR, 50, 0, projOrigin, projVel, player):ToTear()
		fetus.Parent = player
		return fetus
	else]]
	-- BOMBS ------------------------------
	if fireBomb then --Fire dr fetus bomb
		local bomb = player:FireBomb(projOrigin, projVel, player)
		if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_ROCKETS) and not lib.HasItem(player, CollectibleType.COLLECTIBLE_ROCKET_IN_A_JAR) then
			bomb:GetData().samaelForcedRocket = true
			bomb:GetData().samaelRocketTargetVel = projVel * 1.25
			bomb.Variant = BombVariant.BOMB_ROCKET
			bomb.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ENEMIES
		end
		if lib.SameColor(player.TearColor, lib.SamaelTearColor) then
			bomb:SetColor(lib.NullColor, -1, 1, false, false)
		end
		bomb.ExplosionDamage = bomb.ExplosionDamage * damageMult
		
		-- Laser rings around bombs
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_X)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE)
				or lib.HasItem(player, lib.Access(Retribution, "ITEMS", "TECHNOLOGY_OMICRON")) then
			local laser = mod:AddLaserRing(player, bomb, 35)
			laser.ParentOffset = Vector(0, -5)
			laser:SetTimeout(-1)
		end
		
		bomb:Update()
		return bomb
	-- KNIVES ------------------------------
	elseif player:HasWeaponType(WeaponType.WEAPON_KNIFE) and not forceTear then
		return mod:ThrowKnifeScythe(player, projVel, chargeLevel)
	-- BRIMSTONE ------------------------------
	elseif not forceTear and (
				player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE)
				or (REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD)
					and lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE))
			) then
		-- Spawn a laser with FireBrimstone to get some values, but remove it since there are some
		-- position related issues I keep running into with FireBrimstone.
		local testLaser = player:FireBrimstone(projVel:Normalized(), nil, damageMult):ToLaser()
		local laser = Isaac.Spawn(EntityType.ENTITY_LASER, testLaser.Variant, testLaser.SubType, projOrigin, lib.ZeroVector, player):ToLaser()
		laser.Angle = testLaser.Angle
		laser.CollisionDamage = testLaser.CollisionDamage + damageBonus
		laser.Color = testLaser.Color
		laser.TearFlags = testLaser.TearFlags
		local laserData = laser:GetData()
		laserData.targetWidth = testLaser.SpriteScale.X
		laserData.isSamaelLaser = true
		testLaser:Remove()
		
		laser.DisableFollowParent = true
		laser.Parent = player
		laser.DepthOffset = 250

		if lib.HasItem(player, CollectibleType.COLLECTIBLE_ANTI_GRAVITY) then
			laser:SetTimeout(kBrimstoneTime - 10)
		else
			laser:SetTimeout(kBrimstoneTime)
			if laser:GetSprite():GetFilename() == "gfx/007.001_Thick Red Laser.anm2" then
				laser:GetSprite():ReplaceSpritesheet(0, "gfx/effects/samael_lasereffects_notip.png")
				laser:GetSprite():LoadGraphics()
			end
		end
		
		if taintedSawblade then
			local c = laser.Color
			c:SetTint(1, 0, 0, 1)
			taintedSawblade.Color = c
			taintedSawblade.Position = taintedSawblade.Position + Vector(0, taintedSawblade.Size)
		end
		
		return laser
	-- TECH LASERS ------------------------------
	elseif player:HasWeaponType(WeaponType.WEAPON_LASER) and not forceTear then
		local laser
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY) then
			laser = player:FireTechLaser(player.Position, LaserOffset.LASER_TECH1_OFFSET, projVel, true, true, player, damageMult)
			laser.CollisionDamage = laser.CollisionDamage + damageBonus
		end
		return laser
	-- TECH X ------------------------------
	elseif player:HasWeaponType(WeaponType.WEAPON_TECH_X) and not forceTear then
		local laser = player:FireTechXLaser(projOrigin, projVel, 70*chargeLevel, player, damageMult)
		laser.CollisionDamage = laser.CollisionDamage + damageBonus
		return laser
	-- Return the Lil Slugger sawblade if nothing else was fired ------------------------------
	elseif taintedSawblade then
		return taintedSawblade
	-- TEARS ------------------------------
	else
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE) then
			player.TearColor = lib.NullColor -- Override all other colors.
		end
		local tear = player:FireTear(projOrigin, projVel):ToTear() --Fire the tear
		tear.CollisionDamage = tear.CollisionDamage * damageMult + damageBonus
		local tearData = tear:GetData()
		tearData.originalTearVariant = tear.Variant
		local tearSprite = tear:GetSprite()
		if isPencilTear then
			if lib.SameColor(tear.Color, lib.SamaelTearColor) then
				tear.Color = lib.NullColor
			end
			if tear.Variant == TearVariant.BLUE then
				tear:ChangeVariant(TearVariant.BLOOD)
			end
			return tear
		elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_C_SECTION) then
			tear:ChangeVariant(50)
			tear.Scale = tear.Scale * 1.2
			tear:GetSprite():ReplaceSpritesheet(0, "gfx/characters/costumes_forgotten/fetus_tears.png")
			tear:GetSprite():LoadGraphics()
			tear:AddTearFlags(TearFlags.TEAR_FETUS)
			tear:AddTearFlags(TearFlags.TEAR_FETUS_BONE)
			if lib.SameColor(tear.Color, lib.SamaelTearColor) then
				tear.Color = lib.NullColor
			end
		elseif lib.HasTearFlag(tear, TearFlags.TEAR_BURSTSPLIT) then
			tearSprite:Load("gfx/samael_scythe_projectile.anm2", true)
			tearSprite:Play("Idle", 1)
			tearData.isFakeScytheTear = true
		elseif tearData.originalTearVariant == TearVariant.TOOTH
				or tearData.originalTearVariant == TearVariant.EGG
				or tearData.originalTearVariant == TearVariant.RAZOR
				or tearData.originalTearVariant == TearVariant.BLACK_TOOTH
				or (REPENTANCE and FiendFolio and lib.HasItem(player, FiendFolio.ITEM.COLLECTIBLE.EMOJI_GLASSES))
				or (REPENTANCE and REVEL and lib.HasItem(player, REVEL.ITEM.ICETRAY.id)) then
			tear.Scale = tear.Scale + 1
			if lib.SameColor(tear.Color, lib.SamaelTearColor) then
				tear.Color = lib.NullColor
			end
		else
			tear:ChangeVariant(kScytheTearVariant)
			if tearData.originalTearVariant == TearVariant.SCHYTHE then
				tear.Scale = tear.Scale * 0.5
			end
			tear.CollisionDamage = tear.CollisionDamage * kScytheProjectileDamageMult
			tearData.isSamaelTear = true
		end
		
		-- Change scythe tear visuals where appropriate.
		if REPENTANCE then
			if tearData.originalTearVariant == TearVariant.ROCK then
				mod:UpdateProjectileSprite(tear, "rock")
			elseif tearData.originalTearVariant == TearVariant.ICE or (REVEL and lib.HasItem(player, REVEL.ITEM.ICETRAY.id)) then
				mod:UpdateProjectileSprite(tear, "ice")
			elseif tearData.originalTearVariant == TearVariant.COIN or lib.HasTearFlag(tear, TearFlags.TEAR_MIDAS) then
				mod:UpdateProjectileSprite(tear, "gold")
			elseif tearData.originalTearVariant == TearVariant.SCHYTHE then
				mod:UpdateProjectileSprite(tear, "default")
			end
		end

		-- Fiddle with the Size and SpriteScale of the projectile to make it so that layered effects,
		-- such as Fire Mind's flames, appear at a reasonable size.
		if REPENTANCE and (tear.Variant == kScytheTearVariant or tearData.isFakeScytheTear) then
			tear.Scale = lib.Lerp(tear.Scale, 1, 0.5)
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
				tear.Scale = (tear.Scale * 0.5) + (tear.Scale * 0.7 * chargeLevel)
			end
			local scaleAdj = 2.5
			tear.SizeMulti = Vector(scaleAdj, scaleAdj)
			tearSprite.Scale = Vector(tear.Scale/scaleAdj, tear.Scale/scaleAdj)
			tearData.samaelTearSizeMulti = tear.SizeMulti
			tearData.samaelTearSpriteScale = tearSprite.Scale
		end
		
		if bloodClotOrChem then
			tear.Color = lib.BloodColor
		end
		
		tearSprite.FlipX = playerData.samaelScytheFlipped
		lib.AddTearFlag(tear, TearFlags.TEAR_PIERCING)
		
		if REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) then
			tear.CollisionDamage = tear.CollisionDamage * 2 + 2
		end
		return tear
	end
end

function mod:TearUpdate(tear)
	local tearData = tear:GetData()
	if tearData.isSamaelTear and tear.Variant == TearVariant.CUPID_BLOOD and tearData.ArrowheadBuffed then
		tear:ChangeVariant(kScytheTearVariant)
		if tearData.samaelTearSizeMulti then
			tear.SizeMulti = tearData.samaelTearSizeMulti
		end
		if tearData.samaelTearSpriteScale then
			tear:GetSprite().Scale = tearData.samaelTearSpriteScale
		end
		tear.Color = lib.BloodColor
	elseif tear.Variant ~= kScytheTearVariant and tearData.isFakeScytheTear then
		-- Haemoclaria tears use the scythe projectile sprite but aren't actually that variant.
		-- Need to keep their size where it should be.
		local scaleAdj = 2.5
		tear.SizeMulti = Vector(scaleAdj, scaleAdj)
		tear:GetSprite().Scale = Vector(tear.Scale/scaleAdj, tear.Scale/scaleAdj)
	elseif tear.Variant == kScytheTearVariant then
		-- If a scythe tear sticks to an enemy, change it back to its original variant (ie explosivo)
		if tear.StickTarget then
			tear:ChangeVariant(tearData.originalTearVariant)
			tear.StickDiff = tear.StickDiff * 0.5
		end
	end
	
	-- If a scythe tear changes variant (like with sticking tears above, or in the case of something
	-- like eye of belial) then play the scythe tear break animation and revert the scaling to that of
	-- a normal tear.
	if tearData.isSamaelTear and tear.Variant ~= kScytheTearVariant then
		mod:SamaelTearBreak(tear)
		tear.SizeMulti = Vector(1, 1)
		tear:GetSprite().Scale = Vector(1, 1)
		tearData.isSamaelTear = false
	end
	
	-- Immaculate heart orbiting tears.
	if tearData.samaelScytheOrbitStartDir then
		local player = tear.Parent:ToPlayer()
		local orbitDist = 66
		local orbitSpeed = 6
		local orbitStartPoint = tearData.samaelScytheOrbitStartDir:Resized(orbitDist)
		local targetPos = player.Position:__add(orbitStartPoint:Rotated((tear.FrameCount * orbitSpeed) % 360))
		tear.Velocity = lib.LerpVelocity(tear.Velocity, tear.Position, targetPos, 0.25)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.TearUpdate)

-- Fire a group of projectiles.
function mod:FireProjectileGroup(player, numTears, projOrigin, projVel, chargeLevel, isPencilBarrage, radial)
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	local rng = scythe:GetData().rng
	
	local sampleProj

	local groupArc = 4 + numTears*4
	if player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) or isPencilBarrage then
		groupArc = groupArc * 0.66
	end
	for j=0, numTears-1 do
		local projAngleOffset = 0
		if numTears > 1 then
			if radial then
				projAngleOffset = rng:RandomInt(360)
			else
				projAngleOffset = ( (groupArc/(numTears-1))*j ) - (groupArc/2)
			end
		end
		local projOriginOffset = projVel:Normalized():Rotated(projAngleOffset*10) * 10
		if (REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE))
				or player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) or radial then
			projOriginOffset = lib.ZeroVector
		end
		local pos = projOrigin:__add(projOriginOffset)
		local proj = mod:FireSingleProjectile(player, pos, projVel, projAngleOffset, chargeLevel, isPencilBarrage) --Fire the projectile
		-- Randomize spread and arc with monstro's lung
		if player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) or isPencilBarrage then
			proj.Velocity = proj.Velocity * (0.5 + rng:RandomFloat()*0.75)
			if proj:ToTear() then
				proj.Scale = 0.75 + rng:RandomFloat()*0.5
				proj.Height = -5 - rng:RandomFloat()*3
				proj.FallingSpeed = -10 - rng:RandomFloat()*10
				proj.FallingAcceleration = 0.5 + rng:RandomFloat()*1.0
				if isPencilBarrage then
					proj.FallingAcceleration =	proj.FallingAcceleration + 0.5
					proj.CollisionDamage = proj.CollisionDamage * 0.66
				end
			end
		end
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_CURSED_EYE) and not isPencilBarrage then
			local cursedEyeOffset = projVel:Resized(15)
			mod:FireSingleProjectile(player, pos + cursedEyeOffset, projVel, projAngleOffset, chargeLevel, false)
			mod:FireSingleProjectile(player, pos - cursedEyeOffset, projVel, projAngleOffset, chargeLevel, false)
		end
		-- Return one of the projectiles since its useful for some things.
		if j == 0 then
			sampleProj = proj
		end
	end
	
	return sampleProj
end

-- Fire all the projectiles that the player would fire in one "attack", counting multishot etc.
function mod:FireProjectiles(player, cardinalDirection, vectorDirection, chargeLevel, alternateOrigin)
	local playerData = player:GetData()
	local scythe = playerData.samaelScythe
	local rng = scythe:GetData().rng
	
	if playerData.samaelScytheCanFireAutomatically then
		playerData.samaelBloodClotFlag = not playerData.samaelBloodClotFlag
	else
		playerData.samaelBloodClotFlag = playerData.samaelScytheFlipped
	end
	
	if cardinalDirection == Direction.NO_DIRECTION then
		return nil
	end
	
	local projOrigin = Vector(player.Position.X, player.Position.Y)
	if alternateOrigin ~= nil then
		local angle = vectorDirection:GetAngleDegrees()
		if alternateOrigin:GetData().flipped then
			projOrigin = alternateOrigin.Position:__sub(Vector(0, -3):Rotated(angle))
		else
			projOrigin = alternateOrigin.Position:__sub(Vector(0, 3):Rotated(angle))
		end
	end
	local projSpeed = player.ShotSpeed * 10
	local projVel = vectorDirection * projSpeed
	
	-- If the player's own velocity should be added to their projectiles.
	if not lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED)
			and not player:HasWeaponType(WeaponType.WEAPON_KNIFE)
			and not player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) 
			and not (REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE)) then
		projVel = projVel:__add(player:GetTearMovementInheritance(projVel))
	end
	
	if player:HasWeaponType(WeaponType.WEAPON_KNIFE) then
		mod:UpdateKnifeLaser(player, projVel)
	end
	
	-- Cannon plays a distinct noise and has additional origin/velocity offsets.
	if playerData.samaelScytheType == "cannon" then
		if REPENTANCE then
			player:GetData().samaelScythe:PlaySound(SoundEffect.SOUND_BULLET_SHOT, 1, 0, false, 1.00) --Cannon firing sound
		else
			player:GetData().samaelScythe:PlaySound(SoundEffect.SOUND_BOSS1_EXPLOSIONS, 0.8, 0, false, 1.4) --Cannon firing sound
		end
		projOrigin = projOrigin:__add(lib.DirectionalVector(cardinalDirection, 30 * playerData.samaelScytheScale))
		if not player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) then
			projVel = projVel:__add(lib.DirectionalVector(cardinalDirection, 5))
		end
	elseif not player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) and not player:HasWeaponType(WeaponType.WEAPON_KNIFE) then
		player:GetData().samaelScythe:PlaySound(SoundEffect.SOUND_REDLIGHTNING_ZAP, 1, 0, false, 0.66) --Pitch-shifted tech firing sound
	end

	local numTears = mod:getNumTears(player)

	-- Fire the "normal" set of forward-facing projectiles
	local fullWizArc = 90
	local groups = 1 + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_THE_WIZ)
	local groupTears = math.ceil(numTears / groups) -- Shouldn't have to ceil this, but just to be safe

	for i=0, groups-1 do
		local groupAngle = 0
		if groups > 1 then
			groupAngle = ( (fullWizArc/(groups-1))*i ) - (fullWizArc/2)
		end
		local firstProj = mod:FireProjectileGroup(player, groupTears, projOrigin, projVel:Rotated(groupAngle), chargeLevel)
		if alternateOrigin ~= nil and player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) then
			alternateOrigin.Child = firstProj
		end
	end
	
	-- Extra knives (Mom's Knife OR Brimstone + Monstro's Lung)
	if player:HasWeaponType(WeaponType.WEAPON_KNIFE) or player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) then
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_MONSTROS_LUNG) then
			local numLungs = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MONSTROS_LUNG)
			local minExtra = 3 + 2 * (numLungs-1)
			local maxExtra = 5 + 3 * (numLungs-1)
			local numExtraProj = rng:RandomInt(maxExtra-minExtra+1) + minExtra
			for i=0, numExtraProj-1 do
				mod:FireSingleProjectile(player, projOrigin, projVel, rng:RandomInt(360), chargeLevel)
			end
		end
	end
	
	-- Eye Sore
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_SORE) then
		local numItems = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_EYE_SORE)
		local minExtra = numItems
		local maxExtra = 3 + 2 * (numItems-1)
		local numExtraProj = rng:RandomInt(maxExtra-minExtra+1) + minExtra
		for i=0, numExtraProj-1 do
			mod:FireSingleProjectile(player, projOrigin, projVel, rng:RandomInt(360), chargeLevel)
		end
	end
	
	-- Loki's Horns / Mom's Eye
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_LOKIS_HORNS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_EYE) then
		local triggerChance = lib.GetActivationChance(0.25, player.Luck, 15)
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_EYE) then
			triggerChance = lib.GetActivationChance(0.5, player.Luck, 5)
		end
		if rng:RandomFloat() <= triggerChance then
			local lokiTears = 1
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_MONSTROS_LUNG) then
				lokiTears = groupTears
			end
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_LOKIS_HORNS) then
				mod:FireProjectileGroup(player, lokiTears, projOrigin, projVel:Rotated(90), chargeLevel)
				mod:FireProjectileGroup(player, lokiTears, projOrigin, projVel:Rotated(-90), chargeLevel)
			end
			mod:FireProjectileGroup(player, lokiTears, projOrigin, projVel:Rotated(180), chargeLevel)
		end
	end
	
	-- Conjoined
	if player:HasPlayerForm(PlayerForm.PLAYERFORM_BABY) then
		mod:FireSingleProjectile(player, projOrigin, projVel, -45, chargeLevel)
		mod:FireSingleProjectile(player, projOrigin, projVel, 45, chargeLevel)
	end
end

-- Shatter animation/sound for Samael's default scythe projectiles.
function mod:SamaelTearBreak(tear)
	if tear.Variant ~= kScytheTearVariant and not tear:GetData().isSamaelTear then
		return nil
	end
	
	tear = tear:ToTear()
	
	if tear:GetData().visualType == "rock" then
		local eff = Isaac.Spawn(1000, EffectVariant.ROCK_POOF, 0, tear.Position, lib.ZeroVector, Isaac.GetPlayer(0)):ToEffect()
		sfxManager:Play(487, 0.75, 0, false, 1.0)
		eff.SpriteScale = Vector(1.75*tear.Scale, 1.5*tear.Scale)
		eff:GetSprite():SetFrame(1)
		eff:SetColor(tear.Color, -1, 1, false, false)
		
		local rng = RNG()
		rng:SetSeed(tear.InitSeed, 35)
		
		for i=0, 6 do
			local particle = Isaac.Spawn(1000, EffectVariant.TOOTH_PARTICLE, 1, tear.Position, Vector(rng:RandomFloat()*6 - 3, rng:RandomFloat()*6 - 3), Isaac.GetPlayer(0)):ToEffect()
			particle.Color = lib.NewColor(0.55, 0.45, 0.45) * tear.Color
		end
	else
		local eff = Isaac.Spawn(1000, EffectVariant.SCYTHE_BREAK, 0, tear.Position, lib.ZeroVector, Isaac.GetPlayer(0)):ToEffect()
		if tear:GetData().visualType == "ice" then
			sfxManager:Play(SoundEffect.SOUND_FREEZE_SHATTER, 0.75, 0, false, 1.0)
		elseif REPENTANCE then
			sfxManager:Play(492, 0.5, 0, false, 0.75)
		else
			sfxManager:Play(138, 0.4, 0, false, 1.2)
		end
		eff:SetColor(tear.Color, -1, 1, false, false)
		eff:GetSprite():ReplaceSpritesheet(0, "gfx/effects/samael_scythe_projectile_break.png")
		eff:GetSprite():LoadGraphics()
		eff:GetSprite():Play("Hit")
		eff.SpriteScale = Vector(tear.Scale, tear.Scale)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.SamaelTearBreak, EntityType.ENTITY_TEAR)

--------------------------------------------------
---- CHARGE BAR
--------------------------------------------------

function mod:ChargeBarRender(player)
	if not lib.IsSamael(player) then return end
	
	local renderMode = game:GetRoom():GetRenderMode()
	if renderMode == RenderMode.RENDER_WATER_REFRACT or renderMode == RenderMode.RENDER_WATER_REFLECT then return end
	
	if (lib.HasItem(player, CollectibleType.COLLECTIBLE_SOY_MILK) or lib.HasItem(player, CollectibleType.COLLECTIBLE_ALMOND_MILK))
			and not (lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) or (REPENTANCE and player:HasWeaponType(WeaponType.WEAPON_SPIRIT_SWORD))) then
		return
	end
	
	local pData = player:GetData()
	
	if not pData.samaelChargeBar then
		pData.samaelChargeBar = Sprite()
		pData.samaelChargeBar:Load("gfx/samael_chargebar.anm2", true)
		pData.samaelChargeBar.PlaybackSpeed = 0.5
		pData.samaelChargeBar:Play("Disappear", true)
		pData.samaelChargeBar:SetLastFrame()
	end
	
	local sprite = pData.samaelChargeBar
	local currentCharge = pData.samaelScytheCharge or 0
	local maxCharge = pData.samaelScytheMaxCharge
	if not maxCharge then return end
	local charge = (currentCharge / maxCharge) * 100.0
	local minChargeToRender = 20
	
	local showNeptunusCharge = lib.HasItem(player, CollectibleType.COLLECTIBLE_NEPTUNUS) and charge == 0 and pData.neptunusCharge
	if showNeptunusCharge then
		charge = pData.neptunusCharge
		minChargeToRender = 5
		local c = Color(1,1,1,1)
		c:SetColorize(0.0, 1.5, 2.5, 1)
		sprite.Color = c
	else
		sprite.Color = lib.NullColor
	end
	
	local roundedCharge = math.floor(charge)
	
	if roundedCharge >= 100 and (lib.CurrentAnimIs(sprite, "Charging") or lib.CurrentAnimIs(sprite, "Disappear")) then
		sprite:Play("StartCharged", true)
	elseif roundedCharge >= 100 and lib.CurrentAnimIs(sprite, "StartCharged") and not lib.CurrentAnimIs(sprite, "Charged") then
		sprite:Play("Charged", true)
	elseif charge >= minChargeToRender and roundedCharge < 100 then
		sprite:SetFrame("Charging", roundedCharge)
	elseif --[[charge == 0 and]] charge < 100 and not lib.CurrentAnimIs(sprite, "Disappear") then
		sprite:Play("Disappear", true)
	end
	
	local pos = player.Position + Vector(0, -65)
	sprite:Render(Isaac.WorldToScreen(pos), lib.ZeroVector, lib.ZeroVector)
	
	if not game:IsPaused() then
		sprite:Update()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, mod.ChargeBarRender)

--------------------------------------------------
---- BRIMSTONE SYNERGY / LASERS
--------------------------------------------------

function mod:AddLaserRing(player, parent, radius)
	parent = parent or player
	radius = radius or 50
	
	local laser = player:FireTechLaser(player.Position, 0, lib.ZeroVector, false, true, player, 1)
	laser.SubType = 3
	laser.PositionOffset = lib.ZeroVector
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_2)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_5)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_X) then
			laser:GetSprite():Load("gfx/007.009_brimtech.anm2", true)
		else
			laser:GetSprite():Load("gfx/007.001_thick red laser.anm2", true)
		end
		laser:GetSprite():Play("LargeRedLaser", true)
		laser:GetData().samaelBrimstoneRing = true
		laser:GetData().samaelBrimstoneRingRadius = radius
		laser.Radius = radius * 0.5
		laser:SetTimeout(10)
		laser.ParentOffset = Vector(0, -15)
		sfxManager:Play(SoundEffect.SOUND_LASERRING_WEAK)
	else
		laser.Radius = radius
		laser:SetTimeout(5)
		laser.ParentOffset = Vector(0, -15)
	end
	
	laser.CollisionDamage = player.Damage * 0.5
	laser.Parent = parent
	laser.DisableFollowParent = false
	
	laser:Update()
	
	return laser
end

mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, function(_, laser)
	if not laser:GetData().samaelBrimstoneRing then return end
	
	if laser.Parent then
		if laser.Parent:GetData().BrokenBottle then
			laser:Remove()
			return
		end
		laser.Velocity = laser.Parent.Velocity
		laser.Position = laser.Parent.Position
	end
	
	local startTime = 5
	local endTime = 8
	local removeTime = 18
	
	local targetRadius = laser:GetData().samaelBrimstoneRingRadius or 50
	
	local targetScale = Vector(0.5, 0.5)
	local scale = targetScale
	
	local frame = laser.FrameCount
	
	if laser.Timeout > -1 and frame > removeTime then
		laser:Remove()
		return
	elseif frame < startTime then
		laser.Radius = lib.Lerp(targetRadius * 0.5, targetRadius * 1.5, frame / startTime)
	elseif laser.Timeout > -1 and frame > endTime then
		laser.Radius = laser.Radius + 3
		scale = lib.Lerp(scale, lib.ZeroVector, (frame - endTime) / (removeTime - endTime))
	else
		laser.Radius = lib.Lerp(laser.Radius, targetRadius, 0.3)
	end
	
	laser.SpriteScale = scale
end)

function mod:SpawnBloodSplatter(hole, offset)
	local splat = Isaac.Spawn(1000, 2, 2, hole.Position:__add(offset:Rotated(hole.V1:GetAngleDegrees() - 90)), lib.ZeroVector, hole) --Blood effect
	splat:SetColor(hole.SplatColor, -1, 1, false, false)
	hole:PlaySound(SoundEffect.SOUND_MEATY_DEATHS, 0.85, 0, false, 1.0)
end

-- Update function for the brimstone "holes" sliced open by the scythe.
function mod:BrimstoneHoleUpdate(hole) 
	local lifeTime = kBrimstoneTime + 15
	if hole.Variant == 0 then
		lifeTime = 20
	end

	local player = hole.Parent:ToPlayer()
	local sprite = hole:GetSprite()
	local angle = hole.V1:GetAngleDegrees()
	
	local animPrefix = "Small"
	if hole.Variant == 1 then
		animPrefix = "Big"
	end
	
	if lib.CurrentAnimIs(sprite, "Init") then
		sprite:Play(animPrefix .. "Open", true)
		if hole.Variant == 0 then
			local hitboxLaserPos = hole.Position:__add(Vector(27, -3):Rotated(angle - 90))
			local hitboxLaser = Isaac.Spawn(EntityType.ENTITY_LASER, 1, 4, hitboxLaserPos, lib.ZeroVector, player):ToLaser()
			hitboxLaser.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
			hitboxLaser.Parent = hole
			hole.Child = hitboxLaser
			hitboxLaser.DisableFollowParent = true
			hitboxLaser:SetMaxDistance(45)
			hitboxLaser:SetTimeout(lifeTime - 5)
			hitboxLaser.Angle = angle + 90
			hitboxLaser.Visible = false
			hitboxLaser.CollisionDamage = player.Damage * 0.2
			hitboxLaser.Color = player.LaserColor
		end
	end

	if sprite:IsPlaying(animPrefix .. "Open") then
		if sprite:GetFrame() == 0 then
			hole:PlaySound(SoundEffect.SOUND_MEATY_DEATHS, 0.85, 0, false, 1.75)
		elseif sprite:GetFrame() == 3 then
			hole:PlaySound(SoundEffect.SOUND_MEATY_DEATHS, 0.85, 0, false, 1.2)
			local splat = Isaac.Spawn(1000, 2, 2, hole.Position:__add(Vector(0,4):Rotated(hole.V1:GetAngleDegrees() - 90)), lib.ZeroVector, hole) --Blood effect
			splat:SetColor(hole.SplatColor, -1, 1, false, false)
		end
		if sprite:IsEventTriggered("Fire") and hole.Variant == 1 then
			mod:FireProjectiles(player, hole:GetData().direction, hole.V1, hole:GetData().chargeLevel, hole)
			local splat = Isaac.Spawn(1000, 2, 3, hole.Position:__add(Vector(0,4):Rotated(hole.V1:GetAngleDegrees() - 90)), lib.ZeroVector, hole) --Blood effect
			splat:SetColor(hole.SplatColor, -1, 1, false, false)
		end
	elseif sprite:IsFinished(animPrefix .. "Open") then
		sprite:Play(animPrefix .. "Idle", true)
	elseif hole.FrameCount > lifeTime and not lib.CurrentAnimIs(sprite, animPrefix .. "Close") then
		sprite:Play(animPrefix .. "Close", true)
	elseif sprite:IsFinished(animPrefix .. "Close") then
		hole:Remove()
	end
	
	if hole.Child ~= nil and hole.Child:Exists() then
		if hole.Variant == 1 then
			-- Keep the laser and the hole's color in sync if the laser's color changes.
			hole:SetColor(hole.Child:GetColor(), -1, 1, false, false)
		elseif hole.Variant == 0 then
			-- Don't ask
			local n = 1
			if hole.FrameCount % 2 == 0 then
				n = -1
			end
			local hitboxLaser = hole.Child:ToLaser()
			hitboxLaser.Position = hole.Position:__add(Vector(27, -3*n):Rotated(angle - 90*n))
			hitboxLaser.Angle = angle + 90*n
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.BrimstoneHoleUpdate, kBrimstoneHoleId)

-- Spawns a brimstone hole for Samael's brimstone synergy.
function mod:SpawnBrimstoneHole(player, variant, chargeLevel, direction)
	local playerData = player:GetData()
	
	local offset
	if direction then
		offset = lib.DirectionalVector(direction, 70 * playerData.samaelScytheScale)
	else
		direction = playerData.samaelScytheLastCardinalDirection
		offset = playerData.samaelScytheLastVectorDirection * 70 * playerData.samaelScytheScale
	end

	local holePos = player.Position + Vector(0, -7) + offset + player.Velocity*6
	local hole = Isaac.Spawn(kBrimstoneHoleId, variant, 0, holePos, lib.ZeroVector, player):ToNPC()
	hole.Parent = player
	hole:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	hole.CanShutDoors = false
	hole.V1 = offset
	hole:GetData().direction = direction
	hole:GetData().flipped = playerData.samaelScytheFlipped
	hole:SetColor(player.LaserColor, -1, 1, false, false)
	hole.SplatColor = player.LaserColor
	hole.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
	hole.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
	hole.DepthOffset = 200
	hole:GetData().chargeLevel = chargeLevel
	
	if playerData.samaelScytheFlipped then
		hole:GetSprite().FlipX = true
		hole.SpriteRotation = 90 - offset:GetAngleDegrees()
	else
		hole.SpriteRotation = offset:GetAngleDegrees() - 90
	end
end

function mod:BrimstoneSwirlUpdate(swirl)
	if swirl.SpawnerType == EntityType.ENTITY_PLAYER and lib.IsSamael(swirl.SpawnerEntity:ToPlayer()) then
		swirl.DepthOffset = 250
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.BrimstoneSwirlUpdate, EffectVariant.BRIMSTONE_SWIRL)

-- Have to handle laser scaling manually in certain cases.
function mod:SamaelLaserUpdate(laser)
	if laser:GetData().isSamaelLaser then
		local width = laser:GetData().targetWidth
		if laser:GetData().isSamaelGodHead then
			lib.RemoveTearFlag(laser, TearFlags.TEAR_HOMING)
		elseif width and laser.Timeout ~= -1 then
			if laser.Timeout == 0 then
				width = 0
			elseif laser.Timeout < 10 then
				width = width * laser.Timeout/10
			end
		end
		if width then
			laser.SpriteScale = Vector(width, laser:GetData().targetWidth)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, mod.SamaelLaserUpdate)

function mod:FixLaserRingColor(laser)
	if laser:GetData().isTechXMeleeRing then
		--laser.Color = laser.SpawnerEntity:ToPlayer().LaserColor
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, mod.FixLaserRingColor)
mod:AddCallback(ModCallbacks.MC_POST_LASER_RENDER, mod.FixLaserRingColor)

--------------------------------------------------
---- MOM'S KNIFE SYNERGY
--------------------------------------------------

function mod:HasThrownKnifeScythes(player)
	local pData = player:GetData()
	if not pData.thrownKnifeScythes then
		pData.thrownKnifeScythes = {}
	end
	for k, v in pairs(pData.thrownKnifeScythes) do
		if v:Exists() then
			return true
		else
			pData.thrownKnifeScythes[k] = nil
		end
	end
	return false
end

local function ShouldHaveKnifeLaser(player)
	return game:GetRoom():GetFrameCount() > 0 and lib.IsSamael(player) and lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_KNIFE)
end

function mod:MomsKnifeHandler(player)
	local pData = player:GetData()
	
	if ShouldHaveKnifeLaser(player) and (not pData.samaelKnifeLaser or not pData.samaelKnifeLaser:Exists()) then
		local beam = player:FireTechLaser(player.Position, 0, lib.ZeroVector, false, false, player, 0)
		
		beam.CollisionDamage = 0
		
		-- Funny hack that makes the laser incapable of colliding with anything.
		local dummy = Isaac.Spawn(EntityType.ENTITY_SHOPKEEPER, 0, 0, lib.ZeroVector, lib.ZeroVector, nil)
		beam.Parent = dummy
		dummy:Remove()
		
		-- This stops the beam from automatically inheriting TearFlags from the player.
		beam.SpawnerEntity = nil
		
		beam.Timeout = -1	-- Beam will last indefinitely.
		beam.Mass = 0	-- Prevents the beam from "pushing" entities.
		
		beam:GetData().isSamaelKnifeLaser = true
		beam:GetData().samaelKnifeLaserPlayer = player
		pData.samaelKnifeLaser = beam
		
		beam:GetSprite():Load("gfx/memento_mori_link_laser.anm2", true)
		beam:GetSprite():LoadGraphics()
		beam:GetSprite():Play("Laser0")
		beam.Color = lib.InvisibleColor
		
		mod:UpdateKnifeLaser(player)
		beam:Update()
		
		lib.SuppressSound(SoundEffect.SOUND_REDLIGHTNING_ZAP_WEAK)
		lib.SuppressSound(SoundEffect.SOUND_REDLIGHTNING_ZAP)
		lib.SuppressSound(SoundEffect.SOUND_REDLIGHTNING_ZAP_STRONG)
		lib.SuppressSound(SoundEffect.SOUND_REDLIGHTNING_ZAP_BURST)
	elseif pData.samaelKnifeLaser then
		if not ShouldHaveKnifeLaser(player) then
			pData.samaelKnifeLaser:Remove()
			pData.samaelKnifeLaser = nil
		else
			-- Lock the beam at the player's position.
			pData.samaelKnifeLaser.Position = player.Position
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.MomsKnifeHandler)

local kKnifeLaserAcceptableTearFlags =
		TearFlags.TEAR_SPECTRAL
		| TearFlags.TEAR_HOMING
		| TearFlags.TEAR_BOOMERANG
		| TearFlags.TEAR_ORBIT
		| TearFlags.TEAR_CONTINUUM
		| TearFlags.TEAR_WIGGLE
		| TearFlags.TEAR_SPIRAL
		| TearFlags.TEAR_SQUARE
		| TearFlags.TEAR_BIG_SPIRAL
		| TearFlags.TEAR_TURN_HORIZONTAL

function mod:UpdateKnifeLaser(player, dir)
	local pData = player:GetData()
	local laser = pData.samaelKnifeLaser
	
	if not laser then
		mod:MomsKnifeHandler(player)
		laser = pData.samaelKnifeLaser
		if not laser then return end
	end
	
	laser:SetMaxDistance(player.TearRange * 2)
	laser.Angle = (dir or lib.ZeroVector):GetAngleDegrees()
	
	laser.TearFlags = player:GetTearHitParams(WeaponType.WEAPON_LASER).TearFlags & kKnifeLaserAcceptableTearFlags
	if not laser:HasTearFlags(TearFlags.TEAR_SPECTRAL) then
		laser:AddTearFlags(TearFlags.TEAR_SPECTRAL)
	end
end

function mod:KnifeLaserPostUpdate(laser)
	local data = laser:GetData()
	
	if not data.isSamaelKnifeLaser then return end
	
	local player = data.samaelKnifeLaserPlayer
	
	if not player or not ShouldHaveKnifeLaser(player) then
		laser:Remove()
		return
	end
	
	laser:GetSprite():Play("Laser0")
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, mod.KnifeLaserPostUpdate)

-- Update function for the Scythe-Knives
function mod:KnifeScytheUpdate(knife)
	local player = knife.Parent:ToPlayer()
	local pData = player:GetData()
	
	local data = knife:GetData()
	
	local laser = pData.samaelKnifeLaser
	
	if not laser or not laser:Exists() then
		knife:Remove()
		return
	end
	
	local path, pathLength = lib.GetLaserPath(laser)
	local distance = math.min(math.max(pathLength * 0.5, data.samaelScytheKnifeRange or player.TearRange) * (data.samaelScytheKnifeCharge or 1), pathLength)
	local n = knife.FrameCount
	local duration = 1.25 * distance / (data.samaelScytheKnifeSpeed or player.ShotSpeed * 10)
	local targetDist = distance * math.sin(math.pi * n / duration)
	local currentDist = data.samaelScytheKnifeDist or 0
	data.samaelScytheKnifeDist = targetDist
	local prevPos = lib.GetPosAtDistanceAlongLaserPath(path, currentDist)
	local targetPos = lib.GetPosAtDistanceAlongLaserPath(path, targetDist)--:Rotated(data.samaelScytheKnifeAngle or 0)
	if (data.samaelScytheKnifeAngle or 0) ~= 0 then
		prevPos = (prevPos - player.Position):Rotated(data.samaelScytheKnifeAngle or 0) + player.Position
		targetPos = (targetPos - player.Position):Rotated(data.samaelScytheKnifeAngle or 0) + player.Position
	end
	
	knife.Charge = data.samaelScytheKnifeCharge or knife.Charge
	
	if data.samaelBottle then
		if n / duration > 0.5 then
			data.samaelBottleFlipped = true
		end
		local angle = (prevPos - targetPos):GetAngleDegrees() + 90
		if data.samaelBottleFlipped then
			angle = angle - 180
		end
		knife.SpriteRotation = angle
	end
	
	knife.Position = lib.Lerp(knife.Position, targetPos, 0.33)
	knife.Velocity = lib.ZeroVector
	data.samaelScytheKnifeTargetPos = targetPos
	
	if data.BrokenBottle then
		pData.samaelBottleBroken = true
	elseif n >= duration then
		pData.thrownKnifeScythes[knife.InitSeed] = nil
		if not sfxManager:IsPlaying(SoundEffect.SOUND_SCAMPER) then
			sfxManager:Play(SoundEffect.SOUND_SCAMPER, 1, 0, false, 1)
		end
		knife:Remove()
		return
	end
	
	-- Mysterious Liquid
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MYSTERIOUS_LIQUID) and knife.FrameCount % 2 == 0 then
		local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PLAYER_CREEP_GREEN, 0, knife.Position, lib.ZeroVector, player, 0, 0):ToEffect()
		creep:SetDamageSource(EntityType.ENTITY_PLAYER)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.KnifeScytheUpdate, kScytheKnifeType)

function mod:KnifeScytheRender(knife)
	local data = knife:GetData()
	
	if data.samaelScytheKnifeBladeSprite then
		local pos = Isaac.WorldToScreen(knife.Position)
		data.samaelScytheKnifeBladeSprite:SetFrame(knife:GetSprite():GetAnimation(), knife:GetSprite():GetFrame())
		data.samaelScytheKnifeBladeSprite:Render(pos, lib.ZeroVector, lib.ZeroVector)
	end
	
	if not game:IsPaused() and data.samaelScytheKnifeTargetPos then
		knife.Position = lib.Lerp(knife.Position, data.samaelScytheKnifeTargetPos, 0.5)
		data.samaelScytheKnifeTargetPos = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_RENDER, mod.KnifeScytheRender, kScytheKnifeType)

-- Makes sure Samael's scythes can hit the stalactites in the Beast fight.
function mod:KnifeStalactiteFix(stalactite, collider)
	if stalactite.Variant == 1 and collider.Type == EntityType.ENTITY_KNIFE
			and (collider.SubType == kScytheKnifeType or collider.SubType == kScytheHitboxType) then
		stalactite:TakeDamage(collider.CollisionDamage, 0, EntityRef(collider), 0)
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.KnifeStalactiteFix, EntityType.ENTITY_BEAST)

-- Choose whether to set the given entity as the target for a homing knife.
-- My method of making knives work with CanShoot=false doesn't work with homing naturally.
function mod:MaybeUpdateKnifeTarget(knife, possibleTarget)
	local nonHomingTargetPos = knife:GetData().nonHomingTargetPos
	
	if nonHomingTargetPos == nil then
		return nil
	end
	if possibleTarget.Position:Distance(nonHomingTargetPos) > kScytheKnifeHomingRange then
		return nil
	end
	if knife.Target == nil or not knife.Target:Exists() or knife.Target.Position:Distance(nonHomingTargetPos) > possibleTarget.Position:Distance(nonHomingTargetPos) then
		knife.Target = possibleTarget
	end
end

--------------------------------------------------
---- LUDO
--------------------------------------------------

function mod:SpawnOneLudoScythe(player)
	local pData = player:GetData()
	
	local ludo = Isaac.Spawn(EntityType.ENTITY_KNIFE, 0, kLudoScytheType, player.Position, lib.ZeroVector, player):ToKnife()
	local ludoData = ludo:GetData()
	ludo.Parent = pData.samaelScythe
	ludo.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
	
	ludo:ClearEntityFlags(EntityFlag.FLAG_APPEAR) --Skip appear animations
	
	-- Load the graphics of the players' current scythe.
	local sprite = ludo:GetSprite()
	LoadThrownScytheSprite(player, sprite)
	sprite.FlipX = pData.samaelScytheFlipped
	ludo.Size = 35 * pData.samaelScytheScale
	ludo.SpriteScale = Vector(pData.samaelScytheScale, pData.samaelScytheScale)
	
	if pData.samaelScytheShouldColorHandle then
		ludo.Color = pData.samaelScytheColor or lib.NullColor
	else
		local blade = Sprite()
		LoadThrownScytheSprite(player, blade, true)
		blade.Scale = ludo.SpriteScale
		blade.FlipX = sprite.FlipX
		blade.Color = pData.samaelScytheColor or lib.NullColor
		ludoData.samaelLudoBladeSprite = blade
		ludo.Color = lib.NullColor
	end
	
	ludo.CollisionDamage = player.Damage
	if not lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_KNIFE) then
		ludo.CollisionDamage = ludo.CollisionDamage * 0.5
	end
	
	ludoData.LastVelocity = lib.DirectionalVector(pData.samaelScytheLastCardinalDirection, player.ShotSpeed * 12)
	ludoData.samaelScytheReturn = false
	
	return ludo
end

function mod:SpawnLudoScythes(player)
	local playerData = player:GetData()
	local n = mod:getNumTears(player, true)
	local master
	
	for i=0, n-1 do
		local ludo = mod:SpawnOneLudoScythe(player)
		ludo:GetData().numLudoScythes = n
		if i == 0 then
			master = ludo
		else
			ludo:GetData().masterLudoScythe = master
			ludo:GetData().childLudoScytheIndex = i
			ludo.Size = ludo.Size * 0.5
			ludo.SpriteScale = ludo.SpriteScale * 0.5
		end
	end
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_X)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE)
			or lib.HasItem(player, lib.Access(Retribution, "ITEMS", "TECHNOLOGY_OMICRON")) then
		local laser = mod:AddLaserRing(player, master, 60 * playerData.samaelScytheScale)
		laser.ParentOffset = lib.ZeroVector
		laser:SetTimeout(-1)
	end
end

function mod:LudoUpdate(ludo)
	local player = ludo.Parent.Parent:ToPlayer()
	local playerData = player:GetData()
	local ludoData = ludo:GetData()
	
	if ludoData.samaelLudoRemoveNextFrame then
		ludo:Remove()
		return
	end
	
	-- Child behavior
	if ludoData.childLudoScytheIndex then
		if not ludoData.masterLudoScythe or not ludoData.masterLudoScythe:Exists() then
			ludo:Remove()
			return
		end
		local parentPos = ludoData.masterLudoScythe.Position
		local n = ludoData.masterLudoScythe.FrameCount
		local orbitDist = 50 * playerData.samaelScytheScale
		local orbitSpeed = 3
		local orbitStartPoint = Vector(orbitDist,0):Rotated((360 / (ludoData.numLudoScythes-1)) * ludoData.childLudoScytheIndex)
		local targetPos = parentPos:__add(orbitStartPoint:Rotated((n * orbitSpeed) % 360))
		local targetVel = targetPos:__sub(ludo.Position)--:Resized(player.ShotSpeed * 10)
		ludo.Velocity = lib.Lerp(ludoData.LastVelocity, targetVel, 0.5)
		ludoData.LastVelocity = ludo.Velocity
		return
	end
	
	-- Parent behavior
	if Input.IsActionPressed(ButtonAction.ACTION_DROP, player.ControllerIndex) then
		ludoData.samaelScytheReturn = true
	end
	
	if (ludo.FrameCount > 60 or ludoData.samaelScytheReturn)
			and ludo.Position:Distance(player.Position) < 50 then
		sfxManager:Play(SoundEffect.SOUND_SCAMPER, 1, 0, false, 1)
		playerData.hideScythe = false
		playerData.samaelScytheFlipped = not playerData.samaelScytheFlipped
		--local rot = (ludo.Velocity:GetAngleDegrees() % 360) - 180
		local rot = (ludo.Velocity * Vector(1, -1)):GetAngleDegrees() - 90
		if playerData.samaelScytheFlipped then
			rot = -rot
		end
		playerData.samaelScythe.SpriteRotation = rot
		mod:UpdateScytheTargetRotation(player, true)
		ludoData.samaelLudoRemoveNextFrame = true
		return
	end
	
	local dir = player:GetAimDirection()
	if ludo.FrameCount < 15 then
		--dir = lib.DirectionalVector(playerData.samaelScytheLastCardinalDirection, 1)
		dir = ludoData.LastVelocity:Normalized()
	end
	
	local targetVel 
	if ludoData.samaelScytheReturn then
		targetVel = (player.Position - ludo.Position):Resized(player.ShotSpeed * 15)
	else
		targetVel = dir * player.ShotSpeed * 4
	end
	ludo.Velocity = lib.Lerp(ludoData.LastVelocity, targetVel, 0.2)
	ludoData.LastVelocity = ludo.Velocity
	ludoData.samaelScytheUpdated = true
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.LudoUpdate, kLudoScytheType)

function mod:LudoRender(ludo)
	local data = ludo:GetData()
	
	if data.samaelLudoBladeSprite then
		data.samaelLudoBladeSprite:SetFrame(ludo:GetSprite():GetAnimation(), ludo:GetSprite():GetFrame())
		local pos = Isaac.WorldToScreen(ludo.Position)
		data.samaelLudoBladeSprite.Scale = ludo.SpriteScale
		data.samaelLudoBladeSprite:Render(pos, lib.ZeroVector, lib.ZeroVector)
	end
	
	if  not game:IsPaused() and ludo:GetData().samaelScytheUpdated then
		ludo.Position = ludo.Position + ludo.Velocity
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_RENDER, mod.LudoRender, kLudoScytheType)

local function IsSamaelLudoScythe(tookDamage, damageSourceRef)
	return damageSourceRef and damageSourceRef.Entity and tookDamage:IsVulnerableEnemy() 
			and damageSourceRef.Entity.Type == EntityType.ENTITY_KNIFE and damageSourceRef.Entity.SubType == kLudoScytheType
end

function mod:PreLudoHits(tookDamage, damage, damageFlags, damageSourceRef)
	if IsSamaelLudoScythe(tookDamage, damageSourceRef) then
		local ludo = damageSourceRef.Entity:ToKnife()
		local data = ludo:GetData()
		local player = ludo.Parent.Parent:ToPlayer()
		-- Make it so that the ludo scythe only hits on a frequency equal to the players' fire rate.
		if data[tookDamage.InitSeed] and (ludo.FrameCount - data[tookDamage.InitSeed] < player.MaxFireDelay) then
			return false
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.PreLudoHits)

function mod:LudoHits(tookDamage, damage, damageFlags, damageSourceRef)
	if not IsSamaelLudoScythe(tookDamage, damageSourceRef) then return end
	
	local ludo = damageSourceRef.Entity:ToKnife()
	local player = ludo.Parent.Parent:ToPlayer()
	
	ludo:GetData()[tookDamage.InitSeed] = ludo.FrameCount
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MOMS_KNIFE) then
		ludo.TearFlags = player:GetTearHitParams(WeaponType.WEAPON_KNIFE, 1, 1, nil).TearFlags
	else
		ludo.TearFlags = player:GetTearHitParams(WeaponType.WEAPON_TEARS, 1, 1, nil).TearFlags
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.LudoHits)

--------------------------------------------------
---- MISC SYNERGIES
--------------------------------------------------

-- Used to let Isaac's Tears charge up when Samael swings his scythe.
function mod:IsaacsTearsFix(player)
	if not lib.HasItem(player, CollectibleType.COLLECTIBLE_ISAACS_TEARS) then
		return nil
	end
	
	local maxCharges = Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_ISAACS_TEARS).MaxCharges
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BATTERY) then
		maxCharges = maxCharges * 2
	end
	
	mod:TryIsaacsTearsFix(player, ActiveSlot.SLOT_PRIMARY, maxCharges)
	mod:TryIsaacsTearsFix(player, ActiveSlot.SLOT_SECONDARY, maxCharges)
end

function mod:TryIsaacsTearsFix(player, slot, maxCharges)
	if player:GetActiveItem(slot) == CollectibleType.COLLECTIBLE_ISAACS_TEARS then
		local currentCharge = player:GetActiveCharge(slot) + player:GetBatteryCharge(slot)
		if currentCharge < maxCharges then
			player:SetActiveCharge(currentCharge+1, slot)
		end
	end
end

function mod:DeadEyeHandler(player)
	if not lib.HasItem(player, CollectibleType.COLLECTIBLE_DEAD_EYE) then
		return nil
	end
	
	local playerData = player:GetData()
	
	if playerData.enemiesHitThisSwing > 0 then
		player:AddDeadEyeCharge()
	else
		player:ClearDeadEyeCharge()
	end
end

-- Fix some aspects of split tears from Parasite/Compound Fracture.
function mod:ParasiteFix(tear)
	if tear.FrameCount > 0 then return nil end
	if not tear.Parent and not tear.SpawnerEntity then return nil end
	
	local player = (tear.Parent or tear.SpawnerEntity):ToPlayer()
	
	-- TEAR_GLITTER_BOMB flag is normally unused for Tears/Knives, so I use it to help keep track of
	-- if a Tear was spawned via Samael's Scythe.
	if lib.IsSamael(player) and lib.HasTearFlag(tear, TearFlags.TEAR_GLITTER_BOMB) then
		lib.RemoveTearFlag(tear, TearFlags.TEAR_GLITTER_BOMB)
		
		tear.FallingAcceleration = 0
		tear.Height = math.min(tear.Height, -10)
		tear.FallingSpeed = math.min(tear.FallingSpeed, 0.5)
		
		if tear.Variant == TearVariant.BONE and not lib.HasTearFlag(tear, TearFlags.TEAR_BONE)
				and not lib.HasItem(player, CollectibleType.COLLECTIBLE_COMPOUND_FRACTURE) then
			tear:ChangeVariant(0)
			tear.Scale = tear.Scale * 0.66
		end
		
		if tear.Variant == TearVariant.BONE and lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.ParasiteFix)

-- Makes it so that Samael's scythe can knock Cube Baby around.
function mod:HitCubeBaby(cubeBaby, collider)
	if collider.Type == EntityType.ENTITY_KNIFE and collider.SubType == kScytheHitboxType
			and not collider:GetData()[cubeBaby.InitSeed] then
		collider:GetData()[cubeBaby.InitSeed] = true
		local player = collider.SpawnerEntity:ToPlayer()
		cubeBaby.Velocity = cubeBaby.Position:__sub(player.Position):Resized(20)
		sfxManager:Play(SoundEffect.SOUND_FREEZE_SHATTER, 0.75, 0, false, 1.33)
	end
end

-- Makes it so that Samael's scythe can hit frozen enemies.
function mod:HitFrozenEnemy(enemy, collider)
	if collider.Type == EntityType.ENTITY_KNIFE and collider.SubType == kScytheHitboxType
			and not collider:GetData()[enemy.InitSeed] then
		collider:GetData()[enemy.InitSeed] = true
		local player = collider.SpawnerEntity:ToPlayer()
		enemy.Velocity = enemy.Position:__sub(player.Position):Resized(20)
		sfxManager:Play(SoundEffect.SOUND_FREEZE_SHATTER, 0.75, 0, false, 1.33)
	end
end

-- Makes it so that Samael's scythe can hit bombs away.
function mod:HitBomb(bomb, collider)
	if bomb.Variant ~= BombVariant.BOMB_ROCKET and bomb.Variant ~= BombVariant.BOMB_ROCKET_GIGA 
			and collider.Type == EntityType.ENTITY_KNIFE and collider.SubType == kScytheHitboxType
			and not collider:GetData()[bomb.InitSeed] then
		collider:GetData()[bomb.InitSeed] = true
		local player = collider.SpawnerEntity:ToPlayer()
		bomb.Velocity = bomb.Position:__sub(player.Position):Resized(10)
		sfxManager:Play(SoundEffect.SOUND_SCAMPER, 0.78, 0, false, 0.8)
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_BOMB_COLLISION, mod.HitBomb)

-- Allows Samael's scythe to repel Mother's Fistula attack.
function mod:HitFistula(fistula, collider)
	if fistula.Variant ~= 100 then
		return nil
	end
	if collider.Type == EntityType.ENTITY_KNIFE and collider.SubType == kScytheHitboxType
			and not collider:GetData()[fistula.InitSeed] then
		collider:GetData()[fistula.InitSeed] = true
		local player = collider.SpawnerEntity:ToPlayer()
		local originalLength = fistula.TargetPosition:Length()
		fistula.Velocity = fistula.Position:__sub(player.Position):Resized(15)
		fistula.TargetPosition = fistula.Velocity:Resized(originalLength)
		sfxManager:Play(SoundEffect.SOUND_MEATY_DEATHS, 0.75, 0, false, 1.15)
	end
end

-- Giant Cell 1
-- Turns Minisaacs spawned from Samael into subtype 99 (the Forgotten's ones that have a melee attack).
function mod:Minisaacs(minisaac)
	if lib.IsSamael(minisaac.SpawnerEntity) and not mod:IsSpecialMinisaac(minisaac) then
		if minisaac.SubType ~= 99 then
			minisaac.SubType = 99
		end
	end
end

-- Giant Cell 2
-- Replaces the visual of the Minisaac's melee attack with a lil scythe.
function mod:MinisaacScythe(entity)
	if entity.SpawnerType == EntityType.ENTITY_FAMILIAR and entity.SpawnerEntity.Variant == FamiliarVariant.MINISAAC 
			and entity.SpawnerEntity.SubType == 99 and lib.IsSamael(entity.SpawnerEntity.SpawnerEntity)
			and not mod:IsSpecialMinisaac(entity) then
		entity:GetSprite():Load("gfx/samael_minisaac_scythe.anm2", true)
	end
end

if REPENTANCE then
	mod:AddCallback(ModCallbacks.MC_PRE_FAMILIAR_COLLISION, mod.HitCubeBaby, FamiliarVariant.CUBE_BABY)
	mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.HitFrozenEnemy, EntityType.ENTITY_FROZEN_ENEMY)
	mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.HitFistula, EntityType.ENTITY_MOTHER)
	mod:AddPriorityCallback(ModCallbacks.MC_FAMILIAR_UPDATE, CallbackPriority.LATE, mod.Minisaacs, FamiliarVariant.MINISAAC)
	mod:AddCallback(ModCallbacks.MC_POST_KNIFE_INIT, mod.MinisaacScythe, 4)
end

function mod:ExplosionUpdate(explosion)
	if explosion.FrameCount <= 1 and lib.SameColor(explosion.Color, lib.SamaelTearColor) then
		explosion.Color = lib.NullColor
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.ExplosionUpdate, EffectVariant.BOMB_EXPLOSION)

-- Evil Eye
function mod:MaybeSpawnEvilEye(player, dir, rng, attempts)
	if not lib.HasItem(player, CollectibleType.COLLECTIBLE_EVIL_EYE) then return nil end
	
	attempts = attempts or 1
	
	for i=0, attempts-1 do
		if rng:RandomFloat() < lib.GetCappedActivationChance(0.035, 0.1, player.Luck, 20) then
			local eye = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.EVIL_EYE, 0, player.Position, dir:Resized(player.ShotSpeed*3), player)
			eye.Parent = player
			return eye
		end
	end
	
	return nil
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if REVEL and REVEL.BurningBush and REVEL.BurningBush.HasWeapon and not mod.DidBurningBushThing then
		mod.DidBurningBushThing = true
		local origFunc = REVEL.BurningBush.OriginalHasWeapon or REVEL.BurningBush.HasWeapon
		if not REVEL.BurningBush.OriginalHasWeapon then
			REVEL.BurningBush.OriginalHasWeapon = origFunc
		end
		REVEL.BurningBush.HasWeapon = function(player)
			local pType = player:GetPlayerType()
			local data = player:GetData()
			if pType == lib.SamaelId then
				return true
			elseif pType == lib.TaintedSamaelId or pType == lib.OtherSamaelId then
				return false
			end
			return origFunc(player)
		end
	end
end)

--------------------------------------------------
---- OTHER STUFF
--------------------------------------------------

-- Cache update function for stats
function mod:cacheUpdate(player, cacheFlag)
	local playerData = player:GetData()
	
	if lib.IsSamael(player) then
		mod:MaybeUpdateCostumes(player)
	
		if playerData.samaelScythe ~= nil and playerData.samaelScythe:Exists() then
			mod:UpdateScytheType(player)
			mod:UpdateNumScythes(player)
		end
		
		if cacheFlag & CacheFlag.CACHE_RANGE ~= 0 then
			-- Fixes some issues where tears manually spawned for Samael might hit the floor on init.
			if player.TearHeight > -7.5 then
				player.TearHeight = -7.5
			end
		end
		
		if cacheFlag & CacheFlag.CACHE_DAMAGE ~= 0 then
			player.Damage = player.Damage + 0.5
			
			local hasBelialEffect = lib.HasItemEffect(player, CollectibleType.COLLECTIBLE_BOOK_OF_BELIAL)
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_BLOOD_OF_THE_MARTYR or CollectibleType.COLLECTIBLE_BLOOD_MARTYR)
					or hasBelialEffect or playerData.hadBelialEffect then
				player:AddCacheFlags(CacheFlag.CACHE_TEARCOLOR)
				playerData.hadBelialEffect = hasBelialEffect
			end
		end
		
		if cacheFlag & CacheFlag.CACHE_SPEED ~= 0 then
			local change = 0
			
			if lib.IsTaintedSamael(player) or lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) or player:HasTrinket(mod.ITEMS.SAMAELS_FEATHER) then
				change = 0.14
			else
				change = -0.15
			end
			
			if playerData.wraithActive then
				change = change + 0.29
			end
			
			player.MoveSpeed = player.MoveSpeed + change
		end
		
		if cacheFlag & CacheFlag.CACHE_FIREDELAY ~= 0 then
			if lib.IsSamael(player) then
				player.MaxFireDelay = lib.FireDelayIgnoringWeaponType(player)
			end
			if lib.IsTaintedSamael(player) then
				local baseMult = 2.05
				local baseFireDelay = 10
				local fireDelayForNoMult = 40
				if player.MaxFireDelay > baseFireDelay then
					local mult = math.max(lib.Lerp(baseMult, 1.0, (player.MaxFireDelay - baseFireDelay) / (fireDelayForNoMult - baseFireDelay)), 1.0)
					player.MaxFireDelay = player.MaxFireDelay * mult
				elseif player.MaxFireDelay > 0 then
					player.MaxFireDelay = player.MaxFireDelay * baseMult
				end
			end
			if playerData.samaelEpiphoraCounter then
				player.MaxFireDelay = player.MaxFireDelay - math.ceil(player.MaxFireDelay	* (math.min(playerData.samaelEpiphoraCounter, 8)/16))
			end
		end
		
		-- Decide the correct tear color to use.
		-- Samael has a purplish default tear color but I want that to be completely overridden if any
		-- other items change the tear color.
		-- Also, some items, like Haemolacria, don't actually change the player's tear color, but
		-- thankfully they DO have the tearcolor flag so we can account for them here.
		if cacheFlag & CacheFlag.CACHE_TEARCOLOR ~= 0 then
			-- Another note: Some TearColor-changing items like Fire Mind don't actually seem to change
			-- the RGB or offset values of the Color class. I think it changes some kind of internal value
			-- not exposed to the API, because the resulting Color still applies the correct color, even
			-- though if you check the values are still (1,1,1,1,0,0,0).
			-- That's part of why I end up doing these manual checks.
			local hasTearColorChangingItem = false
			
			--local lastOfficialItemId = 729
			local tearColorItemIds = {3, 6, 104, 115, 132, 182, 221, 257, 259, 310, 317, 330, 336}
			local repTearColorItemIds = {561, 572}
			
			for _,id in ipairs(tearColorItemIds) do
				if lib.HasItem(player, id) then
					hasTearColorChangingItem = true
				end
			end
			if not hasTearColorChangingItem and REPENTANCE then
				for _,id in ipairs(repTearColorItemIds) do
					if lib.HasItem(player, id) then
						hasTearColorChangingItem = true
					end
				end
			end
			
			if not hasTearColorChangingItem and lib.SameColor(player.TearColor, lib.NullColor) then
				if lib.HasItemEffect(player, CollectibleType.COLLECTIBLE_BOOK_OF_BELIAL)
						or lib.HasItem(player, CollectibleType.COLLECTIBLE_HAEMOLACRIA)
						or lib.HasItem(player, CollectibleType.COLLECTIBLE_BLOOD_OF_THE_MARTYR or CollectibleType.COLLECTIBLE_BLOOD_MARTYR)
						or lib.HasItem(player, CollectibleType.COLLECTIBLE_MAW_OF_THE_VOID or CollectibleType.COLLECTIBLE_MAW_OF_VOID)
						or lib.HasItem(player, CollectibleType.COLLECTIBLE_SMALL_ROCK) then
					-- Blood tears.
					player.TearColor = lib.BloodColor
				elseif lib.HasItemEffect(player, CollectibleType.COLLECTIBLE_TELEPATHY_BOOK) then
					player.TearColor = lib.NewColor(0.4, 0.15, 0.38, 1, 0.28, 0, 0.45)
				elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_SCORPIO) then
					player.TearColor = lib.NewColor(0.3, 0.85, 0.2)
				elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_STRANGE_ATTRACTOR) then
					player.TearColor = lib.NewColor(0.7, 0.7, 0.7)
				else
					player.TearColor = lib.SamaelTearColor
				end
			end
		end
		
		if cacheFlag & CacheFlag.CACHE_FLYING ~= 0 then
			if player:HasTrinket(mod.ITEMS.SAMAELS_FEATHER) or (not lib.IsTaintedSamael(player) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT)) or lib.IsChallengeSamael(player) then
				player.CanFly = true
			end
		end
		
		playerData.samaelScytheMaxCharge = mod:calcChargeTime(player)
	elseif playerData.wraithActive and cacheFlag & CacheFlag.CACHE_SPEED ~= 0 then
		player.MoveSpeed = player.MoveSpeed + 0.4
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.cacheUpdate)

-- Entity for playing the death anim
--[[function mod:SpecialAnimEntityFunc(npc)
	local sprite = npc:GetSprite()
	if (not npc.Parent or not npc.Parent:Exists()) and npc:GetData().isIllusion then
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, npc.Position, lib.ZeroVector, nil):ToEffect()
		poof.Color = lib.NewColor(0,0,0,0.5)
		npc:Remove()
	end
	if sprite:IsPlaying("Death") and sprite:IsEventTriggered("Blood") then --Trigger the blood splatter effect for death animation
		Isaac.Spawn(1000, SoundEffect.SOUND_MEATY_DEATHS, 0, npc.Position, lib.ZeroVector, npc)
		npc:PlaySound(SoundEffect.SOUND_DEATH_BURST_LARGE, 1, 0, false, 1)
		npc:MakeSplat(5.0)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SpecialAnimEntityFunc, kSpecialAnimEntity)]]

-- Lost contact
function mod:MaybeBlockProjectileWithKnife(knife, proj)
	if proj ~= nil and lib.HasTearFlag(knife, TearFlags.TEAR_SHIELDED)
			and knife.Position:Distance(proj.Position) < (knife.Size + proj.Size) then
		proj:Die()
	end
end

-- The only function that loops through every entity in the room.
-- I tried to limit this mod to only do this once per update, so this handles a handful of things.
-- I think that doing this once per update is better than multiple entities each doing their own
-- FindInRadius calls every update. Hopefully.
function mod:RoomEntitiesLoop()
	local numEnemies = 0
	local enemies = {}
	
	local numKnives = 0
	local knives = {}
	
	local numProjectiles = 0
	local projectiles = {}
	
	for _, entity in pairs(Isaac.GetRoomEntities()) do
		-- Enemy
		if entity:IsVulnerableEnemy() then
			for i=0, numKnives-1 do
				if knives[i].SubType == kScytheKnifeType and lib.HasTearFlag(knives[i], TearFlags.TEAR_HOMING) then
					mod:MaybeUpdateKnifeTarget(knives[i], entity:ToNPC())
				end
			end
			enemies[numEnemies] = entity:ToNPC()
			numEnemies = numEnemies + 1
		-- Samael's Scythe-Knives and Scythe Hitboxes
		elseif entity.Type == EntityType.ENTITY_KNIFE and
				(entity.SubType == kScytheKnifeType or entity.SubType == kScytheHitboxType) then
			local shouldAdd = false
			if entity.SubType == kScytheKnifeType and lib.HasTearFlag(entity:ToKnife(), TearFlags.TEAR_HOMING) then
				for i=0, numEnemies-1 do
					mod:MaybeUpdateKnifeTarget(entity:ToKnife(), enemies[i])
				end
				shouldAdd = true
			end
			if lib.HasTearFlag(entity:ToKnife(), TearFlags.TEAR_SHIELDED) then
				for i=0, numProjectiles-1 do
					mod:MaybeBlockProjectileWithKnife(entity:ToKnife(), projectiles[i])
				end
				shouldAdd = true
			end
			if shouldAdd then
				knives[numKnives] = entity:ToKnife()
				numKnives = numKnives + 1
			end
		-- Enemy projectiles
		elseif entity.Type == EntityType.ENTITY_PROJECTILE then
			for i=0, numKnives-1 do
				if lib.HasTearFlag(knives[i], TearFlags.TEAR_SHIELDED) then
					mod:MaybeBlockProjectileWithKnife(knives[i], entity:ToProjectile())
				end
			end
			projectiles[numProjectiles] = entity:ToProjectile()
			numProjectiles = numProjectiles + 1
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.RoomEntitiesLoop)

-- Update a shopkeeper's graphics if it should be headless.
-- Allows the effect to persist between rooms and even after reloading the game.
function mod:ShopkeeperUpdate(shopkeeper)
	local deadShopkeepers = mod:GetFloorData("DeadShopkeepers")
	if deadShopkeepers["" .. shopkeeper.InitSeed] then
		local sprite = shopkeeper:GetSprite()
		local filename = sprite:GetFilename()

		if filename == "gfx/017.001_Shopkeeper.anm2" and not lib.CurrentAnimIs(sprite, "Shopkeeper 9") then
			sprite:ReplaceSpritesheet(0, "gfx/effects/headless_shopkeepers.png")
		elseif filename == "gfx/017.004_Special Shopkeeper.anm2" then
			sprite:ReplaceSpritesheet(0, "gfx/effects/special_headless_shopkeepers.png")
		elseif REPENTANCE then
			local special = string.find(string.lower(sprite:GetAnimation()), "nickel") or string.find(string.lower(sprite:GetFilename()), "nickel")
			sprite:Load(special and "gfx/017.004_Special Shopkeeper.anm2" or "gfx/017.001_Shopkeeper.anm2", false)
			sprite:ReplaceSpritesheet(0, special and "gfx/effects/special_headless_shopkeepers.png" or "gfx/effects/headless_shopkeepers.png")
			sprite:Play(sprite:GetDefaultAnimation(), true)
		else
			return
		end
		sprite:LoadGraphics()
		shopkeeper:GetData().headless = true
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_NEW_ROOM, CallbackPriority.LATE, function()
	for _, shopkeeper in pairs(Isaac.FindByType(EntityType.ENTITY_SHOPKEEPER)) do
		mod:ShopkeeperUpdate(shopkeeper)
	end
end)

-- Why did I do this
local MODDED_BEHEAD_BLACKLISTS = {
	["gfx/rare shopkeeps/sitting_buddies.anm2"] = {
		"Rare 5",
		"Common 10",
		"Rare 7",
		"Uncommon 10",
	},
	["gfx/rare shopkeeps/sitting_buddies_pt2.anm2"] = {
		"Uncommon 15",
		"Rare 12",
	},
	["gfx/Even More Shopkeepers/MoreSittingBois.anm2"] = {
		"MasterShake",
		"Clotty",
		"Beggar",
	},
	["gfx/Even More Shopkeepers/MoreSittingBois2.anm2"] = {
		"NTEyes",
	},
	["gfx/Even More Shopkeepers/MoreSittingBois3.anm2"] = {
		"SimpleAs",
		"ImaginePlayingGamesOnLinux",
		"Eevee",
	},
	["gfx/Even More Shopkeepers/MoreSittingBois4.anm2"] = {
		"GUNGOD",
		"McDonaldsSprite",
		"ToyDino",
		"Strongbad",
		"MamaLuigi",
		"CorporateStyleKeeper",
		"SmileGuy",
		"AngryBird",
		"Ena",
		"Olexa",
		"JapaneseHomer",
		"CalvinPee",
	},
	["gfx/Even More Shopkeepers/MoreSittingBois5.anm2"] = {
		"JustaflyKeeper",
		"HushyKeeper",
		"RaymanKeeper",
		"Beartato",
		"JunoEnaOC",
		"WalleMO",
		"KirbySqueakSquad",
		"Dead",
		"MrWabs",
	},
	["gfx/Even More Shopkeepers/MoreSittingBois6.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/pablonchas/PablochasReiImpactKeepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/pablonchas/PablonchasRegularKeepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/CainAnonKeepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/Carlos-SpwvKeepers.anm2"] = {"VibRibbon"},
	["gfx/Even More Shopkeepers/Other Artists/ComicKeepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/GargfKeepers.anm2"] = {
		"NickelUltraGreed",
		"SuperKeeperBoy",
		"MomWithKnife",
		"NickelClippy",
		"ChristmasCake",
		"Robot",
		"Dad",
	},
	["gfx/Even More Shopkeepers/Other Artists/Jammerjab1Keepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/Nuclear808Keepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/OKIWontKeepers.anm2"] = {},
	["gfx/Even More Shopkeepers/Other Artists/SmellyOne2-Keepers.anm2"] = {"2SmellyOne2"},
	["gfx/Even More Shopkeepers/Other Artists/Snek-FlippinEgg Keepers Sitting.anm2"] = {
		"Becomefumo",
		"Lilstar",
		"BlueHairLady",
		"PlaceBombHere",
		"Kindalikecatmario",
		"Ant",
		"DeliKeeper",
		"Glep",
		"EggEggEgg",
		"PataponKeeper",
		"Trapinch",
		"RobloxOof",
		"AwaySign",
		"Malkuth",
		"RiskOfRainGuy",
		"bigfootjinx",
	},
	["gfx/Even More Shopkeepers/Other Artists/TribalTiger Keepers.anm2"] = {
		"HoloKnightButActuallyGood",
		"AMOGUSSUSSY",
		"ShakledGhost",
		"BRICKBYBRICK",
	},
	["gfx/Even More Shopkeepers/Other Artists/XorelKeepers.anm2"] = {
		"ErrorKeeper",
		"BusRiders",
		"BurgerBrawl",
	},
}
for _, tab in pairs(MODDED_BEHEAD_BLACKLISTS) do
	for _, anim in ipairs(tab) do
		tab[anim] = true
	end
end

local function AllowBeheadModdedShopkeeper(shopkeeper)
	if not REPENTANCE then return end
	
	local sprite = shopkeeper:GetSprite()
	local filename = sprite:GetFilename():gsub("Nickel ", ""):gsub("Nickel", "")
	
	local blacklist = MODDED_BEHEAD_BLACKLISTS[filename]
	local anim = sprite:GetAnimation():gsub("^Nickel ", ""):gsub("^Nickel", "")
	
	if not blacklist or blacklist[anim] then
		return false
	end
	return true
end

-- Try to behead a shopkeeper.
-- Purely just a cute little visual interaction, though it has a chance to drop a penny.
function mod:TryDecapitateShopkeeper(shopkeeper, rng)
	if shopkeeper:GetData().headless then return end

	local sprite = shopkeeper:GetSprite()
	local filename = sprite:GetFilename()

	if (filename == "gfx/017.001_Shopkeeper.anm2" and not lib.CurrentAnimIs(sprite, "Shopkeeper 9"))
			or filename == "gfx/017.004_Special Shopkeeper.anm2"
			or AllowBeheadModdedShopkeeper(shopkeeper) then
		local deadShopkeepers = mod:GetFloorData("DeadShopkeepers")
		deadShopkeepers["" .. shopkeeper.InitSeed] = true
		mod:ShopkeeperUpdate(shopkeeper)
		local vec = Vector(2,2)
		for i=0, 4 do
			local vel = (vec * (0.25 + 0.75 * rng:RandomFloat())):Rotated(rng:RandomInt(360))
			Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLOOD_PARTICLE, 1,
					shopkeeper.Position:__add(Vector(0, -10)), vel, shopkeeper, 0, 0):ToEffect()
		end
		sfxManager:Play(30, 0.8, 0, false, 1.0)
		if rng:RandomInt(2) == 0 then
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COIN, CoinSubType.COIN_PENNY, 
					Isaac.GetFreeNearPosition(shopkeeper.Position, 5), Vector(2,0):Rotated(rng:RandomInt(360)), nil)
		end
	end
end

--------------------------------------------------
---- ISAAC / ??? / LAMB DEATH ANIMATION
--------------------------------------------------

function mod:IsaacDeath1(entity)
	if entity.Variant > 1 then return nil end
	
	if entity:GetData().samael then
		entity.HitPoints = 0
	end
	
	if entity.HitPoints > 0 then
		return
	end
	
	local player
	for _, p in pairs(lib.GetPlayers()) do
		if not player and lib.IsSamael(p) then
			player = p
		end
	end
	if not player then return nil end
	
	local playerData = player:GetData()

	local newAnim
	
	if entity.Type == EntityType.ENTITY_ISAAC then
		newAnim = "gfx/samael_isaac_death.anm2"
	elseif entity.Type == EntityType.ENTITY_THE_LAMB then
		newAnim = "gfx/samael_lamb_death.anm2"
	else
		return
	end
	
	local sprite = entity:GetSprite()
	
	if sprite:GetFilename() ~= newAnim then
		sprite:Load(newAnim, true)
		if entity.Type == EntityType.ENTITY_ISAAC then
			if entity.Variant == 1 then
				sprite:ReplaceSpritesheet(0, BetterMonsters and "gfx/effects/bluebaby_samael_death_original.png" or "gfx/bosses/classic/boss_078_bluebaby.png")
				sprite:ReplaceSpritesheet(1, "gfx/effects/bluebaby_samael_death.png")
				sprite:ReplaceSpritesheet(3, "gfx/effects/bluebaby_samael_death.png")
				sprite:LoadGraphics()
			elseif entity.Variant == 0 and BetterMonsters then
				sprite:ReplaceSpritesheet(0, "gfx/effects/isaac_samael_death_original.png")
				sprite:LoadGraphics()
			end
			
			if BetterMonsters and REPENTANCE then
				game:ShowHallucination(30, 0)
			end
		end
		
		entity:GetData().samael = player
		
		if entity.Type == EntityType.ENTITY_THE_LAMB then
			entity.FlipX = false
			sprite.FlipX = false
		end
		
		if REPENTANCE then
			local trailStartPos = entity.Position:__add(Vector(400, -135))
			if entity.Type == EntityType.ENTITY_THE_LAMB then
				trailStartPos = trailStartPos:__add(Vector(0, -20))
			end
			entity:GetData().samaelTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SPRITE_TRAIL, 0, trailStartPos, lib.ZeroVector, nil):ToEffect()
			entity:GetData().samaelTrail:GetSprite().Color = lib.NewColor(1, 0, 1, 0.5) -- sets the color of the trail
			entity:GetData().samaelTrail.MinRadius = 0.15
			entity:GetData().samaelTrail.SpriteScale = Vector(1.5,1.5)
		end
		
		player.Visible = false
		player.ControlsEnabled = false
		player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_PLAYERONLY
		playerData.hideScythe = true
		playerData.doingIsaacKillAnimation = 180
		
		local wraith = Isaac.Spawn(EntityType.ENTITY_EFFECT, kWraithEffect, 1, player.Position, lib.ZeroVector, player):ToEffect() --Spawn the special animations entity
		wraith.Parent = player
		wraith:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		
		if lib.IsChallengeSamael(player) then
			local sprite = wraith:GetSprite()
			for layer = 9, 11 do
				sprite:ReplaceSpritesheet(layer, "gfx/characters/samael_b/character_samael_i.png")
			end
			sprite:LoadGraphics()
		end
		
		if lib.IsTaintedSamael(player) then
			wraith:GetSprite():Play("Special1B", 1)
		else
			wraith:GetSprite():Play("Special1", 1)
			wraith:GetSprite().Color = lib.NewColor(1.0,0.75,1.0, 1.0)
			sfxManager:Play(33, 1, 0, false, 1.1)
			--Black poof effect
			local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, player.Position, lib.ZeroVector, player):ToEffect()
			poof:GetSprite().Color = lib.NewColor(0,0,0,0.66,0,0,0)
		end
		
		wraith.Scale = player.SpriteScale.X
		wraith.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		wraith.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
		
		wraith:GetData().isaacBoss = entity
	end
	
	if sprite:GetAnimation() ~= "Death" then
		sprite:Play("Death", true)
	end
	
	if sprite:IsFinished("Death") then
		entity:Kill()
	end
	
	entity.State = 3
	entity.Velocity = lib.ZeroVector
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.IsaacDeath1, EntityType.ENTITY_ISAAC)
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.IsaacDeath1, EntityType.ENTITY_THE_LAMB)

mod:AddPriorityCallback(ModCallbacks.MC_PRE_NPC_UPDATE, -1, function(_, entity)
	if entity.Variant == 1 and BetterMonsters then
		mod:IsaacDeath1(entity)
	end
end, EntityType.ENTITY_ISAAC)

function mod:IsaacDeath2(entity)
	local sprite = entity:GetSprite()
	if entity:GetData().samael ~= nil then
		if sprite:IsEventTriggered("Cut") then
			if not entity:GetData().playedSound then
				if REPENTANCE then
					local trailEndPos = entity.Position:__add(Vector(-400, 60))
					if entity.Type == EntityType.ENTITY_THE_LAMB then
						trailEndPos = trailEndPos:__add(Vector(0, -20))
					end
					entity:GetData().samaelTrail.Position = trailEndPos
					entity:PlaySound(SoundEffect.SOUND_KNIFE_PULL, 1.5, 0, false, 1)
				else
					entity:PlaySound(SoundEffect.SOUND_MEATY_DEATHS, 1.0, 0, false, 1.5)
				end
				entity:GetData().playedSound = true
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_RENDER, mod.IsaacDeath2, EntityType.ENTITY_ISAAC)
mod:AddCallback(ModCallbacks.MC_POST_NPC_RENDER, mod.IsaacDeath2, EntityType.ENTITY_THE_LAMB)

-- Play hallucination helper from the moddingofisaac docs.
local usagetime = -1 -- stores the last time the effect was called.
-- call this function to play the Hallucination effect
function playHallucination()
	local player = Isaac.GetPlayer(0)
	usagetime = Game().TimeCounter
	player:UseActiveItem(510, false, false, false, false) -- use the delirious item without applying the costume
end
-- Removes all spawned NPC entities when activating the function
function mod:onFriendlyInit(npc) 
	if Game().TimeCounter-usagetime == 0 then -- only remove enemies that spawned when the effect was called!
		npc:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.onFriendlyInit)

function mod:IsaacDeath3(entity)
	local sprite = entity:GetSprite()
	if sprite:GetFilename() == "gfx/samael_isaac_death.anm2" or sprite:GetFilename() == "gfx/samael_lamb_death.anm2" then
		entity:BloodExplode()
		local blood = Isaac.Spawn(1000, SoundEffect.SOUND_MEATY_DEATHS, 0, entity.Position, lib.ZeroVector, entity)
		blood.Color = entity.SplatColor
		entity:PlaySound(SoundEffect.SOUND_DEATH_BURST_LARGE, 1, 0, false, 1)
		
		if entity.Type == EntityType.ENTITY_ISAAC and entity.Variant == 0 then
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, TrinketType.TRINKET_ISAACS_HEAD, entity.Position:__add(Vector(0, -15)), Vector(-3, 2), entity)
		elseif entity.Type == EntityType.ENTITY_ISAAC and entity.Variant == 1 then
			-- Make sure we don't spawn the item too close to the end chest.
			local minDistFromCenter = 75
			local spawnPos = entity.Position
			local distFromCenter = spawnPos:Distance(game:GetRoom():GetCenterPos())
			if distFromCenter == 0 then
				spawnPos = spawnPos + RandomVector() * minDistFromCenter
			elseif distFromCenter < minDistFromCenter then
				spawnPos = game:GetRoom():GetCenterPos() + (spawnPos - game:GetRoom():GetCenterPos()):Resized(minDistFromCenter)
			end
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, CollectibleType.COLLECTIBLE_FATES_REWARD, spawnPos, lib.ZeroVector, entity)
		elseif entity.Type == EntityType.ENTITY_THE_LAMB and mod:CanTriggerFinalSequence() then
			mod:GetAllRunData().triggerFinalSequence = true
			Isaac.Spawn(EntityType.ENTITY_ISAAC, 0, 0, game:GetRoom():GetCenterPos(), lib.ZeroVector, entity)
			playHallucination()
		end
		
		entity:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_DEATH, mod.IsaacDeath3, EntityType.ENTITY_ISAAC)
mod:AddCallback(ModCallbacks.MC_POST_NPC_DEATH, mod.IsaacDeath3, EntityType.ENTITY_THE_LAMB)
