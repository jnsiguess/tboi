-- Contains code for Tainted Samael's pocket active, "Memento Mori".
-- While not as bad as samael.lua, the code here is pretty dense and messy as well.

local mod = SamaelMod
local lib = mod.Lib

local game = mod.Game
local sfxManager = mod.SfxManager

--local kDefaultLaserColor = lib.NewColor(1, 1, 1, 0.5, 0.1, 0.1, 1)

local kSigilId = mod.ENTITIES.MEMENTO_MORI_SIGIL.Var
local kMementoMori = mod.ITEMS.MEMENTO_MORI
local kMementoMoriHitboxLaserVariant = Isaac.GetEntityVariantByName("Memento Mori Hitbox Laser")
local kMementoMoriProjectile = Isaac.GetEntityVariantByName("Memento Mori Projectile")

local kMementoMoriFly = Isaac.GetEntityVariantByName("Memento Mori Fly Orbital")

-- Buffer window in frames for detecting double-taps.
local kMementoMoriDoubleTapWindow = 10

-- Invuln period after finishing an attack.
local kMementoMoriMaxIFrames = 18

-- Max placed sigils.
local kMementoMoriMaxSigils = 5

-- Minimum frames it takes to perform a Memento Mori slash.
local kMementoMoriAttackDurationMin = 7
-- Maximum frames it takes to perform a Memento Mori slash.
local kMementoMoriAttackDurationMax = 15

-- Attack distance (or less) for minimum duration.
local kMementoMoriAttackDurationMinLength = 200
-- Attack distance (or higher) for maxiumum duration.
local kMementoMoriAttackDurationMaxLength = 500

-- Frames to pause between Memento Mori slashes.
local kMementoMoriAttackDelay = 2

-- Width of the "slash" trail.
local kMementoMoriTrailWidth = 2

-- Damage Multipliers
local kMementoMoriBaseDamageMult = 1.0
local kMementoMoriComboDamageMult = 0.666
local kMementoMoriProjectileDamageMult = 1.0

local kLaserShapeAffectingTearFlags =
		TearFlags.TEAR_WIGGLE
		| TearFlags.TEAR_ORBIT
		| TearFlags.TEAR_PULSE
		| TearFlags.TEAR_SPIRAL
		| TearFlags.TEAR_SQUARE
		| TearFlags.TEAR_BIG_SPIRAL
		| TearFlags.TEAR_HOMING
		| TearFlags.TEAR_TURN_HORIZONTAL

local kSigilLinkLaserForbiddenTearFlags =
		TearFlags.TEAR_PUNCH | TearFlags.TEAR_HORN | TearFlags.TEAR_NEEDLE
		| TearFlags.TEAR_GREED_COIN | TearFlags.TEAR_COIN_DROP | TearFlags.TEAR_TURN_HORIZONTAL
		| TearFlags.TEAR_ACID | TearFlags.TEAR_ROCK | TearFlags.TEAR_BOUNCE | TearFlags.TEAR_CONTINUUM

--------------------------------------------------
---- HELPER FUNCTIONS
--------------------------------------------------

-- Gets the players' TearColor, ignoring Samael's default purple hue.
local function GetTearColor(player)
	local tearColorIsDefault = lib.SameColor(player.TearColor, lib.SamaelTearColor)
	if not tearColorIsDefault then
		return player.TearColor
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
		return lib.BloodColor
	else
		return lib.SamaelTearColor
	end
end

-- Gets the intended color for Memento Mori sigils and related effects.
local function GetSigilColor(player)
	return player.LaserColor
end

-- Gets the intended color for Memento Mori slash effects.
local function GetSlashColor(player, isFromDeathShadow)
	if isFromDeathShadow or not player:ToPlayer() then
		return lib.NewColor(0.8, 0.1, 0.8, 0.75)
	end
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_GODHEAD) then
		return lib.NewColor(1,1,1, 0.5, 1, 1, 1) * player.LaserColor -- White
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS)
			or lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) or lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_2) then
		return lib.NewColor(0.9, 0, 0, 0.85) * player.LaserColor -- Red
	else
		return lib.NewColor(1, 0, 1, 0.75) * player.LaserColor -- Purple
	end
end

-- (Bitwise flags) Returns true if the flagToCheck is present in flags.
local function HasFlag(flags, flagToCheck)
	return flags & flagToCheck == flagToCheck
end

function mod:CalculateMementoMoriDuration(pathLength)
	if pathLength > kMementoMoriAttackDurationMaxLength then
		return kMementoMoriAttackDurationMax
	elseif pathLength > kMementoMoriAttackDurationMinLength then
		local a = pathLength - kMementoMoriAttackDurationMinLength
		local b = kMementoMoriAttackDurationMaxLength - kMementoMoriAttackDurationMinLength
		return lib.Lerp(kMementoMoriAttackDurationMin, kMementoMoriAttackDurationMax, a/b)
	else
		return kMementoMoriAttackDurationMin
	end
end

--------------------------------------------------
---- DUMMY ITEMS
--------------------------------------------------

local kTaintedSamaelTractorBeam = Isaac.GetItemIdByName("Tainted Samael Tractor Beam")

local TaintedSamaelDummyItems = {
	[CollectibleType.COLLECTIBLE_TRACTOR_BEAM] = kTaintedSamaelTractorBeam,
}

function mod:HandleTaintedSamaelDummyItems(player)
	for normalItem, dummyItem in pairs(TaintedSamaelDummyItems) do
		if lib.IsTaintedSamael(player) and lib.HasItem(player, normalItem) then
			player:RemoveCollectible(normalItem)
			player:AddCollectible(dummyItem)
		elseif not lib.IsTaintedSamael(player) and lib.HasItem(player, dummyItem) then
			player:RemoveCollectible(dummyItem)
			player:AddCollectible(normalItem)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.HandleTaintedSamaelDummyItems)

function mod:DummyTractorBeam(player)
	if lib.HasItem(player, kTaintedSamaelTractorBeam) then
		player.MaxFireDelay = lib.TearsUp(player.MaxFireDelay, 0.5)
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.DummyTractorBeam, CacheFlag.CACHE_FIREDELAY)

--------------------------------------------------
---- SIGIL UPDATE
--------------------------------------------------

-- Used to catch Trisagion lasers that appear when player:FireTear is called.
local expectingTrisagionInit = false
local expectedTrisagionEntity = nil

-- Sigil Update
function mod:SigilUpdate(sigil)
	local sigilData = sigil:GetData()
	local sprite = sigil:GetSprite()
	
	-- GodHead Aura
	if sigil.SubType == 1 then
		if not sigil.Parent or not sigil.Parent:Exists() then
			sigil:Remove()
			return nil
		elseif sigil.Parent:GetSprite():IsPlaying("Death") and not sprite:IsPlaying("Death") then
			sprite:Play("Death", true)
		end
		sigil.Position = sigil.Parent.Position
		return nil
	end
	
	if (not sprite:IsPlaying("Death") and (not sigilData.linkLaser or not sigilData.linkLaser:Exists()))
			or sprite:IsFinished("Death") or game:GetLevel():GetCurrentRoom():GetFrameCount() == 0 
			or not sigil.Parent or not sigil.Parent:ToPlayer() then
		sigil:Remove()
		return nil
	end
	
	if sprite:IsFinished("Appear") then
		sprite:Play("Idle", true)
	elseif sprite:IsPlaying("Death") and sigilData.linkLaser then
		sigilData.linkLaser:Remove()
	end
	
	local player = sigil.Parent:ToPlayer()
	local tearParams = player:GetTearHitParams(WeaponType.WEAPON_TEARS)
	local tearFlags = tearParams.TearFlags
	local rng = player:GetCollectibleRNG(kMementoMori)
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BFFS) then
		sigil.SpriteScale = Vector(0.8, 0.8)
	end
	
	-- EMOJI GLASSES
	if FiendFolio and player:HasCollectible(FiendFolio.ITEM.COLLECTIBLE.EMOJI_GLASSES) then
		mod:EmojiGlassesSigilUpdate(sigil)
	end
	
	-- TECH.5
	-- Randomly connect sigils with tech lasers.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_5) and sigil.Target and sigil.FrameCount % 4 == 0 and rng:RandomFloat() < 0.167 then
		local target = sigil.Target -- Default target ("next" sigil).
		
		-- Normally, lasers are fired between connected sigils.
		-- If Technology 2 is held, sigils are already connected by lasers.
		-- In this case, fire lasers between unconnected sigils, instead.
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_2) then
			-- Find a random sigil (or the player) that is NOT the default target.
			local sigils = player:GetData().mementoMoriSigils
			local possibleTargets = {}
			for i=1, #sigils do
				if sigils[i].InitSeed ~= sigil.InitSeed and sigils[i].InitSeed ~= target.InitSeed and not (sigils[i+1] and sigils[i+1].InitSeed == sigil.InitSeed) then
					possibleTargets[#possibleTargets+1] = sigils[i]
				end
			end
			if player.InitSeed ~= target.InitSeed then
				possibleTargets[#possibleTargets+1] = player
			end
			if #possibleTargets > 0 then
				target = possibleTargets[rng:RandomInt(#possibleTargets+1)]
			else
				target = nil
			end
		end
		
		-- Fire the laser.
		if target then
			local dir = target.Position - sigil.Position
			local laser = player:FireTechLaser(sigil.Position, 3, dir, false, true, player, 1.0)
			laser.Parent = player
			laser:GetData().mementoMoriLaserAnchor = sigil
			laser.Target = target
			laser:GetData().isMementoMoriLinkLaser = true
			laser:GetData().mementoMoriLaserParent = player
			laser:AddTearFlags(TearFlags.TEAR_SPECTRAL)
			mod:MementoMoriLaserUpdate(laser)
		end
	end
	
	-- Tractor Beam / Trisagayone
	-- Tears will periodically travel between sigils, following the visible paths.
	local hasTractorBeam = lib.HasItem(player, CollectibleType.COLLECTIBLE_TRACTOR_BEAM) or lib.HasItem(player, kTaintedSamaelTractorBeam)
	local hasTrisageeon = lib.HasItem(player, CollectibleType.COLLECTIBLE_TRISAGION)
	local fruitCakeActivation = not hasTractorBeam and not hasTrisageeon
			and lib.HasItem(player, CollectibleType.COLLECTIBLE_FRUIT_CAKE) and HasFlag(tearFlags, TearFlags.TEAR_LASERSHOT)
	if (hasTractorBeam or hasTrisageeon or fruitCakeActivation) and sigil.Target then
		local delay = lib.GetUnmodifiedFireDelay(player) * 2
		
		if not hasTractorBeam then
			delay = delay * 1.5
		end
		
		if hasTrisageeon then
			delay = math.max(delay, 5)
		else
			delay = math.max(delay, 2)
		end
		
		if fruitCakeActivation or (sigil.FrameCount % math.ceil(delay) == 0) then
			expectingTrisagionInit = true
			local startingVel = (sigilData.linkLaser.Position - sigil.Position):Normalized()
			local damageMult = 0.5
			if hasTrisageeon then
				damageMult = 1.0
			end
			local tear = player:FireTear(sigil.Position, startingVel, false, true, false, player, damageMult)
			expectingTrisagionInit = false
			local laser = expectedTrisagionEntity
			expectedTrisagionEntity = nil
			
			tear.Parent = sigil
			tear.Target = sigilData.linkLaser
			tear:GetData().isMementoMoriPathTear = true
			tear:GetData().mementoMoriTearSpeed = player.ShotSpeed * 10
			tear:ClearTearFlags(kLaserShapeAffectingTearFlags)
			tear:AddTearFlags(TearFlags.TEAR_SPECTRAL)
			tear.Height = -6
			
			if laser then
				tear:GetData().mementoMoriTris = laser
				laser.SpriteScale = Vector(0,1)
				laser.PositionOffset = lib.ZeroVector
				laser.ParentOffset = lib.ZeroVector
				laser.SpriteOffset = lib.ZeroVector
				laser:ClearTearFlags(kLaserShapeAffectingTearFlags)
				laser:AddTearFlags(TearFlags.TEAR_SPECTRAL)
				
				c = laser.Color
				c:SetTint(c.R, c.G, c.B, 0)
				laser.Color = c
				
				laser:Update()
			end
			
			tear:Update()
		end
	end
	
	-- Strange Attractor
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_STRANGE_ATTRACTOR) then
		if not sigilData.mementoMoriMagnet or not sigilData.mementoMoriMagnet:Exists() then
			local tear = Isaac.Spawn(EntityType.ENTITY_TEAR, 0, 0, sigil.Position, lib.ZeroVector, player):ToTear()
			tear:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			tear:AddEntityFlags(EntityFlag.FLAG_NO_QUERY | EntityFlag.FLAG_NO_REWARD)
			tear:AddTearFlags(TearFlags.TEAR_ATTRACTOR)
			tear.Parent = sigil
			tear:GetData().mementoMoriSigilDummyTear = true
			sigilData.mementoMoriMagnet = tear
			mod:SigilDummyTear(tear)
		end
	end
	
	-- Ocular Rift
	-- Rifts will occasionally appear on sigils for brief periods.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_OCULAR_RIFT) then
		local riftCooldown = sigilData.riftCooldown
		if riftCooldown and riftCooldown <= 0 then
			local rift = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.RIFT, 0, sigil.Position, lib.ZeroVector, player):ToEffect()
			rift.Parent = player
			rift.DepthOffset = 1
			rift.Timeout = 75
		end
		if not riftCooldown or riftCooldown <= 0 then
			riftCooldown = 150 + rng:RandomInt(150)
		end
		sigilData.riftCooldown = riftCooldown - 1
	end
	
	-- LUDO
	-- Sigils can be moved with the fire inputs.
	local stopMovement = true
	if not player:GetData().mementoMoriActive and not sigil:GetSprite():IsPlaying("Death") then
		stopMovement = false
		if (lib.HasItem(player, CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE) or sigilData.mementoMoriSigilForceLudo)
				and not (sigilData.mementoMoriTechEye and sigilData.mementoMoriTechEye.FrameCount < 25) then
			local targetVel = player:GetAimDirection() * player.ShotSpeed * 10
			--sigil.Velocity = lib.Lerp(sigilData.LastVelocity or lib.ZeroVector, targetVel, 0.2)
			--sigilData.LastVelocity = sigil.Velocity
			sigil.Velocity = lib.Lerp(sigil.Velocity, targetVel, 0.2)
			
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE) then
				sigil.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
			else
				sigil.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_GROUND
			end
		elseif sigilData.mementoMoriSigilEmojiMovement then
			mod:HandleEmojiSigilMovement(sigil)
		else
			stopMovement = true
		end
	end
	
	if stopMovement then
		sigil.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
		sigil.Velocity = lib.ZeroVector
	end
	
	if not sigilData.hitboxLaser or sigilData.hitboxLaser.Timeout <= 0 then
		return nil
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.SigilUpdate, kSigilId)

function mod:QueueSlashes(fam)
	local data = fam:GetData()
	if data.mementoMoriSigilAttackTargets then
		for initSeed, targetInfo in pairs(data.mementoMoriSigilAttackTargets) do
			local entity = targetInfo.ref
			if entity and entity:Exists() and entity:IsVulnerableEnemy() and targetInfo.numHits < data.maxHits then
				if game:GetFrameCount() - targetInfo.lastHit >= (data.hitCooldown or 2) then
					entity:TakeDamage(data.mementoMoriAttackDamage, targetInfo.damageFlags, EntityRef(fam), 0)
				end
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.QueueSlashes)

function mod:SigilCollision(sigil, collider, low)
	if sigil.SubType == 1 then
		return nil
	end
	if sigil:GetData().mementoMoriShielded and collider.Type == EntityType.ENTITY_PROJECTILE then
		collider:Die()
	end
	local player = sigil.Parent
	if player and player:ToPlayer() and collider:IsVulnerableEnemy ()
			and lib.HasItem(player:ToPlayer(), CollectibleType.COLLECTIBLE_MOMS_CONTACTS)
			and not collider:HasEntityFlags(EntityFlag.FLAG_FREEZE) and sigil:GetDropRNG():RandomInt(20) == 0 then
		collider:AddFreeze(EntityRef(player), 60)
	end
	if not sigil:GetData().allowCollisionDamage then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_FAMILIAR_COLLISION, mod.SigilCollision, kSigilId)

--------------------------------------------------
---- SIGIL SYNERGY-RELATED ENTITIES UPDATE
--------------------------------------------------

function mod:SigilDummyTear(tear)
	local data = tear:GetData()
	
	if not data.mementoMoriSigilDummyTear then return end
	
	if not tear.Parent or not tear.Parent:Exists() then
		tear:Remove()
		return
	end
	
	tear.Position = tear.Parent.Position
	tear.Height = -5
	tear.FallingAcceleration = -tear.Height
	tear.FallingSpeed = -1
	tear.EntityCollisionClass = 0
	tear.GridCollisionClass = 0
	tear.Visible = false
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.SigilDummyTear)

function mod:SigilDummyTearCollision(ent1, ent2)
	if ent1:GetData().mementoMoriSigilDummyTear or ent2:GetData().mementoMoriSigilDummyTear then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_TEAR_COLLISION, mod.SigilDummyTearCollision)

-- Maintain a reference to the "Marked" targeting reticle.
function mod:MarkUpdate(mark)
	if mark.State == 0 and mark.SpawnerEntity and mark.SpawnerEntity:ToPlayer() then
		local player = mark.SpawnerEntity:ToPlayer()
		local pData = player:GetData()
		local hasMarked = lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED)
		local hasOccultMark = lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_THE_OCCULT)
			and (player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) or player:HasWeaponType(WeaponType.WEAPON_LASER))
		if (hasMarked or hasOccultMark) and lib.HasItem(player, kMementoMori)
				and (mark.Variant == EffectVariant.TARGET or (mark.Variant == EffectVariant.OCCULT_TARGET and hasOccultMark))
				and (not pData.mementoMoriMarkedReference or not pData.mementoMoriMarkedReference:Exists())
				and math.floor(player:GetAimDirection():GetAngleDegrees()) == math.floor((mark.Position - player.Position):GetAngleDegrees()) then
			pData.mementoMoriMarkedReference = mark
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.MarkUpdate, EffectVariant.TARGET)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.MarkUpdate, EffectVariant.OCCULT_TARGET)

-- Sigil Effect/Layer Update
function mod:EffectUpdate(eff)
	if not eff:GetData().isMementoMoriSigilEffect then return end
	
	if (not eff.Parent or not eff.Parent:Exists() or eff.Parent:GetSprite():IsPlaying("Death")) and not eff:GetSprite():IsPlaying("Death") then
		if eff:GetData().needsManualDeath then
			eff:GetSprite():Play("Death")
		elseif eff:GetData().needsManualRemoval then
			eff:Remove()
		else
			eff:Die()
		end
	end
	
	if eff:GetSprite():IsFinished("Death") then
		eff:Remove()
	end
	
	if eff.Parent then
		eff.Position = eff.Parent.Position
		eff.Velocity = eff.Parent.Velocity
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.EffectUpdate)

-- Dr Fetus Bombs
function mod:BombUpdate(bomb)
	if not bomb:GetData().isMementoMoriBomb then return end
	--bomb:GetData().EmojiGlassesEffect = nil
	
	local isRocket = bomb.Variant == BombVariant.BOMB_ROCKET or bomb.Variant == BombVariant.BOMB_ROCKET_GIGA

	if not bomb.Parent or not bomb.Parent:Exists() then
		bomb:Remove()
		return
	elseif bomb.Parent:GetSprite():IsPlaying("Death") then
		if isRocket then
			local player = bomb.Parent.Parent:ToPlayer()
			bomb:GetData().samaelForcedRocket = true
			local dir = bomb.Parent.Position - player.Position
			bomb.Velocity = dir:Resized(player.ShotSpeed * 10) * 0.5
			bomb:GetData().samaelRocketTargetVel = dir:Resized(player.ShotSpeed * 10) * 1.5
			bomb:GetData().samaelRocketStartFrame = bomb.FrameCount
			bomb:GetData().isMementoMoriBomb = false
			bomb.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ENEMIES
			return
		else
			bomb:SetExplosionCountdown(0)
		end
	else
		if isRocket then
			bomb:GetSprite():Play("Idle", true)
		end
		if bomb.FrameCount % 20 == 1 then
			bomb:SetExplosionCountdown(100)
		end
	end
	
	bomb.Position = bomb.Parent.Position
	if isRocket then
		bomb.Position = bomb.Position + Vector(0, 10)
	end
	bomb.Velocity = bomb.Parent.Velocity
