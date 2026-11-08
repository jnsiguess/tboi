local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local SOUL_OF_SAMAEL = mod.ITEMS.SOUL_OF_SAMAEL
local REAPER_BUM_ITEM = mod.ITEMS.REAPER_BUM
local REAPER_BUM = mod.ENTITIES.REAPER_BUM.Var

local kSoulOfSamaelVo = Isaac.GetSoundIdByName("SoulOfSamael")

local kSoulOfSamaelMinSoulsNeeded = 1
local kSoulOfSamaelMaxSoulsNeeded = 5

local function BirthcakeActive(player)
	return lib.HasBirthcake(player) and (player:GetPlayerType() == lib.SamaelId or player:GetPlayerType() == lib.OtherSamaelId)
end

local function SoulOfSamaelActive(player)
	return player:GetData().soulOfSamaelActive
end

local function TaintedSamaelBirthrightActive(player)
	return lib.IsTaintedSamael(player) and lib.HasItem(player, mod.ITEMS.MEMENTO_MORI) and lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT)
end

local function ShouldSpawnSoulForPlayer(player)
	if SoulOfSamaelActive(player) or TaintedSamaelBirthrightActive(player) then return true end
	
	if BirthcakeActive(player) then
		return ((Random() % 101) / 100) <= lib.HasBirthcake(player)/3
	end
end

local function CanSpawnSoulForPlayer(player)
	return SoulOfSamaelActive(player) or TaintedSamaelBirthrightActive(player) or BirthcakeActive(player)
end

local function GetSouls(entity)
	return entity:GetData().samaelSoulCount or 0
end

local function SetSouls(entity, numSouls)
	entity:GetData().samaelSoulCount = numSouls or 0
end

local function AddSoul(entity)
	if entity:ToPlayer() then
		local player = entity:ToPlayer()
		local data = player:GetData()
		if SoulOfSamaelActive(player) or BirthcakeActive(player) then
			data.samaelSoulCount = GetSouls(player) + 1
		end
		if TaintedSamaelBirthrightActive(player) then
			data.mementoMoriSouls = (data.mementoMoriSouls or 0) + 1
		end
	else
		entity:GetData().samaelSoulCount = GetSouls(entity) + 1
	end
end

local function RemoveSouls(entity, numSoulsToRemove)
	entity:GetData().samaelSoulCount = math.max(GetSouls(entity) - numSoulsToRemove, 0)
end

-- Returns all targets that should receive a soul from an enemy.
local function GetSoulTargets()
	local soulTargets = {}
	for _, player in pairs(lib.GetPlayers()) do
		local pData = player:GetData()
		
		if pData.soulTarget and ShouldSpawnSoulForPlayer(player) then
			table.insert(soulTargets, pData.soulTarget)
		end
	end
	for _, entity in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, REAPER_BUM, -1, true)) do
		table.insert(soulTargets, entity)
	end
	return soulTargets
end

-- Sets the color of either a soul or its sprite trail.
local function SetSoulColor(eff, isFamiliarSoul)
	local c = eff.Color
	if isFamiliarSoul then
		c:SetColorize(1,1,1,1)
	else
		c:SetOffset(1, 0, 2)
	end
	eff.Color = c
	eff:GetData().samaelSoulSetColor = true
end

local RareHeartSubTypes = {HeartSubType.HEART_BONE, HeartSubType.HEART_SOUL, HeartSubType.HEART_ETERNAL, HeartSubType.HEART_BLACK}

-- Rewards for Soul of Samael
local function SpawnSoulReward(player, pos)
	pos = pos or player.Position
	local rng = player:GetCardRNG(SOUL_OF_SAMAEL)
	
	local spawnPos = game:GetRoom():FindFreePickupSpawnPosition(pos)
	
	if rng:RandomInt(2) == 0 then
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COIN, 0, spawnPos, lib.ZeroVector, nil)
	elseif rng:RandomInt(3) ~= 0 then
		local heartSubType = HeartSubType.HEART_HALF_SOUL
		if rng:RandomFloat() <= 0.25 then
			heartSubType = RareHeartSubTypes[rng:RandomInt(#RareHeartSubTypes) + 1]
		end
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, heartSubType, spawnPos, lib.ZeroVector, nil)
	else
		local rune = game:GetItemPool():GetCard(rng:Next(), false, true, true)
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, rune, spawnPos, lib.ZeroVector, nil)
	end
end

