local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local REMEMBRANCE_OF_DEATH = mod.ITEMS.REMEMBRANCE_OF_DEATH

local DEATH_SHADOW = mod.ENTITIES.DEATH_SHADOW.Var

local kMementoMoriHitboxLaserVariant = Isaac.GetEntityVariantByName("Memento Mori Hitbox Laser")
local kAttackCooldown = 15
local kHoldBuffer = 5
local kMinDistance = 40

local function StartSlash(player, fam, endPos)
	endPos = endPos or player.Position
	local pDiff =	endPos - fam.Position
	local length = pDiff:Length()
	if length <= 0.1 then
		length = 0.1
	end
	
	sfxManager:Play(SoundEffect.SOUND_KNIFE_PULL, 0.95, 0, false, 0.9)
	
	local laser = Isaac.Spawn(EntityType.ENTITY_LASER, 6, kMementoMoriHitboxLaserVariant, fam.Position - pDiff:Resized(50), lib.ZeroVector, fam):ToLaser()
	laser.Visible = false
	laser.OneHit = true
	laser.Mass = 0
	laser.Parent = player
	laser.Angle = pDiff:GetAngleDegrees()
	laser.DisableFollowParent = true
	laser:SetMaxDistance(length)
	
	laser.ParentOffset = lib.ZeroVector
	laser.PositionOffset = lib.ZeroVector
	
	laser:GetData().isMementoMoriHitboxLaser = true
	
	local data = fam:GetData()
	data.hitboxLaser = laser
	data.currentMementoMoriHitboxLaser = laser
	
	local maxHits = mod:CalcMementoMoriMaxHits(player, true)
	data.maxHits = maxHits
	laser:GetData().maxHits = maxHits
	
	local timeOut = maxHits*2
	
	if maxHits > 6 then
		data.hitCooldown = 1
		timeOut = maxHits
	else
		data.hitCooldown = 2
	end
	
	laser:SetTimeout(timeOut)
	
	local laserDamage = mod:CalcMementoMoriDamage(player, maxHits, 0, 1.0, true)
	if player:HasCollectible(CollectibleType.COLLECTIBLE_BFFS) then
		laserDamage = laserDamage * 1.5
	end
	laser.CollisionDamage = laserDamage
	data.mementoMoriAttackDamage = laserDamage
	laser:GetData().mementoMoriLaserDamage = laserDamage
	
	data.mementoMoriSigilAttackTargets = {}
	data.mementoMoriHitDelay = player.MaxFireDelay
	
	data.slashStartPos = fam.Position
	data.slashEndPos = endPos
	data.isAttacking = true
	data.attackFrames = 0
	data.attackDuration = mod:CalculateMementoMoriDuration(fam.Position:Distance(endPos))
	data.attackReady = false
	data.targetPos = nil
	
	return laser
end

function mod:DoMementoMoriStyleSlash(player, startPos, endPos, trailColor)
	local fam = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, DEATH_SHADOW, 1, startPos, lib.ZeroVector, player):ToFamiliar()
	fam:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	fam.Player = player
	fam.Parent = player
	fam.Visible = false
	fam:GetData().mementoMoriSlashColor = trailColor
	
	local data = player:GetData()
	data.manualMementoMoriSlashes = data.manualMementoMoriSlashes or {}
	data.manualMementoMoriSlashes[fam.InitSeed] = fam
	
	return StartSlash(player, fam, endPos)
end