end
mod:AddCallback(ModCallbacks.MC_POST_BOMB_UPDATE, mod.BombUpdate)

-- Orbital Flies
function mod:FlyUpdate(fly)
	local sigil = fly.Parent
	
	if not sigil or not sigil:Exists() or sigil:GetSprite():IsPlaying("Death") then
		fly:Remove()
		return
	end
	
	fly.Position = sigil.Position + Vector(0, 20):Rotated(fly:GetData().mementoMoriFlyStartRot - (fly.FrameCount*4) % 360)
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.FlyUpdate, kMementoMoriFly)

function mod:FlyCollision(fly, collider, low)
	if collider.Type == EntityType.ENTITY_PROJECTILE then
		collider:Die()
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_FAMILIAR_COLLISION, mod.FlyCollision, kMementoMoriFly)

-- Apples!
function mod:AppleUpdate(apple)
	if apple.SubType ~= 617 then return end

	if not apple.Parent or not apple.Parent:Exists() then
		apple:Remove()
	elseif apple.Parent:GetSprite():IsPlaying("Death") then
		apple:Die()
	else
		apple.Position = apple.Parent.Position
		apple.Velocity = apple.Parent.Velocity
		
		if apple:GetSprite():IsFinished("BombAppear") then
			apple:GetSprite():Play("BombPulse", true)
		end
		if apple:GetSprite():IsPlaying("BombPulse") and not (apple.Parent:GetData().mementoMoriBomb or apple.Parent:GetData().mementoMoriBomb:Exists()) then
			apple:Die()
		end
	end
	
	return true
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_UPDATE, mod.AppleUpdate, EntityType.ENTITY_GUSHER)

function mod:AppleCollision(apple, collider)
	if apple.SubType ~= 617 or not apple.Parent then return end
	
	if apple:GetData().emojiSigilKawaii then return true end
	
	if collider:IsVulnerableEnemy() and not collider:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
		apple.Parent:GetData().mementoMoriAppleTarget = collider
		apple:Die()
	elseif collider.Type == EntityType.ENTITY_PROJECTILE then
		local newTarget = collider.Parent or collider.SpawnerEntity
		if newTarget then
			apple.Parent:GetData().mementoMoriAppleTarget = newTarget
		end
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.AppleCollision, EntityType.ENTITY_GUSHER)

function mod:AppleDeath(apple)
	if apple.SubType ~= 617 or not apple.SpawnerEntity or not apple.SpawnerEntity:ToPlayer() then return end
	
	if apple:GetData().emojiSigilKawaii or not apple.Visible then return end
	
	local targetPos = nil
	
	if apple.Parent then
		local target = apple.Parent:GetData().mementoMoriAppleTarget
		if target and target:Exists() then
			targetPos = target.Position
		end
		if apple.Parent:GetData().mementoMoriBomb then
			apple.Parent:GetData().mementoMoriBomb:SetExplosionCountdown(0)
		end
	end
	
	local player = apple.SpawnerEntity:ToPlayer()
	local rng = player:GetCollectibleRNG(kMementoMori)
	
	if rng:RandomInt(4) == 0 then
		local speed = player.ShotSpeed * 7
		local vel
		
		if targetPos and apple.Position:Distance(targetPos) > 0 then
			vel = (targetPos - apple.Position):Resized(speed)
		else
			vel = Vector(speed, 0):Rotated(rng:RandomInt(360))
		end
		local tear = player:FireTear(apple.Position, vel, false, true, false, player, 1)
		tear:AddTearFlags(TearFlags.TEAR_PIERCING)
		if tear.Variant ~= TearVariant.RAZOR then
			if tear.Variant ~= TearVariant.TOOTH then
				tear.CollisionDamage = tear.CollisionDamage * 4
			end
			tear:ChangeVariant(TearVariant.RAZOR)
		end
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
		tear:GetSprite():LoadGraphics()
	end
	sfxManager:Play(142, 0.8, 0, false, 1.4)
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_DEATH, mod.AppleDeath, EntityType.ENTITY_GUSHER)

-- Technology eyes
function mod:TechEyeUpdate(eye)
	if eye.SubType ~= 617 then return nil end
	
	if not eye.Parent or not eye.Parent:Exists() then
		eye:Remove()
	elseif eye.Parent:GetSprite():IsPlaying("Death") then
		eye:Die()
	else
		eye.Position = eye.Parent.Position
		eye.Velocity = eye.Parent.Velocity
	end
	
	if eye.Parent:GetData().EmojiGlassesEffect then
		eye.Visible = false
	end
	
	local player = eye.Parent:ToFamiliar().Player
	if player and player:Exists() then
		local mark = player:GetData().mementoMoriMarkedReference
		local hasOccultMark = lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_THE_OCCULT)
			and (player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) or player:HasWeaponType(WeaponType.WEAPON_LASER))
		if mark and mark:Exists() and hasOccultMark then
			eye.Target = player:GetData().mementoMoriMarkedReference
		end
	end
	
	if not eye.Target then
		eye.State = 4
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.TechEyeUpdate, EntityType.ENTITY_EYE)

function mod:TechEyeCollision(eye, collider)
	if eye.SubType == 617 then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.TechEyeCollision, EntityType.ENTITY_EYE)

function mod:TechEyeDeath(eye)
	if eye.SubType ~= 617 or not eye.SpawnerEntity or not eye.SpawnerEntity:ToPlayer() then return nil end
	
	if eye.Parent and eye.Parent:GetData().mementoMoriBomb then
		eye.Parent:GetData().mementoMoriBomb:SetExplosionCountdown(0)
	end

	local rng = eye.SpawnerEntity:ToPlayer():GetCollectibleRNG(kMementoMori)
	for i=0, 10 do
		local vel = Vector(rng:RandomFloat()*6 - 3, rng:RandomFloat()*6 - 3)
		Isaac.Spawn(1000, EffectVariant.NAIL_PARTICLE, 1, eye.Position + vel:Resized(20), vel, eye):ToEffect()
	end
	
	local eff = Isaac.Spawn(1000, EffectVariant.POOF01, 1, eye.Position, lib.ZeroVector, eye):ToEffect()
	eff.Color = lib.NewColor(0.75, 0.75, 0.75, 0.85)
	
	sfxManager:Play(SoundEffect.SOUND_POT_BREAK, 0.4, 0, false, 1.2)
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_DEATH, mod.TechEyeDeath, EntityType.ENTITY_EYE)

-- Fixes the damage value of booger tears spawned by sigils.
function mod:BoogerTearFix(tear)
	if tear.FrameCount == 1 and tear.Variant == TearVariant.BOOGER
			and tear.SpawnerType == EntityType.ENTITY_FAMILIAR and tear.SpawnerVariant == kSigilId
			and tear.SpawnerEntity and tear.SpawnerEntity.Parent and tear.SpawnerEntity.Parent:ToPlayer()
			and tear.CollisionDamage < tear.SpawnerEntity.Parent:ToPlayer().Damage then
		tear.CollisionDamage = tear.SpawnerEntity.Parent:ToPlayer().Damage
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.BoogerTearFix)

-- Funny, wonky code to detect if the current sigil placement forms a pentagram.
function mod:CheckPentagram(player, sigils)
	local endSigil = sigils[1]
	
	local first = 1
	local last = 5
	
	if sigils[0] and #sigils == 4 then
		endSigil = sigils[0]
		first = 0
		last = 4
	elseif #sigils == 4 then
		return nil
	end
	
	if not endSigil then return nil end
	
	local distToEnd = endSigil.Position:Distance(player.Position)
	
	for i = first, last do
		if sigils[i] and sigils[i]:Exists() and sigils[i].Position:Distance(player.Position) < distToEnd then
			return nil
		end
	end
	
	local points = {}
	
	for i = first, last do
		if not sigils[i] or not sigils[i]:Exists() then
			return nil
		end
		points[i] = {}
		points[i].Pos = sigils[i].Position
	end
	
	for i = first, last do
		points[i].prev = points[i-1] or points[last]
		points[i].next = points[i+1] or points[first]
	end
	
	local middleIndex = nil
	
	local maxY = nil
	local maxYindex = nil
	
	local minY = nil
	local minYindex = nil
	
	local maxX = nil
	local minX = nil
	
	for i = first, last do
		local isMiddle =
			(points[i].Pos.X > points[i].prev.Pos.X
				and points[i].Pos.X < points[i].next.Pos.X
				and points[i].Pos.X < points[i].prev.prev.Pos.X
				and points[i].Pos.X > points[i].next.next.Pos.X)
			or (points[i].Pos.X < points[i].prev.Pos.X
				and points[i].Pos.X > points[i].next.Pos.X
				and points[i].Pos.X > points[i].prev.prev.Pos.X
				and points[i].Pos.X < points[i].next.next.Pos.X)
			
		if isMiddle then
			middleIndex = i
		end
		
		if not maxYindex or maxY < points[i].Pos.Y then
			maxY = points[i].Pos.Y
			maxYindex = i
		end
		
		if not minYindex or minY > points[i].Pos.Y then
			minY = points[i].Pos.Y
			minYindex = i
		end
		
		if not maxX or maxX < points[i].Pos.X then
			maxX = points[i].Pos.X
		end
		
		if not minX or minX > points[i].Pos.X then
			minX = points[i].Pos.X
		end
	end
	
	if not middleIndex or not (middleIndex == maxYindex or middleIndex == minYindex) then
		return nil
	end
	
	local A = points[middleIndex]
	local B = A.next
	local C = A.next.next
	local D = A.prev.prev
	local E = A.prev
	
	local correctVertOrder =
		(B.Pos.Y < D.Pos.Y and B.Pos.Y < C.Pos.Y and E.Pos.Y < D.Pos.Y and E.Pos.Y < C.Pos.Y)
		or (B.Pos.Y > D.Pos.Y and B.Pos.Y > C.Pos.Y and E.Pos.Y > D.Pos.Y and E.Pos.Y > C.Pos.Y)
	local correctHoriOrder =
		(B.Pos.X < C.Pos.X and B.Pos.X < E.Pos.X and D.Pos.X < C.Pos.X and D.Pos.X < E.Pos.X)
		or (B.Pos.X > C.Pos.X and B.Pos.X > E.Pos.X and D.Pos.X > C.Pos.X and D.Pos.X > E.Pos.X)
		
	if correctVertOrder and correctHoriOrder then
		local x = (maxX + minX) * 0.5
		local y = (maxY + minY) * 0.5
		
		local pos = Vector(x,y)
		
		local sizeX = maxX - minX
		local sizeY = maxY - minY
		local hitboxSize = math.max(sizeX, sizeY) * 0.7
		
		local scaleX = sizeX / 256
		local scaleY = sizeY / 256
		local spriteScale = Vector(scaleX, scaleY)
		
		local flipY = middleIndex == minYindex
		
		local pentagram = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PENTAGRAM_BLACKPOWDER, 0, pos, lib.ZeroVector, player):ToEffect()
		pentagram.Parent = player
		pentagram.State = 1
		pentagram.Size = hitboxSize
		pentagram.SpriteScale = spriteScale
		pentagram:GetSprite().FlipY = flipY
		pentagram:GetData().mementoMoriPentagram = true
		
		-- Get rid of the "My Reflection" return sigil if its not necessary.
		if sigils[0] and first ~= 0 then
			sigils[0]:Remove()
			sigils[0] = nil
		end
	end
end

-- Increases the damage dealt by pentagrams spawned via the Memento Mori synergy.
function mod:PentagramDamageFix(tookDamage, damage, damageFlags, damageSourceRef)
	if not damageSourceRef or not damageSourceRef.Entity then return nil end
	
	if damageSourceRef.Type == EntityType.ENTITY_EFFECT
			and damageSourceRef.Variant == EffectVariant.PENTAGRAM_BLACKPOWDER
			and damageSourceRef.Entity:GetData().mementoMoriPentagram
			and damageSourceRef.Entity.Parent and damageSourceRef.Entity.Parent:ToPlayer() then
		local player = damageSourceRef.Entity.Parent:ToPlayer()
		tookDamage:TakeDamage(math.max(10, player.Damage), 0, EntityRef(player), 0)
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.PentagramDamageFix)

-- The sigils are technically familiars. Make them immune to the Siren.
function mod:SigilsAreSirenImmune(sirenHelper)
	if sirenHelper.FrameCount == 0 and sirenHelper.Target and sirenHelper.Target.Variant == kSigilId then
		sirenHelper:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilsAreSirenImmune, 966)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SigilsAreSirenImmune, 165)

--------------------------------------------------
---- SIGIL SPAWNING
--------------------------------------------------

-- Spawn a memento mori sigil.
function mod:SpawnSigil(player, isReturnSigil, forcePos)
	local playerData = player:GetData()
	local rng = player:GetCollectibleRNG(kMementoMori)
	
	local sigils = playerData.mementoMoriSigils
	sfxManager:Play(501, 0.85, 0, false, 0.7 + 0.1 * #sigils)
	
	local sigilPos = forcePos or player.Position

	local i = #sigils + 1

	if isReturnSigil then
		-- Sigil to return the player to their original position, for the "My Reflection" synergy.
		i = 0
		if playerData.mementoMoriEpicFetus then
			local room = game:GetRoom()
			local pos1 = sigils[1].Position
			local pos2 = (sigils[2] or player).Position
			local pos3 = pos1 + (pos1 - pos2):Resized(25)
			while room:IsPositionInRoom(pos3, 0) do
				pos3 = pos3 + (pos1 - pos2):Resized(25)
			end
			sigilPos = room:GetClampedPosition(pos3, 0)
		end
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_MARKED)
			and playerData.mementoMoriMarkedReference and playerData.mementoMoriMarkedReference:Exists() then
		-- Place the sigil at the location of the "Marked" target, instead.
		sigilPos = playerData.mementoMoriMarkedReference.Position
	end
	
	-- Spawn the sigil entity.
	sigils[i] = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, kSigilId, 0, sigilPos, lib.ZeroVector, player)
	sigils[i].Parent = player
	sigils[i].Target = player
	sigils[i]:ClearEntityFlags(EntityFlag.FLAG_PERSISTENT)
	sigils[i].GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
	sigils[i].CollisionDamage = 0
	sigils[i]:GetData().mementoMoriSigilIndex = i
	
	local tearParams = player:GetTearHitParams(WeaponType.WEAPON_TEARS)
	local tearFlags = tearParams.TearFlags
	local laserParams = player:GetTearHitParams(WeaponType.WEAPON_BRIMSTONE)
	
	local spriteSuffix = nil
	local linkLaserColor = nil
	
	-- Technology (Sigils become eyes that can shoot lasers at enemies)
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY) then
		local eye = Isaac.Spawn(EntityType.ENTITY_EYE, 0, 617, sigils[i].Position, lib.ZeroVector, player):ToNPC()
		eye:AddEntityFlags(EntityFlag.FLAG_FRIENDLY)
		eye:AddEntityFlags(EntityFlag.FLAG_CHARM)
		eye:AddEntityFlags(EntityFlag.FLAG_NO_STATUS_EFFECTS)
		--eye:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		eye.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		eye.GridCollisionClass = 0
		eye.CollisionDamage = 0
		eye.Parent = sigils[i]
		eye.SpawnerEntity = player
		--eye.DepthOffset = 31
		eye.PositionOffset = lib.ZeroVector
		eye.Scale = 0.75
		local color = lib.NewColor(1,1,1, 0.8)
		color:SetColorize(3, 3, 3, 1)
		eye.SplatColor = color
		sigils[i].Visible = false
		sigils[i]:GetData().mementoMoriTechEye = eye
		if FiendFolio and player:HasCollectible(FiendFolio.ITEM.COLLECTIBLE.EMOJI_GLASSES) then
			eye:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			eye.Visible = false
		end
	end
	
	-- Tech X (Sigils have a laser ring around them)
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECH_X) then
		local ring = mod:AddLaserRing(player, sigils[i])
		--local ring = player:FireTechXLaser(player.Position, lib.ZeroVector, 66, player, 0.5)
		ring.ParentOffset = lib.ZeroVector
		ring.PositionOffset = lib.ZeroVector
		--ring.Parent = sigils[i]
		--ring.SubType = 3
		--ring:GetData().lib.IsSamaelLaser = false
		--ring:GetData().isTechXMeleeRing = false
		ring:GetData().isMementoMoriTechRing = true
		ring:SetTimeout(-1)
		ring.CollisionDamage = ring.CollisionDamage * 0.25
	end
	
	-- Apple! (Apples are placed on sigils that attract enemies and shoot razor blades when destroyed)
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_APPLE) then
		local apple = Isaac.Spawn(EntityType.ENTITY_GUSHER, 1, 617, sigils[i].Position, lib.ZeroVector, player):ToNPC()
		apple:AddEntityFlags(EntityFlag.FLAG_FRIENDLY)
		apple:AddEntityFlags(EntityFlag.FLAG_CHARM)
		apple:AddEntityFlags(EntityFlag.FLAG_BAITED)
		apple:AddEntityFlags(EntityFlag.FLAG_NO_STATUS_EFFECTS)
		apple:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		apple.GridCollisionClass = 0
		apple.CollisionDamage = 0
		apple.Parent = sigils[i]
		apple.SpawnerEntity = player
		apple.DepthOffset = 4
		apple.Scale = 0.75
		local color = lib.NewColor(1,1,1, 0.8)
		color:SetColorize(4.5, 4.5, 0, 1)
		apple.SplatColor = color
		apple:GetSprite():Play("Appear", true)
		sigils[i]:GetData().mementoMoriApple = apple
	end
	
	-- Dr Fetus (places bombs on sigils that detonate on activation)
	if i > 0 and lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS) then
		local bomb = player:FireBomb(sigils[i].Position, lib.ZeroVector, player)
		bomb:ClearTearFlags(TearFlags.TEAR_BOOMERANG)
		bomb:GetData().isMementoMoriBomb = true
		--bomb:GetData().EmojiGlassesEffect = nil
		bomb.Parent = sigils[i]
		bomb.DepthOffset = 3
		bomb.Color = lib.NullColor
		if bomb.Variant == BombVariant.BOMB_ROCKET or bomb.Variant == BombVariant.BOMB_ROCKET_GIGA then
			bomb:GetSprite():Play("Idle", true)
		else
			bomb:GetSprite():Play("Appear", true)
		end
		if sigils[i]:GetData().mementoMoriApple then
			bomb.Visible = false
			sigils[i]:GetData().mementoMoriApple:GetSprite():Play("BombAppear")
		elseif sigils[i]:GetData().mementoMoriTechEye then
			bomb.Visible = false
		end
		sigils[i]:GetData().mementoMoriBomb = bomb
	-- Fire mind places a flame on the sigil.
	-- Doesn't trigger with Dr Fetus, since the bomb will become a flaming bomb with contact damage anyway.
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_FIRE_MIND) or HasFlag(tearFlags, TearFlags.TEAR_BURN) then
		local fire = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HOT_BOMB_FIRE, 0, sigils[i].Position, lib.ZeroVector, player):ToEffect()
		fire.Parent = sigils[i]
		fire.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ENEMIES
		fire.SpriteScale = Vector(0.7, 0.8)
		fire:SetColor(GetTearColor(player), -1, 1, false, false)
		fire.DepthOffset = 3
		fire.Timeout = -1
		fire:GetData().isMementoMoriSigilEffect = true
	end
	
	-- Halo of Flies (adds an orbiting fly to sigils that can block projectiles)
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_HALO_OF_FLIES) then
		local numFlies = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_HALO_OF_FLIES)
		for x=0, numFlies-1 do
			local fly = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, kMementoMoriFly, 0, sigils[i].Position, lib.ZeroVector, sigils[i]):ToFamiliar()
			fly.Parent = sigils[i]
			fly:GetData().mementoMoriFlyStartRot = x * (360/numFlies)
		end
	end
	
	-- Muco places mushrooms on the sigils. Purely cosmetic, though the item has other synergies.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MUCORMYCOSIS) then
		local effectVariant = Isaac.GetEntityVariantByName("Memento Mori Sigil Mush")
		local mush = Isaac.Spawn(EntityType.ENTITY_EFFECT, effectVariant, 0, sigils[i].Position, lib.ZeroVector, player):ToEffect()
		mush.Color = GetSigilColor(player)
		mush.DepthOffset = 2
		mush.Parent = sigils[i]
		mush:GetData().isMementoMoriSigilEffect = true
		mush:GetData().needsManualRemoval = true
	end
	
	-- Lost Contact (adds a projectile-blocking shield)
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_LOST_CONTACT) or HasFlag(tearFlags, TearFlags.TEAR_SHIELDED) then
		sigils[i]:GetData().mementoMoriShielded = true
		local effectVariant = Isaac.GetEntityVariantByName("Memento Mori Sigil Shield")
		local shield = Isaac.Spawn(EntityType.ENTITY_EFFECT, effectVariant, 0, sigils[i].Position, lib.ZeroVector, player):ToEffect()
		shield.Parent = sigils[i]
		shield.DepthOffset = 5
		shield.SpriteScale = Vector(0.85, 0.85)
		shield.Color = lib.NewColor(1,1,1, 0.8)
		shield:GetSprite().Offset = Vector(0, 10)
		shield:GetData().isMementoMoriSigilEffect = true
		shield:GetData().needsManualRemoval = true
	end

	-- Godhead (adds a damaging glowing aura to the sigils)
	-- Also involved in the decision on which sprite and color to use for the sigils.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_GODHEAD) or HasFlag(tearFlags, TearFlags.TEAR_GLOW) then
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_SACRED_HEART) or lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
			spriteSuffix = "godhead_red"
			linkLaserColor = "Red"
		else
			spriteSuffix = "godhead"
			linkLaserColor = "White"
		end
		local aura = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, kSigilId, 1, sigils[i].Position, lib.ZeroVector, player):ToFamiliar()
		aura.Parent = sigils[i]
		aura.CollisionDamage = player.Damage * 0.5
		aura.DepthOffset = -1
		aura.Color = GetSigilColor(player)
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS) then
		spriteSuffix = "fetus"
		linkLaserColor = "Red"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
		spriteSuffix = "brimstone"
		linkLaserColor = "Red"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS) then
		spriteSuffix = "fetus"
		linkLaserColor = "Red"
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_SACRED_HEART) then
		spriteSuffix = "cross"
		linkLaserColor = "Red"
	end
	
	-- Load the appropriate graphics.
	if spriteSuffix then
		sigils[i]:GetSprite():ReplaceSpritesheet(0, "gfx/effects/memento_mori_sigil_" .. spriteSuffix .. ".png")
		sigils[i]:GetSprite():ReplaceSpritesheet(1, "gfx/effects/memento_mori_sigil_" .. spriteSuffix .. ".png")
		sigils[i]:GetSprite():ReplaceSpritesheet(2, "gfx/effects/memento_mori_sigil_" .. spriteSuffix .. ".png")
		sigils[i]:GetSprite():LoadGraphics()
	end
	sigils[i].Color = GetSigilColor(player)
	
	--[[if spriteSuffix == "godhead" then
		sigils[i].Color = Color.lib.Lerp(sigils[i].Color, player.LaserColor, 0.5)
	else
		sigils[i].Color = GetSigilColor(player)
	end]]
	
	-- Connect the player/sigils with lasers.
	local laser = nil
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_2) then
		-- Sigils linked by damaging technology lasers.
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_ZERO) then
			laser = Isaac.Spawn(EntityType.ENTITY_LASER, 10, 0, sigils[i].Position, lib.ZeroVector, sigils[i]):ToLaser()
		else
			laser = player:FireTechLaser(sigils[i].Position, 0, lib.ZeroVector, false, false, sigils[i], 1.0)
			laser.Color = GetSigilColor(player)
		end
	else
		-- Sigils linked by the default (non-damaging) visual beams.
		laser = player:FireTechLaser(sigils[i].Position, 0, lib.ZeroVector, false, false, sigils[i], 0.0)
		--laser = Isaac.Spawn(EntityType.ENTITY_LASER, 6617, 0, sigils[i].Position, lib.ZeroVector, sigils[i]):ToLaser()
	 
		laser:GetSprite():Load("gfx/memento_mori_link_laser.anm2", true)
		laser:GetSprite():LoadGraphics()
		
		laser.Color = GetSigilColor(player)
		laser:GetData().isDefaultMementoMoriLinkLaser = true
		--sigils[i]:GetData().hasDefaultMementoMoriLinkLaser = true
	end
	
	laser.CollisionDamage = player.Damage
	
	local laserData = laser:GetData()
	
	-- Tearflags nonsense
	local playerTearFlags = laserParams.TearFlags
	
	-- Additional chance to apply homing with PlayDough Cookie.
	-- This can happen naturally but I felt like the chance was pretty low.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE) and rng:RandomInt(6) == 0 then
		playerTearFlags = playerTearFlags | TearFlags.TEAR_HOMING
	end
	
	local disabledTearFlags = ((playerTearFlags & kLaserShapeAffectingTearFlags) ~ kLaserShapeAffectingTearFlags)
	
	local flagsToAdd = TearFlags.TEAR_SPECTRAL | TearFlags.TEAR_PIERCING
	local flagsToRemove = TearFlags.TEAR_JACOBS | TearFlags.TEAR_BOOMERANG | TearFlags.TEAR_RIFT
			| TearFlags.TEAR_QUADSPLIT | TearFlags.TEAR_SPLIT | TearFlags.TEAR_EXPLOSIVE | TearFlags.TEAR_POP
			| TearFlags.TEAR_ABSORB | TearFlags.TEAR_WAIT
	
	laserData.positiveTearFlagMask = kLaserShapeAffectingTearFlags | flagsToAdd | flagsToRemove
	laserData.negativeTearFlagMask = disabledTearFlags | flagsToRemove
	
	laser.TearFlags = playerTearFlags
	laser:AddTearFlags(laserData.positiveTearFlagMask)
	laser:ClearTearFlags(laserData.negativeTearFlagMask)
	laser:ClearTearFlags(kSigilLinkLaserForbiddenTearFlags)
	
	laser.SpawnerEntity = sigils[i]
	
	if laserData.isDefaultMementoMoriLinkLaser then
		-- Funny hack that makes the laser unable to collide with anything.
		-- Specifically this avoids the link lasers breaking poop and detonating TNT.
		local fly = Isaac.Spawn(EntityType.ENTITY_FLY, 0, 0, lib.ZeroVector, lib.ZeroVector, nil)
		laser.Parent = fly
		fly:Remove()
	else
		laser.Parent = player
	end
	
	laser.CollisionDamage = 0
	laserData.mementoMoriLaserParent = player
	laserData.mementoMoriLaserAnchor = player
	laser.Target = sigils[i]
	laser.DisableFollowParent = true
	
	laser:SetTimeout(-1)
	laser.Mass = 0
	laserData.isMementoMoriLinkLaser = true
	sigils[i]:GetData().linkLaser = laser
	
	if isReturnSigil then
		laserData.mementoMoriLaserAnchor = sigils[1]
		laser.Target = sigils[i]
	elseif i > 1 and sigils[i-1] and sigils[i-1]:GetData().linkLaser then
		sigils[i-1]:GetData().linkLaser:GetData().mementoMoriLaserAnchor = sigils[i]
		sigils[i-1].Target = sigils[i]
	end

	-- Fix the colors.
	if laserData.isDefaultMementoMoriLinkLaser then
		laser:GetData().mementoMoriLinkLaserAltColor = linkLaserColor
		if linkLaserColor then
			laserData.mementoMoriLaserAnim = linkLaserColor
		else
			laserData.mementoMoriLaserAnim = "Laser0"
		end
	end
	
	mod:MementoMoriLaserUpdate(laser)
	laser:Update()
	
	if laserData.isDefaultMementoMoriLinkLaser then
		laser:GetSprite():Play(laserData.mementoMoriLaserAnim, true)
	end
	
	--[[local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 0, player.Position, lib.ZeroVector, player):ToEffect()
	poof.Color = lib.NewColor(1,1,1) * sigils[i].Color]]
	sigils[i]:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
