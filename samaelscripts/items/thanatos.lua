local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfx = mod.SfxManager

local COFFIN_ID = Isaac.GetEntityVariantByName("(Samael) Thanatos Coffin")

local CHAIN_LENGTH = 45

local function GetCoffinParent(ent)
	return lib.Pointee(ent:GetData().thanatosParent)
end

local function GetCoffinChild(ent)
	return lib.Pointee(ent:GetData().thanatosCoffin)
end

local function CountCoffins(player)
	local numCoffins = 0
	local ent = player
	while GetCoffinChild(ent) do
		ent = GetCoffinChild(ent)
		numCoffins = numCoffins + 1
	end
	return numCoffins, ent
end

function mod:ThanatosUpdate(player)
	if not player:HasCollectible(mod.ITEMS.THANATOS) then return end
	
	local data = player:GetData()
	local numCoffins, lastChain = CountCoffins(player)
	local maxCoffins = 3 + 2 * (player:GetCollectibleNum(mod.ITEMS.THANATOS)-1)
	
	if not data.thanatosSpawnedNewRoomCoffins then
		while numCoffins < maxCoffins do
			Isaac.Spawn(EntityType.ENTITY_FAMILIAR, COFFIN_ID, 0, player.Position, lib.ZeroVector, player)
			numCoffins = numCoffins + 1
		end
		data.thanatosSpawnedNewRoomCoffins = true
	elseif numCoffins < maxCoffins then
		if not data.thanatosCoffinCountdown then
			data.thanatosCoffinCountdown = 300
		elseif data.thanatosCoffinCountdown > 0 then
			data.thanatosCoffinCountdown = data.thanatosCoffinCountdown - 1
		else
			Isaac.Spawn(EntityType.ENTITY_FAMILIAR, COFFIN_ID, 0, lastChain.Position, lib.ZeroVector, player)
			data.thanatosCoffinCountdown = nil
		end
	elseif data.thanatosCoffinCountdown then
		data.thanatosCoffinCountdown = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.ThanatosUpdate)

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	for _, player in pairs(lib.GetPlayers()) do
		player:GetData().thanatosSpawnedNewRoomCoffins = nil
	end
	for _, fam in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC)) do
		fam:GetData().thanatosMinionHolyMantleBroken = nil
		fam:GetData().thanatosMinionInvuln = nil
	end
end)

local function UpdateCoffinIndexes(player)
	local ent = player
	local idx = 0
	while GetCoffinChild(ent) do
		ent = GetCoffinChild(ent)
		idx = idx + 1
		ent:GetData().thanatosCoffinIndex = idx
	end
end

