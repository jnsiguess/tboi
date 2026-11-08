local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local NUM_HEADS = 10

local function LoadHeadSprite(soul)
	--[[local fragmentData = mod:GetFragmentData()
	if idx and fragmentData.RoomSeeds then
		seed = fragmentData.RoomSeeds[idx]
	end]]
	local head = soul:GetData().lostSoulOverrideHead
	if not head then
		local seed = mod:IsFragmentRoom() and mod:GetFragmentData().CurrentRoomSeed or game:GetRoom():GetSpawnSeed()
		head = (seed % NUM_HEADS) + 1
	end
	soul:GetData().lostSoulHead = head
	local headStr = (head < 10 and "0" or "") .. head
	local sprite = "gfx/samael_entities/lost_soul/head_" .. headStr .. ".png"
	soul:GetSprite():ReplaceSpritesheet(1, sprite)
	soul:GetSprite():LoadGraphics()
end

function mod:LostSoulInit(soul)
	if soul.Variant ~= mod.ENTITIES.LOST_SOUL.Var then return end
	
	LoadHeadSprite(soul)
	
	soul.SpriteOffset = Vector(0, -3)
	soul:GetSprite():Play("Shake", true)
	
	if soul.SubType == 1 then
		soul:GetData().forceGetup = true
		
		lib.ScheduleForUpdate(function()
			soul.Position = game:GetRoom():FindFreePickupSpawnPosition(Isaac.GetPlayer().Position, 0, true)
		end)
	end
	
	lib.ScheduleForUpdate(function()
		soul:AddCharmed(EntityRef(Isaac.GetPlayer()), -1)
	end)
	
	soul:AddEntityFlags(EntityFlag.FLAG_NO_STATUS_EFFECTS | EntityFlag.FLAG_DONT_OVERWRITE | EntityFlag.FLAG_NO_REWARD | EntityFlag.FLAG_TRANSITION_UPDATE)
	soul.SplatColor = Color(0,0,0, 0.3, 0.8, 0.8, 0.8)
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.LostSoulInit, mod.ENTITIES.LOST_SOUL.ID)

local function MoveToward(entity, targetPos, topSpeed, lerpStrength)
	local targetVel = (targetPos - entity.Position) * 0.5
	topSpeed = topSpeed or 8
	if targetVel:Length() > topSpeed then
		targetVel = targetVel:Resized(topSpeed)
	end
	entity.Velocity = lib.Lerp(entity.Velocity, targetVel, lerpStrength or 0.2)
end

-- We love Repentance+
local playerHasInvincibility = getmetatable(EntityPlayer).__class.HasInvincibility