end

--------------------------------------------------
---- MAIN ACTIVATION FUNCTION
--------------------------------------------------

function mod:ForceActivateMementoMori(player)
	if not player:HasCollectible(kMementoMori) then return end
	local playerData = player:GetData()
	playerData.lastMementoMoriActivationTime = game:GetFrameCount()
	mod:ActivateMementoMori(nil, nil, player)
end

local function SpawnSlashTrail(player, pos)
	local pData = player:GetData()
	
	if pData.mementoMoriTrail and pData.mementoMoriTrail:Exists() then
		pData.mementoMoriTrail.Parent = nil
		pData.mementoMoriTrail.MinRadius = 0.05
	end
	
	local hasEpicFetus = lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS)
	
	-- Spawn the slashing trail, unless the player has Brimstone or Epic Fetus.
	if not lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) and not hasEpicFetus then
		local mementoMoriTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SPRITE_TRAIL, 0, pos or player.Position, lib.ZeroVector, player):ToEffect()
		mementoMoriTrail.DepthOffset = 255
		mementoMoriTrail.RenderZOffset = -255
		mementoMoriTrail.Color = GetSlashColor(player)
		mementoMoriTrail.MinRadius = 0.05
		local width = kMementoMoriTrailWidth
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
			width = width * 0.5
		end
		mementoMoriTrail:GetData().baseWidth = width
		mod:UpdateMementoMoriTrailWidth(player, mementoMoriTrail, 0)
		mementoMoriTrail.Parent = player
		mementoMoriTrail:GetData().isMementoMoriTrail = true
		mementoMoriTrail:Update()
		
		pData.mementoMoriTrail = mementoMoriTrail
	end
end

-- When the player uses Memento Mori.
function mod:ActivateMementoMori(_, _, player, _, _, _)
	local playerData = player:GetData()
	
	-- Disable activations on the first frame of a room to avoid potential issues.
	if game:GetLevel():GetCurrentRoom():GetFrameCount() == 0 or playerData.mementoMoriActive then
		return nil
	end
	
	-- Initialize the sigils if necessary.
	if not playerData.mementoMoriSigils then
		playerData.mementoMoriSigils = {}
	end
	local sigils = playerData.mementoMoriSigils
	
	local forceAttackTrigger = false
	local currentFrame = game:GetFrameCount()
	
	local isDoubleTap =
			playerData.lastMementoMoriActivationTime
			and currentFrame - playerData.lastMementoMoriActivationTime < kMementoMoriDoubleTapWindow

	-- TRUE if this input is a double tap AND the previous activation spawned a sigil.
	local isDoubleTapWithSigilPlace = isDoubleTap
			and playerData.mementoMoriLastSigilPlaced == playerData.lastMementoMoriActivationTime
	
	-- Force the attack activation on double taps, but only if we didn't place our only sigil from the first tap.
	if isDoubleTap and (#sigils > 1 or (#sigils == 1 and not isDoubleTapWithSigilPlace)) then
		forceAttackTrigger = true
	end
	
	if isDoubleTap then
		playerData.lastMementoMoriActivationTime = 0
	else
		playerData.lastMementoMoriActivationTime = currentFrame
	end
	
	if #sigils >= kMementoMoriMaxSigils or forceAttackTrigger then
		-- Initiate the attack.
		
		-- If the player activated Memento Mori via a double tap, and the first tap placed a sigil,
		-- then remove that sigil.
		if (forceAttackTrigger and isDoubleTapWithSigilPlace) or sigils[#sigils].Position:Distance(player.Position) < 10 then
			if #sigils == 1 then
				return nil
			end
			if sigils[#sigils]:GetData().mementoMoriTechEye then
				sigils[#sigils]:GetData().mementoMoriTechEye:Remove()
			end
			sigils[#sigils]:Remove()
			sigils[#sigils] = nil
			--playerData.mementoMoriIndex = playerData.mementoMoriIndex - 1
			--playerData.mementoMoriStocks = playerData.mementoMoriStocks + 1
		end
		
		
		-- True mirrored
		--[[
		local testPos
		local newSigils = {}
		for i, sigil in pairs(sigils) do
			newSigils[#sigils - i + 1] = sigil
		end
		local newSigils2 = {}
		for i, sigil in pairs(newSigils) do
			if i == #sigils then
				testPos = sigil.Position
				sigil:Remove()
			else
				table.insert(newSigils2, sigil)
			end
		end
		sigils = newSigils2
		playerData.mementoMoriSigils = newSigils2
		
		for i, sigil in pairs(sigils) do
			local laser = sigil:GetData().linkLaser
			local laserData = laser:GetData()
			
			laserData.mementoMoriLaserParent = player
			laserData.mementoMoriLaserAnchor = sigils[i+1] or player
			laser.Target = sigil
			sigil.Target = sigils[i+1] or player
		end
		
		-- With "My Reflection", spawn another Sigil that will return the player to their original position.
		--if lib.HasItem(player, CollectibleType.COLLECTIBLE_MY_REFLECTION) or playerData.mementoMoriEpicFetus then
			mod:SpawnSigil(player, true)
		--end
		
		local pppp = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 1, player.Position, lib.ZeroVector, player):ToEffect()
		pppp.Color = lib.NewColor(0,0,0,0.4)
		pppp.SpriteScale = Vector(0.5, 0.5)
		
		local pooffff = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, player.Position, lib.ZeroVector, player):ToEffect()
		pooffff.Color = lib.NewColor(0,0,0,0.5)
		local pooffff2 = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, testPos, lib.ZeroVector, player):ToEffect()
		pooffff2.Color = lib.NewColor(0,0,0,0.5)
		
		player.Position = testPos
		]]
		
		SpawnSlashTrail(player)
		
		playerData.mementoMoriAttackDelay = 0
		playerData.mementoMoriActive = true
		playerData.mementoMoriCombo = 0
		playerData.mementoMoriIndex = #sigils
		
		local hasEpicFetus = lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS)
		
		-- Epic Fetus & Anti-Gravity will trigger the attacks without moving the player.
		if hasEpicFetus or lib.HasItem(player, CollectibleType.COLLECTIBLE_ANTI_GRAVITY) then
			playerData.disjointedMementoMori = true
			if hasEpicFetus then
				playerData.mementoMoriEpicFetus = true
				local targetingLaser = EntityLaser.ShootAngle(2, player.Position, 90, 0, Vector(0, -1025), nil)
				targetingLaser.Color = GetSlashColor(player)
				targetingLaser.Parent = player
				targetingLaser.ParentOffset = lib.ZeroVector
				targetingLaser.DisableFollowParent = true
				targetingLaser:SetMaxDistance(1000)
				targetingLaser:GetData().isMementoMoriEpicFetusLaser = true
				playerData.mementoMoriEpicFetusLaser = targetingLaser
			end
		else
			-- Player is intangible during the attack animation.
			player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
			player.ControlsEnabled = false
			player.Visible = false
			if player.GridCollisionClass == EntityGridCollisionClass.GRIDCOLL_WALLS or player.GridCollisionClass == EntityGridCollisionClass.GRIDCOLL_NOPITS then
				playerData.mementoMoriOriginalGridCol = player.GridCollisionClass
			else
				playerData.mementoMoriOriginalGridCol = EntityGridCollisionClass.GRIDCOLL_GROUND
			end
			player.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
		end
		
		-- Visual poof from the attack triggering.
		if not hasEpicFetus then
			local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 1, player.Position, lib.ZeroVector, player):ToEffect()
			poof.Color = lib.NewColor(0,0,0,0.4)
			poof.SpriteScale = Vector(0.5, 0.5)
		end
		
		-- Check if the player drew a pentagram with the sigils.
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_BLACK_POWDER) then
			mod:CheckPentagram(player, sigils)
		end
		
		if FiendFolio and player:HasCollectible(FiendFolio.ITEM.COLLECTIBLE.EXCELSIOR) then
			local numRockets = #sigils
			if numRockets > 4 then
				numRockets = numRockets + 1
			end
			if Excelsior then
				numRockets = numRockets + 1
			end
			table.insert(playerData.ExcelsiorQueuedUpdates, {
				Count = numRockets,
				Damage = math.max(player.Damage, 7),
				Init = true,
			})
		end
	elseif not isDoubleTap --[[and playerData.mementoMoriStocks > 0]] then
		local mostRecentSigil = sigils[#sigils]
		if mostRecentSigil and mostRecentSigil.Position:Distance(player.Position) < 10 then
			return nil
		end
		
		-- Spawn a sigil.
		mod:SpawnSigil(player)
		playerData.mementoMoriLastSigilPlaced = currentFrame

		-- Gain brief iframes.
		--playerData.mementoMoriIFrames = kMementoMoriMaxIFrames
		
		-- Consume a sigil stock.
		--playerData.mementoMoriStocks = playerData.mementoMoriStocks - 1
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.ActivateMementoMori, kMementoMori)

--------------------------------------------------
---- LASERS
--------------------------------------------------

-- Replaces the sprite of the "impact" for Memento Mori link lasers so they look like they fade out.
function mod:MementoMoriLinkLaserTips(eff)
	if eff.SpawnerEntity and eff.SpawnerEntity:GetData().isDefaultMementoMoriLinkLaser then
		local altColor = eff.SpawnerEntity:GetData().mementoMoriLinkLaserAltColor
		local filename = "memento_mori_link_laser_tip"
		if altColor then
			filename = filename .. "_" .. altColor
		end
		eff:GetSprite():Load("gfx/" .. filename .. ".anm2", true)
		eff:GetSprite():LoadGraphics()
		eff:GetSprite():Play("End", true)
		eff.SpriteScale = eff.SpawnerEntity.SpriteScale
		eff.Color = eff.SpawnerEntity.Color
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.MementoMoriLinkLaserTips, EffectVariant.LASER_IMPACT)

--[[function mod:TestLaseru(laser)
	local laserData = laser:GetData()
	if laserData.isMementoMoriLinkLaser and laserData.mementoMoriRenderCounter then
		laser:GetSprite():SetFrame(laserData.mementoMoriLaserAnim, laser.FrameCount % 4)
		if laserData.mementoMoriRenderCounter == 1 then
			laserData.mementoMoriRenderCounter = 0
		elseif laserData.mementoMoriRenderCounter == 0 then
			laserData.mementoMoriRenderCounter = -1
			--laser:Render(lib.ZeroVector)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_RENDER, mod.TestLaseru)]]