function mod:ThanatosCoffin(fam)
	local data = fam:GetData()
	local sprite = fam:GetSprite()
	local player = fam.Player or Isaac.GetPlayer(0)
	
	-- Set up / maintain parenting chain.
	if not GetCoffinParent(fam) then
		local newParent = player
		local idx = 0
		while GetCoffinChild(newParent) do
			newParent = GetCoffinChild(newParent)
			idx = idx + 1
		end
		newParent:GetData().thanatosCoffin = EntityPtr(fam)
		data.thanatosParent = EntityPtr(newParent)
		UpdateCoffinIndexes(player)
		if data.thanatosCoffinChainLength then
			data.thanatosCoffinChainLength = CHAIN_LENGTH * 2
		end
	end
	
	local parent = GetCoffinParent(fam) or player
	local idx = data.thanatosCoffinIndex or 0
	
	-- Follow parent
	if not data.thanatosCoffinChainLength or math.abs(data.thanatosCoffinChainLength - CHAIN_LENGTH) < 0.5 then
		data.thanatosCoffinChainLength = CHAIN_LENGTH
	elseif data.thanatosCoffinChainLength ~= CHAIN_LENGTH then
		data.thanatosCoffinChainLength = lib.Lerp(data.thanatosCoffinChainLength, CHAIN_LENGTH, 0.03)
	end
	local targetDist = data.thanatosCoffinChainLength
	local targetPosOffset = fam.Position - parent.Position
	if targetPosOffset:Length() > targetDist then
		targetPosOffset = targetPosOffset:Resized(targetDist)
	end
	targetPos = parent.Position + targetPosOffset
	fam.Velocity = lib.Lerp(fam.Position, targetPos, 0.3) - fam.Position
	
	-- Floating
	local x = game:GetFrameCount() - idx * 10
	local amplitude = 8
	local wavelength = 30
	local y = amplitude * (0.5*math.sin(math.pi * x / wavelength) + 0.5)
	fam.PositionOffset = Vector(0, -(6 + y))
	
	-- Collision with other coffins
	for _, ent in pairs(Isaac.FindInRadius(fam.Position, 10, EntityPartition.FAMILIAR)) do
		if ent.Variant == COFFIN_ID and GetPtrHash(fam) ~= GetPtrHash(ent) then
			if fam.Position:Distance(ent.Position) == 0 then
				fam:AddVelocity(RandomVector())
			else
				fam:AddVelocity((fam.Position - ent.Position):Resized(1))
			end
		end
	end
	
	-- Collision with projectiles
	for _, proj in pairs(Isaac.FindInRadius(fam.Position, 20, EntityPartition.BULLET)) do
		if not proj:ToProjectile():HasProjectileFlags(ProjectileFlags.CANT_HIT_PLAYER) then
			proj:Die()
			fam:SetColor(Color(1,0,0, 1, 0.5,0,0), 5, 1, true, false)
			local currentFrame = game:GetFrameCount()
			local lastHit = fam:GetData().thanatosCoffinLastHit or 0
			if fam.HitPoints > 1 and currentFrame - lastHit > 30 then
				fam.HitPoints = fam.HitPoints - 1
				fam:GetData().thanatosCoffinLastHit = currentFrame
			end
			if fam.SubType ~= 1 and fam.HitPoints == 1 then
				fam.SubType = 1
			end
		end
	end
	
	if fam.SubType == 1 then
		if sprite:IsFinished("Break") or sprite:IsEventTriggered("BREAK") then
			fam:Die()
			mod:SpawnThanatosMinion(player, fam.Position)
			sfx:Play(SoundEffect.SOUND_POT_BREAK)
			sfx:Play(SoundEffect.SOUND_CHAIN_BREAK)
			local num = 8
			for i=0, num-1 do
				local projVel = Vector(6,0):Rotated(360 * i/num)
				local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLUE_FLAME, 0, fam.Position, projVel, player):ToEffect()
				flame:SetDamageSource(EntityType.ENTITY_PLAYER)
				flame.LifeSpan = 60
				flame.Timeout = 60
				flame.State = 1
				flame.CollisionDamage = player.Damage * 6
			end
			for i=0, 10 do
				local vel = Vector((Random()%7) - 3, (Random()%7) - 3) * 2
				Isaac.Spawn(1000, EffectVariant.NAIL_PARTICLE, 1, fam.Position + vel:Resized(20), vel, fam):ToEffect()
			end
		elseif not sprite:IsPlaying("Break") then
			sprite:Play("Break", true)
		end
		
		if sprite:IsEventTriggered("CRACK") then
			sfx:Play(SoundEffect.SOUND_POT_BREAK, 1, 0, false, 1.5)
			local num = 4
			for i=1, 3 do
				local projVel = Vector(4,0):Rotated(Random() % 360)
				local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLUE_FLAME, 0, fam.Position, projVel, player):ToEffect()
				flame:SetDamageSource(EntityType.ENTITY_PLAYER)
				flame.LifeSpan = 60
				flame.Timeout = 60
				flame.State = 1
				flame.CollisionDamage = player.Damage * 6
			end
			for i=0, 5 do
				local vel = Vector((Random()%7) - 3, (Random()%7) - 3)
				Isaac.Spawn(1000, EffectVariant.NAIL_PARTICLE, 1, fam.Position + vel:Resized(20), vel, fam):ToEffect()
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.ThanatosCoffin, COFFIN_ID)

-- Chain rendering
local chainSprite = Sprite()
chainSprite:Load("gfx/samael_entities/thanatos/chain.anm2", true)
chainSprite:Play("Idle", true)
--chainSprite.Color = kSoulColor

