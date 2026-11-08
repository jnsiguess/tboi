local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local MINI_REAPER = mod.ENTITIES.MINI_REAPER.Var

local NUM_MINI_REAPER_HEADS = 5
local ALT_HEAD_CHANCE = 0.2

function mod:SamaelBirthrightUpdate(player)
	local pData = player:GetData()
	
	if not lib.IsSamael(player) or not lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) then return end
	
	if not pData.samaelMiniReapers then
		pData.samaelMiniReapers = {}
	end
	
	local n = 3 + 2 * (player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BIRTHRIGHT) - 1)
	
	if pData.wraithActive then
		for i=0, n-1 do
			if not pData.samaelMiniReapers[i] or not pData.samaelMiniReapers[i]:Exists() then
				pData.samaelMiniReapers[i] = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, MINI_REAPER, 0, player.Position, RandomVector()*10, player)
				pData.samaelMiniReapers[i]:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
				pData.samaelMiniReapers[i]:Update()
			end
		end
	else
		pData.samaelMiniReapers = {}
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.SamaelBirthrightUpdate)

function mod:MiniReaperInit(fam)
	fam.GridCollisionClass = 0
	fam.EntityCollisionClass = 0
	
	local rng = fam:GetDropRNG()
	
	local head = (fam.SubType == 1) and 2 or 1
	
	if rng:RandomFloat() <= ALT_HEAD_CHANCE then
		head = 1 + rng:RandomInt(NUM_MINI_REAPER_HEADS)
	end
	
	if head > 1 then
		local sprite = fam:GetSprite()
		sprite:ReplaceSpritesheet(5, "gfx/samael_entities/mini_reaper/mini_reaper_"..head..".png")
		if head == 2 then
			for layer = 0, 4 do
				sprite:ReplaceSpritesheet(layer, "gfx/samael_entities/mini_reaper/mini_reaper_body_alt.png")
			end
		end
		sprite:LoadGraphics()
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, mod.MiniReaperInit, MINI_REAPER)