-- An invisible entity is used as the target for enemy souls so that they aren't counted towards Urn of Souls.
function mod:SoulTargetUpdate(player)
	local pData = player:GetData()
	
	if CanSpawnSoulForPlayer(player) then
		if not pData.soulTarget or not pData.soulTarget:Exists() then
			local target = Isaac.Spawn(EntityType.ENTITY_EFFECT, mod.ENTITIES.DUMMY.Var, 0, player.Position, lib.ZeroVector, nil)
			target.Parent = player
			target.Visible = false
			target:GetData().isSamaelSoulTarget = true
			pData.soulTarget = target
		else
			pData.soulTarget.Position = player.Position
		end
	elseif pData.soulTarget then
		pData.soulTarget:Remove()
		pData.soulTarget = nil
	end
	
	if BirthcakeActive(player) then
		local soulsNeeded = 4
		if GetSouls(player) >= soulsNeeded then
			RemoveSouls(player, soulsNeeded)
			SpawnSoulReward(player)
		end
	elseif SoulOfSamaelActive(player) then
		local soulsNeeded = math.min(pData.soulOfSamaelSoulsNeeded or kSoulOfSamaelMinSoulsNeeded, kSoulOfSamaelMaxSoulsNeeded)
		if GetSouls(player) >= soulsNeeded then
			RemoveSouls(player, soulsNeeded)
			SpawnSoulReward(player)
			soulsNeeded = soulsNeeded + 1
		end
		pData.soulOfSamaelSoulsNeeded = soulsNeeded
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.SoulTargetUpdate)

-- Spawn a soul when an enemy dies.
function mod:SpawnSoul(entity, force)
	if not force and not lib.AllowOnDeathEffect(entity) then return end
	
	for _, target in pairs(GetSoulTargets()) do
		local soul = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.ENEMY_SOUL, 0, entity.Position, lib.ZeroVector, entity):ToEffect()
		--soul.SpriteScale = Vector(0.75, 1.0)
		local data = soul:GetData()
		soul.Target = target
		local isFamiliarSoul = target.Type == EntityType.ENTITY_FAMILIAR
		SetSoulColor(soul, isFamiliarSoul)
		data.isSamaelSoul = true
		if target:GetData().isSamaelSoulTarget and target.Parent then
			data.samaelSoulTarget = target.Parent
		else
			data.samaelSoulTarget = target
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.SpawnSoul)

function mod:PlayerSoulFunny(player)
	for _, player in pairs(lib.GetPlayers()) do
		local anim = player:GetSprite():GetAnimation()
		
		if (anim == "Death" or anim == "LostDeath") and not player:GetData().fakeDeathPillActive and not player:GetData().samaelSpawnedJokeSoul then
			mod:SpawnSoul(player, true)
			player:GetData().samaelSpawnedJokeSoul = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.PlayerSoulFunny)

-- Update the color of the Sprite Trail for souls.
function mod:SoulUpdate(soul)
	local data = soul:GetData()
	if not data.isSamaelSoul then return end
	
	if data.samaelSoulSetColor and soul.FrameCount == 1 and soul.Child then
		local isFamiliarSoul = data.samaelSoulTarget and data.samaelSoulTarget.Type == EntityType.ENTITY_FAMILIAR
		SetSoulColor(soul.Child, isFamiliarSoul)
		soul.Child.SpriteScale = Vector(1.2, 1.2)
	end
	
	if soul:GetSprite():IsPlaying("Collect") and soul:GetSprite():GetFrame() == 0 then
		if data.samaelSoulTarget.Type ~= EntityType.ENTITY_EFFECT then
			local c = soul.Color
			c:SetOffset(0.4, 0.4, 0.8)
			data.samaelSoulTarget:SetColor(c, 10, 1, true, false)
		end
		sfxManager:Play(SoundEffect.SOUND_SOUL_PICKUP, 0.85, 0, false, 0.7)
		if data.samaelSoulTarget then
			AddSoul(data.samaelSoulTarget)
		end
	end
	
	if not soul:GetSprite():IsPlaying("Collect") and (not soul.Target or not soul.Target:Exists()) then
		soul:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SoulUpdate, EffectVariant.ENEMY_SOUL)

--------------------------------------------------
---- SOUL OF SAMAEL
--------------------------------------------------

-- Activation for Soul of Samael
function mod:SoulOfSamael(_, player, useFlags)
	player:GetData().soulOfSamaelActive = true
	
	if not lib.IsSamael(player) then
		player:AddNullCostume(Isaac.GetCostumeIdByPath("gfx/characters/samael/samael_costume.anm2"))
	end
	
	local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 1, player.Position, lib.ZeroVector, player):ToEffect()
	poof.Color = lib.NewColor(0,0,0,0.4)
	poof.SpriteScale = Vector(0.8, 0.8)
	
	sfxManager:Play(SoundEffect.SOUND_CANDLE_LIGHT)
	
	-- Announcer voice
	if useFlags & (UseFlag.USE_NOHUD | UseFlag.USE_MIMIC) == 0 then
		local voiceMode = Options.AnnouncerVoiceMode
		if voiceMode == 2 or (voiceMode == 0 and Random() % 2 == 0) then
			sfxManager:Play(kSoulOfSamaelVo, 0.7)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.SoulOfSamael, SOUL_OF_SAMAEL)

-- Soul of Samael effect doesn't persist between rooms.
function mod:EndSoulOfSamael()
	for _, player in pairs(lib.GetPlayers()) do
		local pData = player:GetData()
		if pData.soulOfSamaelActive then
			pData.soulOfSamaelActive = false
			if not lib.IsSamael(player) then
				player:TryRemoveNullCostume(Isaac.GetCostumeIdByPath("gfx/characters/samael/samael_costume.anm2"))
			end
			pData.soulOfSamaelSoulsNeeded = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.EndSoulOfSamael)