local function RenderChain(parent, child)
	local pos1 = child.Position + child.PositionOffset - Vector(0, 15)
	local pos2 = parent.Position + parent.PositionOffset - Vector(0, 15)
	local dist = pos1:Distance(pos2)
	
	local numChains = 5
	for i=1, numChains do
		local n = i / (numChains+1)
		local pos = lib.Lerp(pos1, pos2, n)
		local maxDistOffset = CHAIN_LENGTH * 0.5
		--local yScale = math.min(math.abs(CHAIN_LENGTH - dist), maxDistOffset)
		--local yScale2 = lib.Lerp(5, 10, yScale / maxDistOffset)
		local a = CHAIN_LENGTH * 0.5
		local b = CHAIN_LENGTH * 2
		local hang = math.min(math.max(a, dist), b)
		local hangVal = lib.Lerp(10, 0, (hang-a) / (b-a))
		local yOffset = hangVal * math.sin(math.pi * n)
		local renderPos = Isaac.WorldToScreen(pos + Vector(0, yOffset))
		chainSprite:Render(renderPos, lib.ZeroVector, lib.ZeroVector)
	end
end

function mod:ThanatosRenderChainFromChild(fam)
	local parent = GetCoffinParent(fam)
	
	if parent and parent:Exists() and parent.Position.Y >= fam.Position.Y then
		RenderChain(parent, fam)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_FAMILIAR_RENDER, mod.ThanatosRenderChainFromChild, COFFIN_ID)

function mod:ThanatosRenderChainFromParent(ent)
	local coffin = GetCoffinChild(ent)
	
	if coffin and coffin:Exists() and ent.Position.Y < coffin.Position.Y then
		RenderChain(ent, coffin)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, mod.ThanatosRenderChainFromParent)
mod:AddCallback(ModCallbacks.MC_POST_FAMILIAR_RENDER, mod.ThanatosRenderChainFromParent, COFFIN_ID)

-- Stats

function mod:ThanatosDamageUp(player)
	if lib.HasItem(player, mod.ITEMS.THANATOS) then
		player.Damage = player.Damage + 0.3 * player:GetCollectibleNum(mod.ITEMS.THANATOS)
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.ThanatosDamageUp, CacheFlag.CACHE_DAMAGE)

function mod:ThanatosTearHeight(player)
	if lib.HasItem(player, mod.ITEMS.THANATOS) then
		player.TearHeight = player.TearHeight - 8
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.ThanatosTearHeight, CacheFlag.CACHE_RANGE)

-- Minions

local BODY_ANM2 = "gfx/samael_entities/thanatos/body.anm2"
local SOUL_ANM2 = "gfx/samael_entities/thanatos/soul.anm2"

local THANATOS_MINIONS = {
	BONY = {
		Scale = 0.5,
		Anm2 = BODY_ANM2,
		SpriteSheet = "bony",
		TearVariant = TearVariant.BONE,
	},
	FORGOTTEN = {
		Scale = 0.5,
		Anm2 = BODY_ANM2,
		SpriteSheet = "forgotten",
		Melee = true,
		MeleeSprite = "gfx/008.001_Bone Club.anm2",
	},
	BLUE_BABY = {
		Scale = 0.5,
		Anm2 = BODY_ANM2,
		SpriteSheet = "bluebaby",
	},
	KEEPER = {
		Scale = 0.5,
		Anm2 = BODY_ANM2,
		SpriteSheet = "keeper",
		TearVariant = TearVariant.COIN,
		TearFlags = TearFlags.TEAR_GREED_COIN,
		TearFlagsChance = 0.5,
		TripleShot = true,
		FireDelay = 28,
		ShotSpeedMult = 0.8,
	},
	SOUL = {
		Anm2 = SOUL_ANM2,
		SpriteSheet = "soul",
		Flight = true,
		TearColor = Color(1.5, 2, 2, 0.5),
		TearFlags = TearFlags.TEAR_SPECTRAL,
	},
	LOST = {
		Anm2 = SOUL_ANM2,
		SpriteSheet = "lost",
		Flight = true,
		HP = 1,
		TearColor = Color(1.5, 2, 2, 0.5),
		TearFlags = TearFlags.TEAR_SPECTRAL,
	},
}
for id, tab in pairs(THANATOS_MINIONS) do
	tab.ID = id
end

function mod:ThanatosTest(player, pos)
	for k,v in pairs(THANATOS_MINIONS) do
		mod:SpawnThanatosMinion(player, pos, k)
	end
end

