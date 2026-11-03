local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local REMEMBRANCE_OF_THE_FORGOTTEN = mod.ITEMS.REMEMBRANCE_OF_THE_FORGOTTEN

local FORGOTTEN_SKULL = mod.ENTITIES.FORGOTTEN_SKULL.Var
local FORGOTTEN_SOUL = mod.ENTITIES.FORGOTTEN_SOUL.Var

local kForgottenSkullNewRoomBuffer = 5
local kForgottenFamiliarLifespan = 30 * 10
local kSoulColor = Color(1.5, 1.7, 2.0, 0.65, 0.05, 0.12, 0.2)

local function GetBoys(player)
	local data = player:GetData()
	if not data.samaelRotfData then
		data.samaelRotfData = {}
	end
	return data.samaelRotfData
end

local function CountBoys(player)
	local data = GetBoys(player)
	
	local count = 0
	
	for key, ent in pairs(data) do
		if not ent or not ent:Exists() then
			local foundIt = false
			for _, foundSoul in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FORGOTTEN_SOUL, 0)) do
				if foundSoul.InitSeed == key then
					data[key] = foundSoul
					foundIt = true
					break
				end
			end
			if foundIt then
				count = count + 1
			else
				data[key] = nil
			end
		elseif not (ent.Type == EntityType.ENTITY_FAMILIAR and ent.Variant == FORGOTTEN_SOUL)
				and not (ent.Type == EntityType.ENTITY_PICKUP and ent.Variant == FORGOTTEN_SKULL) then
			data[key] = nil
		else
			count = count + 1
		end
	end
	
	return count
end

local function TrySpawnSkull(player)
	if game:GetRoom():IsClear() and game:GetRoom():GetAliveEnemiesCount() == 0 then return end
	
	local current = CountBoys(player)
	local maximum = player:GetCollectibleNum(REMEMBRANCE_OF_THE_FORGOTTEN)
	
	if current < maximum then
		local room = game:GetRoom()
		local skullPos = room:FindFreePickupSpawnPosition(room:GetRandomPosition(0))
		local skull = Isaac.Spawn(EntityType.ENTITY_PICKUP, FORGOTTEN_SKULL, 0, skullPos, lib.ZeroVector, player)
		
		GetBoys(player)[skull.InitSeed] = skull
		
		return skull
	end
end

function mod:RotfPlayerUpdate(player)
	--local c = player.Color
	--print(c.R.."-"..c.G.."-"..c.B.." / "..c.RO.."-"..c.GO.."-"..c.BO)
	-- 1.5, 1.7, 2.0, 0.05, 0.12, 0.2
	--player.Color = Color(1.5, 1.7, 2.0, 1.0, 0.05, 0.12, 0.2)
	
	local data = player:GetData()
	
	if player:HasCollectible(REMEMBRANCE_OF_THE_FORGOTTEN) and game:GetRoom():GetFrameCount() > kForgottenSkullNewRoomBuffer then
		if not data.rotfCountdown then
			data.rotfCountdown = 15 + Random() % 30
		else
			data.rotfCountdown = data.rotfCountdown - 1
		end
		
		if data.rotfCountdown <= 0 then
			TrySpawnSkull(player)
			data.rotfCountdown = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.RotfPlayerUpdate)

local function FixVector(vec)
	return Vector(math.floor(vec.X), math.floor(vec.Y))
end

local function FireFamiliarProjectileGroup(player, fam, numTears, projOrigin, projVel)
	local groupArc = 2 + numTears*2
	
	for j=0, numTears-1 do
		local projAngleOffset = 0
		if numTears > 1 then
			projAngleOffset = ( (groupArc/(numTears-1))*j ) - (groupArc/2)
		end
		local projOriginOffset = projVel:Resized(10):Rotated(projAngleOffset*15)
		local pos = projOrigin + projOriginOffset
		local vel = projVel
		if numTears > 2 then
			vel = vel:Rotated(projAngleOffset)
		end
		local tear = player:FireTear(pos, vel, true, true, false, fam, 1.0)
	end
end

local function FireFamiliarProjectiles(player, fam)
	local numTears = lib.GetNumProjectiles(player)
	local fullWizArc = 90
	local groups = 1 + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_THE_WIZ)
	local groupTears = math.ceil(numTears / groups)
	
	local projOrigin = fam.Position + Vector(0, -5)
	local projVel = lib.DirectionalVector(player:GetFireDirection(), player.ShotSpeed * 10)
	
	if numTears == 1 then
		local eyeOffset = fam:GetData().lastEyeOffset
		if eyeOffset == LaserOffset.LASER_TECH1_OFFSET then
			eyeOffset = LaserOffset.LASER_TECH2_OFFSET
		else
			eyeOffset = LaserOffset.LASER_TECH1_OFFSET
		end
		fam:GetData().lastEyeOffset = eyeOffset
		projOrigin = projOrigin + player:GetLaserOffset(eyeOffset, projVel) * 0.5 + Vector(0, 10)
	end
	
	projVel = projVel + player:GetTearMovementInheritance(projVel)
	
	for i=0, groups-1 do
		local groupAngle = 0
		if groups > 1 then
			groupAngle = ( (fullWizArc/(groups-1))*i ) - (fullWizArc/2)
		end
		FireFamiliarProjectileGroup(player, fam, groupTears, projOrigin, projVel:Rotated(groupAngle))
	end