function mod:MiniReaperUpdate(fam)
	local player = fam.Player or Isaac.GetPlayer()
	local room = game:GetRoom()
	
	if (fam.SubType ~= 2 and room:GetFrameCount() == 0) or (fam.SubType == 2 and lib.GetDistFromRoomEdge(fam.Position) > 500) then
		fam:Remove()
		return
	elseif fam.SubType == 0 and not player:GetData().wraithActive then
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 1, fam.Position, lib.ZeroVector, player):ToEffect()
		poof.Color = lib.NewColor(0,0,0,0.4)
		poof.SpriteScale = Vector(0.4, 0.4)
		local poof2 = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, fam.Position, lib.ZeroVector, player):ToEffect()
		poof2.Color = lib.NewColor(0,0,0,0.5)
		poof2.SpriteScale = Vector(0.75, 0.75)
		
		for i=1, 6 do
			local bubbleTrail = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HAEMO_TRAIL, 617, fam.Position - Vector(0, 5), RandomVector()*10, nil):ToEffect()
			bubbleTrail.SpriteScale = Vector(0.4, 0.4)
			bubbleTrail:GetData().samaelBubbleSpeed = 15
			local n = 0
			bubbleTrail.Color = Color(n,n,n, 0.75)
			bubbleTrail:GetSprite():Play("Poof2", true)
			bubbleTrail:GetSprite().PlaybackSpeed = 0.4
			bubbleTrail.Target = player
		end
		
		fam:Remove()
		return
	end
	
	local data = fam:GetData()
	
	fam:PickEnemyTarget(9999, 10, 1 << 0 | 2 << 0)
	
	if fam.SubType == 2 and fam.Coins > 0 then
		fam.Coins = fam.Coins - 1
	end
	
	local targetPos
	local dir = Vector(0, 1)
	
	if fam.SubType == 2 and fam.Coins <= 0 then
		targetPos = fam.Position + (fam.Position - room:GetCenterPos()):Resized(40)
		dir = targetPos - fam.Position
	elseif fam.Target and fam.Target:Exists() then
		targetPos = fam.Target.Position + (fam.Position - fam.Target.Position):Resized(fam.Target.Size + 30):Rotated((Random() % 10) - 5)
		dir = fam.Target.Position - fam.Position
	else
		targetPos = player.Position + (fam.Position - player.Position):Resized(player.Size + 30):Rotated((Random() % 10) - 5)
		if fam.Velocity:Length() > 2 then
			dir = player.Position - fam.Position
		end
	end
	
	local maxSpeed = 20
	local targetVel = (targetPos - fam.Position):Resized(math.min(maxSpeed, targetPos:Distance(fam.Position) * 0.5)):Rotated(((Random() % 10) - 5)*0.1)
	fam.Velocity = lib.Lerp(fam.Velocity, targetVel, 0.1)
	
	for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, MINI_REAPER)) do
		if ent.InitSeed ~= fam.InitSeed and fam.Position:Distance(ent.Position) < fam.Size then
			local pushDir = fam.Position - ent.Position
			if pushDir:Length() == 0 then
				pushDir = RandomVector()
			end
			fam.Velocity = fam.Velocity + pushDir:Resized(1.0)
		end
	end
	
	local clampedDir = lib.GetDirectionFromVector(dir)
	local sprite = fam:GetSprite()
	
	local n = math.sin(game:GetFrameCount() * math.pi / 15) * 0.5 + 0.5
	sprite.Offset = Vector(0, -2 * n)
	
	local anim = "Down"
	if clampedDir == Direction.LEFT then
		anim = "Left"
	elseif clampedDir == Direction.RIGHT then
		anim = "Right"
	elseif clampedDir == Direction.UP then
		anim = "Up"
	end
	
	if not sprite:IsPlaying(anim) then
		sprite:Play(anim, true)
	end
	
	if not sprite:IsOverlayPlaying("Head"..anim) then
		sprite:PlayOverlay("Head"..anim, true)
	end
	
	if not data.samaelMiniScythe or not data.samaelMiniScythe:Exists() then
		local scythe = Isaac.Spawn(EntityType.ENTITY_EFFECT, mod.ENTITIES.MINI_REAPER_SCYTHE.Var, mod.ENTITIES.MINI_REAPER_SCYTHE.Sub, fam.Position, lib.ZeroVector, fam):ToEffect()
		scythe:FollowParent(fam)
		data.samaelMiniScythe = scythe
	end
	local scytheSprite = data.samaelMiniScythe:GetSprite()
	data.samaelMiniScythe.SpriteScale = Vector(0.66, 0.66)
	data.samaelMiniScythe:GetData().miniReaperScytheRot = dir:GetAngleDegrees() - 90
	
	if fam.FireCooldown > 0 then
		fam.FireCooldown = fam.FireCooldown - 1
	elseif fam.Target and fam.Target:Exists() then
		fam.FireCooldown = math.max(math.ceil(lib.GetUnmodifiedFireDelay(player)), 1)
		
		local hitboxPos = fam.Position + dir:Resized(20)
		local hitboxSize = 30
		local hitSomething = false
		
		for _, enemy in pairs(Isaac.FindInRadius(hitboxPos, hitboxSize, EntityPartition.ENEMY)) do
			if enemy:IsVulnerableEnemy() then
				enemy:TakeDamage(player.Damage, 0, EntityRef(fam), 0)
				hitSomething = true
			end
		end
		
		if hitSomething then
			if scytheSprite:IsPlaying("Swing") then
				scytheSprite.FlipX = not scytheSprite.FlipX
			end
			scytheSprite:Play("Swing", true)
		else
			fam.FireCooldown = math.max(math.ceil(fam.FireCooldown * 0.5), 4)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.MiniReaperUpdate, MINI_REAPER)