function mod:LostSoul(soul)
	if soul.Variant ~= mod.ENTITIES.LOST_SOUL.Var then return end
	
	local parentPlayer = (soul.Parent and soul.Parent:Exists()) and soul.Parent:ToPlayer()
	local player = parentPlayer or Isaac.GetPlayer()
	local room = game:GetRoom()
	local sprite = soul:GetSprite()
	local data = soul:GetData()
	local fragmentData = mod:GetFragmentData()
	
	soul.EntityCollisionClass = EntityCollisionClass.ENTCOLL_ALL
	soul:AddCharmed(EntityRef(player), -1)
	
	if mod:IsFragmentCombatRoom() and (room:IsClear() or fragmentData.openPortalForSoul) and not soul:IsDead() then 
		fragmentData.savedSoul = soul:GetData().lostSoulHead or 1
	end
	
	if not mod:IsFragmentRoom() and room:IsClear() and not soul:IsDead() then
		local eff = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, soul.Position, soul.Velocity)
		eff:GetSprite():Play("Prize", true)
		
		local runData = mod:GetAllRunData()
		runData.lostSoulsSaved = (runData.lostSoulsSaved or 0) + 1
		
		soul:Remove()
		return
	end
	
	if room:GetFrameCount() > 1 and (room:IsClear() or fragmentData.openPortalForSoul) and game:IsPaused() and mod:IsFragmentCombatRoom() then
		if sprite:GetAnimation() ~= "Trapdoor" then
			sprite:Play("Trapdoor", true)
		end
		soul.Position = lib.Lerp(soul.Position, player.Position, 0.2)
		soul.Velocity = lib.ZeroVector
	elseif sprite:GetAnimation() == "Shake" then
		room:KeepDoorsClosed()
		if mod:IsFragmentCombatRoom() then
			fragmentData.soulWaiting = true
		end
		if soul:GetData().forceGetup and soul.FrameCount > 25 then
			soul.Parent = player
			sprite:Play("GetUp", true)
		elseif room:GetFrameCount() > 1 then
			soul.Velocity = lib.Lerp(soul.Velocity, lib.ZeroVector, 0.1)
			for _, p in pairs(lib.GetPlayers()) do
				if p.Position:Distance(soul.Position) <= 40 then
					soul.Parent = p
					parentPlayer = p
					player = p
					sprite:Play("GetUp", true)
					break
				end
			end
		end
	elseif sprite:IsFinished("Hurt") and not parentPlayer then
		sprite:Play("Shake", true)
	elseif sprite:IsFinished("GetUp") or sprite:IsFinished("Hurt") or sprite:GetAnimation() == "Trapdoor" then
		sprite:Play("Idle", true)
	end
	
	local hidingBehindPlayer = false
	
	if sprite:GetAnimation() == "Idle" or sprite:GetAnimation() == "Hurt" then
		if not parentPlayer then
			soul.Parent = player
		end
		
		soul.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
		
		local buffer = player.Size + 25
		local playerAimDir = player:GetAimDirection()
		if playerAimDir:Length() == 0 then
			local offset = soul.Position - player.Position
			if offset:Length() == 0 then
				offset = RandomVector()
			end
			local targetOffset = offset:Resized(math.min(buffer, soul.Position:Distance(player.Position)))
			local targetPos = player.Position + targetOffset
			MoveToward(soul, targetPos, 10 * player.MoveSpeed * 0.8, 0.25)
		else
			local targetOffset = playerAimDir:Resized(-buffer)
			local targetPos = player.Position + targetOffset
			MoveToward(soul, targetPos, 10 * player.MoveSpeed * 1.2, 0.5)
			hidingBehindPlayer = true
		end
		
		local playerTeleporting = player:GetData().mementoMoriActive or player:GetSprite():IsPlaying("TeleportUp") or player:GetSprite():IsPlaying("TeleportDown")
		
		if playerTeleporting or player:GetData().wraithActive or playerHasInvincibility(player, 0, EntityRef(player)) then
			if playerTeleporting then
				soul.Visible = false
				data.soulHidden = true
				soul.Position = player.Position
			end
			data.iFrames = 15
		elseif data.soulHidden then
			soul.Visible = true
			data.soulHidden = nil
		end
	else
		soul.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_GROUND
	end
	
	if hidingBehindPlayer then
		sprite.FlipX = (soul.Position - player.Position).X > 0
	elseif soul.Velocity.X ~= 0 then
		sprite.FlipX = soul.Velocity.X < 0
	end
	
	if data.iFrames then
		data.iFrames = data.iFrames - 1
		if soul.FrameCount % 4 == 0 then
			soul:SetColor(lib.InvisibleColor, 2, 0, false, false)
		end
		if data.iFrames <= 0 then
			data.iFrames = nil
		end
	end
	
	data.lastUpdate = game:GetFrameCount()
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.LostSoul, mod.ENTITIES.LOST_SOUL.ID)

-- Very rough method of keeping Lost Souls from dying to friendly ghost explosions :/
local lastFriendlyGhostExplosion = 0

mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, function(_, eff)
	if eff.SubType > 0 and eff.SpawnerType == EntityType.ENTITY_PLAYER then
		lastFriendlyGhostExplosion = game:GetFrameCount()
	elseif eff.SpawnerType == 0 then  -- VADE RETRO
		for _, otherEff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.ENEMY_GHOST, 0)) do
			if otherEff.SpawnerType == EntityType.ENTITY_PLAYER and otherEff.Position:Distance(eff.Position) < 1 then
				eff:GetData().probablyvaderetro = true
				lastFriendlyGhostExplosion = game:GetFrameCount()
			end
		end
	end
