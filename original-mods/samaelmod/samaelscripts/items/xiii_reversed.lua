local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager
local musicManager = mod.MusicManager

local XIII_REVERSED = mod.ITEMS.XIII_REVERSED

local kRespawnSpeedUpRate = 0.001

local kMinRespawnTime = 200
local kMaxRespawnTime = 800

local kMinRespawnTimeHP = 5
local kMaxRespawnTimeHP = 60

local kRespawnTimeMaxVariance = 0.1

local kPointsNeededBase = 30
local kPointsNeededPerFloor = 15
local kPointsNeededPerReward = 0
local kPointsNeededPerRewardMult = 1.25

local respawnSpeed = 1.0
local effectActive = false
local effectFrames = 0
local endEffectInFrames = -1
local spawnTable
local allEnemiesPoints = 0
local ghostCountdown = 0

local SPAWN_EFFECT = {
	TYPE = EntityType.ENTITY_EFFECT,
	VARIANT = Isaac.GetEntityVariantByName("(Samael) Spawning Effect"),
	MAIN = 5,
	GLOW = 4,
}

local kAmbientSound = Isaac.GetSoundIdByName("SamaelXiiiReversedAmbience")
local kAmbientLayer = Isaac.GetSoundIdByName("SamaelXiiiReversedLayer")

function mod:XiiiReversed(_, player, useFlags)
	local room = game:GetRoom()
	local roomType = room:GetType()
	
	if useFlags & UseFlag.USE_CARBATTERY ~= 0 then
		return false
	end
	
	if effectActive or not room:IsClear() or roomType == RoomType.ROOM_BOSS or roomType == RoomType.ROOM_MINIBOSS or roomType == RoomType.ROOM_BOSSRUSH then
		lib.RefundInvalidCardUse(player, XIII_REVERSED, useFlags, SoundEffect.SOUND_REVERSE_DEATH)
		return false
	else
		local numEnemies = room:GetAliveEnemiesCount()
		
		room:SetClear(false)
		room:RespawnEnemies()
		room:SetClear(true)
		
		if room:GetAliveEnemiesCount() <= numEnemies then
			lib.RefundInvalidCardUse(player, XIII_REVERSED, useFlags, SoundEffect.SOUND_REVERSE_DEATH)
			return false
		end
		
		effectActive = true
		respawnSpeed = 1.0
		game:ShowHallucination(999999, 0)
		allEnemiesPoints = 0
		effectFrames = 0
		ghostCountdown = 0
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_CARD, CallbackPriority.EARLY, mod.XiiiReversed, XIII_REVERSED)

local function GetXiiiPoints()
	return mod:GetAllCurrentRoomData().XiiiPoints or 0
end
local function AddXiiiPoints(points)
	mod:GetAllCurrentRoomData().XiiiPoints = GetXiiiPoints() + points
end
local function ResetXiiiPoints()
	mod:GetAllCurrentRoomData().XiiiPoints = 0
end

local function TarotClothActive()
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasCollectible(CollectibleType.COLLECTIBLE_TAROT_CLOTH) then
			return true
		end
	end
	return false
end

function mod:XiiiPointsFromKill(npc)
	if effectActive and lib.AllowOnDeathEffect(npc, false, true) then
		local points = npc.MaxHitPoints
		if TarotClothActive() then
			points = math.ceil(points * 1.5)
		end
		AddXiiiPoints(points)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.XiiiPointsFromKill)

local function SpawnXiiiRewards()
	local room = game:GetRoom()
	
	local points = GetXiiiPoints()
	local pointsNeeded = kPointsNeededBase + kPointsNeededPerFloor * (game:GetLevel():GetStage() - 1)
	
	while points >= pointsNeeded do
		points = points - pointsNeeded
		Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, 2, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
		pointsNeeded = pointsNeeded * kPointsNeededPerRewardMult + kPointsNeededPerReward
	end
	
	ResetXiiiPoints()
end