function mod:MementoMoriLaserUpdate(laser)
	local laserData = laser:GetData()
	-- Sigil-linking lasers.
	if laserData.isMementoMoriLinkLaser then
		--local player = laser.Parent:ToPlayer()
		local player = laserData.mementoMoriLaserParent
		
		if not laser.Target or not laser.Target:Exists() or not player then
			laser:Remove()
			return nil
		end
		
		if laser.FrameCount % 5 == 0 and laserData.positiveTearFlagMask and laserData.negativeTearFlagMask then
			-- Refresh TearFlags
			local laserParams = player:GetTearHitParams(WeaponType.WEAPON_BRIMSTONE)
			laser.TearFlags = laserParams.TearFlags
			laser:AddTearFlags(laserData.positiveTearFlagMask)
			laser:ClearTearFlags(laserData.negativeTearFlagMask)
			laser:ClearTearFlags(kSigilLinkLaserForbiddenTearFlags)
		else
			-- Clear TearFlags that we don't want to trigger every single frame.
			-- (They can still trigger, just limited to one roll every 5 frames.)
			laser:ClearTearFlags(TearFlags.TEAR_SPORE)
			laser:ClearTearFlags(TearFlags.TEAR_BOOGER)
			laser:ClearTearFlags(TearFlags.TEAR_STICKY)
			laser:ClearTearFlags(TearFlags.TEAR_LIGHT_FROM_HEAVEN)
		end
		--[[Isaac.DebugString("hi")
		
		laser:ClearEntityFlags(1152921504606846975)
		Isaac.DebugString(laser.GridCollisionClass)
		--laser.GridCollisionClass = GridCollisionClass.COLLISION_NONE
		laser.Child = nil
		if laser.FrameCount == 0 then
			laser.Parent = player
		end
		if laser.FrameCount > 0 then
			laser.Parent = nil
			laser.SpawnerEntity = nil
			laser.SpawnerType = 0
			laser.SpawnerVariant = 0
		end]]
		--[[if laser.FrameCount > 0 then
			laser.Parent = nil
		end]]
		
		--laser:GetSprite():Play(laserData.mementoMoriLaserAnim, true)
		--laserData.mementoMoriRenderCounter = 1
		
		if laserData.isDefaultMementoMoriLinkLaser then
			laser:GetSprite():SetFrame(laserData.mementoMoriLaserAnim, laser.FrameCount % 4)
			
			if not laserData.isForDeathShadow then
				-- Make the paths flash slightly
				local freq = 30
				local freqOffset = 3 * (laser.Target:GetData().mementoMoriSigilIndex or 0)
				
				if (game:GetFrameCount() + freqOffset) % freq == 0 then
					local dur = 10
					local x = 0.3
					
					local c = laser.Color
					c:SetOffset(x,x,x)
					laser:SetColor(c, dur, 1, true, false)
					
					c = laser.Target.Color
					c:SetOffset(x,x,x)
					laser.Target:SetColor(c, dur, 1, true, false)
				end
			end
		end
		
		laser.ParentOffset = Vector(0,0)
		laser.PositionOffset = Vector(0,0)
		laser.Velocity = lib.ZeroVector
		
		local startPos = laser.Position
		local endPos = laser.Target.Position
		
		local anchor = laserData.mementoMoriLaserAnchor
		if anchor and anchor:Exists() then
			startPos = anchor.Position
		end
		
		--[[if player:HasCollectible(CollectibleType.COLLECTIBLE_CONTINUUM) then
			startPos = lib.FindWall(endPos, startPos - endPos)
			endPos = lib.FindWall(startPos, endPos - startPos)
		end]]
		
		laser.Position = startPos
		
		local pDiff = endPos - startPos
		
		local length = pDiff:Length()
		if laser.Variant == 2 then
			length = length - 17
		elseif laserData.isDefaultMementoMoriLinkLaser then
			length = length - 15
		end
		
		if length <= 16 then
			length = 16
			laser.Visible = false
		else
			laser.Visible = true
		end
		
		laser.Angle = pDiff:GetAngleDegrees()
		laser:SetMaxDistance(length)
	end
	
	-- Electricity.
	if laserData.isMementoMoriElectricity then
		laser:ClearTearFlags(TearFlags.TEAR_JACOBS)
		if laser.Parent then
			laser.Position = laser.Parent.Position
		end
		if laser.Target then
			laser:SetMaxDistance(laser.Position:Distance(laser.Target.Position))
			laser.Angle = (laser.Target.Position - laser.Position):GetAngleDegrees()
		else
			laser.Angle = laser.Angle + 69
		end
	end
	
	if laserData.isMementoMoriHitboxLaser and not laserData.isMementoMoriBrimstone then
		if not laserData.sigil or not laserData.sigil:Exists() then
			laser:Die()
			return nil
		end
	end
	
	-- Tech X ring around sigils.
	if laserData.isMementoMoriTechRing then
		if not laser.Parent then
			laser:Remove()
		elseif laser.Parent:GetSprite():IsPlaying("Death") then
			laser:GetData().isTechXMeleeRing = false
			local c = laser.Color
			if c.A <= 0 then
				laser:Remove()
			else
				c:SetTint(c.R, c.G, c.B, c.A - 0.075)
				laser.Color = c
			end
		end
	end
	
	-- Brimstone attack lasers (used in place of Hitbox lasers with Brimstone).
	if laserData.isMementoMoriBrimstone then
		local width = laserData.mementoMoriBrimstoneWidth
		local timeOut = laser.Timeout + 18
		if laser.Variant == 1 and laser.Parent and laser.Parent:ToLaser() then
			timeOut = laser.Parent:ToLaser().Timeout + 18
		end
		if timeOut == 0 then
			width = 0
		elseif timeOut < 10 then
			width = width * timeOut/10
		end
		laser.SpriteScale = Vector(width, 1)
		
		local player = laser.Parent:ToPlayer()

		if player then
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_TOUGH_LOVE)
					and player:GetCollectibleRNG(kMementoMori):RandomFloat() < lib.GetActivationChance(0.1, player.Luck, 10, true) then
				laser.CollisionDamage = laserData.mementoMoriLaserDamage * 3.2
			else
				laser.CollisionDamage = laserData.mementoMoriLaserDamage
			end
		end
		
		if laserData.doubleUpdate and laserData.lastUpdate ~= game:GetFrameCount() then
			laserData.lastUpdate = game:GetFrameCount()
			laser:Update()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, mod.MementoMoriLaserUpdate)

-- Fires additional brimstone lasers in random directions if the player had multishot.
function mod:MaybeSpawnChildBrimstoneLasers(player, parentLaser, pos)
	local numLasers = lib.GetNumProjectiles(player)
	for i=1, numLasers-1 do
		local childLaser = Isaac.Spawn(EntityType.ENTITY_LASER, 1, 0, pos, lib.ZeroVector, player):ToLaser()
		childLaser:GetData().doubleUpdate = parentLaser:GetData().doubleUpdate
		childLaser:GetData().mementoMoriBrimstoneWidth = parentLaser:GetData().mementoMoriBrimstoneWidth
		childLaser:GetData().maxHits = parentLaser:GetData().maxHits
		childLaser:GetData().sigil = parentLaser:GetData().sigil
		
		childLaser:GetData().isMementoMoriBrimstone = true
		childLaser:GetData().isMementoMoriHitboxLaser = true
		
		childLaser.Parent = parentLaser
		childLaser:SetTimeout(parentLaser.Timeout)
		childLaser.CollisionDamage = parentLaser.CollisionDamage
		childLaser.TearFlags = parentLaser.TearFlags
		childLaser.Color = parentLaser.Color
		
		childLaser.DisableFollowParent = true

		childLaser.Angle = player:GetCollectibleRNG(kMementoMori):RandomInt(360)
		
		childLaser.ParentOffset = Vector(0,0)
		childLaser.PositionOffset = Vector(0,0)
		childLaser:Update()
	end
end

----------------------------------------------------------------------------------------------------
---- GLOBAL UPDATE HANDLER
---- Mostly handles the global update processing of the Memento Mori attack/slashing.
----------------------------------------------------------------------------------------------------

function mod:CalcMementoMoriMaxHits(player, forDeathShadow)
	local tears = lib.CalcUnmodifiedTears(player)
	
	local maxHits = 1
	if not forDeathShadow and lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
		maxHits = math.min(math.floor(tears) + 1, 50)
	elseif tears <= 6 then
		maxHits = math.max(math.floor(tears - 1), 1)
	else
		maxHits = math.min(math.floor(0.5*tears + 2), 15)
	end
	
	return maxHits
end

function mod:CalcMementoMoriDamage(player, maxHits, flatBonus, comboBonus, forDeathShadow)
	-- Total damage that should be achieved if the player hits an enemy with all slashes.
	local totalDamage = kMementoMoriBaseDamageMult * math.max(lib.CalcUnmodifiedTears(player), 1) * player.Damage + flatBonus
	
	-- Minimum per-hit damage.
	local minDamage = player.Damage * 0.6
	
	-- Ipecac's involvement with the damage stat is really wonky.
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC) then
		if player:HasWeaponType(WeaponType.WEAPON_TEARS) then
			totalDamage = totalDamage * 0.5
		else
			totalDamage = totalDamage * 2
		end
	end
	
	-- Final calc for how much damage each tick of the laser should deal.
	local laserDamage = math.max(minDamage, totalDamage / maxHits) * comboBonus
	if not forDeathShadow and lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
		laserDamage = laserDamage * 0.5
	end
	
	return laserDamage
end

function mod:GiveTaintedSamaelPocketActive(player)
	if not REPENTANCE then return end
	
	if lib.IsTaintedSamael(player) and not lib.IsChallengeSamael(player) and player:GetActiveItem(ActiveSlot.SLOT_POCKET) == 0 then
		player:SetPocketActiveItem(kMementoMori)
	end
	
	if player:GetActiveItem(ActiveSlot.SLOT_POCKET) == kMementoMori then
		if player:GetActiveItem(ActiveSlot.SLOT_PRIMARY) == kMementoMori then
			player:RemoveCollectible(kMementoMori, true, ActiveSlot.SLOT_PRIMARY)
		end
		if player:GetActiveItem(ActiveSlot.SLOT_SECONDARY) == kMementoMori then
			player:RemoveCollectible(kMementoMori, true, ActiveSlot.SLOT_SECONDARY)
		end
	end
end

function mod:TaintedSamaelPocketActiveCheck()
	for _, player in pairs(lib.GetPlayers()) do
		mod:GiveTaintedSamaelPocketActive(player)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, mod.TaintedSamaelPocketActiveCheck)
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.TaintedSamaelPocketActiveCheck)

function mod:TaintedSamaelInit(player)
	if lib.IsTaintedSamael(player) and not lib.IsChallengeSamael(player) then
		player:AddBoneHearts(1)
		lib.ScheduleForUpdate(function()
			mod:TaintedSamaelPocketActiveCheck()
		end, 0, nil, true)
		
		lib.PreLoadGfx({
			"memento_mori_sigil",
			"memento_mori_particle",
			"memento_mori_trail",
			"memento_mori_projectile",
		})
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_INIT, mod.TaintedSamaelInit)