function mod:SpawnThanatosMinion(player, pos, minionID)
	player = player or Isaac.GetPlayer()
	pos = pos or player.Position
	
	local data = minionID and THANATOS_MINIONS[minionID] or lib.PickRandom(THANATOS_MINIONS, player:GetCollectibleRNG(mod.ITEMS.THANATOS))
	if not data then
		lib.LogErr("Failed to spawn thanatos minion: " .. (minionID or "<NIL>"))
		return
	end
	
	local fam = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC, data.Melee and 99 or 0, pos, Vector.Zero, player):ToFamiliar()
	mod:InitThanatosMinion(fam, data.ID)
end

function mod:InitThanatosMinion(fam, minionID)
	local id = fam:GetData().samaelThanatosMinion or minionID
	if not id then return end
	local data = THANATOS_MINIONS[id]
	if not data then return end
	
	if data.Anm2 then
		fam:GetSprite():Load(data.Anm2, true)
	end
	if data.SpriteSheet then
		fam:GetSprite():ReplaceSpritesheet(0, "gfx/samael_entities/thanatos/" .. data.SpriteSheet .. ".png")
		fam:GetSprite():ReplaceSpritesheet(1, "gfx/samael_entities/thanatos/" .. data.SpriteSheet .. ".png")
		fam:GetSprite():LoadGraphics()
	end
	
	fam.SubType = data.Melee and 99 or 0
	fam:GetData().samaelThanatosMinion = id
end

table.insert(mod.PRE_SAVE, function()
	local tab = {}
	
	for _, fam in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC)) do
		local id = fam:GetData().samaelThanatosMinion
		if id then
			tab[""..fam.InitSeed] = id
		end
	end
	
	mod:GetAllRunData().THANATOS = tab
end)

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
	local data = mod:GetAllRunData().THANATOS
	
	if not data then return end
	
	for _, fam in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC)) do
		local id = data[""..fam.InitSeed]
		if id then
			mod:InitThanatosMinion(fam, id)
		end
	end
	
	mod:GetAllRunData().THANATOS = nil
end)

function mod:ThanatosBabyUpdate(fam)
	local id = fam:GetData().samaelThanatosMinion
	if not id or not THANATOS_MINIONS[id] then return end
	
	local minionInfo = THANATOS_MINIONS[id]
	local player = fam.Player or Isaac.GetPlayer()
	local data = fam:GetData()
	local sprite = fam:GetSprite()
	
	if minionInfo.Scale then
		fam.SpriteScale = Vector(minionInfo.Scale, minionInfo.Scale)
	end
	if minionInfo.HP and fam.MaxHitPoints > minionInfo.HP then
		fam.MaxHitPoints = minionInfo.HP
		if fam.HitPoints > fam.MaxHitPoints then
			fam.HitPoints = fam.MaxHitPoints
		end
	end
	if data.thanatosMinionInvuln then
		data.thanatosMinionInvuln = data.thanatosMinionInvuln - 1
		if fam.FrameCount % 4 == 0 then
			fam:SetColor(lib.InvisibleColor, 2, 0, false, false)
		end
		if data.thanatosMinionInvuln <= 0 then
			data.thanatosMinionInvuln = nil
		end
	end
	
	fam.GridCollisionClass = minionInfo.Flight and EntityGridCollisionClass.GRIDCOLL_WALLS or EntityGridCollisionClass.GRIDCOLL_GROUND
	fam.PositionOffset = Vector.Zero
	
	if fam.FireCooldown > 0 and data.samaelLastFireCooldown == 0 then
		if minionInfo.FireDelay then
			fam.FireCooldown = minionInfo.FireDelay
		end
		data.HeadFrameDelay = math.ceil(fam.FireCooldown * 0.3)
		
		if minionInfo.TripleShot then
			if (data.thanatosTripleShotCounter or 0) < 2 then
				fam.FireCooldown = 0
				data.thanatosTripleShotCounter = (data.thanatosTripleShotCounter or 0) + 1
			else
				data.thanatosTripleShotCounter = 0
			end
		end
	end
	data.samaelLastFireCooldown = fam.FireCooldown
	
	if (data.HeadFrameDelay or 0) > 0 then
		sprite:SetOverlayFrame(sprite:GetOverlayAnimation(), 2)
		data.HeadFrameDelay = data.HeadFrameDelay - 1
	else
		sprite:SetOverlayFrame(sprite:GetOverlayAnimation(), 0)
	end
	
	if id == "BLUE_BABY" and game:GetFrameCount() % 90 == 0 and #Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.DIP) < 3 then
		player:ThrowFriendlyDip(0, fam.Position, fam.Position + RandomVector() * (30 + Random()%30))
		game:Fart(fam.Position, 40, fam, 0.5)
	end
	
	-- Offset the natural health degredation of minisaacs.
	if fam.FrameCount % 15 == 0 and fam.HitPoints < fam.MaxHitPoints then
		fam.HitPoints = math.min(fam.HitPoints + 0.2, fam.MaxHitPoints)
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.ThanatosBabyUpdate, FamiliarVariant.MINISAAC)