local ForbiddenEntityType = {}
for _, eType in pairs({
			EntityType.ENTITY_NULL,
			EntityType.ENTITY_PLAYER,
			EntityType.ENTITY_TEAR,
			EntityType.ENTITY_FAMILIAR,
			EntityType.ENTITY_BOMB,
			EntityType.ENTITY_PICKUP,
			EntityType.ENTITY_SLOT,
			EntityType.ENTITY_LASER,
			EntityType.ENTITY_KNIFE,
			EntityType.ENTITY_PROJECTILE,
			EntityType.ENTITY_FIREPLACE,
			EntityType.ENTITY_PITFALL,
			EntityType.ENTITY_MOVABLE_TNT,
			EntityType.ENTITY_POKY,
			EntityType.ENTITY_WALL_HUGGER,
			EntityType.ENTITY_GENERIC_PROP,
			EntityType.ENTITY_DUMMY,
			EntityType.ENTITY_MINECART,
			EntityType.ENTITY_SIREN_HELPER,
			EntityType.ENTITY_HORNFEL_DOOR,
			EntityType.ENTITY_TRIGGER_OUTPUT,
			EntityType.ENTITY_ENVIRONMENT,
			999, -- Also effects?
			EntityType.ENTITY_EFFECT,
			EntityType.ENTITY_TEXT,
			EntityType.ENTITY_SHOPKEEPER,
			EntityType.ENTITY_STONEHEAD,
			EntityType.ENTITY_CONSTANT_STONE_SHOOTER,
			EntityType.ENTITY_STONE_EYE,
			EntityType.ENTITY_BRIMSTONE_HEAD,
			EntityType.ENTITY_GAPING_MAW,
			EntityType.ENTITY_BROKEN_GAPING_MAW,
			EntityType.ENTITY_QUAKE_GRIMACE,
			EntityType.ENTITY_BOMB_GRIMACE,
		}) do
	ForbiddenEntityType[eType] = true
end
function mod:EntityTypeRespawnForbidden(id)
	return ForbiddenEntityType[id] or id > 1000
end

-- Based on StageAPI code. Thanks DeadInfinity.
local function GetCurrentRoomSpawns()
	local room = game:GetRoom()
	local roomWidth = room:GetGridWidth()
	local roomData = game:GetLevel():GetCurrentRoomDesc().Data
	
	local tab = {}
	
	local spawns = roomData.Spawns
	
	for i = 0, spawns.Size - 1 do
		local spawn = spawns:Get(i)
		
		if spawn then
			local gridIdx = roomWidth + 1 + (spawn.X + roomWidth * spawn.Y)
			local pos = room:GetGridPosition(gridIdx)
			tab[i] = {
				Spawns = {},
				Position = pos,
			}
			
			local sumWeight = spawn.SumWeights
			local maxWeight = 0
			local weight = 0
			
			for j = 1, spawn.EntryCount do
				local entry = spawn:PickEntry(weight)
				if not mod:EntityTypeRespawnForbidden(entry.Type) then
					table.insert(tab[i].Spawns, {
						Type = entry.Type,
						Variant = entry.Variant,
						SubType = entry.Subtype,
						Weight = entry.Weight,
					})
				end
				maxWeight = math.max(maxWeight, entry.Weight)
				weight = weight + entry.Weight / sumWeight
			end
			
			if #tab[i].Spawns == 0 then
				tab[i] = nil
			elseif maxWeight == 0 then
				-- Handles the case where all enemies were given a weight of 0.
				for _, enemyData in pairs(tab[i].Spawns) do
					enemyData.Weight = 1.0
				end
			end
		end
	end
	
	return tab
end

local function AllowEntityToRespawn(entity)
	return entity and entity:IsEnemy() and not mod:EntityTypeRespawnForbidden(entity.Type) and entity.MaxHitPoints > 1
end

local function GetEntityRespawnTime(entity, rng)
	local hp = math.max(kMinRespawnTimeHP, math.min(entity.MaxHitPoints, kMaxRespawnTimeHP))
	local respawnTime = lib.Lerp(kMinRespawnTime, kMaxRespawnTime, (hp - kMinRespawnTimeHP) / (kMaxRespawnTimeHP - kMinRespawnTimeHP))
	
	if rng then
		local variance = respawnTime * (rng:RandomFloat() * kRespawnTimeMaxVariance)
		if rng:RandomInt(2) == 0 then
			variance = -variance
		end
		respawnTime = respawnTime + variance
	end
	
	return respawnTime
end

local whiteOut