function mod:MementoMoriHandler(player)
	if not lib.IsTaintedSamael(player) and not lib.HasItem(player, kMementoMori) then return end
	
	local playerData = player:GetData()
	local rng = player:GetCollectibleRNG(kMementoMori)
	local room = game:GetRoom()
	local isNewRoom = game:GetLevel():GetCurrentRoom():GetFrameCount() == 0
	
	-- Initialize or reset the set of sigils when necessary.
	if not playerData.mementoMoriSigils or isNewRoom then
		if playerData.mementoMoriSigils then
			for i=0, #playerData.mementoMoriSigils do
				if playerData.mementoMoriSigils[i] then
					playerData.mementoMoriSigils[i]:Remove()
				end
			end
		end
		playerData.mementoMoriSigils = {}
		playerData.mementoMoriAttackDelay = 0
	end
	local sigils = playerData.mementoMoriSigils
	
	-- Brief invuln state after attacking.
	if playerData.mementoMoriIFrames and playerData.mementoMoriIFrames > 0 then
		--[[if playerData.mementoMoriIFrames % 6 == 0 then
			player:SetColor(lib.NewColor(0,0,0,1,0,0,0), 3, 999, true, true)
		end]]
		playerData.mementoMoriIFrames = playerData.mementoMoriIFrames - 1
	end
	
	if not playerData.mementoMoriActive then return end
	
	if not playerData.disjointedMementoMori then
		player:SetMinDamageCooldown(kMementoMoriMaxIFrames)
	end
	
	-- "Active" attack code (slashing/dashing between sigils).
	if playerData.mementoMoriAttackDelay <= 0 then
		local i = playerData.mementoMoriIndex
		
		if sigils[i] and sigils[i]:Exists() then
			-- Start a "slash" to the next sigil.
			if not playerData.mementoMoriEpicFetus then
				sfxManager:Play(540, 0.95, 0, false, 0.85 + 0.05 * playerData.mementoMoriCombo)
			end
			
			local tearParams = player:GetTearHitParams(WeaponType.WEAPON_BRIMSTONE)
			
			local startPos = playerData.mementoMoriForceStartPos or player.Position
			if sigils[i+1] then
				startPos = sigils[i+1].Position
			end
			local endPos = sigils[i].Position
			
			local pathStart
			local pathEnd
			
			playerData.mementoMoriLastSigilPos = sigils[i].Position
			playerData.mementoMoriStartPosition = startPos
			playerData.mementoMoriEndPosition = endPos
			
			if player:HasCollectible(CollectibleType.COLLECTIBLE_CONTINUUM) then
				startPos = lib.FindWall(endPos, startPos - endPos)
				endPos = lib.FindWall(startPos, endPos - startPos)
				local padding = 150
				pathStart = startPos + (startPos - endPos):Resized(padding)
				pathEnd = endPos + (endPos - startPos):Resized(padding)
				SpawnSlashTrail(player, pathStart)
			end
			
			local pDiff = endPos - startPos
			local length = pDiff:Length()
			if length <= 0.1 then
				length = 0.1
			end
			
			playerData.mementoMoriTrajectory = endPos - startPos
			playerData.mementoMoriCurrentTrajectory = playerData.mementoMoriTrajectory
			playerData.mementoMoriPosition = startPos

			-- Decide how many "hits" each slash should be capable of doing.
			local tears = math.max(lib.CalcUnmodifiedTears(player), 1)
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_CURSED_EYE) then
				tears = tears + 2
			end
			
			local maxHits = mod:CalcMementoMoriMaxHits(player)
			
			local timeOut = maxHits*2
			
			local bloodClotActive = (lib.HasItem(player, CollectibleType.COLLECTIBLE_BLOOD_CLOT) or lib.HasItem(player, CollectibleType.COLLECTIBLE_CHEMICAL_PEEL)) and i % 2 == 0
			local bloodClotBonus = 0
			local trailColor = GetSlashColor(player)
			
			if bloodClotActive then
				local bloodClots = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BLOOD_CLOT)
				local chemPeels = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_CHEMICAL_PEEL)
				bloodClotBonus = bloodClots + chemPeels * 2
				trailColor = lib.BloodColor
			end
			
			if playerData.mementoMoriTrail and not lib.HasItem(player, CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE) then
				playerData.mementoMoriTrail.Color = trailColor
			end
			
			-- Bonus damage from successive slashes (combo).
			local comboMultiplierBonus = kMementoMoriComboDamageMult * playerData.mementoMoriCombo
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) then
					comboMultiplierBonus = comboMultiplierBonus * 0.85
				end
				comboMultiplierBonus = comboMultiplierBonus * 4.5
			end
			local comboMultiplier = 1 + comboMultiplierBonus

			local laserDamage = mod:CalcMementoMoriDamage(player, maxHits, bloodClotBonus, comboMultiplier)
			
			-- Spawn the hitbox laser.
			local laser = nil
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) and not playerData.mementoMoriEpicFetus then
				-- With brimstone, spawn an actual brimstone laser.
				local laserType = 11
				local laserWidth = 1
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
					if playerData.mementoMoriCombo >= 4 then
						laserType = 6
						timeOut = timeOut + 1
					else
						laserWidth = lib.Lerp(0.5, 1.0, playerData.mementoMoriCombo / 3)
					end
				end
				laser = Isaac.Spawn(EntityType.ENTITY_LASER, laserType, 0, startPos - pDiff:Resized(30), lib.ZeroVector, player):ToLaser()
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_SOY_MILK) or lib.HasItem(player, CollectibleType.COLLECTIBLE_ALMOND_MILK) then
					timeOut = math.ceil(timeOut * 2.5)
					laser:GetData().doubleUpdate = true
					laserWidth = laserWidth * 0.5
				end
				laser:GetData().mementoMoriBrimstoneWidth = laserWidth
				laser:GetData().isMementoMoriBrimstone = true
			else
				-- Spawn an invisible "hitbox" laser.
				laser = Isaac.Spawn(EntityType.ENTITY_LASER, 6, kMementoMoriHitboxLaserVariant, startPos - pDiff:Resized(50), lib.ZeroVector, sigils[i]):ToLaser()
				if maxHits > 6 then
					sigils[i]:GetData().hitCooldown = 1
					timeOut = maxHits
				else
					sigils[i]:GetData().hitCooldown = 2
				end
				laser.Visible = false
				laser.OneHit = true
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_PISCES) or player:HasTrinket(TrinketType.TRINKET_BLISTER) then
					laser.Mass = 20
				end
			end
			
			-- Initialize a bunch of laser values.
			laser:GetData().sigil = sigils[i]
			laser.Parent = player
			laser:SetTimeout(timeOut)
			laser.Angle = pDiff:GetAngleDegrees()
			laser.DisableFollowParent = true
			laser:SetMaxDistance(length)
			
			laser.ParentOffset = Vector(0,0)
			laser.PositionOffset = Vector(0,0)
	
			laser:GetData().isMementoMoriHitboxLaser = true
			sigils[i]:GetData().hitboxLaser = laser
			playerData.currentMementoMoriHitboxLaser = laser

			laser.CollisionDamage = laserDamage
			sigils[i]:GetData().mementoMoriAttackDamage = laserDamage
			laser:GetData().mementoMoriLaserDamage = laserDamage
			
			sigils[i]:GetData().maxHits = maxHits
			laser:GetData().maxHits = maxHits
			
			playerData.mementoMoriHitDelay = lib.GetUnmodifiedFireDelay(player)
			
			local laserData = sigils[i]:GetData().linkLaser:GetData()
			laser.TearFlags = tearParams.TearFlags
			
			if laserData.positiveTearFlagMask and laserData.negativeTearFlagMask then
				laser:AddTearFlags(laserData.positiveTearFlagMask)
				laser:ClearTearFlags(laserData.negativeTearFlagMask)
			end
			
			-- Default synergies with Proptosis and Lump of Coal are handled elsewhere.
			if not laser:GetData().isMementoMoriBrimstone then
				laser:ClearTearFlags(TearFlags.TEAR_SHRINK | TearFlags.TEAR_GROW)
			end
			
			-- The effect of "My Reflection" completely messes up the pathing, so we give it a custom
			-- synergy instead, handled elsewhere.
			laser:ClearTearFlags(TearFlags.TEAR_BOOMERANG)
			
			laser:ClearTearFlags(TearFlags.TEAR_BOUNCE)
			
			laser.Color = GetSigilColor(player)
			
			-- Update the laser so we can reference its path.
			laser:Update()
			
			-- An additional update is needed for brimstone.
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_BRIMSTONE) and not playerData.mementoMoriEpicFetus then
				laser:Update()
				-- Multishot brimstone.
				mod:MaybeSpawnChildBrimstoneLasers(player, laser, sigils[i].Position)
			end
			
			-- Create a set of Vector points: The start point, the laser's sample points, then the end point.
			playerData.mementoMoriPath = {}
			if pathStart then
				playerData.mementoMoriPath[0] = pathStart
			else
				playerData.mementoMoriPath[0] = Vector(startPos.X, startPos.Y)
			end
			
			if #laser:GetSamples() > 2 and laser.MaxDistance > 45 then
				for n=0, #laser:GetSamples()-1 do
					playerData.mementoMoriPath[#playerData.mementoMoriPath+1] = Vector(laser:GetSamples():Get(n).X, laser:GetSamples():Get(n).Y)
				end
			else
				local k = 12
				if player:HasTrinket(TrinketType.TRINKET_PULSE_WORM) then
					k = 60
				elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_PROPTOSIS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_LUMP_OF_COAL) then
					k = 30
				end
				for n=1, k-2 do
					playerData.mementoMoriPath[#playerData.mementoMoriPath+1] = lib.Lerp(startPos, endPos, n/(k-1))
				end
			end
			playerData.mementoMoriPath[#playerData.mementoMoriPath+1] = Vector(endPos.X, endPos.Y)
			
			if pathEnd then
				playerData.mementoMoriPath[#playerData.mementoMoriPath+1] = pathEnd
			end
			
			if playerData.mementoMoriTrail then
				-- "Slash" trail should persist longer if its not just a straight line.
				if #playerData.mementoMoriPath >= 30 then
					playerData.mementoMoriTrail.MinRadius = 0.002
				elseif #playerData.mementoMoriPath > 10 then
					playerData.mementoMoriTrail.MinRadius = 0.01
				else
					playerData.mementoMoriTrail.MinRadius = 0.065
				end
			end
			
			-- Save the full length of our path so we don't have to recalculate it.
			playerData.mementoMoriPathLength = mod:CalcPathLength(playerData.mementoMoriPath)
			
			-- Counter for how many frames we've been doing this attack.
			playerData.mementoMoriAttackTime = 0
			
			-- Remove the sigil and move the index to the next sigil.
			sigils[i]:GetSprite():Play("Death")
			playerData.mementoMoriIndex = i - 1
	
			-- Calculate the # of frames the attack animation should last.
			-- The time increases slightly with distance to accommodate the animations.
			playerData.mementoMoriAttackDuration = mod:CalculateMementoMoriDuration(playerData.mementoMoriPathLength)
			
			-- The time until we reach the next sigil (the duration of the attack itself) is slightly
			-- shorter than the time until the next attack is triggered.
			playerData.mementoMoriAttackDelay = playerData.mementoMoriAttackDuration + kMementoMoriAttackDelay
			
			playerData.mementoMoriPrevTargetDist = 0
			
			if player:GetPlayerType() == lib.TaintedSamaelId and lib.HasBirthcake(player) then
				mod:MementoMoriPetrification(player, playerData.mementoMoriAttackDuration)
			end
			
			if playerData.mementoMoriEpicFetus then
				playerData.mementoMoriAttackDelay = playerData.mementoMoriAttackDelay * 2
				playerData.mementoMoriAttackDuration = playerData.mementoMoriAttackDelay
				-- With epic fetus we don't actually want to keep the hitbox laser around after we get
				-- the path from it.
				laser:Remove()
			elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_JACOBS_LADDER) or laser:HasTearFlags(TearFlags.TEAR_JACOBS) then
				-- Emit electricity from the player while attacking.
				mod:FireElectricity(player, math.ceil(playerData.mementoMoriAttackDuration))
			end
			
			-- Handles the width of the slashing trail.
			if playerData.mementoMoriTrail and playerData.mementoMoriTrail:Exists() then
				if playerData.mementoMoriCombo > 0 and lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
					playerData.mementoMoriTrail:GetData().baseWidth = (playerData.mementoMoriTrail:GetData().baseWidth or 1) * 1.5
				end
				mod:UpdateMementoMoriTrailWidth(player, playerData.mementoMoriTrail, 0)
			end
			
			-- -- SPLIT TEARS BEGIN -- --
			local splitTears = 0
			local randomAngles = true
			
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_CRICKETS_BODY) or laser:HasTearFlags(TearFlags.TEAR_QUADSPLIT)
					or lib.HasItem(player, CollectibleType.COLLECTIBLE_MUCORMYCOSIS) or laser:HasTearFlags(TearFlags.TEAR_SPORE) then
				splitTears = 4
				randomAngles = false
			elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_COMPOUND_FRACTURE) or laser:HasTearFlags(TearFlags.TEAR_BONE) then
				splitTears = rng:RandomInt(3) + 1
			end
			
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_MONSTROS_LUNG) then
				local projVel = pDiff:Resized(player.ShotSpeed * 5)
				mod:FireProjectileGroup(player, 8, sigils[i].Position, projVel, 0.5, true, true)
				splitTears = 0
			elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_HAEMOLACRIA) or laser:HasTearFlags(TearFlags.TEAR_BURSTSPLIT) then
				local tear = player:FireTear(sigils[i].Position, lib.ZeroVector, false, true, false)
				tear.Height = 0
				tear:ClearTearFlags(TearFlags.TEAR_QUADSPLIT | TearFlags.TEAR_SPLIT | TearFlags.TEAR_BONE | TearFlags.TEAR_EXPLOSIVE)
				tear:Die()
			end
			
			if splitTears > 0 then
				local vel = Vector(0, player.ShotSpeed * 10):Rotated(rng:RandomInt(360))
				local pos = sigils[i].Position
				for x=0, splitTears-1 do
					local projVel
					if randomAngles then
						projVel = vel:Rotated(rng:RandomInt(360))
					else
						projVel = vel:Rotated((360/splitTears)*x)
					end

					local scale = 0.75
					local damageMult = 0.5
					if splitTears < 4 then
						scale = 1.0
						damageMult = 1.0
					end
					local tear = player:FireTear(pos, projVel, false, true, false, player, damageMult)
					tear.Scale = tear.Scale * scale
					
					if tear.FallingAcceleration > 0 then
						tear.FallingAcceleration = 0
						tear.Height = math.max(tear.Height, -15)
						tear.FallingSpeed = math.max(tear.FallingSpeed, 0.5)
					end
					
					if lib.HasItem(player, CollectibleType.COLLECTIBLE_MUCORMYCOSIS) and tear.Variant ~= TearVariant.SPORE then
						tear:ChangeVariant(TearVariant.SPORE)
						tear:GetSprite():LoadGraphics()
						tear:AddTearFlags(TearFlags.TEAR_SPORE)
					end
					
					if tear.Variant ~= TearVariant.BLUE and lib.SameColor(tear.Color, lib.SamaelTearColor) then
						tear.Color = lib.NullColor
					end
					
					tear:ClearTearFlags(TearFlags.TEAR_BURSTSPLIT)
					tear:ClearTearFlags(TearFlags.TEAR_QUADSPLIT)
				end
			end
			-- -- SPLIT TEARS END -- --
			
			-- Fart
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_JUPITER)
					or lib.HasItem(player, CollectibleType.COLLECTIBLE_BLACK_BEAN)
					or lib.HasItem(player, CollectibleType.COLLECTIBLE_MUCORMYCOSIS)
					or laser:HasTearFlags(TearFlags.TEAR_SPORE) then
				local color = lib.NullColor
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_MUCORMYCOSIS) or laser:HasTearFlags(TearFlags.TEAR_SPORE) then
					color = GetSigilColor(player)
					color:SetTint(color.R, color.G, color.B, 0.5)
				end
				game:Fart(sigils[i].Position, 85, player, 1, 0, color)
			end
			
			-- Ocular Rift but only via Fruit Cake. The regular item spawns them on sigils passively.
			if not lib.HasItem(player, CollectibleType.COLLECTIBLE_OCULAR_RIFT) and laser:HasTearFlags(TearFlags.TEAR_RIFT) then
				local rift = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.RIFT, 0, sigils[i].Position, lib.ZeroVector, player):ToEffect()
				rift.Parent = player
				rift.DepthOffset = 1
				rift.Timeout = 50
			end
			
			-- Flames
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_GHOST_PEPPER) then
				for x=1, maxHits do
					if rng:RandomFloat() < lib.GetActivationChance(0.0833, player.Luck, 11, true) then
						local projVel = Vector(13, 0):Rotated(rng:RandomInt(360))
						local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLUE_FLAME, 0, sigils[i].Position, projVel, player):ToEffect()
						flame:SetDamageSource(EntityType.ENTITY_PLAYER)
						flame.LifeSpan = 60
						flame.Timeout = 60
						flame.State = 1
						flame.CollisionDamage = player.Damage * 6
					end
				end
			end
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRDS_EYE) then
				for x=1, maxHits do
					if rng:RandomFloat() < lib.GetActivationChance(0.0833, player.Luck, 11, true) then
						local projVel = Vector(13, 0):Rotated(rng:RandomInt(360))
						local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.RED_CANDLE_FLAME, 0, sigils[i].Position, projVel, player):ToEffect()
						flame:SetDamageSource(EntityType.ENTITY_PLAYER)
						flame.CollisionDamage = player.Damage * 4
					end
				end
			end
			if FiendFolio and lib.HasItem(player, CollectibleType.COLLECTIBLE_PEPPERMINT) then
				local projVel = Vector(10, 0):Rotated(rng:RandomInt(360))
				FiendFolio:firePeppermint(player, sigils[i].Position, projVel)
			end
			
			-- Terra
			if lib.HasItem(player, CollectibleType.COLLECTIBLE_TERRA) or laser:HasTearFlags(TearFlags.TEAR_ROCK) then
				local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SHOCKWAVE, 0, sigils[i].Position, lib.ZeroVector, player):ToEffect()
				eff.Parent = player
				eff:SetRadii(0, 20 + 20 * playerData.mementoMoriCombo)
				eff:SetTimeout(5 + 3 * playerData.mementoMoriCombo)
			end
			
			-- Consecration
			if TaintedTreasure and TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.CONSECRATION) then
				TaintedTreasure:DoConsecrationWave(player, sigils[i].Position)
			end
			
			-- EMOJI GLASSES
			if sigils[i]:GetData().EmojiGlassesEffect then
				mod:EmojiSigilTrigger(sigils[i])
			end
			
			-- POP
			if (lib.HasItem(player, CollectibleType.COLLECTIBLE_POP) or laser:HasTearFlags(TearFlags.TEAR_POP)) and sigils[1] then
				local dir = (sigils[1].Position - (sigils[2] or player).Position):Normalized()
				--local dir = (sigils[1].Position - playerData.mementoMoriPath[#playerData.mementoMoriPath-1]):Normalized()
				local tip = sigils[1].Position + dir * 10
				
				if not playerData.mementoMoriPop then
					playerData.mementoMoriPop = mod:FirePopTear(player, tip)
					playerData.mementoMoriPop:GetData().mementoMoriPopTargetVel = dir * player.ShotSpeed * 20
				end
				local size = playerData.mementoMoriPop.Size * 2
				
				playerData.mementoMoriPopCounter = (playerData.mementoMoriPopCounter or -1) + 1
				--for n=1, 3 do
				local n = playerData.mementoMoriPopCounter
				if n > 0 then
					local rowOffset = dir:Resized(n * size)
					local columnOffset = dir:Rotated(90):Resized(n * size * 0.5)
					for x=0, n do
						local ballOffset = dir:Rotated(-90):Resized(x * size)
						local popTear = mod:FirePopTear(player, tip + rowOffset + columnOffset + ballOffset)
						popTear:GetData().mementoMoriParentPopTear = playerData.mementoMoriPop
					end
				end
				
				if i==1 then
					playerData.mementoMoriPopReady = true
				end
			end
			
			mod:MaybeSpawnEvilEye(player, pDiff, rng, maxHits)
			
			if lib.HasItem(player, lib.Access(Retribution, "ITEMS", "TECHNOLOGY_OMICRON")) then
				local ring = mod:AddLaserRing(player, nil, 40)
				ring:SetTimeout(math.floor(playerData.mementoMoriAttackDuration))
			end
			
			if room:GetAliveEnemiesCount() > 0 and player:HasCollectible(CollectibleType.COLLECTIBLE_BIRTHRIGHT) then
				mod:SpawnMementoMoriGhost(player, sigils[i].Position)
			end
			
			-- Count up the combo (number of slashes in one activation).
			playerData.mementoMoriCombo = playerData.mementoMoriCombo + 1
		else
			-- There are no sigils left - end the attack and reset a bunch of values.
			if not playerData.mementoMoriEpicFetus and room:GetFrameCount() > 1 then
				local pos = (sigils[0] or sigils[1] or player).Position
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, pos, lib.ZeroVector, player):ToEffect()
				poof.Color = lib.NewColor(0,0,0,0.5)
			end
			
			if playerData.disjointedMementoMori then
				playerData.disjointedMementoMori = false
				playerData.mementoMoriEpicFetus = false
			else
				--[[if player:HasCollectible(CollectibleType.COLLECTIBLE_CONTINUUM) and playerData.mementoMoriLastSigilPos then
					player.Position = playerData.mementoMoriLastSigilPos
				end]]
				if room:GetFrameCount() > 1 then
					player.Position = playerData.mementoMoriLastSigilPos
					player.Velocity = playerData.mementoMoriCurrentTrajectory:Resized(player.MoveSpeed * 5)
					playerData.mementoMoriForceScytheSwing = playerData.mementoMoriTrajectory
					playerData.mementoMoriIFrames = kMementoMoriMaxIFrames
				end
				player.ControlsEnabled = true
				player.Visible = true
				player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
				player.GridCollisionClass = playerData.mementoMoriOriginalGridCol or EntityGridCollisionClass.GRIDCOLL_GROUND
				playerData.mementoMoriOriginalGridCol = nil
			end
			
			if playerData.mementoMoriEpicFetusLaser and  playerData.mementoMoriEpicFetusLaser:Exists() then
				playerData.mementoMoriEpicFetusLaser:SetTimeout(1)
				playerData.mementoMoriEpicFetusLaser = nil
			end
			
			playerData.mementoMoriPop = nil
			playerData.mementoMoriPopCounter = nil
			playerData.mementoMoriPopReady = nil
			playerData.mementoMoriPopGo = nil
			
			playerData.mementoMoriActive = false
			
			if playerData.mementoMoriTrail and playerData.mementoMoriTrail:Exists() then
				playerData.mementoMoriTrail.Parent = nil
				playerData.mementoMoriTrail.MinRadius = 0.05
			end
			
			playerData.mementoMoriTrajectory = nil
			playerData.mementoMoriCurrentTrajectory = nil
			playerData.mementoMoriPosition = nil
			playerData.currentMementoMoriHitboxLaser = nil
			
			playerData.mementoMoriProjectileFired = nil
			playerData.mementoMoriForceStartPos = nil
			
			playerData.mementoMoriCombo = 0
			playerData.mementoMoriSigils = {}
			
			if player:GetPlayerType() == lib.TaintedSamaelId and lib.HasBirthcake(player) then
				mod:MementoMoriPetrification(player, 10 * lib.HasBirthcake(player))
			end
		end
	else
		-- An attack/slash is currently ongoing. Handle the effects.
		if playerData.mementoMoriPath and playerData.mementoMoriAttackTime <= playerData.mementoMoriAttackDuration then
			playerData.mementoMoriAttackTime = playerData.mementoMoriAttackTime + 1
			local percent = playerData.mementoMoriAttackTime / playerData.mementoMoriAttackDuration
			
			local targetDist = playerData.mementoMoriPathLength * percent
			
			local currentPos = mod:UpdateAlongPath(
					player,
					playerData.mementoMoriPath,
					playerData.mementoMoriPrevTargetDist or 0,
					targetDist)
			
			if not playerData.disjointedMementoMori then
				--[[if playerData.mementoMoriPosition then
					player.Position = playerData.mementoMoriPosition
				end]]
				local origPos = player.Position
				local targetPos = lib.Lerp(playerData.mementoMoriStartPosition, playerData.mementoMoriEndPosition, percent)
				targetPos = room:GetClampedPosition(targetPos, 0)
				player.Velocity = (targetPos - origPos) * 0.5
			end
			
			if currentPos:Distance(playerData.mementoMoriPosition) > 0 then
				playerData.mementoMoriCurrentTrajectory = currentPos - playerData.mementoMoriPosition
			end
			playerData.mementoMoriPosition = currentPos
			playerData.mementoMoriPrevTargetDist = targetDist
			
			if playerData.mementoMoriPopReady and playerData.mementoMoriPop and percent >= 0.99 then
				playerData.mementoMoriPop.Velocity = playerData.mementoMoriPop:GetData().mementoMoriPopTargetVel
				if (playerData.mementoMoriPopCounter or 0) > 2 then
					sfxManager:Play(Isaac.GetSoundIdByName("SamaelBillardsBreak"), 1.0, 0, false, 1.0)
				else
					sfxManager:Play(Isaac.GetSoundIdByName("SamaelBillardsBreakWeak"), 1.0, 0, false, 1.0)
				end
				playerData.mementoMoriPopGo = true
			end
			
			-- Fire the projectile(s).
			if not (player:HasWeaponType(WeaponType.WEAPON_BOMBS) and lib.HasItem(player, CollectibleType.COLLECTIBLE_ROCKET_IN_A_JAR))
					and lib.IsSamael(player) and not playerData.mementoMoriEpicFetus and not playerData.mementoMoriProjectileFired
					and not sigils[playerData.mementoMoriIndex] and percent >= 0.99 then
				if playerData.mementoMoriTrajectory and playerData.mementoMoriTrajectory:Length() > 0 then
					if not (TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.THE_BOTTLE) and mod:HasThrownKnifeScythes(player)) then
						mod:FireMementoMoriProjectiles(player, (sigils[0] or sigils[1]).Position, playerData.mementoMoriTrajectory)
					end
					playerData.mementoMoriProjectileFired = true
					sfxManager:Play(SoundEffect.SOUND_LIGHTBOLT, 1, 0, false, 1.0)
				end
			end
			
			-- Little bubbles that appear along the path.
			local bubbleTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HAEMO_TRAIL, 617, currentPos, lib.ZeroVector, player):ToEffect()
			local c = GetSlashColor(player)
			c:SetTint(c.R, c.G, c.B, 0.5)
			bubbleTrail.Color = c
			bubbleTrail.SpriteScale = Vector(0.85, 0.85)
			bubbleTrail.DepthOffset = -2
			bubbleTrail:GetSprite():Play("Poof2", true)
		end
		
		if playerData.mementoMoriTrail and lib.HasItem(player, CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE) then
			local speed = 25
			local i = (game:GetFrameCount() % speed) / speed
			local freq = 2 * math.pi
			local r = (math.sin(freq*i + 0) * 127 + 128) / 255
			local g = (math.sin(freq*i + 2) * 127 + 128) / 255
			local b = (math.sin(freq*i + 4) * 127 + 128) / 255
			playerData.mementoMoriTrail.Color = lib.NewColor(r,g,b)
		end
	
		playerData.mementoMoriAttackDelay = playerData.mementoMoriAttackDelay - 1
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.MementoMoriHandler)

--------------------------------------------------
---- BIRTHRIGHT
--------------------------------------------------

local kMementoMoriBirthrightGhostCost = 2
local kMementoMoriBirthrightGhostCap = 20

local MementoMoriBirthrightGhosts = {}

function mod:MementoMoriGhostHandler(player)
	if not lib.IsTaintedSamael(player) or not player:HasCollectible(CollectibleType.COLLECTIBLE_BIRTHRIGHT) then return end
	
	local data = player:GetData()
	
	if not data.mementoMoriSouls or data.mementoMoriSouls < 0 then
		data.mementoMoriSouls = 0
	elseif data.mementoMoriSouls > kMementoMoriBirthrightGhostCap * kMementoMoriBirthrightGhostCost then
		data.mementoMoriSouls = kMementoMoriBirthrightGhostCap * kMementoMoriBirthrightGhostCost
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.MementoMoriGhostHandler)

function mod:SpawnMementoMoriGhost(player, pos)
	local data = player:GetData()
	
	if not data.mementoMoriSouls or data.mementoMoriSouls < 1 then
		return
	end
	
	data.mementoMoriSouls = data.mementoMoriSouls - kMementoMoriBirthrightGhostCost
	
	local ghost = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PURGATORY, 1, pos, lib.ZeroVector, player):ToEffect()
	ghost:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	ghost:GetSprite():Play("Charge", true)
	ghost:GetData().mementoMoriGhost = true
	ghost.Color = GetSlashColor(player)
	MementoMoriBirthrightGhosts[ghost.InitSeed] = ghost
end

function mod:MementoMoriGhostExplosion(eff)
	if eff.FrameCount ~= 0 then return end
	
	for k, ghost in pairs(MementoMoriBirthrightGhosts) do
		if not ghost:Exists() and ghost.Position:Distance(eff.Position) < 1 then
			eff.Color = ghost.Color
			MementoMoriBirthrightGhosts[k] = nil
			return
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.MementoMoriGhostExplosion, 15)

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	MementoMoriBirthrightGhosts = {}
	
	-- Fix things when the player abruptly changes rooms mid-memento mori
	for _, player in pairs(lib.GetPlayers()) do
		if player:GetData().mementoMoriAttackDelay and player:GetData().mementoMoriAttackDelay > 0 then
			player:GetData().mementoMoriAttackDelay = 0
		end
	end
end)

--[[local counterSprite = Sprite()
counterSprite:Load("gfx/samael_counter.anm2", true)
counterSprite:Play(counterSprite:GetDefaultAnimation())
local counterOffset = Vector(-5, -10)
local counterColor = lib.NullColor
local counterPausedColor = Color(0.5, 0.5, 0.5, 1.0)]]

function mod:GetMementoMoriBirthrightGhosts(player)
	if not lib.IsTaintedSamael(player) or not player:HasCollectible(CollectibleType.COLLECTIBLE_BIRTHRIGHT) then return 0 end
	local numSouls = player:GetData().mementoMoriSouls or 0
	local numGhosts = math.min(math.floor(numSouls / kMementoMoriBirthrightGhostCost), kMementoMoriBirthrightGhostCap)
	return numGhosts
end

--[[function mod:MementoMoriGhostCounter(shaderName)
	if shaderName ~= "PauseScreenCompletionMarks" or not game:GetHUD():IsVisible() then return end
	
	for _, player in pairs(lib.GetPlayers()) do
		if lib.IsTaintedSamael(player) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) then
			local numSouls = player:GetData().mementoMoriSouls or 0
			local numGhosts = math.min(math.floor(numSouls / kMementoMoriBirthrightGhostCost), kMementoMoriBirthrightGhostCap)
			local scale, x, y = mod.GetSlotRenderPosition(mod.GetSlot(player, kMementoMori))
			counterSprite:SetFrame(numGhosts)
			if game:IsPaused() then
				counterSprite.Color = counterPausedColor
			else
				counterSprite.Color = counterColor
			end
			counterSprite:Render(Vector(x,y) + counterOffset * scale, lib.ZeroVector, lib.ZeroVector)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_GET_SHADER_PARAMS, mod.MementoMoriGhostCounter)]]

--------------------------------------------------
---- PROJECTILE
--------------------------------------------------