function mod:ThanatosMinionTearsInit(tear)
	if tear.SpawnerEntity and tear.SpawnerEntity.Type == EntityType.ENTITY_FAMILIAR and tear.SpawnerEntity:GetData().samaelThanatosMinion then
		local fam = tear.SpawnerEntity
		local minionInfo = THANATOS_MINIONS[fam:GetData().samaelThanatosMinion] or {}
		if minionInfo.TearScale then
			tear.Scale = minionInfo.TearScale
		end
		if minionInfo.TearVariant then
			tear:ChangeVariant(minionInfo.TearVariant)
		end
		if minionInfo.TearFlags and (not minionInfo.TearFlagsChance or minionInfo.TearFlagsChance >= (Random() % 101) / 100) then
			tear:AddTearFlags(minionInfo.TearFlags)
		end
		if minionInfo.TearColor then
			tear.Color = minionInfo.TearColor
		end
		
		if fam:GetData().thanatosTripleShotCounter == 1 then
			tear.Velocity = tear.Velocity:Rotated(10)
		end
		if fam:GetData().thanatosTripleShotCounter == 2 then
			tear.Velocity = tear.Velocity:Rotated(-10)
			tear:Update()
		end
		
		tear.FallingSpeed = tear.FallingSpeed - 0.5
		
		if minionInfo.ShotSpeedMult then
			tear.Velocity = tear.Velocity:Resized(tear.Velocity:Length() * minionInfo.ShotSpeedMult)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_INIT, mod.ThanatosMinionTearsInit)

function mod:ThanatosMinionTears(tear)
	if tear.FrameCount == 0 and tear.SpawnerEntity and tear.SpawnerEntity.Type == EntityType.ENTITY_FAMILIAR and tear.SpawnerEntity:GetData().samaelThanatosMinion then
		tear.CollisionDamage = 3
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.ThanatosMinionTears)

function mod:ThanatosMinionMeleeInit(knife)
	if knife.SpawnerType == EntityType.ENTITY_FAMILIAR and knife.SpawnerEntity
			and knife.SpawnerEntity.Variant == FamiliarVariant.MINISAAC 
			and knife.SpawnerEntity.SubType == 99 and knife.SpawnerEntity:GetData().samaelThanatosMinion then
		local fam = knife.SpawnerEntity
		local minionInfo = THANATOS_MINIONS[fam:GetData().samaelThanatosMinion] or {}
		if minionInfo.MeleeSprite then
			knife:GetSprite():Load(minionInfo.MeleeSprite, true)
		end
		fam:GetData().samaelThanatosMinionSwing = not fam:GetData().samaelThanatosMinionSwing
		knife:GetSprite():Play(fam:GetData().samaelThanatosMinionSwing and "Swing" or "Swing2", true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_INIT, mod.ThanatosMinionMeleeInit, 4)

function mod:ThanatosMinionMelee(knife)
	if knife.SpawnerType == EntityType.ENTITY_FAMILIAR and knife.SpawnerEntity
			and knife.SpawnerEntity.Variant == FamiliarVariant.MINISAAC 
			and knife.SpawnerEntity.SubType == 99 and knife.SpawnerEntity:GetData().samaelThanatosMinion then
		knife.CollisionDamage = 3
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.ThanatosMinionMelee, 4)