end

function mod:ForgottenFamiliarInit(fam)
	local sprite = fam:GetSprite()
	sprite:ReplaceSpritesheet(1, "gfx/characters/costumes/character_018_thesoul.png")
	sprite:ReplaceSpritesheet(4, "gfx/characters/costumes/character_018_thesoul.png")
	sprite:ReplaceSpritesheet(12, "gfx/characters/costumes/character_018_thesoul.png")
	sprite:LoadGraphics()
	sprite:Play("HeadDown", true)
	sprite.PlaybackSpeed = 0.5
	
	fam.PositionOffset = Vector(0, -4)
	fam.Color = kSoulColor
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, mod.ForgottenFamiliarInit, FORGOTTEN_SOUL)

local HeadAnim = {
	[Direction.NO_DIRECTION] = "HeadDown",
	[Direction.DOWN] = "HeadDown",
	[Direction.UP] = "HeadUp",
	[Direction.LEFT] = "HeadLeft",
	[Direction.RIGHT] = "HeadRight",
}

local CopiedAnimations = {}
for _, anim in pairs({"Hit", "Sad", "Happy", "TeleportUp", "TeleportDown", "Jump"}) do
	CopiedAnimations[anim] = true
end

function mod:ForgottenFamiliar(fam)
	if game:GetRoom():GetFrameCount() == 0 then
		fam:Remove()
		return
	end
	
	local player = fam.Player or Isaac.GetPlayer(0)
	local originPlayer = fam.SpawnerEntity and fam.SpawnerEntity:ToPlayer() or player
	
	if not GetBoys(originPlayer)[fam.InitSeed] then
		fam:Remove()
		return
	end
	
	local sprite = fam:GetSprite()
	sprite:SetOverlayRenderPriority(true)
	
	local overlay = sprite:GetOverlayAnimation()
	
	if sprite:GetOverlayAnimation() ~= "WalkDown" then
		sprite:PlayOverlay("WalkDown", true)
	end
	
	local targetDist = 45
	
	local targetPosOffset = fam.Position - player.Position
	if targetPosOffset:Length() > targetDist then
		targetPosOffset = targetPosOffset:Resized(targetDist)
	end
	targetPos = player.Position + targetPosOffset
	fam.Velocity = lib.Lerp(fam.Position, targetPos, 0.3) - fam.Position
	
	for _, otherFam in pairs(Isaac.FindInRadius(fam.Position, fam.Size, EntityPartition.FAMILIAR)) do
		if GetPtrHash(fam) ~= GetPtrHash(otherFam) then
			fam.Velocity = fam.Velocity + (fam.Position - otherFam.Position):Resized(1)
		end
	end
	
	if fam.FireCooldown > 0 then
		fam.FireCooldown = fam.FireCooldown - 1
	else
		fam.ShootDirection = Direction.NO_DIRECTION
	end
	
	if fam.HeadFrameDelay > 0 then
		fam.HeadFrameDelay = fam.HeadFrameDelay - 1
	end
	
	if fam.FireCooldown <= 0 and player:GetFireDirection() ~= Direction.NO_DIRECTION then
		FireFamiliarProjectiles(player, fam)
		local fireDelay = lib.GetUnmodifiedFireDelay(player) + 1
		fam.FireCooldown = math.floor(fireDelay)
		fam.ShootDirection = player:GetFireDirection()
		fam.HeadFrameDelay = math.max(2, math.ceil(fireDelay * 0.3))
	end
	
	local playerSprite = player:GetSprite()
	local playerAnim = playerSprite:GetAnimation()
	
	if CopiedAnimations[playerAnim] then
		sprite:SetFrame(playerAnim, playerSprite:GetFrame())
	elseif fam.FireCooldown > 0 and fam.ShootDirection ~= Direction.NO_DIRECTION then
		local anim = HeadAnim[fam.ShootDirection]
		local frame = 0
		if fam.HeadFrameDelay > 0 then
			frame = 2
		end
		sprite:SetFrame(anim, frame)
	else
		local anim = HeadAnim[player:GetHeadDirection()]
		sprite:SetFrame(anim, 0)
	end
	
	if fam.FrameCount > kForgottenFamiliarLifespan then
		local data = GetBoys(originPlayer)
		
		sfxManager:Play(SoundEffect.SOUND_RECALL, 1, 0, false, 1.2)
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 10, fam.Position, lib.ZeroVector, nil)
		
		local pos = fam.Position
		data[fam.InitSeed] = nil
		fam:Remove()
		
		local skull = TrySpawnSkull(originPlayer)
		if skull then
			local soul = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.ENEMY_SOUL, 0, pos, lib.ZeroVector, nil):ToEffect()
			soul:GetData().samaelSoulTarget = skull
			soul:GetData().isSamaelSoul = true
			soul.Target = skull
			soul.Velocity = RandomVector() * 10
		end
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.ForgottenFamiliar, FORGOTTEN_SOUL)