function mod:MiniReaperScythe(eff)
	if eff.SubType ~= mod.ENTITIES.MINI_REAPER_SCYTHE.Sub then return end
	
	if not eff.Parent or not eff.Parent:Exists() then
		eff:Remove()
		return
	end
	
	local sprite = eff:GetSprite()
	
	if sprite:IsFinished("Swing") then
		sprite:Play("Idle", true)
		sprite.FlipX = not sprite.FlipX
	end
	
	local rotSpeed = 0.25
	if sprite:IsPlaying("Swing") then
		rotSpeed = 0.5
	end
	
	local currentRot = sprite.Rotation
	local targetRot = eff:GetData().miniReaperScytheRot or 0
	if sprite.FlipX then
		targetRot = targetRot * -1
	end
	sprite.Rotation = lib.RotateToward(currentRot, targetRot, rotSpeed)
	local offsetVector = Vector.FromAngle(sprite.Rotation+90):Resized(8)
	if sprite.FlipX then
		offsetVector = Vector(-offsetVector.X, offsetVector.Y)
	end
	sprite.Offset = Vector(0, -10) + offsetVector
	eff.ParentOffset = Vector(0, (eff.Parent:GetSprite():GetAnimation() == "Down") and 1 or -0.5)
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.MiniReaperScythe, mod.ENTITIES.MINI_REAPER_SCYTHE.Var)

function mod:MiniReaperHit(tookDamage, damage, damageFlags, damageSourceRef)
	if tookDamage and tookDamage:ToNPC() and damageSourceRef and damageSourceRef.Type == EntityType.ENTITY_FAMILIAR and damageSourceRef.Variant == MINI_REAPER then
		tookDamage = tookDamage:ToNPC()
		local blood = Isaac.Spawn(1000, 2, 0, lib.Lerp(damageSourceRef.Position, tookDamage.Position, 0.75), lib.ZeroVector, tookDamage) --Blood effect
		blood:GetSprite().Color = tookDamage.SplatColor
		tookDamage:PlaySound(SoundEffect.SOUND_MEATY_DEATHS, 0.75, 0, false, 2.2)
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.MiniReaperHit)

-- Contract of Servitude (Tainted Treasures)

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
	if TaintedTreasure and TaintedCollectibles and TaintedCollectibles.CONTRACT_OF_SERVITUDE then
		local eidDesc = "#Spawns The Forgotten as a familiar, who will attack nearby enemies with their bone club"
				.."#Will collapse after taking enough hits, but then get back up after some time"
				.."#Inherits your stats"
		TaintedTreasure:AddContract(lib.SamaelId, "Guide him", eidDesc)
	end
end)

local function CountFakeForgotten()
	local count = 0
	for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC, 99)) do
		if ent:GetData().isThanatophiliaMinion then
			count = count + 1
		end
	end
	return count
end

function mod:CheckContract()
	local currentCount = CountFakeForgotten()
	local targetCount = 0
	for _, player in pairs(lib.GetPlayers()) do
		if lib.IsSamael(player) and not lib.IsTaintedSamael(player) and player:HasCollectible(TaintedCollectibles.CONTRACT_OF_SERVITUDE) then
			targetCount = targetCount + player:GetCollectibleNum(TaintedCollectibles.CONTRACT_OF_SERVITUDE)
			while currentCount < targetCount do
				mod:SpawnFakeForgotten(player)
				currentCount = currentCount + 1
			end
		end
	end
end

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	if TaintedTreasure and TaintedCollectibles and TaintedCollectibles.CONTRACT_OF_SERVITUDE then
		mod:CheckContract()
	end
end)

mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, function(_, player)
	if TaintedTreasure and TaintedCollectibles and TaintedCollectibles.CONTRACT_OF_SERVITUDE then
		local numContracts = player:GetCollectibleNum(TaintedCollectibles.CONTRACT_OF_SERVITUDE)
		if numContracts ~= player:GetData().samaelContracts then
			mod:CheckContract()
		end
		player:GetData().samaelContracts = numContracts
	end
end)