--------------------------------------------------
---- REAPER BUM
--------------------------------------------------

local RareWisps = {
	CollectibleType.COLLECTIBLE_NECRONOMICON,
	CollectibleType.COLLECTIBLE_BEST_FRIEND,
	CollectibleType.COLLECTIBLE_BIBLE,
	CollectibleType.COLLECTIBLE_BOOK_OF_BELIAL,
	CollectibleType.COLLECTIBLE_BOOK_OF_THE_DEAD,
	CollectibleType.COLLECTIBLE_CRACK_THE_SKY,
	CollectibleType.COLLECTIBLE_HOURGLASS,
	mod.ITEMS.MALAKH_MOT,
}

local ReaperBumPayouts = {
	{  -- Bone Orbitals
		Weight = 40,
		Value = 5,
		Payout = function(player, pos)
			local bone = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FamiliarVariant.BONE_ORBITAL, 0, pos, lib.ZeroVector, player)
			bone.Parent = player
		end,
	},
	{  -- Wisps
		Weight = 40,
		Value = 5,
		Payout = function(player, pos)
			local rng = player:GetCollectibleRNG(REAPER_BUM_ITEM)
			local wispType = 0
			if rng:RandomInt(4) == 0 then
				wispType = RareWisps[rng:RandomInt(#RareWisps)+1]
			end
			player:AddWisp(wispType, pos, true)
		end,
	},
	{  -- Bone Heart
		Weight = 6,
		Value = 10,
		Payout = function(player, pos)
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_HEART, HeartSubType.HEART_BONE, pos, lib.ZeroVector, nil)
		end,
	},
	{  -- Tarot Card/Rune
		Weight = 14,
		Value = 10,
		Payout = function(player, pos)
			local rng = player:GetCollectibleRNG(REAPER_BUM_ITEM)
			local cardOrRune = game:GetItemPool():GetCard(rng:Next(), false, true, false)
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TAROTCARD, cardOrRune, pos, lib.ZeroVector, nil)
		end,
	},
}
for idx, tab in pairs(ReaperBumPayouts) do
	tab.Idx = idx
end

function mod:SpawnReaperBums(player)
	local pData = player:GetData()
	
	local numBums = player:GetCollectibleNum(REAPER_BUM_ITEM) + player:GetEffects():GetCollectibleEffectNum(REAPER_BUM_ITEM)
	local rng = player:GetCollectibleRNG(REAPER_BUM_ITEM)
	
	player:CheckFamiliar(REAPER_BUM, numBums, rng, Isaac.GetItemConfig():GetCollectible(REAPER_BUM_ITEM))
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.SpawnReaperBums)

function mod:ReaperBumInit(bum)
	if bum.Player then
		bum.Player:GetCollectibleRNG(REAPER_BUM_ITEM):Next()
	end
	bum:GetSprite():Play("FloatDown", true)
	bum:AddToFollowers()
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, mod.ReaperBumInit, REAPER_BUM)

-- Coins is used to store the # of souls collected.
-- Keys is used to store the Index of the new payout in the ReaperBumPayouts table.
function mod:ReaperBumUpdate(bum)
	local data = bum:GetData()
	local sprite = bum:GetSprite()
	local player = bum.Player or Isaac.GetPlayer(0)
	local pData = player:GetData()
	local rng = player:GetCollectibleRNG(REAPER_BUM_ITEM)
	
	if bum.Keys < 1 or bum.Keys > #ReaperBumPayouts then
		bum.Keys = lib.PickRandom(ReaperBumPayouts, rng).Idx
	end
	
	local nextPayout = ReaperBumPayouts[bum.Keys]
	
	local souls = GetSouls(bum)
	if souls > 0 then
		bum.Coins = bum.Coins + souls
		SetSouls(bum, 0)
	end
	
	local anim = sprite:GetAnimation()
	if bum.Coins >= nextPayout.Value and anim ~= "PreSpawn" and anim ~= "Spawn" then
		bum.Coins = bum.Coins - nextPayout.Value
		sprite:Play("PreSpawn", true)
	end
	
	if sprite:IsFinished("PreSpawn") then
		nextPayout.Payout(player, game:GetRoom():FindFreePickupSpawnPosition(bum.Position))
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_BFFS) and rng:RandomInt(3) == 0 then
			nextPayout.Payout(player, game:GetRoom():FindFreePickupSpawnPosition(bum.Position))
		end
		bum.Keys = 0
		sprite:Play("Spawn", true)
	end
	
	if sprite:IsFinished("Spawn") then
		sprite:Play("FloatDown", true)
	end
	
	bum:FollowParent()
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.ReaperBumUpdate, REAPER_BUM)

--[[function mod:ReaperBumRender(bum)
	local pos = Isaac.WorldToScreen(bum.Position)
	Isaac.RenderText(""..bum.Coins, pos.X, pos.Y, 255, 255, 255, 1)
end
mod:AddCallback(ModCallbacks.MC_POST_FAMILIAR_RENDER, mod.ReaperBumRender, REAPER_BUM)]]