function mod:FireMementoMoriProjectiles(player, projOrigin, dir)
	local rng = player:GetCollectibleRNG(kMementoMori)
	
	local projVel = dir:Resized(player.ShotSpeed * 10)
	local damageMult = kMementoMoriProjectileDamageMult
	
	-- Primary projectiles
	local numTears = lib.GetNumProjectiles(player)
	
	if player:HasCollectible(CollectibleType.COLLECTIBLE_MOMS_KNIFE) then
		numTears = numTears + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MOMS_KNIFE) - 1
	end
	
	local fullWizArc = 90
	local groups = 1 + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_THE_WIZ)
	local groupTears = math.ceil(numTears / groups) -- Shouldn't have to ceil this, but just to be safe
	
	for i=0, groups-1 do
		local groupAngle = 0
		if groups > 1 then
			groupAngle = ( (fullWizArc/(groups-1))*i ) - (fullWizArc/2)
		end
		
		local firstProj = mod:FireMementoMoriProjectileGroup(player, groupTears, projOrigin, projVel:Rotated(groupAngle), damageMult)
	end
	
	-- Eye Sore
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_SORE) then
		local numItems = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_EYE_SORE)
		local minExtra = numItems
		local maxExtra = 3 + 2 * (numItems-1)
		local numExtraProj = rng:RandomInt(maxExtra-minExtra+1) + minExtra
		for i=0, numExtraProj-1 do
			mod:FireSingleMementoMoriProjectile(player, projOrigin, projVel:Rotated(rng:RandomInt(360)), damageMult)
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
				mod:FireMementoMoriProjectileGroup(player, lokiTears, projOrigin, projVel:Rotated(90), damageMult)
				mod:FireMementoMoriProjectileGroup(player, lokiTears, projOrigin, projVel:Rotated(-90), damageMult)
			end
			mod:FireMementoMoriProjectileGroup(player, lokiTears, projOrigin, projVel:Rotated(180), damageMult)
		end
	end
end

function mod:FireMementoMoriProjectileGroup(player, numTears, projOrigin, projVel, baseDamageMult)
	local rng = player:GetCollectibleRNG(kMementoMori)
	
	mod:UpdateKnifeLaser(player, projVel)
	
	local groupArc = 4 + numTears*4
	for j=0, numTears-1 do
		local projAngleOffset = 0
		if numTears > 1 then
			projAngleOffset = ( (groupArc/(numTears-1))*j ) - (groupArc/2)
		end
		local pos = projOrigin
		local vel = projVel:Rotated(projAngleOffset)
		
		local flurry = 1
		local damageMultMult = 1.0
		local posVariance = 0
		local angleVariance = 0
		
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_SOY_MILK) or lib.HasItem(player, CollectibleType.COLLECTIBLE_ALMOND_MILK) then
			flurry = 6
			posVariance = 10
			angleVariance = 8
		end
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_CURSED_EYE) then
			flurry = flurry + 2
			damageMultMult = 1.2
		end
		
		local damageMult = damageMultMult * baseDamageMult / flurry
		
		mod:FireSingleMementoMoriProjectile(player, pos, vel, damageMult)
		
		for i=1, flurry-1 do
			lib.ScheduleForUpdate(function()
				local finalVel = vel
				local offset = projVel:Resized(i*5)
				if posVariance > 0 then
					offset = offset + RandomVector()*posVariance
				end
				if angleVariance > 0 then
					finalVel = finalVel:Rotated((Random() % (angleVariance*2)) - angleVariance)
				end
				mod:FireSingleMementoMoriProjectile(player, pos - offset, finalVel, damageMult)
			end, i)
		end
	end
end

function mod:FireSingleMementoMoriProjectile(player, projOrigin, projVel, mult)
	local pos = projOrigin
	if not player:GetData().disjointedMementoMori then
		pos = pos + projVel:Resized(20)
	end
	
	local hasKnife = player:HasCollectible(CollectibleType.COLLECTIBLE_MOMS_KNIFE)
	local hasLilSlugger = TaintedTreasure and TaintedCollectibles and lib.HasItem(player, TaintedCollectibles.LIL_SLUGGER)
	
	if hasKnife or hasLilSlugger then
		if hasKnife then
			mod:ThrowKnifeScythe(player, projVel, 1.0)
		end
		if hasLilSlugger then
			TaintedTreasure:FireSawblade(player, projVel, nil, mult, projOrigin)
		end
		return
	end
	
	local originalTearFlags = player.TearFlags
	player.TearFlags = player.TearFlags & ~TearFlags.TEAR_POP
	local tear = player:FireTear(pos, projVel, true, true, true, player, mult)
	player.TearFlags = originalTearFlags
	
	tear.CollisionDamage = lib.CalcUnmodifiedDps(player) * mult
	
	local sprite = tear:GetSprite()
	
	if FiendFolio and player:HasCollectible(FiendFolio.ITEM.COLLECTIBLE.EMOJI_GLASSES) then
		tear.Scale = tear.Scale + 1
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_C_SECTION) then
		tear:ChangeVariant(50)
		sprite:ReplaceSpritesheet(0, "gfx/characters/costumes_forgotten/fetus_tears.png")
		sprite:LoadGraphics()
		tear:AddTearFlags(TearFlags.TEAR_FETUS)
		tear:AddTearFlags(TearFlags.TEAR_FETUS_BONE)
		tear.Scale = tear.Scale * 1.2
		--tear.Color = GetSlashColor(player)
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
		--tear:GetData().isMementoMoriProjectile = true
	elseif lib.HasItem(player, CollectibleType.COLLECTIBLE_TERRA) then
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
		tear.Scale = tear.Scale * 1.25
	elseif REVEL and player:HasCollectible(REVEL.ITEM.ICETRAY.id) then
		if lib.SameColor(tear.Color, lib.SamaelTearColor) then
			tear.Color = lib.NullColor
		end
		tear.Scale = tear.Scale + 1
	else
		tear:ChangeVariant(kMementoMoriProjectile)
		tear.Scale = lib.Lerp(tear.Scale, 1, 0.75)
		--if lib.HasItem(player, CollectibleType.COLLECTIBLE_CHOCOLATE_MILK) then
		--	tear.Scale = (tear.Scale * 0.5) + (tear.Scale * 0.7 * mult)
		--end
		local scaleAdj = 3
		tear.SizeMulti = Vector(scaleAdj, scaleAdj)
		sprite.Scale = Vector(tear.Scale/scaleAdj, tear.Scale/scaleAdj)
		sprite:Play(sprite:GetDefaultAnimation(), true)
		if TaintedCollectibles and player:HasCollectible(TaintedCollectibles.ARROWHEAD) then
			sprite:SetLastFrame()
		end
		tear.Color = GetSlashColor(player)
		tear:GetData().isMementoMoriProjectile = true
	end
	
	local range = math.max(400, player.TearRange)
	if player:HasCollectible(CollectibleType.COLLECTIBLE_CONTINUUM) or tear:HasTearFlags(TearFlags.TEAR_BOUNCE) then
		range = range * 3
	end
	tear:GetData().mementoMoriProjectileRange = range
	
	tear:ClearTearFlags(TearFlags.TEAR_POP)
	tear:ClearTearFlags(TearFlags.TEAR_QUADSPLIT)
	tear:ClearTearFlags(TearFlags.TEAR_BURSTSPLIT)
	tear:ClearTearFlags(TearFlags.TEAR_SPLIT)
	tear:ClearTearFlags(TearFlags.TEAR_BONE)
	
	if tear:HasTearFlags(TearFlags.TEAR_LASERSHOT) then
		tear:ClearTearFlags(TearFlags.TEAR_LASERSHOT)
		tear:AddTearFlags(TearFlags.TEAR_PIERCING)
	end
	
	mod:MementoMoriTearUpdate(tear)
	
	if FiendFolio and lib.HasItem(player, CollectibleType.COLLECTIBLE_MODEL_ROCKET) then
		tear.Position = tear.Position + tear.Velocity:Resized(40 * (player:GetData().samaelScytheScale or 1))
	end
end

function mod:MementoMoriTearUpdate(tear)
	local tData = tear:GetData()
	
	if not tData.isMementoMoriProjectile then return end
	
	if tear.Variant ~= kMementoMoriProjectile and tear.Variant == TearVariant.CUPID_BLOOD and tData.ArrowheadBuffed then
		local sprite = tear:GetSprite()
		tear:ChangeVariant(kMementoMoriProjectile)
		sprite:Play(sprite:GetDefaultAnimation(), true)
		sprite:SetLastFrame()
		local scaleAdj = 3
		tear.SizeMulti = Vector(scaleAdj, scaleAdj)
		sprite.Scale = Vector(tear.Scale/scaleAdj, tear.Scale/scaleAdj)
		tear.Color = lib.BloodColor
	end
	
	if tear.Variant == kMementoMoriProjectile then
		tear.SpriteRotation = tear.Velocity:GetAngleDegrees()
	end
	
	if tear.FrameCount % 2 == 0 then
		lib.ScheduleForUpdate(function()
			mod:SpawnProjectileTrail(tear)
		end)
	end
	
	if tData.mementoMoriProjectileRange then
		tear.Height = -10
		tear.FallingAcceleration = -0.1
		tear.FallingSpeed = -1
		
		local lastPos = tData.mementoMoriTearLastPos or tear.Position
		tData.mementoMoriTearDistanceTraveled = (tData.mementoMoriTearDistanceTraveled or 0) + lastPos:Distance(tear.Position)
		tData.mementoMoriTearLastPos = tear.Position
		
		if tData.mementoMoriTearDistanceTraveled > tData.mementoMoriProjectileRange then
			tear:Die()
			return
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.MementoMoriTearUpdate)

function mod:SpawnProjectileTrail(tear)
	if not tear:GetData().isMementoMoriProjectile then return end
	tear = tear:ToTear()
	
	local pos = tear.Position -- tear.Velocity:Resized(10)
	local effect = Isaac.Spawn(EntityType.ENTITY_EFFECT, 6618, 1, pos, lib.ZeroVector, tear):ToEffect()
	if tear.Variant == 50 then
		effect:GetSprite():Load("gfx/002.050_fetus tear.anm2", false)
		effect:GetSprite():ReplaceSpritesheet(0, "gfx/characters/costumes_forgotten/fetus_tears.png")
		effect:GetSprite():LoadGraphics()
	else
		effect.SpriteScale = Vector(tear.SpriteScale.X * tear.SizeMulti.X, tear.SpriteScale.Y * tear.SizeMulti.Y) * tear.Scale
	end
	effect:GetSprite():SetFrame(tear:GetSprite():GetAnimation(), tear:GetSprite():GetFrame())
	effect:GetSprite():Stop()
	effect.SpriteRotation = tear.SpriteRotation
	effect.Timeout = 20
	effect.SpriteOffset = Vector(0, tear.Height)
	effect.Color = tear.Color
	effect.DepthOffset = tear.DepthOffset - 10
	--effect.RenderZOffset = tear.RenderZOffset - 10
end
--mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.SpawnProjectileTrail, EntityType.ENTITY_TEAR)

function mod:MementoMoriProjectileDeath(tear)
	if not tear:GetData().isMementoMoriProjectile then return end
	tear = tear:ToTear()
	
	mod:SpawnProjectileTrail(tear)
	
	if tear.Variant ~= kMementoMoriProjectile then return end
	
	local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 5, tear.Position, lib.ZeroVector, nil):ToEffect()
	local sprite = eff:GetSprite()
	sprite:ReplaceSpritesheet(0, "gfx/effects/samael_effect_010_poof02_bloodcloud.png")
	sprite:LoadGraphics()
	
	local c = tear.Color
	c:SetTint(c.R, c.G, c.B, 0.7)
	eff.Color = c
	--eff.Rotation = 90 - tear.Velocity:GetAngleDegrees()
	sprite.Scale = Vector(0.5, 0.6) * tear.Scale
	eff:GetData().mementoMoriFade = true
	
	sfxManager:Play(162, 1, 0, false, 2.5)
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.MementoMoriProjectileDeath, EntityType.ENTITY_TEAR)

function mod:MementoMoriProjectilePoof(eff)
	if eff:GetData().mementoMoriFade then
		local c = eff.Color
		c:SetTint(c.R, c.G, c.B, c.A * 0.85)
		eff.Color = c
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.MementoMoriProjectilePoof, 16)

--------------------------------------------------
---- PATHING SHENANIGANS
--------------------------------------------------

-- Returns the total length of the given path.
function mod:CalcPathLength(path)
	local length = 0
	local prev = nil
	
	for i=0, #path do
		local curr = path[i]
		if prev then
			length = length + prev:Distance(curr)
		end
		prev = curr
	end
	
	return length
end

-- Affects how long the blur trail lingers.
local kMementoMoriBlurLength = 20

-- Render the black blur trail effect that represents Samael moving quickly.
function mod:SpawnBlurTrail(player, prev, curr, prevDist, currentDist, targetDist, i, notMementoMori)
	local subTargetDist = targetDist - i*13
	
	if subTargetDist < 0 or subTargetDist <= prevDist or subTargetDist > currentDist then
		return nil
	end

	local pos = lib.Lerp(prev, curr, (subTargetDist - prevDist) / curr:Distance(prev))
	
	local effect = Isaac.Spawn(EntityType.ENTITY_EFFECT, 6618, 0, pos, lib.ZeroVector, player):ToEffect()
	effect.SpriteRotation = (curr - prev):GetAngleDegrees()
	effect:GetSprite():SetFrame(i % 5)
	effect.SpriteScale = lib.Lerp(Vector(1.0, 1.1), lib.ZeroVector, i/kMementoMoriBlurLength)
	effect.Color = Color(0,0,0, (kMementoMoriBlurLength-i)/kMementoMoriBlurLength)
	
	if targetDist < player:GetData().mementoMoriPathLength then
		effect.Timeout = 0
	else
		effect.Timeout = 20
	end
	
	effect.DepthOffset = 3
	
	if notMementoMori then
		return
	end
	
	local effect2 = Isaac.Spawn(EntityType.ENTITY_EFFECT, 6618, 0, pos, lib.ZeroVector, player):ToEffect()
	effect2.SpriteRotation = (curr - prev):GetAngleDegrees()
	effect2:GetSprite():SetFrame(i % 5)
	effect2.SpriteScale = lib.Lerp(Vector(1.0, 1.1), lib.ZeroVector, i/kMementoMoriBlurLength) * 1.2
	local c = GetSlashColor(player)
	c:SetTint(c.R, c.G, c.B, 0.1)
	effect2.Color = c
	
	if targetDist < player:GetData().mementoMoriPathLength then
		effect2.Timeout = 0
	else
		effect2.Timeout = 20
	end
	
	effect2.DepthOffset = -15
end

-- Update the width of the slashing trail, for effects that should change it over time.
function mod:UpdateMementoMoriTrailWidth(player, trail, n)
	local trailWidth = trail:GetData().baseWidth or 1
	local x = math.abs(n - 0.5)*2
	if player:ToPlayer() then
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_PROPTOSIS) then
			trailWidth = lib.Lerp(trailWidth * 0.4, trailWidth * 2.5, x)
		end
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_LUMP_OF_COAL) then
			trailWidth = lib.Lerp(trailWidth * 2.5, trailWidth * 0.4, x)
		end
		if player:HasTrinket(TrinketType.TRINKET_PULSE_WORM) then
			trailWidth = trailWidth + trailWidth * math.sin(9 * math.pi * n) * 0.5
		end
	end
	trail.SpriteScale = Vector(trailWidth, 1)
end

local function PlaydoughCookieRoll(player)
	return lib.HasItem(player, CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE) and player:GetCollectibleRNG(kMementoMori):RandomInt(9) == 0
end

-- Synergies that trigger along the path of the Memento Mori slashes.
function mod:MementoMoriTrailSynergies(player, currentPos, prevPos)
	local tearFlags = player:GetTearHitParams(WeaponType.WEAPON_TEARS).TearFlags
	local playerData = player:GetData()
	local rng = player:GetCollectibleRNG(kMementoMori)
	
	-- Epic Fetus
	if playerData.mementoMoriEpicFetusLaser then
		playerData.mementoMoriEpicFetusLaser.Position = currentPos
	end
	if playerData.mementoMoriEpicFetus then
		local delay = (playerData.mementoMoriEpicFetusDelay or 0)
		if delay <= 0 then
			local numAirstrikes = lib.GetNumProjectiles(player)
			local spread = numAirstrikes * 20
			local v = (currentPos - prevPos):Rotated(90):Resized(spread * 0.5)
			for i=0, numAirstrikes-1 do
				local airStrikePos = currentPos
				if numAirstrikes > 1 then
					airStrikePos = currentPos - v + v:Resized(spread * i / (numAirstrikes-1))
				end
				local airStrike = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.TARGET, 0, airStrikePos, lib.ZeroVector, player):ToEffect()
				airStrike.State = 1
				airStrike.LifeSpan = 20
				airStrike.Timeout = 20
			end
			
			playerData.mementoMoriEpicFetusDelay = 5
		else
			playerData.mementoMoriEpicFetusDelay = delay - 1
			return nil	-- Skip other effects for this frame.
		end
	end
	-- PEE
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_NUMBER_ONE) or PlaydoughCookieRoll(player) then
		local vel = Vector(0, player.ShotSpeed * 10):Rotated(rng:RandomInt(360))
		local tear = player:FireTear(currentPos, vel, false, true, false)
		tear.TearFlags = TearFlags.TEAR_NORMAL
		tear:ChangeVariant(0)
		tear.Scale = tear.Scale * 0.75
		tear.FallingSpeed = tear.FallingSpeed + rng:RandomFloat()*2
		tear.FallingAcceleration = tear.FallingAcceleration + rng:RandomFloat()*0.2
		tear:GetData().isMementoMoriPee = true
	end
	-- Fire Mind / flame trail
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_FIRE_MIND) or HasFlag(tearFlags, TearFlags.TEAR_BURN) or PlaydoughCookieRoll(player) then
		local fireJet = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.FIRE_JET, 0, currentPos, lib.ZeroVector, player)
		fireJet:GetData().noHurtPlayer = true
		fireJet.CollisionDamage = player.Damage
	end
	-- Holy Light
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_HOLY_LIGHT) or HasFlag(tearFlags, TearFlags.TEAR_LIGHT_FROM_HEAVEN) or PlaydoughCookieRoll(player) then
		local delay = playerData.mementoMoriHolyLightDelay or 0
		if delay == 0 then
			if not playerData.mementoMoriHolyLightQueue then
				playerData.mementoMoriHolyLightQueue = {}
			end
			table.insert(playerData.mementoMoriHolyLightQueue, currentPos)
			delay = 2
		end
		playerData.mementoMoriHolyLightDelay = delay - 1
	end
	-- Large Zit
	if (lib.HasItem(player, CollectibleType.COLLECTIBLE_LARGE_ZIT) and rng:RandomInt(3) == 0) or PlaydoughCookieRoll(player) then
		player:DoZitEffect(lib.NormalVector:Rotated(rng:RandomInt(360)))
	end
	-- Green creep
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_MYSTERIOUS_LIQUID) or HasFlag(tearFlags, TearFlags.TEAR_MYSTERIOUS_LIQUID_CREEP) or PlaydoughCookieRoll(player) then
		local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PLAYER_CREEP_GREEN, 0, currentPos, lib.ZeroVector, player, 0, 0):ToEffect()
		creep:SetDamageSource(EntityType.ENTITY_PLAYER)
		creep.CollisionDamage = math.min(lib.CalcUnmodifiedDps(player) * 0.01, 1)
	end
	-- Fart's
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_JUPITER) or PlaydoughCookieRoll(player) then
		local fart = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SMOKE_CLOUD, 0, currentPos, lib.ZeroVector, player):ToEffect()
		fart.Scale = 0.5
		fart.Timeout = 100
	end
	-- API can't apply the new tear synergies for Aquarius, so there's no real point in this.
	--[[if lib.HasItem(player, CollectibleType.COLLECTIBLE_AQUARIUS) then
		local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PLAYER_CREEP_HOLYWATER_TRAIL, 0, currentPos, lib.ZeroVector, player, 0, 0):ToEffect()
		creep.Parent = player
		creep.SpawnerEntity = player
		creep:SetDamageSource(EntityType.ENTITY_PLAYER)
	end]]
	-- Parasite Split Tears
	if (lib.HasItem(player, CollectibleType.COLLECTIBLE_PARASITE) or HasFlag(tearFlags, TearFlags.TEAR_SPLIT) or PlaydoughCookieRoll(player)) and prevPos then
		local vel = (currentPos - prevPos):Rotated(90):Resized(player.ShotSpeed * 10)
		for i=0,1 do
			local tear = player:FireTear(currentPos, vel:Rotated(180*i), false, true, false)
			tear.Scale = tear.Scale * 0.5
			tear.FallingSpeed = tear.FallingSpeed + rng:RandomFloat()*2
			tear.FallingAcceleration = tear.FallingAcceleration + rng:RandomFloat()*0.2
			tear.CollisionDamage = tear.CollisionDamage * 0.5
			tear:ClearTearFlags(TearFlags.TEAR_SPLIT)
			tear:ClearTearFlags(TearFlags.TEAR_QUADSPLIT)
			tear:ClearTearFlags(TearFlags.TEAR_BURSTSPLIT)
			tear:ClearTearFlags(TearFlags.TEAR_BONE)
		end
	end
	-- Burning Bush
	if REVEL and REVEL.ITEM.BURNBUSH:PlayerHasCollectible(player) then
		for i=1,2 do
			local speed = 3 + (Random()%5)
			local angle = -20 + (Random()%40)
			local vel = (prevPos - currentPos):Resized(speed):Rotated(angle)
			local fire = REVEL.ShootFireTear(player, currentPos, vel, 1)
			fire.SpriteOffset = Vector(0, 15)
		end
	end