-- Handling code for when a slash is currently ongoing.
local function HandleAttacking(fam, player)
	local data = fam:GetData()
	
	if not data.mementoMoriTrail or not data.mementoMoriTrail:Exists() then
		local trail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SPRITE_TRAIL, 0, fam.Position, lib.ZeroVector, fam):ToEffect()
		trail.DepthOffset = 255
		trail.RenderZOffset = -255
		if data.mementoMoriSlashColor then
			trail.Color = data.mementoMoriSlashColor
		else
			trail.Color = lib.NewColor(1, 0, 1, 0.75)
		end
		trail.MinRadius = 0.05
		trail:GetData().baseWidth = 2
		mod:UpdateMementoMoriTrailWidth(fam, trail, 0)
		trail.Parent = fam
		trail:Update()
		data.mementoMoriTrail = trail
	end
	
	if not data.attackFrames then data.attackFrames = 0 end
	local percent = data.attackFrames / data.attackDuration
	
	local targetPos = player.Position
	if data.slashEndPos and fam.SubType == 1 then
		targetPos = data.slashEndPos
	end
	
	local dist = data.slashStartPos:Distance(targetPos)
	data.mementoMoriPathLength = dist
	local targetDist = dist * percent
	
	mod:UpdateAlongPath(
		fam,
		{[0]=data.slashStartPos, [1]=targetPos},
		data.prevTargetDist or 0,
		targetDist,
		true)
	data.prevTargetDist = targetDist
	
	if data.attackFrames >= data.attackDuration then
		if fam.SubType == 1 then
			player:GetData().manualMementoMoriSlashes[fam.InitSeed] = nil
			fam:Remove()
			return
		end
		data.isAttacking = false
		data.attackCooldown = kAttackCooldown
		
		for i=1, 10 do
			local bubbleTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HAEMO_TRAIL, 617, player.Position, lib.ZeroVector, player):ToEffect()
			bubbleTrail.Color = Color(0,0,0,1)
			bubbleTrail.SpriteScale = Vector(1, 1)
			bubbleTrail.DepthOffset = -2
			bubbleTrail:GetSprite():Play("Poof2", true)
			bubbleTrail:GetData().samaelTarget = player
			bubbleTrail.Velocity = RandomVector() * 10
		end
	else
		data.attackFrames = data.attackFrames + 1
	end
end

mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, function(_, eff)
	local target = eff:GetData().samaelTarget
	if target then
		local targetVel = target.Position - lib.Lerp(eff.Position, target.Position, 0.25)
		eff.Velocity = lib.Lerp(eff.Velocity, targetVel, 0.15)
	end
end, EffectVariant.HAEMO_TRAIL)

-- Attacking code is handled in the player update to make it smoother (60fps).
function mod:RodPlayerUpdate(player)
	local data = player:GetData()
	if game:GetRoom():GetFrameCount() > 0 and data.samaelShadow and data.samaelShadow:Exists() and data.samaelShadow:GetData().isAttacking then
		HandleAttacking(data.samaelShadow, player)
	end
	if data.manualMementoMoriSlashes then
		for k, fam in pairs(data.manualMementoMoriSlashes) do
			if fam and fam:Exists() and fam:ToFamiliar() then
				HandleAttacking(fam, player)
			else
				data.manualMementoMoriSlashes[k] = nil
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.RodPlayerUpdate)

-- Spawn/remove the shadow familiar as needed.
function mod:RodPeffectUpdate(player)
	local data = player:GetData()
	
	if player:HasCollectible(REMEMBRANCE_OF_DEATH) then
		if not data.samaelShadow or not data.samaelShadow:Exists() then
			data.samaelShadow = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, DEATH_SHADOW, 0, player.Position, lib.ZeroVector, player):ToFamiliar()
			data.samaelShadow:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			data.samaelShadow.Player = player
			data.samaelShadow.Parent = player
			data.samaelShadow.Visible = false
		end
	elseif data.samaelShadow then
		data.samaelShadow:Remove()
		data.samaelShadow = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.RodPeffectUpdate)

function mod:DeathShadowInit(fam)
	fam.Visible = false
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, mod.DeathShadowInit, DEATH_SHADOW)