end, EffectVariant.ENEMY_GHOST)

mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, function(_, eff)
	if eff.Variant == EffectVariant.ENEMY_GHOST and eff.SubType == 0 and eff.SpawnerType == EntityType.ENTITY_PLAYER then
		lastFriendlyGhostExplosion = game:GetFrameCount()
	end
end, EntityType.ENTITY_EFFECT)

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	lastFriendlyGhostExplosion = 0
end)

function mod:LostSoulCollision(soul, collider)
	if soul.Variant ~= mod.ENTITIES.LOST_SOUL.Var then return end
	
	if collider.Type == EntityType.ENTITY_ETERNALFLY then
		-- Just don't let them touch eternal flies.
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.LostSoulCollision, mod.ENTITIES.LOST_SOUL.ID)

function mod:LostSoulHurt(soul, damage, flags, source)
	if soul.Variant ~= mod.ENTITIES.LOST_SOUL.Var then return end
	
	if source.Type == EntityType.ENTITY_ETERNALFLY then
		-- Just don't let them die to eternal flies.
		return false
	end
	
	local data = soul:GetData()
	if game:GetRoom():GetFrameCount() < 5 or (data.iFrames and data.iFrames > 0) or game:IsPaused() then
		return false
	end
	
	-- Broadly resist damage from friendly ghost explosions, ugh.
	if game:GetFrameCount() - lastFriendlyGhostExplosion <= 13 then
		for _, eff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.ENEMY_GHOST, 1)) do
			if (eff.SpawnerType == EntityType.ENTITY_PLAYER or eff:GetData().probablyvaderetro) and eff.Position:Distance(soul.Position) < 100 then
				return false
			end
		end
	end
	
	sfxManager:Play(SoundEffect.SOUND_ISAAC_HURT_GRUNT, 0.8, 0, false, 1.5)
	soul:GetSprite():Play("Hurt", true)
	soul:GetData().iFrames = 30
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.LostSoulHurt, mod.ENTITIES.LOST_SOUL.ID)

function mod:LostSoulDeath(soul)
	if soul.Variant ~= mod.ENTITIES.LOST_SOUL.Var then return end
	
	local eff = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, soul.Position)
	eff:GetSprite():Play("Death", true)
	if mod:IsFragmentCombatRoom() then
		mod:GetFragmentData().savedSoul = nil
		mod:GetFragmentData().soulDied = true
	end
	sfxManager:Play(SoundEffect.SOUND_ISAACDIES, 0.8, 0, false, 1.5)
	
	local runData = mod:GetAllRunData()
	runData.lostSoulsKilled = (runData.lostSoulsKilled or 0) + 1
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.LostSoulDeath, mod.ENTITIES.LOST_SOUL.ID)

--------------------------------------------------
-- Freed / Passenger
--------------------------------------------------

local SOUL_OFFSET = {
	Vector(-2, 0),
	Vector(20, -1),
	Vector(11, 2),
	Vector(9, -8),
}
local SOUL_DEPTH = { 2, 3, 1, 4 }

local function GetLostSoulPassengerOffset(soul, ferryman)
	local i = math.max(soul.State, 1)
	local offset = SOUL_OFFSET[i] or SOUL_OFFSET[1]
	if ferryman:GetSprite().FlipX then
		offset = offset * Vector(-1, 1)
	end
	return offset
end

function mod:LostSoulEffectInit(soul)
	if soul.SubType ~= mod.ENTITIES.LOST_SOUL_EFFECT.Sub then return end
	
	lib.ScheduleForUpdate(function()
		LoadHeadSprite(soul)
	end)
	
	local ferryman = soul.SpawnerEntity
	if ferryman then
		soul:GetSprite():Play("BoatWave", true)
		soul.SpriteOffset = GetLostSoulPassengerOffset(soul, ferryman)
	elseif mod:IsFragmentEntrance() then
		soul:GetSprite():Play("Prize", true)
		
		local runData = mod:GetAllRunData()
		runData.lostSoulsSaved = (runData.lostSoulsSaved or 0) + 1
		
		soul.SpriteOffset = Vector(0, -3)
	else
		soul:GetSprite():Play("Fade", true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.LostSoulEffectInit, mod.ENTITIES.LOST_SOUL_EFFECT.Var)