end

-- Move Samael along the path to the next sigil, even if it curves.
function mod:UpdateAlongPath(player, path, prevTargetDist, targetDist, notMementoMori)
	local playerData = player:GetData()
	
	local prev = nil
	local dist = 0
	
	-- Iterate through the path until we find our desired position at this frame.
	for i=0, #path do
		local curr = path[i]
		if prev then
			local newDist = dist + curr:Distance(prev)
			
			if not playerData.mementoMoriEpicFetus then
				-- Draw the blur trail leading up to our target position.
				for i=0, kMementoMoriBlurLength do
					mod:SpawnBlurTrail(player, prev, curr, dist, newDist, targetDist, i, notMementoMori)
				end
			end
			
			if targetDist <= newDist then
				-- We've found where we want to be. Calculate the exact position.
				local currentPos = lib.Lerp(prev, curr, (targetDist - dist) / curr:Distance(prev))
				if curr:Distance(prev) == 0 then
					currentPos = curr
				end
				--[[if not playerData.disjointedMementoMori then
					player.Position = currentPos
				end]]
				if player:ToPlayer() then
					mod:MementoMoriTrailSynergies(player, currentPos, prev)
				end
				
				if playerData.mementoMoriTrail then
					mod:UpdateMementoMoriTrailWidth(player, playerData.mementoMoriTrail, newDist / playerData.mementoMoriPathLength)
					playerData.mementoMoriTrail.Position = currentPos
					playerData.mementoMoriTrail:Update()
				end
				
				return currentPos
			end
			dist = newDist
		end
		
		if dist > prevTargetDist and playerData.mementoMoriTrail and playerData.mementoMoriTrail:Exists() and i > 0 then
			-- Update the trail effects multiple times per frame to show the shape of the whole path.
			mod:UpdateMementoMoriTrailWidth(player, playerData.mementoMoriTrail, dist / playerData.mementoMoriPathLength)
			playerData.mementoMoriTrail.Position = curr
			playerData.mementoMoriTrail:Update()
		end
		
		prev = curr
	end
	
	local finalPos = path[#path]
	
	if playerData.mementoMoriTrail and playerData.mementoMoriTrail:Exists() then
		mod:UpdateMementoMoriTrailWidth(player, playerData.mementoMoriTrail, 0)
		playerData.mementoMoriTrail.Position = finalPos
		--playerData.mementoMoriTrail:Update()
	end
	--[[if not playerData.disjointedMementoMori then
		player.Position = finalPos
	end]]
	if playerData.mementoMoriEpicFetusLaser then
		playerData.mementoMoriEpicFetusLaser.Position = finalPos
	end
	
	return finalPos
end

-- HAEMO_TRAIL is used to render some simple effects.
function mod:HaemoTrailUpdate(trail)
	if trail:GetData().freezeVelocity then
		trail.Velocity = lib.ZeroVector
		if trail.Timeout == 0 then
			trail:Remove()
		end
	end
	if trail.SubType == 617 and trail.Target then
		trail.Velocity = lib.Lerp(trail.Velocity, (trail.Target.Position - trail.Position):Resized(trail:GetData().samaelBubbleSpeed or 5), 0.2)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.HaemoTrailUpdate, EffectVariant.HAEMO_TRAIL)

-- Black blur trail left behind by slashes.
function mod:MementoMoriBlurTrailUpdate(trail)
	if trail.Timeout == 0 then
		trail:Remove()
	else
		trail.SpriteScale = trail.SpriteScale * 0.90
		local c = trail.Color
		c:SetTint(c.R, c.G, c.B, c.A * 0.75)
		trail.Color = c
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.MementoMoriBlurTrailUpdate, 6618)

function mod:MementoMoriHolyLightHandler(player)
	if game:GetRoom():GetFrameCount() <= 1 then
		player:GetData().mementoMoriHolyLightQueue = nil
	end
	
	local queue = player:GetData().mementoMoriHolyLightQueue
	local bit = player:GetData().mementoMoriHolyLightBit or false
	if queue and #queue > 0 and player.FrameCount % 2 == 0 then
		player:GetData().mementoMoriHolyLightBit = not bit
		if not bit then
			return
		end
		local pos = table.remove(queue, 1)
		local light = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.CRACK_THE_SKY, 0, pos, lib.ZeroVector, player):ToEffect()
		light:GetData().noHurtPlayer = true
		--light:GetSprite().PlaybackSpeed = 2
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.MementoMoriHolyLightHandler)

--------------------------------------------------
---- SIGIL PATH TEARS (TRACTOR BEAM / TRISAGAYONE)
--------------------------------------------------

-- Funny code that lets tears travel along the path of a laser.
-- Check out "Tractor Beam 2.0" for a better example of this behavior.
function mod:SigilPathTearPostUpdate(tear)
	local tData = tear:GetData()

	if not tData.isMementoMoriPathTear then return nil end
	
	if not tear.Parent or not tear.Target or not tear.Target:ToLaser() then
		tear:Die()
		return nil
	end
	
	-- Lock the height of the tears.
	tear.Height = -6
	tear.FallingAcceleration = 0
	tear.FallingSpeed = -1
	
	local laser = tear.Target:ToLaser()
	
	local startPos = laser.EndPoint
	local endPos = (laser:GetData().mementoMoriLaserAnchor or laser).Position
	
	if tData.mementoMoriTris then
		startPos = startPos + (startPos - endPos):Resized(50)
	end
	
	-- Get the sample points for the path laser.
	local samplePoints = laser:GetSamples()
	
	-- Put the startPos, the path of the laser, and the endPos into a new table.
	-- While this is being done, sum up the total length of the current path.
	local totalDist = 0
	local path = {}
	table.insert(path, Vector(startPos.X, startPos.Y))
	for i=#samplePoints-1, -1, -1 do
		if i == -1 then
			table.insert(path, Vector(endPos.X, endPos.Y))
		else
			table.insert(path, Vector(samplePoints:Get(i).X, samplePoints:Get(i).Y))
		end
		totalDist = totalDist + path[#path]:Distance(path[#path-1])
	end
	
	-- The current distance that the tear has already traveled along the path.
	local currentDist = tData.mementoMoriTearCurrentDist or -tData.mementoMoriTearSpeed
	
	-- If the tear has reached the end of the path, kill it.
	if currentDist >= totalDist-1 or tear.Position:Distance(endPos) < 10 then
		if tData.mementoMoriTris then
			tData.mementoMoriTris.Color = lib.InvisibleColor
		end
		tear.Position = endPos
		tear:Die()
		return nil
	end
	
	-- How far along the path we should be next update.
	local targetDist = math.max(math.min(totalDist, currentDist + tData.mementoMoriTearSpeed), 0)
	
	-- Trisagayone
	if tData.mementoMoriTris then
		local distRatio = targetDist / totalDist
		local x = math.sin(math.pi * distRatio)

		tData.mementoMoriTris.SpriteScale = Vector(x, 1)
		
		if tData.mementoMoriTris.FrameCount > 2 then
			local n = 4
			local colorScale = math.atan(n * x) /	math.atan(n)
			local c = tData.mementoMoriTris.Color
			c:SetTint(c.R, c.G, c.B, colorScale)
			tData.mementoMoriTris.Color = c
		end
	end
	
	-- The Vector position we should move towards now.
	local newTargetPos = nil
	
	-- Find the newTargetPos.
	local dist = 0
	for i=1, #path do
		local newDist = 0
		
		-- Count up the current distance we've traveled along the path in this loop.
		if path[i-1] then
			newDist = dist + path[i]:Distance(path[i-1])
		end
		
		-- If newDist >= targetDist, we've found where we want to be.
		if newDist >= targetDist then
			if not path[i-1] or newDist == targetDist then
				newTargetPos = path[i]
			else
				local percent = (targetDist - dist) / (newDist - dist)
				newTargetPos = lib.Lerp(path[i-1], path[i], percent)
			end
			break
		end
		
		dist = newDist
	end
	
	if newTargetPos then
		-- By next update we should be at this distance.
		tData.mementoMoriTearCurrentDist = targetDist
		
		-- Set the tear's position to the previous targetPos.
		tear.Position = tData.mementoMoriPathTearTargetPos or startPos
		
		tData.mementoMoriPathTearTargetPos = newTargetPos
		
		-- Keep track of the Velocity our tear SHOULD have right now, even though we keep velocity set
		-- to 0 to avoid twitchy visuals for more complicated paths. The "true" velocity is useful for
		-- making sure knockback can still be applied (see PreTearCollision).
		tData.mementoMoriPathTearTrueVelocity = (newTargetPos - tear.Position):Resized(tData.mementoMoriTearSpeed)
		tear.Velocity = lib.ZeroVector
		
		-- Keep the tear rotated in the direction of movement.
		tear.SpriteRotation = tData.mementoMoriPathTearTrueVelocity:GetAngleDegrees()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.SigilPathTearPostUpdate)

-- "Why are you setting the Tear's position on both update and render instead of using velocities?"
-- A combination of the funny shapes with certain worms, like ring worm, alongside the fact that the
-- shape of the laser (and therefore the path) is CONSTANTLY changing, and the lack of a proper
-- "PRE_UPDATE" callback, I wasn't able to get this to work smoothly via velocities alone.
-- It's probably possible but this worked better for me.
-- As long as the movement looks smooth in-game (it does) then it's good with me.
-- The main drawback with this is that some effects rely on velocity, like knockback.
-- The knockback issue I can solve with my MC_PRE_TEAR_COLLISION callback below, though.
function mod:SigilPathTearPostRender(tear)
	local tData = tear:GetData()
	if tData.isMementoMoriPathTear and tData.mementoMoriPathTearTargetPos then
		tear.Position = lib.Lerp(tear.Position, tData.mementoMoriPathTearTargetPos, 0.5)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_RENDER, mod.SigilPathTearPostRender)

-- Pre-collision involving a Tractor Beam-affected tear, give it the velocity it should have.
-- This only affects the collision calculations (such as, notably, knockback).
function mod:SigilPathTearPreCollision(tear)
	if tear:GetData().isMementoMoriPathTear and tear:GetData().mementoMoriPathTearTrueVelocity then
		tear.Velocity = tear:GetData().mementoMoriPathTearTrueVelocity
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_TEAR_COLLISION, mod.SigilPathTearPreCollision)

function mod:TrisagionInit(laser)
	if expectingTrisagionInit then
		expectedTrisagionEntity = laser
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_INIT, mod.TrisagionInit)

function mod:TrisagionPostUpdate(laser)
	if laser.Parent and laser.Parent:ToTear() and laser.Parent:GetData().isMementoMoriPathTear
			and laser.Parent:GetData().mementoMoriPathTearTargetPos then
		local dir = (laser.Parent:GetData().mementoMoriPathTearTargetPos - laser.Parent.Position):Normalized()
		laser.Parent.Velocity = dir
		laser.Angle = laser.Parent.SpriteRotation
	end
	
	if laser:GetData().mementoMoriTrisFade then
		local c = laser.Color
		c:SetTint(c.R, c.G, c.B, c.A * 0.5)
		laser.Color = c
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, mod.TrisagionPostUpdate)

--------------------------------------------------
---- SYNERGIES
--------------------------------------------------

-- For manually spawning electricity lasers and locking them to targets where possible.
function mod:FireElectricity(player, timeOut, source, cap)
	if not player then return nil end

	local source = source or player
	local remaining = cap or 4

	local pos = source.Position
	
	local rng = player:GetCollectibleRNG(kMementoMori)

	local enemies = Isaac.FindInRadius(pos, 125, EntityPartition.ENEMY)
	for _, ent in pairs(enemies) do
		if remaining > 0 and ent:IsVulnerableEnemy() and not ent:HasEntityFlags(EntityFlag.FLAG_FRIENDLY | EntityFlag.FLAG_PERSISTENT) then
			local laser = EntityLaser.ShootAngle(10, pos, math.ceil((ent.Position - pos):GetAngleDegrees()), timeOut, lib.ZeroVector, player)
			laser:AddTearFlags(TearFlags.TEAR_JACOBS)
			laser:SetMaxDistance(ent.Position:Distance(pos))
			laser.Parent = source
			laser.Target = ent
			laser.CollisionDamage = player.Damage * 0.5
			laser:GetData().isMementoMoriElectricity = true
			remaining = remaining - 1
		end
	end
	
	for i=1, remaining do
		local laser = EntityLaser.ShootAngle(10, pos, rng:RandomInt(360), timeOut, lib.ZeroVector, player)
		--laser:AddTearFlags(TearFlags.TEAR_JACOBS)
		laser:SetMaxDistance(50 + rng:RandomInt(25))
		laser.Parent = source
		laser.CollisionDamage = player.Damage * 0.5
		laser:GetData().isMementoMoriElectricity = true
	end
end

-- For the special pee tears that leave yellow creep.
function mod:PostTearDeath(tear)
	tear = tear:ToTear()
	if not tear then return nil end
	
	if tear:GetData().isMementoMoriPee and tear.SpawnerEntity and tear.SpawnerEntity:ToPlayer() then
		local player = tear.SpawnerEntity:ToPlayer()
		if not player then return nil end
		local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.PLAYER_CREEP_RED, 0, tear.Position, lib.ZeroVector, tear.SpawnerEntity):ToEffect()
		creep.Parent = player
		creep.CollisionDamage = player.Damage
		creep.Color = lib.NewColor(1,1,1,1,1,1,0)
		creep.Timeout = math.floor(creep.Timeout * 0.5)
		creep.Scale = tear.Scale
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.PostTearDeath, EntityType.ENTITY_TEAR)

-- Spawns a brief Tech Zero laser between two entities.
function mod:SpawnTechZeroLaser(player, source, target)
	local pos = source.Position
	local targetPos = target.Position

	if pos:Distance(targetPos) <= 1 then
		return nil
	end
	
	local laser = EntityLaser.ShootAngle(10, pos, (targetPos - pos):GetAngleDegrees(), 4, lib.ZeroVector, player)
	laser.ParentOffset = Vector(0,0)
	laser.PositionOffset = Vector(0,0)
	laser:SetMaxDistance(targetPos:Distance(pos))
	laser:AddTearFlags(TearFlags.TEAR_SPECTRAL)
	laser.Parent = source
	laser.Target = target
	laser.CollisionDamage = player.Damage
	laser:GetData().isMementoMoriElectricity = true
	laser.OneHit = true
end

-- Post-update for Tech Zero to detect when to spawn lasers.
function mod:TechZero(player)
	local playerData = player:GetData()
	local sigils = playerData.mementoMoriSigils
	
	if not lib.IsTaintedSamael(player) or not lib.HasItem(player, kMementoMori)
			or not lib.HasItem(player, CollectibleType.COLLECTIBLE_TECHNOLOGY_ZERO)
			or not sigils or #sigils == 0 then
		return nil
	end

	if playerData.samaelScythe and playerData.samaelScytheState == 2 and playerData.samaelScythe:GetSprite():GetFrame() == 1 then
		local currentFrame = game:GetFrameCount()
		local lastFrame = playerData.taintedSamaelTechZero or 0
		if currentFrame <= lastFrame + 2 then
			return nil
		end
		for i=1, #sigils do
			mod:SpawnTechZeroLaser(player, player, sigils[i])
			if sigils[i-1] and sigils[i-1]:Exists() then
				mod:SpawnTechZeroLaser(player, sigils[i-1], sigils[i])
			end
		end
		playerData.taintedSamaelTechZero = currentFrame
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.TechZero)

-- Adjusts the damage caused by green creep from Memento Mori effects.
-- This is mainly a nerf, since green creep is VERY strong.
function mod:PostCreepInit(creep)
	if creep.FrameCount == 0 and creep.SpawnerEntity and creep.SpawnerEntity.Type == EntityType.ENTITY_FAMILIAR and creep.SpawnerEntity.Variant == kSigilId then
		local player = creep.SpawnerEntity.Parent:ToPlayer()
		creep.CollisionDamage = math.min(lib.CalcUnmodifiedDps(player) * 0.01, 1)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.PostCreepInit, EffectVariant.PLAYER_CREEP_GREEN)

-- Prevent "Tough Love" teeth from colliding with the entity they were "spawned from".
function mod:PreToothCollision(tooth, collider)
	if tooth:GetData().mementoMoriToothParent and tooth:GetData().mementoMoriToothParent.InitSeed == collider.InitSeed then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_TEAR_COLLISION, mod.PreToothCollision, TearVariant.TOOTH)

-- Spawns a "pop" tear.
function mod:FirePopTear(player, tearPos)
	local tear = player:FireTear(tearPos, lib.ZeroVector, false, true, false, nil, 1)
	if lib.SameColor(tear.Color, lib.SamaelTearColor) then
		tear.Color = lib.NullColor
	end
	tear:GetData().mementoMoriPopTearInit = true
	tear:GetData().mementoMoriPopStartPos = tearPos
	tear:GetData().mementoMoriPopHeight = tear.Height
	
	
	tear.Parent = player
	
	local effect = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 0, tearPos, lib.ZeroVector, player)
	effect.SpriteScale = Vector(0.4, 0.4) * tear.Scale
	effect.SpriteOffset = Vector(0, tear.Height + tear.Size)
	effect.DepthOffset = 1
	effect.Color = Color(1,1,1,0.5) * tear.Color
	effect:GetSprite().PlaybackSpeed = 2
	
	return tear
end

function mod:PopTearUpdate(tear)
	local tData = tear:GetData()
	if not tData.mementoMoriPopTearInit or not tear.Parent then return nil end
	local player = tear.Parent:ToPlayer()
	if not player then return nil end
	
	if (player:GetData().mementoMoriPopGo or not player:GetData().mementoMoriPop) and tear.Velocity:Length() > 0 then
		tear.Velocity = tear.Velocity:Resized(player.ShotSpeed * 10)
		tData.mementoMoriPopTearInit = false
	else
		mod:PopTearFix(tear)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.PopTearUpdate)

function mod:PopTearFix(tear)
	local tData = tear:GetData()
	if not tData.mementoMoriPopTearInit or not tData.mementoMoriPopStartPos or not tear.Parent then return nil end
	local player = tear.Parent:ToPlayer()
	if not player then return nil end
	
	if not player:GetData().mementoMoriPopGo and player:GetData().mementoMoriPop then
		tear.Position = tData.mementoMoriPopStartPos
		tear.Velocity = lib.ZeroVector
		tear.Height = tData.mementoMoriPopHeight
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_RENDER, mod.PopTearUpdate)

--------------------------------------------------
---- "SLASH" VISUAL EFFECT
--------------------------------------------------

function mod:SlashUpdate(slash)
	local data = slash:GetData()
	if data.isMementoMoriTrail then
		return false
	end
	if data.isMementoMoriSlash then
		if slash.Timeout <= 2 then
			slash.SpriteScale = Vector(slash.SpriteScale.X * 0.5, 1)
		end
		if slash.Timeout <= 0 then
			slash.Parent = nil
			slash:SetTimeout(1)
			data.isMementoMoriSlash = false
		end
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, CallbackPriority.EARLY, mod.SlashUpdate, EffectVariant.SPRITE_TRAIL)