function mod:ThanatosMinionDmg(fam, damage, damageFlags, source)
	if not fam.Variant == FamiliarVariant.MINISAAC then return end
	local id = fam:GetData().samaelThanatosMinion
	
	if fam:GetData().thanatosMinionInvuln then
		return false
	end
	
	if not id or not THANATOS_MINIONS[id] then return end
	local minionInfo = THANATOS_MINIONS[id]
	
	if id == "LOST" and not fam:GetData().thanatosMinionHolyMantleBroken then
		fam:GetData().thanatosMinionHolyMantleBroken = true
		fam:GetData().thanatosMinionInvuln = 60
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 11, fam.Position, lib.ZeroVector, fam):ToEffect()
		poof.SpriteScale = Vector(0.7, 0.7)
		poof:FollowParent(fam)
		poof.SpriteOffset = Vector(0, -7)
		sfx:Play(SoundEffect.SOUND_HOLY_MANTLE, 0.7, 0, false, 1.3)
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.ThanatosMinionDmg, EntityType.ENTITY_FAMILIAR)

function mod:ThanatosMinionDeath(fam)
	if fam.Variant ~= FamiliarVariant.MINISAAC or not fam:IsDead() or fam.HitPoints > 0 then return end
	local id = fam:GetData().samaelThanatosMinion
	if not id or not THANATOS_MINIONS[id] then return end
	local minionInfo = THANATOS_MINIONS[id]
	
	if id == "BONY" or id == "FORGOTTEN" then
		lib.BoneGibsBurst(fam.Position)
	elseif id == "LOST" or id == "SOUL" then
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 10, fam.Position, lib.ZeroVector, nil)
		if minionInfo.Anm2 then
			poof:GetSprite():Load(minionInfo.Anm2, true)
		end
		if minionInfo.SpriteSheet then
			poof:GetSprite():ReplaceSpritesheet(0, "gfx/samael_entities/thanatos/" .. minionInfo.SpriteSheet .. ".png")
			poof:GetSprite():ReplaceSpritesheet(1, "gfx/samael_entities/thanatos/" .. minionInfo.SpriteSheet .. ".png")
			poof:GetSprite():LoadGraphics()
		end
		poof:GetSprite():Play("LostDeath", true)
		sfx:Play(SoundEffect.SOUND_ISAACDIES, 0.8, 0, false, 1.5)
	elseif id == "BLUE_BABY" then
		local player = (fam.SpawnerEntity and fam.SpawnerEntity:ToPlayer()) and fam.SpawnerEntity:ToPlayer() or Isaac.GetPlayer()
		for i=1, 3 do
			player:ThrowFriendlyDip(0, fam.Position, fam.Position + RandomVector() * (30 + Random()%30))
		end
		game:Fart(fam.Position, 40, fam, 0.5)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.ThanatosMinionDeath, EntityType.ENTITY_FAMILIAR)

function mod:IsSpecialMinisaac(ent)
	if ent then
		local data = ent:GetData()
		return data.isThanatophiliaMinion or data.sigilOfLilithBaby or data.samaelThanatosMinion
	end
end

-- Don't tell KittenChilly about this
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_INIT, function()
	if UniqueMinisaacs and not UniqueMinisaacs.ConnorWasHere then
		local DONT_TELL_KITTENCHILLY = {
			{ModCallbacks.MC_FAMILIAR_INIT, UniqueMinisaacs.ReplaceAnimation, FamiliarVariant.MINISAAC},
			{ModCallbacks.MC_FAMILIAR_UPDATE, UniqueMinisaacs.ReplaceSpritesheet, FamiliarVariant.MINISAAC},
			{ModCallbacks.MC_ENTITY_TAKE_DMG, UniqueMinisaacs.Gehedenn, EntityType.ENTITY_FAMILIAR},
			{ModCallbacks.MC_POST_TEAR_INIT, UniqueMinisaacs.ChangeTears},
		}
		for _, tab in pairs(DONT_TELL_KITTENCHILLY) do
			local callback = tab[1]
			local func = tab[2]
			local thing = tab[3]
			
			if func then
				UniqueMinisaacs:RemoveCallback(callback, func)
				UniqueMinisaacs:AddPriorityCallback(callback, CallbackPriority.LATE, function(...)
					local args = {...}
					local ent = args[2]
					if ent and (mod:IsSpecialMinisaac(ent) or mod:IsSpecialMinisaac(ent.SpawnerEntity)) then
						return
					end
					func(...)
				end, thing)
			end
		end
		UniqueMinisaacs.ConnorWasHere = true
	end
end)