local function LostSoulInBoat(ferryman, soul)
	if not ferryman or not ferryman:Exists() then
		soul:Remove()
		return
	end
	
	soul:GetData().isInBoat = true
	soul.Position = ferryman.Position
	
	local targetOffset = GetLostSoulPassengerOffset(soul, ferryman)
	soul.SpriteOffset = lib.Lerp(soul.SpriteOffset, targetOffset, 0.2)
	
	soul.DepthOffset = -5 - (SOUL_DEPTH[soul.State] or 1)
	
	local x = ferryman:GetData().samaelFerrymanFade
	if x then
		local c = Color(x,x,x,1)
		soul:SetColor(c, 2, 1, false, true)
	end
end

function mod:LostSoulEffect(soul)
	if soul.SubType ~= mod.ENTITIES.LOST_SOUL_EFFECT.Sub then return end
	
	local ferryman = soul.SpawnerEntity
	if ferryman and ferryman:Exists() then
		LostSoulInBoat(ferryman, soul)
		return
	elseif soul:GetData().isInBoat then
		soul:Remove()
		return
	end
	
	local fragmentData = mod:GetFragmentData()
	local sprite = soul:GetSprite()
	
	if sprite:IsPlaying("Death") then
		local t = soul.FrameCount
		local dur = 40
		if t >= dur then
			soul:Remove()
			return
		end
		local riseSpeed = 3
		local shakeAmp = 6
		local shakeInterval = 8
		local offset = shakeAmp * math.sin(t * math.pi / shakeInterval)
		sprite.Offset = Vector(offset, -t * riseSpeed)
		sprite.Color = Color(1,1,1, 1 - t/dur)
	elseif sprite:IsFinished("Prize") then
		local maybeFerryman = Isaac.FindByType(EntityType.ENTITY_SLOT, mod.ENTITIES.FERRYMAN.Var)
		if #maybeFerryman > 0 then
			soul.TargetPosition = maybeFerryman[1].Position + Vector(10, 5)
			sprite:Play("IdleHappy", true)
		else
			sprite:Play("Fade", true)
		end
	elseif sprite:GetAnimation() == "Prize" then
		if sprite:IsEventTriggered("Prize") then
			if game.Challenge == mod.CHALLENGES.THE_REAPER.ID then
				mod:ReaperChallengeSoulReward(soul.Position)
			elseif mod:IsFragmentRoom() then
				mod:FragmentReward(soul.Position)
				fragmentData.pendingReward = false
			else
				mod:FragmentFragmentReward(soul.Position)
			end
		end
		if soul.TargetPosition:Length() > 0 then
			MoveToward(soul, soul.TargetPosition)
		else
			soul.Velocity = lib.Lerp(soul.Velocity, lib.ZeroVector, 0.1)
		end
	elseif sprite:IsFinished("Fade") or (sprite:GetAnimation() == "IdleHappy" and soul.Position:Distance(soul.TargetPosition) < 15) then
		if mod:IsFragmentRoom() then
			fragmentData.soulsInBoat = fragmentData.soulsInBoat or {}
			table.insert(fragmentData.soulsInBoat, soul:GetData().lostSoulHead or 1)
			fragmentData.soulHeadedToBoat = nil
		end
		soul:Remove()
	elseif sprite:GetAnimation() == "IdleHappy" then
		MoveToward(soul, soul.TargetPosition)
	end
	
	if soul.Velocity.X ~= 0 then
		sprite.FlipX = soul.Velocity.X < 0
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.LostSoulEffect, mod.ENTITIES.LOST_SOUL_EFFECT.Var)