-- Spawns a visual laser to link between the player and the shadow.
local function SpawnLinkLaser(fam, player)
	local laser = player:FireTechLaser(fam.Position, 0, lib.ZeroVector, false, false, fam, 0.0)
	 
	laser:GetSprite():Load("gfx/memento_mori_link_laser.anm2", true)
	laser:GetSprite():LoadGraphics()
	
	laser.Color = Color(0.8, 0.8, 0.8, 0.8)
	
	local laserData = laser:GetData()
	laserData.isDefaultMementoMoriLinkLaser = true
	laser.TearFlags = TearFlags.TEAR_SPECTRAL | TearFlags.TEAR_PIERCING
	laser.SpawnerEntity = fam
	
	local fly = Isaac.Spawn(EntityType.ENTITY_FLY, 0, 0, lib.ZeroVector, lib.ZeroVector, nil)
	laser.Parent = fly
	fly:Remove()
	
	laser.CollisionDamage = 0
	laserData.mementoMoriLaserParent = player
	laserData.mementoMoriLaserAnchor = player
	laser.Target = fam
	laser.DisableFollowParent = true
	
	laser:SetTimeout(-1)
	laser.Mass = 0
	laserData.isMementoMoriLinkLaser = true
	laserData.isForDeathShadow = true
	
	laserData.mementoMoriLaserAnim = "Laser0"
	
	laser:Update()
	laser:Update()
	laser.SpriteScale = Vector(0.5, 0.5)
	
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP_WEAK)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP_STRONG)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP_BURST)
	
	return laser
end

-- The Reaper/Death Shadow familiar.
function mod:DeathShadowUpdate(fam)
	if not fam.Player then
		fam.Player = Isaac.GetPlayer(0)
	end
	if not fam.Parent then
		fam.Parent = fam.Player
	end
	
	local player = fam.Player
	
	if not player or (fam.SubType == 0 and (not player:GetData().samaelShadow or GetPtrHash(player:GetData().samaelShadow) ~= GetPtrHash(fam))) then
		fam:Remove()
		return
	end
	
	if not fam:GetSprite():IsPlaying("Shadow") then
		fam:GetSprite():Play("Shadow", true)
	end
	
	local data = fam:GetData()
	
	if game:GetRoom():GetFrameCount() == 0 then
		data.isAttacking = false
	end
	if not data.attackCooldown then
		data.attackCooldown = 0
	end
	if data.attackCooldown > 0 then
		data.attackCooldown = data.attackCooldown - 1
	end
	
	local shouldBeVisible = false
	
	if not data.isAttacking then
		if fam.SubType == 1 then
			fam:Remove()
			return
		end
		if data.mementoMoriTrail then
			data.mementoMoriTrail:Update()
			data.mementoMoriTrail.Parent = nil
			data.mementoMoriTrail = nil
		end
		if data.attackCooldown > 0 then
			fam.Position = player.Position
		elseif player:GetFireDirection() == Direction.NO_DIRECTION then
			if fam.ShootDirection ~= Direction.NO_DIRECTION and data.attackReady then
				StartSlash(player, fam)
				
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 1, fam.Position, lib.ZeroVector, fam):ToEffect()
				poof.Color = lib.NewColor(0,0,0,0.4)
				poof.SpriteScale = Vector(0.5, 0.5)
				
				data.heldFrames = 0
			else
				data.targetPos = player.Position
				fam.Position = player.Position
				fam.Velocity = lib.ZeroVector
			end
		else
			data.heldFrames = (data.heldFrames or 0) + 1
			
			if game:GetRoom():GetFrameCount() > 1 and (not data.linkLaser or not data.linkLaser:Exists()) then
				data.linkLaser = SpawnLinkLaser(fam, player)
			end
			
			if not data.targetPos then
				data.targetPos = fam.Position
			end
			fam.Position = data.targetPos
			fam.Velocity = lib.ZeroVector
			
			if data.attackReady then
				shouldBeVisible = true
			elseif data.heldFrames >= kHoldBuffer and player.Position:Distance(fam.Position) > kMinDistance then
				data.attackReady = true
			end
		end
	end
	fam.ShootDirection = player:GetFireDirection()
	
	if shouldBeVisible and not fam.Visible then
		fam:SetColor(lib.InvisibleColor, 6, 1, true, true)
		
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, fam.Position, lib.ZeroVector, fam):ToEffect()
		poof.Color = lib.NewColor(0,0,0,0.666)
		
		sfxManager:Play(SoundEffect.SOUND_BLACK_POOF)
	end
	
	fam.Visible = shouldBeVisible
	if data.linkLaser then
		data.linkLaser.Visible = shouldBeVisible
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.DeathShadowUpdate, DEATH_SHADOW)

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	for _, fam in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, DEATH_SHADOW)) do
		fam:GetData().targetPos = fam:ToFamiliar().Player.Position
	end
end)