function mod:SpawnSlashEffect(player, pos, v, width, target, isFromDeathShadow, overrideColor)
	local slash = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SPRITE_TRAIL, 0, pos - v, lib.ZeroVector, player):ToEffect()
	slash.DepthOffset = 255
	slash.RenderZOffset = -255
	slash:GetSprite().Color = overrideColor or GetSlashColor(player, isFromDeathShadow)
	slash.MinRadius = 0.045
	if not isFromDeathShadow and player:GetData().mementoMoriCombo and player:GetData().mementoMoriCombo >= 4 then
		slash.MinRadius = 0.025
		width = width * 1.5
		mod:SpawnSlashEffect(player, pos, v, width, target, true, overrideColor)
	end
	slash.Parent = player
	slash.SpriteScale = Vector(width * 0.2, 1)
	
	slash:Update()
	
	slash.SpriteScale = Vector(width * 0.5, 1)
	
	local t = 5
	slash.Velocity = v / (t*0.5)
	slash:SetTimeout(t)
	slash:GetData().isMementoMoriSlash = true
	
	slash:Update()
	
	slash.SpriteScale = Vector(width, 1)
	
	if target.Type ~= EntityType.ENTITY_FIREPLACE then
		local rng = player:GetCollectibleRNG(kMementoMori)
		for i=1, 3 do
			local angleRange = 25
			local angle = rng:RandomInt(angleRange) - angleRange*0.5
			local speedRange = 4
			local speed = 5 + rng:RandomInt(speedRange) - speedRange*0.5
			local vel = v:Rotated(angle):Resized(speed)
			local gib = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLOOD_PARTICLE, 0, pos, vel, target):ToEffect()
			gib.Color = target.SplatColor
			gib.SpriteScale = Vector(0.75, 0.75)
		end
	end
end

--------------------------------------------------
---- DAMAGE CALLBACKS
--------------------------------------------------

function mod:TaintedSamaelHurtSound(player)
	lib.SuppressSound(SoundEffect.SOUND_ISAAC_HURT_GRUNT)
	
	local roll = Random() % 3
	local pitchAdj = 0.1
	local pitchMult = 1.0
	
	if player then
		pitchMult = 1 / lib.Lerp(player.SpriteScale.X, 1.0, 0.5)
	end
	
	if roll == 0 then
		sfxManager:Play(693, 1.0, 0, false, (1.75 + pitchAdj)*pitchMult)
	elseif roll == 1 then
		sfxManager:Play(694, 1.0, 0, false, (2.0 + pitchAdj)*pitchMult)
	else --if roll == 2 then
		sfxManager:Play(695, 1.0, 0, false, (2.25 + pitchAdj)*pitchMult)
	end
	sfxManager:Play(500, 0.8, 0, false, 1.2)
end

-- Resist damage taken while in Memento Mori's iFrames.
function mod:TaintedSamaelPreDamage(player, damage, damageFlags, damageSourceRef)
	player = player:ToPlayer()

	local playerData = player:GetData()
	
	local isSelfDamage = damageSourceRef.Type == EntityType.ENTITY_PLAYER
	local isRedHeartDamage = damageFlags & DamageFlag.DAMAGE_RED_HEARTS == DamageFlag.DAMAGE_RED_HEARTS
	local isExplosionDamage = damageFlags & DamageFlag.DAMAGE_EXPLOSION == DamageFlag.DAMAGE_EXPLOSION
	local isSpikesDamage = damageFlags & DamageFlag.DAMAGE_SPIKES == DamageFlag.DAMAGE_SPIKES
	local isRedPoopDamage = damageFlags & DamageFlag.DAMAGE_POOP == DamageFlag.DAMAGE_POOP
	local isCurseRoomDamage = damageFlags & DamageFlag.DAMAGE_CURSED_DOOR == DamageFlag.DAMAGE_CURSED_DOOR
	
	if ((playerData.mementoMoriActive and not playerData.disjointedMementoMori) or (playerData.mementoMoriIFrames or 0) > 0)
			and not isSelfDamage and not isRedHeartDamage then
		return false
	end
	
	if damageSourceRef.Entity and damageSourceRef.Entity:GetData().noHurtPlayer then
		return false
	end
	
	-- Panic Button automatically triggers wraith mode before taking damage.
	if player:HasCollectible(kMementoMori)
			and player:HasTrinket(TrinketType.TRINKET_PANIC_BUTTON)
			and playerData.mementoMoriSigils and #playerData.mementoMoriSigils > 0
			and not isSelfDamage and not isRedHeartDamage and not isRedPoopDamage
			and not isExplosionDamage and not isSpikesDamage and not isCurseRoomDamage then
		mod:ForceActivateMementoMori(player)
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.TaintedSamaelPreDamage, EntityType.ENTITY_PLAYER)

mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, function(_, player)
	-- Play a different hurt sound for Tainted Samael.
	if lib.IsChallengeSamael(player) then
		mod:OtherSamaelHurtSound(player)
	elseif lib.IsTaintedSamael(player) then
		mod:TaintedSamaelHurtSound(player)
	end
end, EntityType.ENTITY_PLAYER)

-- Main pre-damage callback for Memento Mori slashes.
function mod:MementoMoriPreTakeDamage(tookDamage, damage, damageFlags, damageSourceRef)
	if not damageSourceRef or not damageSourceRef.Entity then return nil end
	
	if damageSourceRef.Type == EntityType.ENTITY_LASER and damageSourceRef.Entity:GetData().isMementoMoriEpicFetusLaser then
		return false
	end
	
	if damageSourceRef.Type == EntityType.ENTITY_FAMILIAR and (damageSourceRef.Variant == kSigilId or damageSourceRef.Variant == mod.ENTITIES.DEATH_SHADOW.Var) then
		if (damageFlags & DamageFlag.DAMAGE_LASER ~= 0) then
			local isFromDeathShadow = damageSourceRef.Variant == mod.ENTITIES.DEATH_SHADOW.Var
			local sigil = damageSourceRef.Entity
			local laser = sigil:GetData().hitboxLaser or sigil:GetData().linkLaser
			if not laser or not laser:ToLaser() then return nil end
			laser = laser:ToLaser()
			local player = sigil.Parent
			if not laser or not player or not player:ToPlayer() then return nil end
			player = player:ToPlayer()
			
			local laserData = laser:GetData()
			local playerData = player:GetData()
			
			if tookDamage.InitSeed == laserData.mementoMoriNoHit then
				return false
			end
			
			if laserData.isMementoMoriLinkLaser and not laserData.isDefaultMementoMoriLinkLaser then
				-- Link laser dealing damage (Tech 2).
				local key = "lastMementoMoriLinkLaserHit:" .. player.InitSeed .. "/" .. laser.InitSeed
				local lastHit = tookDamage:GetData()[key] or 0
				local currentFrame = game:GetFrameCount()
				local hitDelay = lib.GetUnmodifiedFireDelay(player) * 0.5
				if lastHit <= currentFrame - hitDelay then
					tookDamage:GetData()[key] = currentFrame
					tookDamage:TakeDamage(player.Damage * 0.25, damageFlags, EntityRef(player), 0)
				end
				return false
			end
			
			if not laserData.isMementoMoriHitboxLaser or playerData.mementoMoriEpicFetus then
				return false
			end
			
			-- Allow Memento Mori slashes to pierce armor, since they can't be spammed, and should be
			-- a worthwhile reward if placed carefully.
			damageFlags = damageFlags | DamageFlag.DAMAGE_IGNORE_ARMOR
			
			local isFireplace = (tookDamage.Type == EntityType.ENTITY_FIREPLACE)
			if tookDamage:IsInvincible() or laserData.isMementoMoriBrimstone or (not tookDamage:IsVulnerableEnemy() and not isFireplace) then
				return nil
			end
			
			local sigilData = sigil:GetData()
			
			if not sigilData.mementoMoriSigilAttackTargets then
				sigilData.mementoMoriSigilAttackTargets = {}
			end
			if not sigilData.mementoMoriSigilAttackTargets[tookDamage.InitSeed] then
				sigilData.mementoMoriSigilAttackTargets[tookDamage.InitSeed] = {
					ref = tookDamage,
					numHits = 0,
				}
			end
			
			local hitData = sigilData.mementoMoriSigilAttackTargets[tookDamage.InitSeed]
			
			if not isFromDeathShadow and playerData.mementoMoriCombo >= 4 then
				sfxManager:Play(SoundEffect.SOUND_TOOTH_AND_NAIL, 1.0, 0, false, 1.0)
				--local pos = player.Position
				
				local pos = tookDamage.Position + Vector(0, -1) * tookDamage.Size
				
				local scale = math.max(0.4, 0.025 * tookDamage.Size)
				
				--[[for i=0,3 do
					local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 3, pos + Vector(2,0):Rotated(i*90), lib.ZeroVector, player):ToEffect()
					poof.Color = Color(0,0,0, 0.25)
					poof.SpriteScale = Vector(scale, scale)
				end
				
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 3, pos, lib.ZeroVector, player):ToEffect()
				poof.Color = tookDamage.SplatColor
				poof.SpriteScale = Vector(scale, scale)]]
				
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 3, pos, lib.ZeroVector, player):ToEffect()
				poof.Color = Color(0,0,0, 0.75)
				poof.SpriteScale = Vector(scale, scale)
				
				--tookDamage:SetColor(Color(0,0,0,1), 10, 9999, false, false)
				--poof.SpriteScale = Vector(0.02, 0.02) * tookDamage.Size
				if not laserData.didFinalMementoMoriHit then
					--game:ShakeScreen(40)
					--sfxManager:Play(482, 1.2, 0, false, 0.6)
					sfxManager:Play(615, 1.75, 0, false, 0.8)
					sfxManager:Play(33, 0.75, 0, false, 1.5)
					
					if player:GetPlayerType() == lib.TaintedSamaelId and lib.HasBirthcake(player) then
						mod:MementoMoriFear(player)
					end
					
					laserData.didFinalMementoMoriHit = true
				end
			elseif isFromDeathShadow then
				sfxManager:Play(SoundEffect.SOUND_TOOTH_AND_NAIL, 0.85, 0, false, 1.5)
			else
				sfxManager:Play(SoundEffect.SOUND_TOOTH_AND_NAIL, 0.85, 0, false, 1.5 + 0.1 * playerData.mementoMoriCombo)
			end
			
			-- Adjust damage based on distance with Proptosis or Lump of Coal.
			if not isFromDeathShadow and (lib.HasItem(player, CollectibleType.COLLECTIBLE_PROPTOSIS) or lib.HasItem(player, CollectibleType.COLLECTIBLE_LUMP_OF_COAL)) then
				local endPoint = sigil.Position
				local startPoint = laser.Position - (laser.Position - endPoint):Resized(50)
				
				local distToStart = startPoint:Distance(tookDamage.Position)
				local distToEnd = endPoint:Distance(tookDamage.Position)
				local distToNearest = math.min(distToStart, distToEnd)
				
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_PROPTOSIS) then
					local minDist = 75
					local maxDist = 250
					local minDamage = damage * 0.5
					local maxDamage = damage * 1.5
					if distToNearest < minDist then
						damage = maxDamage
					elseif distToNearest > maxDist then
						damage = minDamage
					else
						damage = lib.Lerp(maxDamage, minDamage, (distToNearest - minDist) / (maxDist - minDist))
					end
				end
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_LUMP_OF_COAL) then
					local minDist = 75
					local maxDist = 200
					local minDamage = damage
					local maxDamage = damage * 1.5
					if distToNearest < minDist then
						damage = minDamage
					elseif distToNearest > maxDist then
						damage = maxDamage
					else
						damage = lib.Lerp(minDamage, maxDamage, (distToNearest - minDist) / (maxDist - minDist))
					end
				end
			end
			
			local tearParams = player:GetTearHitParams(WeaponType.WEAPON_TEARS)
			local tearFlags = tearParams.TearFlags
			local rng = player:GetCollectibleRNG(kMementoMori)
			
			-- Draw "slashing" effects.
			local slashSizeFactor = tookDamage.Size
			if not isFromDeathShadow and playerData.mementoMoriCombo >= 4 then
				slashSizeFactor = slashSizeFactor * 1.5
			end
			local scaleValue = tearParams.TearScale
			if isFromDeathShadow then
				scaleValue = 1
			end
			local pos = tookDamage.Position - Vector(0, slashSizeFactor * 0.5) + lib.NormalVector:Resized(rng:RandomFloat()*slashSizeFactor*0.75):Rotated(rng:RandomInt(360))
			local v = Vector(0, 20 + slashSizeFactor * 0.75):Rotated(rng:RandomInt(360)) * math.max(math.min(scaleValue, 2), 0.75)
			
			local numSlashes = lib.GetNumProjectiles(player)
			if isFromDeathShadow then
				numSlashes = 1
			end
			
			for i=0, numSlashes-1 do
				local w = numSlashes * 8
				local offset = lib.ZeroVector
				if numSlashes > 1 then
					offset = v:Rotated(90):Resized(w * (math.floor(i*0.5))/(numSlashes-1)) - v:Rotated(90):Resized(w * 0.15 + rng:RandomFloat()*0.15)
				end
				local slashWidth = 0.85 * math.min(scaleValue, 2)
				if i % 2 == 0 then
					mod:SpawnSlashEffect(player, pos + offset, v, slashWidth, tookDamage, isFromDeathShadow, sigil:GetData().mementoMoriSlashColor)
				else
					mod:SpawnSlashEffect(player, pos + offset:Rotated(90), v:Rotated(90), slashWidth, tookDamage, isFromDeathShadow, sigil:GetData().mementoMoriSlashColor)
				end
				if i > 0 then
					tookDamage:TakeDamage(damage, damageFlags, EntityRef(player), 0)
				end
				if not isFromDeathShadow then
					-- Mulligan/Guppy
					if (lib.HasItem(player, CollectibleType.COLLECTIBLE_MULLIGAN) and rng:RandomInt(6) == 0)
							or (player:HasPlayerForm(PlayerForm.PLAYERFORM_GUPPY) and rng:RandomInt(2) == 0)
							or HasFlag(tearFlags, TearFlags.TEAR_MULLIGAN) then
						player:AddBlueFlies(1, sigil.Position, tookDamage)
					end
					-- Parasitoid
					--local parasitoidRoll = lib.HasItem(player, CollectibleType.COLLECTIBLE_PARASITOID) and rng:RandomFloat() < lib.GetCappedActivationChance(0.15, 0.5, player.Luck, 5, false)
					if HasFlag(tearFlags, TearFlags.TEAR_EGG) then
						if rng:RandomInt(2) == 0 then
							player:AddBlueFlies(rng:RandomInt(2) + 1, sigil.Position, tookDamage)
						else
							for s=0, rng:RandomInt(2) do
								player:AddBlueSpider(sigil.Position)
							end
						end
					end
					-- Bleed
					if lib.HasItem(player, CollectibleType.COLLECTIBLE_BACKSTABBER) then
						local chance = lib.GetCappedActivationChance(0.1, 1, player.Luck, 20, true)
						if rng:RandomFloat() <= chance then
							tookDamage:AddEntityFlags(EntityFlag.FLAG_BLEED_OUT)
						end
					end
					-- Tough Love
					if lib.HasItem(player, CollectibleType.COLLECTIBLE_TOUGH_LOVE) and rng:RandomFloat() < lib.GetActivationChance(0.1, player.Luck, 10, true) then
						local projVel = Vector(0, player.ShotSpeed * 7):Rotated(rng:RandomInt(360))
						local tear = player:FireTear(tookDamage.Position, projVel, false, true, false, player, 1)
						if tear.Variant ~= TearVariant.TOOTH then
							if tear.Variant ~= TearVariant.RAZOR then
								tear.CollisionDamage = tear.CollisionDamage * 3.2
							end
							tear:ChangeVariant(TearVariant.TOOTH)
						end
						if lib.SameColor(tear.Color, lib.SamaelTearColor) then
							tear.Color = lib.NullColor
						end
						tear:GetData().mementoMoriToothParent = tookDamage
						tear:GetSprite():LoadGraphics()
					end
					-- Time Itself
					if hitData.numHits == 0 and tookDamage:GetData().FFMultiEuclideanDuration and tookDamage:GetData().FFMultiEuclideanDuration >= 0 then
						local dir = playerData.mementoMoriTrajectory or RandomVector()
						local endPos = tookDamage.Position + dir:Resized(player.TearRange)
						local slash = mod:DoMementoMoriStyleSlash(player, tookDamage.Position, endPos, Color(1,1,1,1))
						slash:GetData().mementoMoriNoHit = tookDamage.InitSeed
					end
				end
			end
			
			-- Queue up additional hits against this entity where appropriate.
			hitData.lastHit = game:GetFrameCount()
			hitData.damageFlags = damageFlags
			hitData.numHits = hitData.numHits + 1
			
			if not isFromDeathShadow then
				-- Eye of Belial
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_BELIAL) or HasFlag(tearFlags, TearFlags.TEAR_BELIAL) then
					local projVel = (playerData.mementoMoriCurrentTrajectory or lib.NormalVector:Rotated(rng:RandomInt(360))):Resized(player.ShotSpeed * 7)
					local tear = player:FireTear(tookDamage.Position, projVel, false, true, false, player, 1)
					tear:AddTearFlags(TearFlags.TEAR_PIERCING | TearFlags.TEAR_HOMING)
					tear:ChangeVariant(TearVariant.BELIAL)
					if lib.SameColor(tear.Color, lib.SamaelTearColor) then
						tear.Color = lib.NullColor
					end
					--tear:GetData().mementoMoriToothParent = tookDamage
					tear:GetSprite():LoadGraphics()
				end
				
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_CURSED_EYE) and rng:RandomInt(10) == 0 then
					lib.TeleportEnemy(player, tookDamage)
				end
				
				-- Eye of Greed / Midas Touch
				if lib.HasItem(player, CollectibleType.COLLECTIBLE_EYE_OF_GREED)
						or lib.HasItem(player, CollectibleType.COLLECTIBLE_MIDAS_TOUCH) then
					local chance = 0.1 * player:GetCollectibleNum(CollectibleType.COLLECTIBLE_EYE_OF_GREED)
							+ 0.05 * player:GetCollectibleNum(CollectibleType.COLLECTIBLE_MIDAS_TOUCH)
					if rng:RandomFloat() < chance then
						tookDamage:AddMidasFreeze(EntityRef(player), 90)
					end
				elseif HasFlag(tearFlags, TearFlags.TEAR_MIDAS) then
					tookDamage:AddMidasFreeze(EntityRef(player), 90)
				end
			end
			
			if FiendFolio and sigil:GetData().sigilForceCrit then
				damage = damage * FiendFolio.CritDamageMult
				FiendFolio:doCriticalHitFx(tookDamage.Position, tookDamage, player)
			end
			
			if FiendFolio and sigil:GetData().sigilForceBruise then
				FiendFolio.AddBruise(tookDamage, player, 120, 1, 1)
			end
			
			-- With ipecac, create an explosion instead of dealing direct damage.
			if not isFromDeathShadow and lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC) then
				game:BombExplosionEffects(
					pos,
					damage, --math.max(damage, 44),
					tearParams.TearFlags,
					tearParams.TearColor,
					player,
					math.max(tearParams.TearScale, 0.5)
				)
			else
				tookDamage:TakeDamage(damage, damageFlags, EntityRef(player), 0)
			end
			
			-- Yes, return false. The actual damage is inflicted via TakeDamage or by the sigil after being queued up in mementoMoriSigilAttackTargets.
			return false
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.MementoMoriPreTakeDamage)

-- Book of virtues
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, function(_, fam)
	if fam.SubType == kMementoMori then
		local sigil
		for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, kSigilId)) do
			if not sigil or sigil.FrameCount > ent.FrameCount then
				sigil = ent
			end
		end
		fam:GetData().mementoMoriSigil = sigil
		fam:Update()
	end
end, FamiliarVariant.WISP)

mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, function(_, fam)
	if fam.SubType == kMementoMori then
		local sigil = fam:GetData().mementoMoriSigil
		if not sigil or not sigil:Exists() or sigil:GetSprite():IsPlaying("Death") then
			fam:Remove()
			return
		end
		fam.Position = sigil.Position
		fam.Velocity = lib.ZeroVector
		local c = GetSlashColor(fam.Player or Isaac.GetPlayer())
		c:SetOffset(0.2, 0.2, 0.2)
		fam.Color = c
		fam:RemoveFromOrbit()
	end
end, FamiliarVariant.WISP)

function mod:MementoMoriPetrification(player, duration)
	local pData = player:GetData()
	
	for _, npc in pairs(Isaac.FindInRadius(player.Position, 9999, EntityPartition.ENEMY)) do
		if npc:IsEnemy() and not npc:IsBoss() then
			npc:AddFreeze(EntityRef(player), math.ceil(duration))
		end
	end
end

function mod:MementoMoriFear(player)
	local pData = player:GetData()
	
	for _, npc in pairs(Isaac.FindInRadius(player.Position, 9999, EntityPartition.ENEMY)) do
		if npc:IsEnemy() then
			local duration = 30*4
			if npc:IsBoss() then
				npc:AddFreeze(EntityRef(player), duration)
			end
			npc:AddFear(EntityRef(player), duration)
		end
	end
end