-- Chain rendering
local chainSprite = Sprite()
chainSprite:Load("gfx/1000.114_forgotten chain.anm2", true)
chainSprite:Play("Idle", true)
chainSprite.Color = kSoulColor

local function RenderChain(player, fam)
	local pos1 = fam.Position - Vector(0, 15)
	local pos2 = player.Position - Vector(0, 15)
	
	local numChains = 5
	for i=1, numChains do
		local n = i / (numChains+1)
		local pos = lib.Lerp(pos1, pos2, n)
		local yOffset = 10 * math.sin(math.pi * n)
		local renderPos = Isaac.WorldToScreen(pos + Vector(0, yOffset))
		chainSprite:Render(renderPos, lib.ZeroVector, lib.ZeroVector)
	end
end

mod:AddCallback(ModCallbacks.MC_POST_FAMILIAR_RENDER, function(_, fam)
	local player = fam.Player or Isaac.GetPlayer(0)
	
	if player.Position.Y >= fam.Position.Y then
		RenderChain(player, fam)
	end
end, FORGOTTEN_SOUL)

mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, function(_, player)
	for _, fam in pairs(GetBoys(player)) do
		if fam:ToFamiliar() and fam:Exists() and player.Position.Y < fam.Position.Y and GetPtrHash(player) == GetPtrHash(fam.Player) then
			RenderChain(player, fam)
		end
	end
end)

local function ActivateForgottenSkull(skull, touchedPlayer)
	sfxManager:Play(SoundEffect.SOUND_SCAMPER)
	sfxManager:Play(SoundEffect.SOUND_RECALL_FINISH, 1, 0, false, 1.2)
	skull:GetSprite():Play("Collect", true)
	skull.Velocity = lib.ZeroVector
	
	local originPlayer = skull.SpawnerEntity and skull.SpawnerEntity:ToPlayer() or parentPlayer
	
	local data = GetBoys(originPlayer)
	data[skull.InitSeed] = nil
	
	local fam = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FORGOTTEN_SOUL, 0, skull.Position, lib.ZeroVector, originPlayer):ToFamiliar()
	fam:SetColor(Color(1,1,1,1,0.5,0.5,0.75), 15, 1, true, false)
	fam.SpawnerEntity = originPlayer
	fam.Player = touchedPlayer
	data[fam.InitSeed] = fam
	GetBoys(touchedPlayer)[fam.InitSeed] = fam
end

-- Detect when the player touches the skull.
function mod:ForgottenSkullCollision(skull, collider)
	if skull:IsShopItem() then return end
	
	if skull:GetSprite():GetAnimation() == "Collect" then
		return true
	end
	
	if collider and collider:ToPlayer() then
		ActivateForgottenSkull(skull, collider:ToPlayer())
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_PICKUP_COLLISION, mod.ForgottenSkullCollision, FORGOTTEN_SKULL)

-- Post-update function (handles animations and some other stuff).
function mod:ForgottenSkullUpdate(skull)
	local sprite = skull:GetSprite()
	
	if game:GetRoom():GetFrameCount() <= kForgottenSkullNewRoomBuffer or sprite:IsFinished("Collect") then
		skull:Remove()
		return
	end
	
	if sprite:GetAnimation() == "Collect" then
		mod:ForgottenSkullStopMoving(skull)
	elseif sprite:IsPlaying("Appear") and sprite:IsEventTriggered("DropSound") then
		sfxManager:Play(SoundEffect.SOUND_FETUS_JUMP)
	end
	
	if sprite:GetAnimation() == "Idle" or sprite:WasEventTriggered("DropSound") then
		local meleePickupPlayer = mod:GetBoneSwingPickupPlayer(skull)
		if meleePickupPlayer then
			ActivateForgottenSkull(skull, meleePickupPlayer)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_UPDATE, mod.ForgottenSkullUpdate, FORGOTTEN_SKULL)

-- Freeze the skull in place when touched.
function mod:ForgottenSkullStopMoving(skull)
	local data = skull:GetData()
	
	if skull:GetSprite():GetAnimation() == "Collect" then
		if data.samaelPickupFixedPos then
			skull.Position = data.samaelPickupFixedPos
		end
		skull.Velocity = lib.ZeroVector
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_RENDER, mod.ForgottenSkullStopMoving, FORGOTTEN_SKULL)