-- Continually respawn enemies while the effect is active.
function mod:XiiiReversedEffect()
	local currentPoints = GetXiiiPoints()
	
	if not effectActive then
		if currentPoints > 0 then
			SpawnXiiiRewards()
		end
		return
	end
	
	local playedSound = false
	if not sfxManager:IsPlaying(kAmbientSound) then
		sfxManager:Play(kAmbientSound, 0.45, 0, true, 1.0)
		playedSound = true
	end
	if not sfxManager:IsPlaying(kAmbientLayer) then
		sfxManager:Play(kAmbientLayer, 1.25, 0, true, 1.0)
		playedSound = true
	end
	if playedSound then
		musicManager:VolumeSlide(0.5)
		musicManager:PitchSlide(0.9)
	end
	
	if not spawnTable then
		spawnTable = GetCurrentRoomSpawns()
	end
	
	local room = game:GetRoom()
	
	-- Keep doors closed.
	for i=0, 7 do
		local door = room:GetDoor(i)
		if door and door:IsOpen() then
			door:Close(true)
		end
	end
	room:KeepDoorsClosed()
	
	if endEffectInFrames > 0 then
		endEffectInFrames = endEffectInFrames - 1
	end
	if endEffectInFrames == 0 then
		for _, player in pairs(lib.GetPlayers()) do
			player.ControlsEnabled = false
			game:ChangeRoom(game:GetLevel():GetCurrentRoomIndex())
			player:AnimateAppear()
			player.Velocity = lib.ZeroVector
			player:GetData().xiiiReversedEndFrames = 30
		end
		
		whiteOut = Sprite()
		whiteOut:Load("gfx/ui/bossoverlay_whiteout.anm2", true)
		whiteOut.PlaybackSpeed = 1.5
		whiteOut.Scale = Vector(10,10)
		whiteOut:Play("Fade", true)
		whiteOut:SetFrame(15)
		
		endEffectInFrames = -1
		return
	end
	
	local rng = RNG()
	rng:SetSeed(mod:GetAllCurrentRoomData().XiiiRngSeed or room:GetAwardSeed(), 1)
	
	local countingTotalPoints = (allEnemiesPoints == 0)
	
	local emptyRoom = room:GetAliveEnemiesCount() == 0
	
	for i, spawnData in pairs(spawnTable) do
		if not spawnData.Cooldown or spawnData.Cooldown <= 0 then
			local enemyData = lib.PickRandom(spawnData.Spawns, rng)
			if enemyData then
				local spawnPos = spawnData.Position + RandomVector() * 1
				local entity = Isaac.Spawn(enemyData.Type, enemyData.Variant, enemyData.SubType, spawnPos, lib.ZeroVector, nil)
				if AllowEntityToRespawn(entity) then
					local firstSpawn = not spawnData.Cooldown
					spawnData.Cooldown = GetEntityRespawnTime(entity, rng)
					spawnData.MaxCooldown = spawnData.Cooldown
					if countingTotalPoints then
						allEnemiesPoints = allEnemiesPoints + entity.MaxHitPoints
					end
					if firstSpawn then
						entity:Remove()
					end
				else
					entity:Remove()
					enemyData.Weight = 0
					spawnData.Cooldown = 10
				end
			else
				spawnTable[i] = nil
			end
		elseif emptyRoom then
			spawnData.Cooldown = spawnData.Cooldown - respawnSpeed * 5
		else
			spawnData.Cooldown = spawnData.Cooldown - respawnSpeed
		end
		
		if spawnData.Cooldown and spawnData.MaxCooldown then
			if not spawnData.Effect or not spawnData.Effect:Exists() then
				spawnData.Effect = Isaac.Spawn(SPAWN_EFFECT.TYPE, SPAWN_EFFECT.VARIANT, SPAWN_EFFECT.MAIN, spawnData.Position, lib.ZeroVector, nil)
			end
			local minPercent = 0.75
			local a = 0
			if spawnData.Cooldown <= spawnData.MaxCooldown * minPercent then
				a = (spawnData.MaxCooldown * minPercent - spawnData.Cooldown) / (spawnData.MaxCooldown * minPercent)
			end
			local c = Color(1,1,1,a)
			local brightness = 1.5
			c:SetColorize(brightness,brightness,brightness,brightness)
			spawnData.Effect.Color = c
		end
	end
	
	local ghostSpawnStartPoints = allEnemiesPoints * 2
	
	if currentPoints > ghostSpawnStartPoints and ghostCountdown <= 0 then
		local posOptions = {}
		for i=0, 7 do
			if room:IsDoorSlotAllowed(i) then
				table.insert(posOptions, room:GetDoorSlotPosition(i))
			end
		end
		local pos = posOptions[rng:RandomInt(#posOptions) + 1]
		local direction = (room:GetCenterPos() - pos):Resized(10)
		local ghost = Isaac.Spawn(EntityType.ENTITY_BEAST, 3, 0, pos - direction:Resized(100), lib.ZeroVector, nil):ToNPC()
		ghost:GetData().samaelXiii = true
		ghost:GetData().samaelXiiiSpeedLimit = 10 * (currentPoints / ghostSpawnStartPoints)
		ghost:Update()
		ghost.Velocity = direction
		
		ghostCountdown = lib.Lerp(120, 60, math.min(1, (currentPoints - ghostSpawnStartPoints) / (ghostSpawnStartPoints * 2)))
	end
	
	local respawnSpeedUp = kRespawnSpeedUpRate * math.max(1, currentPoints / allEnemiesPoints)
	if emptyRoom then
		respawnSpeedUp = respawnSpeedUp * 5
	end
	respawnSpeed = respawnSpeed + respawnSpeedUp
	
	effectFrames = effectFrames + 1
	if ghostCountdown > 0 then
		ghostCountdown = ghostCountdown - 1
	end
	mod:GetAllCurrentRoomData().XiiiRngSeed = rng:GetSeed()
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.XiiiReversedEffect)

function mod:XiiiReversedSpawnEffect(eff)
	if eff.SubType == SPAWN_EFFECT.MAIN then
		if not eff.Child or not eff.Child:Exists() then
			eff.Child = Isaac.Spawn(SPAWN_EFFECT.TYPE, SPAWN_EFFECT.VARIANT, SPAWN_EFFECT.GLOW, eff.Position, lib.ZeroVector, nil)
			eff.Child.Parent = eff
		end
		eff.Child.Color = eff.Color
	elseif eff.SubType == SPAWN_EFFECT.MAIN then
		if not eff.Parent then
			eff:Remove()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.XiiiReversedSpawnEffect, SPAWN_EFFECT.VARIANT)

function mod:XiiiReversedTesting()
	local pos = Isaac.WorldToScreen(game:GetRoom():GetCenterPos())
	Isaac.RenderText(""..(mod:GetAllCurrentRoomData().XiiiPoints or 0), pos.X, pos.Y, 1, 1, 1, 255)
end
--mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.XiiiReversedTesting)

function mod:StopXiiiReversed()
	if effectActive then
		effectActive = false
		if mod.GameStarted then
			game:ShowHallucination(-1, 0)
			sfxManager:Play(SoundEffect.SOUND_FLASHBACK)
		end
		sfxManager:Stop(SoundEffect.SOUND_DEATH_CARD)
		sfxManager:Stop(kAmbientSound)
		sfxManager:Stop(kAmbientLayer)
		musicManager:UpdateVolume()
		musicManager:ResetPitch()
	end
	spawnTable = nil
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.StopXiiiReversed)

function mod:XiiiReversedPlayerDamage(player, damage, damageFlags, damageSourceRef)
	player = player:ToPlayer()
	if not player then return end
	local data = player:GetData()
	
	local isSelfDamage = damageSourceRef.Type == EntityType.ENTITY_PLAYER
			or (damageFlags & (DamageFlag.DAMAGE_RED_HEARTS | DamageFlag.DAMAGE_IV_BAG | DamageFlag.DAMAGE_CURSED_DOOR | DamageFlag.DAMAGE_DEVIL) ~= 0)
	if isSelfDamage then return end
	
	if data.xiiiReversedEndFrames and data.xiiiReversedEndFrames > 0 then
		return false
	end
	
	if effectActive then
		if effectFrames > 30 and endEffectInFrames <= 0 then
			endEffectInFrames = 10
			player:TakeDamage(1, DamageFlag.DAMAGE_FAKE, EntityRef(player), 0)
		end
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.XiiiReversedPlayerDamage, EntityType.ENTITY_PLAYER)

function mod:XiiiReversedFlash() 
	if whiteOut then
		if whiteOut:IsFinished() then
			whiteOut = nil
		else
			whiteOut:Render(lib.ZeroVector,lib.ZeroVector,lib.ZeroVector)
			whiteOut:Update()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.XiiiReversedFlash)

function mod:XiiiReversedPlayerUpdate(player)
	local data = player:GetData()
	if data.xiiiReversedEndFrames then
		if data.xiiiReversedEndFrames <= 0 or player:GetSprite():GetAnimation() ~= "Appear" then
			player.ControlsEnabled = true
			data.xiiiReversedEndFrames = nil
		else
			player.ControlsEnabled = false
			player.Velocity = lib.ZeroVector
			data.xiiiReversedEndFrames = data.xiiiReversedEndFrames - 1
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.XiiiReversedPlayerUpdate)

function mod:XiiiReversedGhost(entity)
	local data = entity:GetData()
	
	if entity.Variant ~= 3 or not data.samaelXiii then return end
	
	if entity.State == 8 then
		local clampedPos = game:GetRoom():GetClampedPosition(entity.Position, 0)
		if entity.Position:Distance(clampedPos) > 0 then
			local hitTopOrBottom = clampedPos.Y ~= entity.Position.Y
			if hitTopOrBottom then
				entity.Velocity = Vector(entity.Velocity.X, -entity.Velocity.Y)
			end
			local hitSide = clampedPos.X ~= entity.Position.X
			if hitSide then
				entity.Velocity = Vector(-entity.Velocity.X, entity.Velocity.Y)
			end
			
			entity.Position = clampedPos
		end
		
		if data.samaelXiiiSpeedLimit then
			local speedLimit = data.samaelXiiiSpeedLimit
			local targetVelocity = entity.Velocity:Resized(speedLimit)
			if entity.Velocity:Length() > speedLimit then
				entity.Velocity = targetVelocity
			else
				entity.Velocity = lib.Lerp(entity.Velocity, targetVelocity, 0.1)
			end
		end
	end
	
	-- print(entity.StateFrame) -- 62
	entity.StateFrame = entity.StateFrame + 1
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.XiiiReversedGhost, EntityType.ENTITY_BEAST)