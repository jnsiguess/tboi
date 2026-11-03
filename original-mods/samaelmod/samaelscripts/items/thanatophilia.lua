local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local THANATOPHILIA = mod.ITEMS.THANATOPHILIA

local kRareAltChance = 0.2
local kSuperRareAltChance = 0.05

local kBaseMinionSpawnChance = 1.0
local kMinimumMinionSpawnChance = 0.25

local ThanatophiliaMinionCounter = 0
local NumThanatophiliaMinions = 0

local MINIONS = mod.THANATOPHILIA_MINIONS

local function GetMaxMinions()
	local maxMinions = 10
	local numThanatophilia = 0
	for _, player in pairs(lib.GetPlayers()) do
		numThanatophilia = numThanatophilia + player:GetCollectibleNum(THANATOPHILIA)
	end
	if numThanatophilia == 0 then return 0 end
	return maxMinions + 4 * (numThanatophilia - 1)
end

local function GetMinionSpawnChance()
	local maxMinions = GetMaxMinions()
	if NumThanatophiliaMinions >= maxMinions then
		return 0
	end
	local halfMaxMinions = math.floor(maxMinions * 0.5)
	local chance = lib.Lerp(kBaseMinionSpawnChance, kMinimumMinionSpawnChance, NumThanatophiliaMinions / halfMaxMinions)
	return math.max(chance, kMinimumMinionSpawnChance)
end

-- Thanatophilia minions always expect to find a player as their SpawnerEntity.
local function GetPlayerParent(entity)
	local player = entity.SpawnerEntity
	if not player or not player:ToPlayer() then
		lib.LogErr("Thanatophilia minion with no player SpawnerEntity: " .. entity.Type.."."..entity.Variant.."."..entity.SubType)
		player = Isaac.GetPlayer(0)
		entity.SpawnerEntity = player
	end
	return player:ToPlayer()
end

local function InitMinion(entity, player)
	local data = entity:GetData()
	
	entity.SpawnerEntity = player
	
	if entity:ToNPC() then
		entity:AddCharmed(EntityRef(player), -1)
	end
	
	if not (entity:ToFamiliar() and entity.Variant == FamiliarVariant.BONE_ORBITAL) then
		data.isThanatophiliaMinion = true
	end
	
	if entity:IsBoss() then
		entity:AddEntityFlags(EntityFlag.FLAG_PERSISTENT)
		data.samaelCorrectFriendlyPos = true
	end
end

-- Writes data on all current Thanatophilia minions to the SaveData so that they can be properly recovered when the run is continued.
local function SaveMinionData()
	local tab = {}
	
	for _, entity in pairs(Isaac.GetRoomEntities()) do
		local data = entity:GetData()
		local key = ""..entity.Type.."."..entity.Variant.."."..entity.SubType
		
		if data.isSirenBonyMinion then
			entity:Remove()
		end
		
		if entity:Exists() and data.isThanatophiliaMinion then
			local entityData = {
				IsMinion = true,
				ExtraData = data.thanatophiliaExtraData,
				PlayerIdx = GetPlayerParent(entity).ControllerIndex,
				Type = entity.Type,
				Variant = entity.Variant,
				SubType = entity.SubType,
			}
			
			if entity:ToNPC() and entity:ToNPC():IsChampion() then
				entityData.ChampionColorIdx = entity:ToNPC():GetChampionColorIdx()
			end
			
			table.insert(lib.GetOrInit(tab, key), entityData)
		end
	end
	
	mod:GetAllRunData().THANATOPHILIA = tab
end
table.insert(mod.PRE_SAVE, SaveMinionData)

function mod.SpawnThanatophiliaMinion(minion, player, source, extraData)
	local rng = player:GetCollectibleRNG(THANATOPHILIA)
	
	local pos
	
	if source then
		pos = source.Position
	else
		pos = game:GetRoom():FindFreePickupSpawnPosition(player.Position, 40, true, false)
	end
	
	local chance
	if not source or source:ToPlayer() or source.Type == EntityType.ENTITY_PICKUP then
		chance = 1.0
	elseif extraData and extraData.Chance then
		chance = extraData.Chance
	else
		chance = GetMinionSpawnChance()
	end
	if player:HasTrinket(mod.ITEMS.SIGIL_OF_SAMAEL) or player:HasTrinket(mod.ITEMS.SCYTHE_FOSSIL) then
		chance = chance * 1.5
	end
	if chance < 1.0 and rng:RandomFloat() > chance then return end
	
	lib.Log("Thanatophilia: Spawning " .. minion.Type ..".".. minion.Variant  ..".".. minion.SubType)
	local entity = Isaac.Spawn(minion.Type, minion.Variant, minion.SubType, pos, lib.ZeroVector, player)
	entity = entity:ToNPC() or entity:ToFamiliar()
	if not entity then
		lib.LogErr("Failed to spawn Thanatophilia minion " .. minion.Type ..".".. minion.Variant ..".".. minion.SubType)
		return
	end
	
	--[[if entity.Type == EntityType.ENTITY_FAMILIAR and entity.Variant == FamiliarVariant.MINISAAC then
		--entity.Visible = false
		--mod:BoneFriendSpawnFx(entity.Position)
		entity:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	end]]
	
	local data = entity:GetData()
	data.thanatophiliaSpawnPos = pos
	
	InitMinion(entity, player)
	
	if entity:ToNPC() and source and source:ToNPC() and source:ToNPC():IsChampion() then
		entity:ToNPC():MakeChampion(entity.InitSeed, source:ToNPC():GetChampionColorIdx(), false)
	end
	
	if extraData then
		local exData = lib.ShallowCopy(extraData)
		data.thanatophiliaExtraData = exData
		
		if exData.SkipAppear then
			entity:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		end
		
		if exData.Anim then
			entity:GetSprite():Play(exData.Anim, true)
		end
		
		if source and exData.Scale and source.Scale then
			exData.Scale = exData.Scale * source.Scale
		end
	end
	
	return entity
end

-- Finds the associated Thanatophilia minion for the given entity, and then calls its Spawn function.
local function SummonMinion(player, source, meatCleaver)
	lib.Log("Thanatophilia: Spawning minion from enemy: " .. source.Type .. "." .. source.Variant .. "." .. source.SubType)
	
	local typeLevel = mod.THANATOPHILIA_MINION_ASSIGNMENTS[source.Type]
	if not typeLevel then return end
	local varLevel = typeLevel[source.Variant] or typeLevel[-1]
	if not varLevel then return end
	local minionInfo = varLevel[source.SubType] or varLevel[-1]
	
	if not minionInfo then
		minionInfo = varLevel[-1]
	end
	
	if not minionInfo and typeLevel[-1] then
		minionInfo = typeLevel[-1][-1]
	end
	
	if not minionInfo or not minionInfo.Minion then return end
	
	if (source.Type == EntityType.ENTITY_LARRYJR or source.Type == EntityType.ENTITY_PIN) and (source.Parent or source.Child) then
		-- Only consider the final segment.
		return
	end
	
	if not minionInfo or not minionInfo.Minion then return end
	
	local rng = player:GetCollectibleRNG(THANATOPHILIA)
	
	local numToSpawn = 1
	
	if minionInfo.ExtraData and minionInfo.ExtraData.Num then
		numToSpawn = minionInfo.ExtraData.Num
	end
	
	local minion = minionInfo.Minion
	local exData = minionInfo.ExtraData or {}
	
	if meatCleaver then
		exData.Chance = 1.0
	elseif exData.SuperRareAlt and rng:RandomFloat() <= kSuperRareAltChance then
		minion = minionInfo.ExtraData.SuperRareAlt
		exData = {Chance=1.0}
		numToSpawn = 1
	elseif exData.RareAlt and rng:RandomFloat() <= kRareAltChance then
		minion = minionInfo.ExtraData.RareAlt
		exData = {Chance=1.0}
		numToSpawn = 1
	end
	
	for i=0, numToSpawn-1 do
		local spawnedMinion = minion:Spawn(player, source, exData)
		
		-- Failed to spawn minion. Abort.
		if not spawnedMinion then return end
		
		-- Run an update to give the entity a chance to fully initialize.
		spawnedMinion:Update()
		
		if meatCleaver then
			return spawnedMinion
		end
		
		if i == 1 then
			spawnedMinion.Position = spawnedMinion.Position - Vector(spawnedMinion.Size*2, 0)
			spawnedMinion.FlipX = true
		elseif i == 2 then
			spawnedMinion.Position = spawnedMinion.Position - Vector(spawnedMinion.Size, spawnedMinion.Size*2)
		end
	end
end

-- Not all friendly enemies are loaded back in on continue. If any are missing, spawn them back in.
function mod:ThanatophiliaSpawnMissingMinions()
	local saveData = mod:GetRunData("THANATOPHILIA")
	
	for _, tab in pairs(saveData) do
		for _, data in pairs(tab) do
			local player
			for _, p in pairs(lib.GetPlayers()) do
				if p.ControllerIndex == data.PlayerIdx then
					player = p
				end
			end
			if player and data.Type and data.Variant and data.SubType then
				local minion = Isaac.Spawn(data.Type, data.Variant, data.SubType, player.Position, lib.ZeroVector, player)
				InitMinion(minion, player)
				minion:GetData().thanatophiliaExtraData = data.ExtraData
				if data.ChampionColorIdx then
					minion:MakeChampion(minion.InitSeed, data.ChampionColorIdx, false)
				end
			end
		end
	end
	
	mod.PERSISTENT_DATA.THANATOPHILIA = {}
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.ThanatophiliaSpawnMissingMinions)

-- Weighted list of random friend spawns.
mod.BoneFriends = {
	Common = {
		Weight = 64,
		List = {
			MINIONS.Bony,
		},
	},
	Uncommon = {
		Weight = 25,
		List = {
			MINIONS.BlackBony,
			MINIONS.HeadlessBony,
			MINIONS.BonyHead,
			MINIONS.BoneFly,
			MINIONS.ClicketyClack,
			MINIONS.MiniBony,
			MINIONS.BabyBony,
			-- Modded
			MINIONS.Sternum,
			MINIONS.Crepitus,
			MINIONS.DoomFly,
			MINIONS.Jawbone,
			MINIONS.Cracker,
			MINIONS.BoneWorm,
			MINIONS.ClicketyClash
		},
	},
	Rare = {
		Weight = 10,
		List = {
			MINIONS.BigBony,
			MINIONS.HeadlessBigBony,
			MINIONS.Necro,
			MINIONS.Revenant,
			MINIONS.Pasty,
			-- Modded
			MINIONS.Marlin,
			MINIONS.DryWheeze,
			MINIONS.Pyroclasm,
			MINIONS.Ribbone,
			MINIONS.BoneAngel,
			MINIONS.DrShambles,
			MINIONS.Draugr,
			MINIONS.Haugr,
			MINIONS.RagBony,
		},
	},
	SuperRare = {
		Weight = 1,
		List = {
			MINIONS.HolyBony,
			MINIONS.QuadRevenant,
			-- Modded
			MINIONS.MolarSystem,
			MINIONS.Jaugr,
		},
	}
}

-- Spawns a random thanatophilia minion from the above table.
function mod:SpawnBoneFriend(player, source, rng, noEffects)
	player = player or Isaac.GetPlayer()
	rng = rng or player:GetCollectibleRNG(THANATOPHILIA)
	local choices = lib.PickRandom(mod.BoneFriends, rng).List
	local friend = lib.PickRandom(choices, rng):Spawn(player, source)
	
	if friend and not noEffects then
		mod:BoneFriendSpawnFx(friend.Position)
	end
	
	return friend
end

function mod:BoneFriendSpawnFx(pos)
	sfxManager:Play(SoundEffect.SOUND_SHOVEL_DIG)
	for i=1, 10 do
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.ROCK_PARTICLE, 0, pos, RandomVector() * 2, nil)
	end
	Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.ROCK_EXPLOSION, 0, pos, lib.ZeroVector, nil)
	
	local dirt = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.DIRT_PATCH, 0, pos, lib.ZeroVector, nil)
	dirt:GetSprite():Play("DugUp", true)
	dirt:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR)
end

-------------------- GENERAL MINION CODE / CALLBACKS --------------------

function mod:ThanatophiliaMinionInit(entity)
	local data = entity:GetData()
	
	if data.thanatophiliaMinionInitialized then return end
	
	-- On run continue, load saved data about existing minions.
	if not mod.GameStarted then
		local saveData = mod:GetRunData("THANATOPHILIA")
		local key = ""..entity.Type.."."..entity.Variant.."."..entity.SubType
		if saveData and saveData[key] then
			local loadedData = table.remove(saveData[key])
			if loadedData and loadedData.IsMinion and not data.isSamaelsSamaelBaby then
				data.isThanatophiliaMinion = true
				if loadedData.PlayerIdx then
					for _, player in pairs(lib.GetPlayers()) do
						if player.ControllerIndex == loadedData.PlayerIdx then
							entity.SpawnerEntity = player
						end
					end
				end
				data.thanatophiliaExtraData = loadedData.ExtraData
			end
		end
	end
	
	if data.isThanatophiliaMinion then
		local exData = data.thanatophiliaExtraData
		
		if exData then
			if exData.Scale then
				entity.Scale = exData.Scale
			end
			
			if exData.State then
				entity:ToNPC().State = exData.State
			end
		end
		
		data.thanatophiliaMinionInitialized = true
		
		if entity.Type == EntityType.ENTITY_FAMILIAR and entity.Variant == FamiliarVariant.MINISAAC then
			entity:GetSprite():Load(entity.SubType == 99 and "gfx/thanatophilia/forgotten.anm2" or "gfx/samael_entities/thanatos/body.anm2", true)
			entity.Visible = false
			entity:Update()
			entity.Visible = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ThanatophiliaMinionInit)
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.ThanatophiliaMinionInit)

local function PushAwayFrom(entity, nearbyEntity, force)
	local data = entity:GetData()
	
	local pushDir = entity.Position - nearbyEntity.Position
	if pushDir:Length() == 0 then
		pushDir = lib.NormalVector:Rotated(game:GetFrameCount() % 360)
	end
	if data.expectedVel then
		data.expectedVel = data.expectedVel + pushDir:Resized(force)
	else
		entity.Velocity = entity.Velocity + pushDir:Resized(force)
	end
end

function mod:ThanatophiliaMinionUpdate(entity)
	local data = entity:GetData()
	
	--[[local c = entity.Color
	c:SetColorize(1, 1, 1.5, 1)
	entity.Color = c
	entity:SetColor(c, 5, 9999, false, true)]]
	
	if data.isThanatophiliaMinion then
		ThanatophiliaMinionCounter = ThanatophiliaMinionCounter + 1
	end
	
	if data.isThanatophiliaMinion or data.isSamaelsSamaelBaby then
		for _, nearbyEntity in pairs(Isaac.FindInRadius(entity.Position, entity.Size, EntityPartition.ENEMY)) do
			if (nearbyEntity:GetData().isThanatophiliaMinion or nearbyEntity:GetData().isSamaelsSamaelBaby) and nearbyEntity.InitSeed ~= entity.InitSeed then
				PushAwayFrom(entity, nearbyEntity, 0.5)
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ThanatophiliaMinionUpdate)

function mod:ThanatophiliaMinionResetCount()
	NumThanatophiliaMinions = ThanatophiliaMinionCounter
	ThanatophiliaMinionCounter = 0
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.ThanatophiliaMinionResetCount)

-- Let minions resist damage during initialization so they don't instantly die from
-- (or interfere with) their source entity's on-death effects.
function mod:ThanatophiliaMinionDamage(entity)
	if entity:GetData().isThanatophiliaMinion and entity.FrameCount < 30 then
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.ThanatophiliaMinionDamage)

function mod:ThanatophiliaMinionCollide(entity)
	-- Don't let minions collide with projectiles instantly when spawned.
	if entity:GetData().isThanatophiliaMinion and entity.FrameCount < 30 then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.ThanatophiliaMinionCollide)

function mod:ThanatophiliaMinionProjCollide(proj, collider)
	local colliderIsMinion = collider and collider:GetData().isThanatophiliaMinion
	
	-- Don't let minions collide with projectiles instantly when spawned.
	if colliderIsMinion and collider.FrameCount < 30 then
		return true
	end
	
	local source = proj.SpawnerEntity or proj.Parent
	local projFromMinion = source and source:GetData().isThanatophiliaMinion
	
	-- Don't let minions collide with the projectiles of minions.
	if colliderIsMinion and projFromMinion then
		return true
	end
	
	-- Don't let minion projectiles collide with the player.
	if projFromMinion and collider.Type == EntityType.ENTITY_PLAYER then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_PROJECTILE_COLLISION, mod.ThanatophiliaMinionProjCollide)

-- Minor FF Compat
if FiendFolio then
	function mod:PossessedDeath(entity)
		if entity:GetData().isThanatophiliaMinion and entity.Variant == MINIONS.Possessed.Variant then
			entity:Remove()
		end
	end
	if MINIONS.Possessed and MINIONS.Possessed.Type then
		mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.PossessedDeath, MINIONS.Possessed.Type)
	end
end

-------------------- MISC CALLBACKS --------------------

local function SummonMinionFromShopkeeper(player, shopkeeper)
	if shopkeeper.Variant > 4 then return end
	
	local sprite = shopkeeper:GetSprite()
	local filename = sprite:GetFilename()
	
	if shopkeeper:GetData().headless then
		MINIONS.HeadlessBony:Spawn(player, shopkeeper)
	elseif filename == "gfx/017.001_Shopkeeper.anm2" and lib.CurrentAnimIs(sprite, "Shopkeeper 9") then
		MINIONS.HeadlessBony:Spawn(player, shopkeeper)
		MINIONS.BonyHead:Spawn(player, shopkeeper)
	elseif filename == "gfx/017.002_secret room keeper.anm2" or filename == "gfx/017.005_special secret room keeper.anm2" then
		if lib.CurrentAnimIs(sprite, "Guy2") then
			MINIONS.BonyHead:Spawn(player, shopkeeper)
		elseif lib.CurrentAnimIs(sprite, "Guy7") then
			MINIONS.HeadlessBony:Spawn(player, shopkeeper)
		end
	else
		MINIONS.Bony:Spawn(player, shopkeeper)
	end
end

local function FindPlayerWithThanatophilia()
	for _, player in pairs(lib.GetPlayers()) do
		if lib.HasItem(player, THANATOPHILIA) then
			return player
		end
	end
end

function mod:ThanatophiliaPlayerUpdate(player)
	if not player:HasCollectible(THANATOPHILIA) then return end
	
	local data = player:GetData()
	
	local currentFrame = game:GetFrameCount()
	local lastSummon = data.thanatophiliaSummonedFreeMinions or 0
	
	local minimumMinions = player:GetCollectibleNum(THANATOPHILIA) + 1
	
	local summonDelaySeconds = 20
	
	if game:GetRoom():GetFrameCount() > 30 and NumThanatophiliaMinions < minimumMinions and (lastSummon == 0 or currentFrame-lastSummon > 30*summonDelaySeconds) then
		for i=1, math.min(minimumMinions, (minimumMinions+1) - NumThanatophiliaMinions) do
			mod:SpawnBoneFriend(player)
		end
		data.thanatophiliaSummonedFreeMinions = currentFrame
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.ThanatophiliaPlayerUpdate)

function mod:ThanatophiliaPostDeath(entity)
	entity = entity:ToNPC()
	if not entity then return end
	
	local data = entity:GetData()
	local player = FindPlayerWithThanatophilia()
	
	if player and not data.isThanatophiliaMinion and not data.noNormalThanatophiliaSpawn and not data.isSirenBonyMinion and lib.AllowOnDeathEffect(entity, true) then
		if entity.Type == EntityType.ENTITY_SHOPKEEPER then
			SummonMinionFromShopkeeper(player, entity)
		else
			SummonMinion(player, entity)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.ThanatophiliaPostDeath)

function mod:ThanatophiliaEnemyUpdate(entity)
	entity = entity:ToNPC()
	if not entity then return end
	
	local data = entity:GetData()
	
	if data.isThanatophiliaMinion then return end
	
	local thanatophiliaPlayer = FindPlayerWithThanatophilia()
	
	if thanatophiliaPlayer then
		if entity.Type == EntityType.ENTITY_PESTILENCE and not data.thanatophiliaSpawnedHead and entity.HitPoints < entity.MaxHitPoints * 0.5 then
			MINIONS.BigBonyHead:Spawn(thanatophiliaPlayer, entity)
			data.thanatophiliaSpawnedHead = true
		end
		
		-- Entity transformed into another entity type on death.
		if data.samaelDeathType and data.samaelDeathType ~= entity.Type then
			if data.samaelDeathType == EntityType.ENTITY_GAPER and entity.Type == EntityType.ENTITY_GUSHER then
				MINIONS.BonyHead:Spawn(thanatophiliaPlayer, entity)
			elseif data.samaelDeathType == EntityType.ENTITY_FAT_SACK and entity.Type == EntityType.ENTITY_BLUBBER then
				MINIONS.BigBonyHead:Spawn(thanatophiliaPlayer, entity)
			end
			data.samaelDeathType = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ThanatophiliaEnemyUpdate)

local PlayerHasNoBones = {}
PlayerHasNoBones[PlayerType.PLAYER_THELOST] = true
PlayerHasNoBones[PlayerType.PLAYER_BLACKJUDAS] = true
PlayerHasNoBones[PlayerType.PLAYER_THESOUL] = true
PlayerHasNoBones[PlayerType.PLAYER_JUDAS_B] = true
PlayerHasNoBones[PlayerType.PLAYER_THELOST_B] = true
PlayerHasNoBones[PlayerType.PLAYER_JACOB2_B] = true
PlayerHasNoBones[PlayerType.PLAYER_THESOUL_B] = true

local PlayerBonesInitialized = false
function mod:InitModdedCharBones()
	if not PlayerBonesInitialized then
		local noBonesChars = {" Edith", "Sodom", "Gomorrah", "Bela"}
		for _, name in pairs(noBonesChars) do
			local pType = Isaac.GetPlayerTypeByName(name, false)
			if pType >= PlayerType.NUM_PLAYER_TYPES then
				PlayerHasNoBones[pType] = true
			end
			local bType = Isaac.GetPlayerTypeByName(name, true)
			if bType >= PlayerType.NUM_PLAYER_TYPES then
				PlayerHasNoBones[bType] = true
			end
		end
		PlayerBonesInitialized = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.InitModdedCharBones)

function mod:SpawnMinionFromPlayer(thanatophiliaPlayer, deadPlayer)
	if PlayerHasNoBones[deadPlayer:GetPlayerType()] then
		return
	elseif lib.IsTaintedSamael(deadPlayer) then
		MINIONS.TaintedSamaelBony:Spawn(thanatophiliaPlayer, deadPlayer)
	elseif lib.IsSamael(deadPlayer) then
		MINIONS.SamaelBony:Spawn(thanatophiliaPlayer, deadPlayer)
	else
		MINIONS.ForgottenBony:Spawn(thanatophiliaPlayer, deadPlayer)
	end
end

function mod:SpawnMinionFromPlayerHandler(player)
	local data = player:GetData()
	
	--[[local isCoopGhost = player:IsCoopGhost()
	local thanatophiliaPlayer = FindPlayerWithThanatophilia()
	
	if thanatophiliaPlayer and isCoopGhost and not data.thanatophiliaPlayerIsDead then
		mod:SpawnMinionFromPlayer(thanatophiliaPlayer, player)
	end
	
	data.thanatophiliaPlayerIsDead = isCoopGhost
	data.thanatophiliaPrevPlayerType = pType]]
	
	local thanatophiliaPlayer = FindPlayerWithThanatophilia()
	
	if not thanatophiliaPlayer then return end
	
	local sprite = player:GetSprite()
	local totalHealth = player:GetHearts() + player:GetSoulHearts() + player:GetBoneHearts()
	
	if sprite:IsPlaying("Death") then
		if totalHealth == 0 and sprite:GetFrame() == 20 and data.thanatophiliaDeathLastFrame ~= sprite:GetFrame() then
			mod:SpawnMinionFromPlayer(thanatophiliaPlayer, player)
			local rng = player:GetDropRNG()
			player:BloodExplode()
			if player.Visible then
				player.Visible = false
				data.thanatophiliaDeathSetVisible = true
			end
			for i=0, 6 do
				local particle = Isaac.Spawn(1000, EffectVariant.BLOOD_PARTICLE, 0, player.Position, Vector(rng:RandomFloat()*6 - 3, rng:RandomFloat()*6 - 3), player):ToEffect()
			end
		end
		data.thanatophiliaDeathLastFrame = sprite:GetFrame()
	elseif data.thanatophiliaDeathSetVisible then
		player.Visible = true
		data.thanatophiliaDeathSetVisible = false
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.SpawnMinionFromPlayerHandler)

function mod:JacobCorpse(entity)
	local thanatophiliaPlayer = FindPlayerWithThanatophilia()
	
	if thanatophiliaPlayer and entity:GetSprite():GetFilename() == "gfx/001.000_Player.anm2" and entity:GetSprite():IsFinished("Death") then
		local rng = entity:GetDropRNG()
		entity:BloodExplode()
		for i=0, 6 do
			local particle = Isaac.Spawn(1000, EffectVariant.BLOOD_PARTICLE, 0, entity.Position, Vector(rng:RandomFloat()*6 - 3, rng:RandomFloat()*6 - 3), entity):ToEffect()
		end
		entity:Remove()
		MINIONS.ForgottenBony:Spawn(thanatophiliaPlayer, entity)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.JacobCorpse, EffectVariant.DEVIL)

-- Resist any damage that came from a minion.
function mod:ThanatophiliaPlayerDamage(player, damage, damageFlags, damageSourceRef)
	if damageSourceRef.Entity and (damageSourceRef.Entity:GetData().isThanatophiliaMinion or damageSourceRef.Entity:GetData().fromThanatophiliaMinion) then
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.ThanatophiliaPlayerDamage, EntityType.ENTITY_PLAYER)

function mod:SpecialBonyProjectiles(proj)
	local npc = proj.SpawnerEntity
	if npc and npc:GetData().isThanatophiliaMinion then
		local player = GetPlayerParent(npc)
		
		local tearVariant
		local tearFlags
		local tearColor
		
		if npc.Type == MINIONS.DapperBurningBony.Type and npc.Variant == MINIONS.DapperBurningBony.Variant then
			tearVariant = TearVariant.BONE
			tearFlags = TearFlags.TEAR_BURN
			tearColor = Color(0.5, 0.3, 0.2, 1)
		elseif (npc.Type == MINIONS.SamaelBony.Type and npc.Variant == MINIONS.SamaelBony.Variant)
				or (npc.Type == MINIONS.TaintedSamaelBony.Type and npc.Variant == MINIONS.TaintedSamaelBony.Variant) then
			tearVariant = TearVariant.SCHYTHE
			tearFlags = TearFlags.TEAR_PIERCING
		end
		
		if tearVariant then
			local tear = Isaac.Spawn(EntityType.ENTITY_TEAR, tearVariant, 0, proj.Position, proj.Velocity, npc):ToTear()
			tear.Parent = npc
			if tearFlags then
				tear:AddTearFlags(tearFlags)
			end
			if tearColor then
				tear.Color = tearColor
			end
			tear.CollisionDamage = 10
			tear.FallingSpeed = 1
			tear:GetData().fromThanatophiliaMinion = true
			tear:Update()
			
			proj:Remove()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PROJECTILE_INIT, mod.SpecialBonyProjectiles)

-- For custom entities that should never appear except as a minion.
local function ForceMinion(entity)
	if not entity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY | EntityFlag.FLAG_CHARM | EntityFlag.FLAG_PERSISTENT) then
		InitMinion(entity, Isaac.GetPlayer(0))
	end
end

-------------------- MOM'S DEAD HAND --------------------

function mod:DeadHandUpdate(entity)
	local data = entity:GetData()
	
	if not data.isThanatophiliaMinion then return end
	
	if entity.FrameCount == 1 then
		entity:GetSprite():Play("JumpUp")
		data.thanatophiliaInit = true
	end
	
	if data.thanatophiliaInit and data.thanatophiliaSpawnPos then
		entity.Position = data.thanatophiliaSpawnPos
		if not entity:GetSprite():IsPlaying("JumpUp") then
			data.thanatophiliaInit = false
		end
	end
	
	local target = entity.Target or entity:GetPlayerTarget()
	
	if target:ToPlayer() and entity.State == 4 and entity.StateFrame > 70 then
		entity.StateFrame = 70
		data.thanatophiliaWasTargetingPlayer = true
	end
	
	if not target:ToPlayer() and data.thanatophiliaWasTargetingPlayer then
		entity.StateFrame = 20
		data.thanatophiliaWasTargetingPlayer = false
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.DeadHandUpdate, EntityType.ENTITY_MOMS_DEAD_HAND)

-------------------- NECRO --------------------

function mod:NecroUpdate(entity)
	local data = entity:GetData()
	if not data.isThanatophiliaMinion then return end
	
	if entity.FrameCount == 1 then
		entity.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
	end
	
	local target = entity.Target or entity:GetPlayerTarget()
	
	if target:ToPlayer() then
		entity.ProjectileDelay = 20
	end
	
	local player = GetPlayerParent(entity)
	
	if entity.State ~= 8 then
		local parentDist = player.Position:Distance(entity.Position)
		local targetVel = lib.ZeroVector
		if parentDist > 90 then
			targetVel = (player.Position - entity.Position):Resized(3)
		end
		data.expectedVel = lib.Lerp(data.expectedVel or entity.Velocity, targetVel, 0.05)
		entity.Velocity = data.expectedVel
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.NecroUpdate, EntityType.ENTITY_NECRO)

-------------------- HEADLESS BIG BONY --------------------

function mod:HeadlessBigBonyUpdate(entity)
	if entity.Variant ~= MINIONS.HeadlessBigBony.Variant then return end
	
	local data = entity:GetData()
	local sprite = entity:GetSprite()
	
	if sprite:IsPlaying("Land") and sprite:IsEventTriggered("Hit") then
		sfxManager:Stop(SoundEffect.SOUND_MEAT_JUMPS)
		sfxManager:Play(SoundEffect.SOUND_HELLBOSS_GROUNDPOUND)
		
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.SHOCKWAVE, 0, entity.Position, lib.ZeroVector, entity):ToEffect()
		eff.Parent = entity
		eff:SetRadii(15, math.ceil(50 * entity.Scale))
		eff:SetTimeout(math.ceil(8 * entity.Scale))
		
		local params = ProjectileParams()
		params.Variant = ProjectileVariant.PROJECTILE_BONE
		entity:FireProjectiles(entity.Position, Vector(8, math.floor(8 * entity.Scale)), 9, params)
	end
	
	local target = entity.Target or entity:GetPlayerTarget()
	
	if target:ToPlayer() then
		if entity.State == 6 and game:GetRoom():GetAliveEnemiesCount() == 0 and not sprite:IsPlaying("Jump") and not sprite:IsPlaying("Land") then
			sfxManager:Stop(SoundEffect.SOUND_FETUS_JUMP)
			entity.State = 4
		end
		
		local player = GetPlayerParent(entity)
		local parentDist = player.Position:Distance(entity.Position)
		
		if parentDist < 90 then
			data.expectedVel = lib.Lerp(data.expectedVel or entity.Velocity, lib.ZeroVector, 0.15)
			entity.Velocity = data.expectedVel
		else
			data.expectedVel = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.HeadlessBigBonyUpdate, EntityType.ENTITY_FAT_SACK)

function mod:HeadlessBigBonyDeath(entity)
	if entity.Variant ~= MINIONS.HeadlessBigBony.Variant then return end
	entity:Remove()
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.HeadlessBigBonyDeath, EntityType.ENTITY_FAT_SACK)

-------------------- BONY HEAD --------------------

function mod:BonyHeadUpdate(entity)
	local data = entity:GetData()
	
	if not data.isThanatophiliaMinion then return end
	
	if entity.FrameCount == 1 then
		entity.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
	end
	
	local sprite = entity:GetSprite()
	local player = GetPlayerParent(entity)
	local parentDist = player.Position:Distance(entity.Position)
	local targetVel = lib.ZeroVector
	
	if not sprite:IsPlaying("Attack") and parentDist > 60 then
		targetVel = (player.Position - entity.Position):Resized(4)
	end
	
	local currentVel = entity.Velocity

	if entity.Velocity:Length() < 4 and data.expectedVel then
		currentVel = data.expectedVel
	end
	data.expectedVel = lib.Lerp(currentVel, targetVel, 0.1)
	entity.Velocity = data.expectedVel
	
	if sprite:IsEventTriggered("ShootBone") and entity.Target then
		local vel = (entity.Target.Position - entity.Position):Resized(8)
		local params = ProjectileParams()
		params.Variant = ProjectileVariant.PROJECTILE_BONE
		
		local mode = 0
		if entity.Variant == MINIONS.BigBonyHead.Variant then
			mode = 2
		end
		
		entity:FireProjectiles(entity.Position, vel, mode, params)
		
		if entity.SubType ~= 1 then
			entity.ProjectileCooldown = 20
		end
		
		sfxManager:Play(SoundEffect.SOUND_SCAMPER)
	elseif sprite:IsEventTriggered("Slam") and entity.Target and not entity.Target:ToPlayer() then
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.CRACKWAVE, 0, entity.Position, lib.ZeroVector, entity):ToEffect()
		eff.Parent = entity
		eff.Rotation = (entity.Target.Position - entity.Position):GetAngleDegrees()
		eff:SetRadii(0,0)
		sfxManager:Play(SoundEffect.SOUND_HELLBOSS_GROUNDPOUND)
		entity.ProjectileCooldown = 100
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.BonyHeadUpdate, EntityType.ENTITY_HORF)

function mod:BonyHeadDie(entity)
	if entity.Variant ~= MINIONS.BonyHead.Variant or entity.SubType ~= 1 then return end
	
	local player = GetPlayerParent(entity)
	
	Isaac.Explode(entity.Position, player, 40)
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.BonyHeadDie, EntityType.ENTITY_HORF)

-------------------- BURNING BONY --------------------

function mod:BurningBonyDeath(entity)
	if entity.Variant == MINIONS.BurningBony.Variant then
		entity:Remove()
		
		lib.BoneGibsBurst(entity.Position)
		
		local player = GetPlayerParent(entity)
		
		local flame = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.RED_CANDLE_FLAME, 0, entity.Position, lib.ZeroVector, player):ToEffect()
		flame:SetDamageSource(EntityType.ENTITY_PLAYER)
		flame.CollisionDamage = 23  -- Same as red candle flames.
		
		local projAngleOffset = entity:GetDropRNG():RandomInt(360)
		
		for i=0, 2 do
			local projVel = Vector(7,0):Rotated((360/3)*i + projAngleOffset)
			local tear = Isaac.Spawn(EntityType.ENTITY_TEAR, TearVariant.BONE, 0, entity.Position, projVel, entity):ToTear()
			tear.Parent = entity
			tear:AddTearFlags(TearFlags.TEAR_BURN)
			tear.CollisionDamage = 7.5
			tear.FallingSpeed = 1
			tear:GetData().fromThanatophiliaMinion = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.BurningBonyDeath, MINIONS.BurningBony.Type)

function mod:BurningBonyUpdate(entity)
	if entity.Variant ~= MINIONS.BurningBony.Variant then return end
	
	ForceMinion(entity)
	
	local sprite = entity:GetSprite()
	
	if entity.FrameCount <= 1 then
		sprite:PlayOverlay("Head", true)
	end
	
	local target = entity.Target or entity:GetPlayerTarget()
	local dir = entity.Velocity
	if target then
		dir = target.Position - entity.Position
	end
	local angle = dir:GetAngleDegrees() + 180
	
	local overlayAnim = nil
	
	local headTurnBuffer = 10
	
	if dir:Length() == 0 then
		overlayAnim = "Head"
	elseif angle < 45 - headTurnBuffer or angle > 315 + headTurnBuffer then
		if sprite.FlipX then
			overlayAnim = "HeadRight"
		else
			overlayAnim = "HeadLeft"
		end
	elseif angle > 45 + headTurnBuffer and angle < 135 - headTurnBuffer then
		overlayAnim = "HeadUp"
	elseif angle > 135 + headTurnBuffer and angle < 225 - headTurnBuffer then
		if sprite.FlipX then
			overlayAnim = "HeadLeft"
		else
			overlayAnim = "HeadRight"
		end
	elseif angle > 225 + headTurnBuffer and angle < 315 - headTurnBuffer then
		overlayAnim = "Head"
	end
	
	if overlayAnim and not sprite:IsOverlayPlaying(overlayAnim) then
		sprite:PlayOverlay(overlayAnim, true)
	end
	
	if target:ToPlayer() and entity.Position:Distance(target.Position) < entity.Size + target.Size + 5 then
		entity.Velocity = lib.Lerp(entity.Velocity, (target.Position - entity.Position):Rotated(90):Resized(4), 0.5)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.BurningBonyUpdate, MINIONS.BurningBony.Type)

function mod:BurningBonyDamage(tookDamage, damage, damageFlags, damageSourceRef)
	if damageSourceRef.Entity and damageSourceRef.Type == MINIONS.BurningBony.Type and damageSourceRef.Variant == MINIONS.BurningBony.Variant
			and not tookDamage:ToPlayer() then
		tookDamage:AddBurn(damageSourceRef, 120, 7.5)
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.BurningBonyDamage)

mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
	if npc.Variant ~= MINIONS.DapperBurningBony.Variant then return end
	
	ForceMinion(npc)
	
	local sprite = npc:GetSprite()
	if not sprite:IsOverlayPlaying() then
		sprite:PlayOverlay("F", true)
		sprite:SetOverlayRenderPriority(true)
	end
end, MINIONS.DapperBurningBony.Type)

-------------------- BIG BONY --------------------

function mod:ThanatophiliaBigBonyUpdate(entity)
	local data = entity:GetData()
	
	local target = entity.Target or entity:GetPlayerTarget()
	
	if data.isThanatophiliaMinion and target:ToPlayer() then
		local dist = target.Position:Distance(entity.Position)
		if dist < 80 then
			data.expectedVel = lib.Lerp(data.expectedVel or entity.Velocity, lib.ZeroVector, 0.15)
			entity.Velocity = data.expectedVel
		else
			data.expectedVel = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ThanatophiliaBigBonyUpdate, EntityType.ENTITY_BIG_BONY)

function mod:ThanatophiliaBigBone(entity)
	if entity.SpawnerEntity and entity.SpawnerEntity:GetData().isThanatophiliaMinion then
		entity:GetSprite():Load("gfx/samael_big_bone.anm2", true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.ThanatophiliaBigBone, EntityType.ENTITY_BIG_BONY)

-------------------- CLICKETY CLACK --------------------

function mod:ClicketyClackUpdate(entity)
	local data = entity:GetData()
	
	local target = entity.Target or entity:GetPlayerTarget()
	
	if data.isThanatophiliaMinion and target:ToPlayer() then
		local dist = target.Position:Distance(entity.Position)
		if dist < 65 then
			data.expectedVel = lib.Lerp(data.expectedVel or entity.Velocity, lib.ZeroVector, 0.2)
			entity.Velocity = data.expectedVel
		else
			data.expectedVel = nil
		end
		if entity.Velocity:Length() < 0.25 then
			entity:GetSprite():SetFrame("WalkDown", 0)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ClicketyClackUpdate, EntityType.ENTITY_CLICKETY_CLACK)

function mod:ClicketyClackDeath(entity)
	entity = entity:ToNPC()
	if entity and entity:GetData().isThanatophiliaMinion then
		local player = GetPlayerParent(entity)
		
		local params = ProjectileParams()
		params.Variant = ProjectileVariant.PROJECTILE_BONE
		entity:FireProjectiles(entity.Position, Vector(7, 4), 9, params)
		
		entity:Remove()
		
		if FiendFolio and entity.Variant == FiendFolio.FF.ClicketyClash.Var then
			for i=1, 2 do
				local dude = MINIONS.ClicketyClack:Spawn(player, entity)
				if dude then
					dude.Position = dude.Position + RandomVector() * 15
				end
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.ClicketyClackDeath, EntityType.ENTITY_CLICKETY_CLACK)

-------------------- BABY BONY --------------------

function mod:BabyBonyUpdate(entity)
	if entity.Variant ~= 3 or entity.SubType ~= MINIONS.BabyBony.SubType then return end
	
	ForceMinion(entity)
	
	local sprite = entity:GetSprite()
	
	local target = entity.Target or entity:GetPlayerTarget()
	local vec = lib.ZeroVector
	local angle = 0
	
	if target then
		vec = target.Position - entity.Position
		if entity.State == 7 and entity.V1:GetAngleDegrees() ~= 0 then
			vec = entity.V1
		end
		angle = vec:Rotated(-90):GetAngleDegrees()
		if angle > 0 then angle = (angle - 180) * (-1) + 180 end
		if angle < 0 then angle = angle * -1 end
		angle = 360 - angle
	end
	
	if sprite:IsEventTriggered("ShootBone") then
		local vel = vec:Resized(8)
		local params = ProjectileParams()
		params.Variant = ProjectileVariant.PROJECTILE_BONE
		entity:FireProjectiles(entity.Position, vel, 2, params)
		sfxManager:Play(SoundEffect.SOUND_SCAMPER)
	end
	
	if not sprite:WasEventTriggered("ShootBone") then
		local anim = sprite:GetAnimation()
		
		local facingLeft = angle > 0 and angle < 180
		local facingUp = angle > 90 and angle < 270
		
		if anim == "Move" and facingUp then
			sprite:Play("MoveUp", true)
		elseif anim == "DashStart" and facingUp then
			sprite:Play("DashStartUp", true)
		elseif anim == "Attack" then
			if facingUp and facingLeft then
				sprite:Play("AttackUpFlipped", true)
			elseif facingUp then
				sprite:Play("AttackUp", true)
			elseif facingLeft then
				sprite:Play("AttackFlipped", true)
			end
		elseif anim == "DashHori" then
			if facingUp and facingLeft then
				sprite:Play("DashHoriUpFlipped", true)
			elseif facingUp then
				sprite:Play("DashHoriUp", true)
			elseif facingLeft then
				sprite:Play("DashHoriFlipped", true)
			end
		end
		
		local headAngle = angle + 15
		if headAngle >= 360 then headAngle = headAngle - 360 end
		
		local frame = math.floor(lib.Lerp(0, 12, headAngle / 360))
		sprite:SetOverlayFrame("Head", frame)
		
		if facingUp then
			--sprite:SetOverlayRenderPriority(true)
		else
			sprite:SetOverlayRenderPriority(false)
		end
	end
	sprite.FlipX = false
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.BabyBonyUpdate, EntityType.ENTITY_BABY)

function mod:BabyBonyDamage(tookDamage, damage, damageFlags, damageSourceRef)
	if tookDamage.Variant ~= 3 or tookDamage.SubType ~= MINIONS.BabyBony.SubType then return end
	
	local anim = tookDamage:GetSprite():GetAnimation()
	if tookDamage.State == 7 and not (anim == "DashStart" or anim == "DashStartUp") then
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, mod.BabyBonyDamage, EntityType.ENTITY_BABY)

-------------------- SIREN BONY --------------------

local kSirenBonyOrbitalVariant = Isaac.GetEntityVariantByName("(Thanatophilia) Siren Bony Orbital")

function mod:SirenBonyUpdate(entity)
	if entity.Variant ~= MINIONS.SirenBony.Variant or entity.SubType ~= MINIONS.SirenBony.SubType then return end
	
	entity.State = 0
	
	ForceMinion(entity)
	
	local sprite = entity:GetSprite()
	
	if sprite:IsPlaying("Appear") then return end
	
	local data = entity:GetData()
	
	local player = GetPlayerParent(entity)
	local floatMode = player:HasCollectible(CollectibleType.COLLECTIBLE_DOGMA)
	local parentDist = player.Position:Distance(entity.Position)
	
	local anim = "Idle"
	local flipped = false
	local playbackSpeed = 1
	
	entity.ProjectileDelay = entity.ProjectileDelay - 1
	
	if (entity.ProjectileDelay <= 0 and game:GetRoom():GetAliveEnemiesCount() > 0) or sprite:IsPlaying("Attack") or sprite:IsPlaying("AttackFloat") then
		anim = "Attack"
		entity.ProjectileDelay = 120
	end
	
	if anim ~= "Attack" and parentDist > 90 then
		local hasDirectPath = game:GetRoom():CheckLine(entity.Position, player.Position, 1)
		if hasDirectPath then
			targetVel = (player.Position - entity.Position):Resized(3.5)
			entity.Velocity = lib.Lerp(entity.Velocity, targetVel, 0.1)
		else
			entity.Pathfinder:FindGridPath(player.Position, 0.5, 0, true)
		end
	else
		entity.Velocity = lib.Lerp(entity.Velocity, lib.ZeroVector, 0.1)
		if not floatMode then
			playbackSpeed = 0.75
		end
	end
	
	if floatMode then
		anim = anim .. "Float"
	elseif anim ~= "Attack" and entity.Velocity:Length() > 0.2 then
		local dir = lib.GetDirectionFromVector(entity.Velocity)
		if dir == Direction.UP then
			anim = "WalkUp"
		elseif dir == Direction.DOWN then
			anim = "WalkDown"
		elseif dir == Direction.LEFT then
			anim = "WalkHori"
			flipped = true
		elseif dir == Direction.RIGHT then
			anim = "WalkHori"
		end
	end
	
	if sprite:GetAnimation() ~= anim then
		sprite:Play(anim, true)
	end
	sprite:SetOverlayRenderPriority(true)
	if floatMode and sprite:GetOverlayAnimation() ~= "FloatBody" then
		sprite:SetOverlayRenderPriority(true)
		sprite:PlayOverlay("FloatBody", true)
	elseif not floatMode and sprite:GetOverlayAnimation() == "FloatBody" then
		sprite:RemoveOverlay()
	end
	
	sprite.PlaybackSpeed = playbackSpeed
	sprite.FlipX = flipped
	
	if sprite:IsEventTriggered("Spawn") then
		local spawnedOrbital = false
		local numOrbitals = 3
		for i=0, numOrbitals-1 do
			if not data["sirenBonyOrbital"..i] or not data["sirenBonyOrbital"..i]:Exists() then
				local orbital = Isaac.Spawn(EntityType.ENTITY_IMP, kSirenBonyOrbitalVariant, 0, entity.Position, lib.ZeroVector, nil):ToNPC()
				orbital:AddEntityFlags(EntityFlag.FLAG_FRIENDLY | EntityFlag.FLAG_CHARM)
				orbital:GetData().isSirenBonyMinion = true
				orbital:GetData().sirenBonyOrbitalOffset = i * (360 / numOrbitals)
				orbital.Parent = entity
				--orbital:GetSprite():Load("gfx/thanatophilia/siren_minion.anm2", true)
				orbital:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
				data["sirenBonyOrbital"..i] = orbital
				spawnedOrbital = true
			end
		end
		if spawnedOrbital then
			local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, entity.Position, lib.ZeroVector, entity):ToEffect()
			poof.Color = lib.NewColor(0,0,0,1)
		end
		
		--[[local params = ProjectileParams()
		params.Variant = ProjectileVariant.PROJECTILE_BONE
		entity:FireProjectiles(entity.Position, Vector(8, 8), 9, params)]]
		
		local projAngleOffset = entity:GetDropRNG():RandomInt(360)
		local numProj = 8
		
		for i=0, numProj-1 do
			local projVel = Vector(7,0):Rotated((360/numProj)*i + projAngleOffset)
			local tear = Isaac.Spawn(EntityType.ENTITY_TEAR, TearVariant.BONE, 0, entity.Position, projVel, entity):ToTear()
			tear.Parent = entity
			tear:AddTearFlags(TearFlags.TEAR_DARK_MATTER)
			tear.CollisionDamage = 13
			tear.FallingSpeed = 0.1
			tear:GetData().fromThanatophiliaMinion = true
			tear.Scale = 1.5
		end
		
		sfxManager:Play(Isaac.GetSoundIdByName("SamaelSirenSound"), 0.85, 0, false, 0.7)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SirenBonyUpdate, MINIONS.SirenBony.Type)

function mod:SirenBonyDie(entity)
	entity = entity:ToNPC()
	if not entity or entity.Variant ~= MINIONS.SirenBony.Variant or entity.SubType ~= MINIONS.SirenBony.SubType or not entity:IsDead() then return end
	
	local skull = Isaac.Spawn(EntityType.ENTITY_SIREN, 1, 0, entity.Position, entity.Velocity, nil):ToNPC()
	skull.TargetPosition = entity.Position
	skull:GetData().isFromSirenBony = true
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.SirenBonyDie, MINIONS.SirenBony.Type)

function mod:SirenSkullUpdate(entity)
	if entity.Variant ~= 1 then return end
	entity.SplatColor = Color(0,0,0,1)
	if FindPlayerWithThanatophilia() and entity.FrameCount > 60 and not entity:GetData().isFromSirenBony then
		entity:TakeDamage(9999, DamageFlag.DAMAGE_EXPLOSION, EntityRef(Isaac.GetPlayer(0)), 0)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SirenSkullUpdate, EntityType.ENTITY_SIREN)

local sirenSkullDying = 0

function mod:SirenSkullDie(entity)
	if entity.Variant ~= 1 then return end
	
	if FindPlayerWithThanatophilia() then
		sirenSkullDying = 2
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.SirenSkullDie, EntityType.ENTITY_SIREN)

function mod:SirenSkullRemove(entity)
	if entity.Variant ~= 1 then return end
	
	sirenSkullDying = 0
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.SirenSkullRemove, EntityType.ENTITY_SIREN)

function mod:NoHost(entity)
	if sirenSkullDying > 0 then
		entity:Remove()
		sirenSkullDying = 0
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.NoHost, EntityType.ENTITY_HOST)

function mod:SirenSkullHandler()
	if sirenSkullDying > 0 then
		sirenSkullDying = sirenSkullDying - 1
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.SirenSkullHandler)

function mod:SirenBonyFamiliarUpdate(entity)
	if entity.Variant ~= kSirenBonyOrbitalVariant then return end
	
	local data = entity:GetData()
	if not data.isSirenBonyMinion then
		entity:Remove()
		return
	end
	
	if not entity.Parent or not entity.Parent:Exists() then
		entity:Die()
		return
	end
	
	local rot = (entity.Parent.FrameCount*3) % 360 + data.sirenBonyOrbitalOffset
	local targetPos = entity.Parent.Position + Vector(30,0):Rotated(rot)
	if game:GetRoom():GetFrameCount() == 0 then
		entity.Position = targetPos
	else
		entity.Position = lib.Lerp(data.prevPos or entity.Position, targetPos, 0.5)
	end
	data.prevPos = nil
	
	local target = entity.Target or entity:GetPlayerTarget()
	local sprite = entity:GetSprite()
	
	if sprite:IsPlaying("Attack") and sprite:GetFrame() == 0 then
		local dir = lib.GetDirectionFromVector(target.Position - entity.Position)
		if dir == Direction.RIGHT then
			sprite:Play("AttackHori", true)
			sprite.FlipX = false
		elseif dir == Direction.LEFT then
			sprite:Play("AttackHori", true)
			sprite.FlipX = true
		elseif dir == Direction.UP then
			sprite:Play("AttackUp", true)
		end
	end
	
	if entity.State == 8 and target:ToPlayer() then
		entity.State = 4
	end
	
	if entity.State == 6 then
		entity:GetSprite():Play("Vanish2", true)
		entity:GetSprite():SetLastFrame()
		entity.StateFrame = 30
		data.prevPos = entity.Position
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SirenBonyFamiliarUpdate, EntityType.ENTITY_IMP)

function mod:ThanatophiliaProjectileUpdate(proj)
	if not proj.Parent then return end

	if proj.FrameCount == 1 then
		if proj.Parent:GetData().isThanatophiliaMinion then
			-- Nothing atm
		elseif proj.Parent:GetData().isSirenBonyMinion then
			proj.Variant = ProjectileVariant.PROJECTILE_BONE
			local sprite = proj:GetSprite()
			sprite:Load("gfx/009.001_bone projectile.anm2")
			proj.SpriteScale = Vector(0.75, 0.75)
			sfxManager:Play(SoundEffect.SOUND_SCAMPER, 0.85, 0, false, 1.2)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PROJECTILE_UPDATE, mod.ThanatophiliaProjectileUpdate)

-------------------- FORGOTTEN --------------------

function mod:SpawnFakeForgotten(player)
	local him = MINIONS.Forgotten:Spawn(player or Isaac.GetPlayer())
	mod:BoneFriendSpawnFx(him.Position)
	him.Visible = false
	return him
end

function mod:ThanatophiliaForgottenBoneInit(entity)
	if entity.SpawnerEntity and entity.SpawnerType == EntityType.ENTITY_FAMILIAR
			and entity.SpawnerVariant == FamiliarVariant.MINISAAC 
			and entity.SpawnerEntity:GetData().isThanatophiliaMinion then
		entity:GetSprite():Load("gfx/008.001_Bone Club.anm2", true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_INIT, mod.ThanatophiliaForgottenBoneInit, 4)

function mod:ThanatophiliaForgottenBoneUpdate(entity)
	if entity.SpawnerEntity and entity.SpawnerType == EntityType.ENTITY_FAMILIAR
			and entity.SpawnerVariant == FamiliarVariant.MINISAAC 
			and entity.SpawnerEntity:GetData().isThanatophiliaMinion then
		entity.SpriteScale = Vector(1,1)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.ThanatophiliaForgottenBoneUpdate, 4)

function mod:PlayForgottenAnim(entity, anim, stopAtEnd)
	local data = entity:GetData()
	local sprite = entity:GetSprite()
	data.forgottenAnim = anim
	data.forgottenAnimFrame = 0
	sprite:Play(anim, true)
	sprite:SetLastFrame()
	data.forgottenAnimLastFrame = sprite:GetFrame()
	sprite:SetFrame(0)
	data.forgottenAnimStopAtEnd = stopAtEnd
end

function mod:ThanatophiliaForgottenDamage(entity, damage, damageFlags, damageSourceRef)
	local data = entity:GetData()
	
	if data.isThanatophiliaMinion and entity.Variant == FamiliarVariant.MINISAAC and entity.SubType == 99 then
		local specialForgottenInvincible = data.samaelContractForgotten and entity:ToFamiliar().Player and entity:ToFamiliar().Player:GetData().wraithActive
		if not specialForgottenInvincible and not data.thanatophiliaForgottenDown and (entity:GetData().iFrames or 0) <= 0 then
			if entity.HitPoints <= 1 then
				entity.HitPoints = entity.MaxHitPoints
				data.thanatophiliaForgottenDown = true
				data.thanatophiliaForgottenDownTime = 300
				mod:PlayForgottenAnim(entity, "ForgottenDeath", true)
			else
				entity.HitPoints = entity.HitPoints - 1
				mod:PlayForgottenAnim(entity, "Hit")
				entity:GetData().iFrames = 40
				sfxManager:Play(SoundEffect.SOUND_BONE_SNAP)
				if not entity.Target and damageSourceRef and damageSourceRef.Entity then
					if damageSourceRef.Entity:IsVulnerableEnemy() then
						entity.Target = damageSourceRef.Entity
					elseif damageSourceRef.Entity.SpawnerEntity and damageSourceRef.Entity.SpawnerEntity:IsVulnerableEnemy() then
						entity.Target = damageSourceRef.Entity.SpawnerEntity
					end
				end
			end
		end
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, mod.ThanatophiliaForgottenDamage, EntityType.ENTITY_FAMILIAR)

local FORGOTTEN_HP = 6

function mod:ThanatophiliaForgottenUpdate(entity)
	local data = entity:GetData()
	local sprite = entity:GetSprite()
	
	if not data.isThanatophiliaMinion then return end
	
	if entity.SubType ~= 99 then
		data.samaelThanatosMinion = "BONY"
		return
	end
	
	--[[if not entity:HasEntityFlags(EntityFlag.FLAG_TRANSITION_UPDATE) then
		entity:AddEntityFlags(EntityFlag.FLAG_TRANSITION_UPDATE)
	end]]
	
	local player = entity.Player or Isaac.GetPlayer()
	
	local isSpecialForgotten = lib.IsSamael(player) and not lib.IsTaintedSamael(player)
			and TaintedTreasure and TaintedCollectibles and TaintedCollectibles.CONTRACT_OF_SERVITUDE
			and player:HasCollectible(TaintedCollectibles.CONTRACT_OF_SERVITUDE)
	data.samaelContractForgotten = isSpecialForgotten
	
	if isSpecialForgotten and player:GetData().wraithActive then
		entity:PickEnemyTarget(9999, 10, 1 << 0)
		if entity.FrameCount % 12 == 0 then
			entity:SetColor(Color(1, 1, 1, 1, 0.5, 0, 0.5), 10, 1, true, false)
		end
	end
	
	if entity.MaxHitPoints ~= FORGOTTEN_HP or entity.HitPoints > FORGOTTEN_HP then
		entity.MaxHitPoints = FORGOTTEN_HP
		entity.HitPoints = FORGOTTEN_HP
	else
		-- Negates passive health drain on Minisaacs, plus this fake forgotten only takes 1 damage at a time.
		entity.HitPoints = math.ceil(entity.HitPoints)
	end
	
	if isSpecialForgotten and entity.FireCooldown > 0 and data.samaelLastFireCooldown == 0 then
		local fireDelay = math.min(lib.GetUnmodifiedFireDelay(player), 21)
		if player:GetData().wraithActive then
			fireDelay = fireDelay * 0.5
		end
		entity.FireCooldown = math.ceil(fireDelay)
	end
	data.samaelLastFireCooldown = entity.FireCooldown
	
	if player:GetSprite():IsPlaying("Trapdoor") or player:GetSprite():IsPlaying("LightTravel") then
		local anim = player:GetSprite():GetAnimation()
		mod:PlayForgottenAnim(entity, anim)
		data.forgottenAnimFrame = player:GetSprite():GetFrame()
		local offset = (anim == "Trapdoor") and Vector(0, -10) or Vector(20, 15)
		local targetVel = (player.Position + offset) - entity.Position
		if targetVel:Length() > 100 then
			targetVel = targetVel:Resized(100)
		end
		entity.Velocity = lib.Lerp(entity.Velocity, targetVel, 0.2)
		entity:AddEntityFlags(EntityFlag.FLAG_TRANSITION_UPDATE)
	elseif entity:HasEntityFlags(EntityFlag.FLAG_TRANSITION_UPDATE) then
		entity:ClearEntityFlags(EntityFlag.FLAG_TRANSITION_UPDATE)
	end
	
	if player:GetSprite():GetAnimation() == "Appear" and player.Position:Distance(entity.Position) == 0 and game:GetRoom():GetFrameCount() == 0 then
		entity.Position = entity.Position + Vector(40, -20)
		entity.Visible = true
		mod:PlayForgottenAnim(entity, "AppearShort")
	end
	
	if data.forgottenAnim then
		if data.forgottenAnim == "AppearShort" then
			entity.Velocity = lib.ZeroVector
		end
		
		entity.FireCooldown = 10
		entity:GetSprite():RemoveOverlay()
		
		local frame = data.forgottenAnimFrame or 0
		sprite:SetFrame(data.forgottenAnim, frame)
		data.forgottenAnimFrame = frame + 1
		
		if sprite:IsEventTriggered("boneburst") then
			lib.BoneGibsBurst(entity.Position)
		end
		--[[if sprite:IsEventTriggered("ow") then
			sfxManager:Play(SoundEffect.SOUND_ISAAC_HURT_GRUNT)
		end]]
		
		if data.forgottenAnimLastFrame and frame >= data.forgottenAnimLastFrame and not data.forgottenAnimStopAtEnd then
			data.forgottenAnim = nil
		end
	end
	
	if data.thanatophiliaForgottenDown then
		local downTime = data.thanatophiliaForgottenDownTime or 0
		entity.Velocity = lib.ZeroVector
		if downTime <= 0 or game:GetRoom():IsClear() then
			data.thanatophiliaForgottenDown = false
			data.forgottenAnim = nil
			Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, entity.Position, lib.ZeroVector, nil)
			sfxManager:Play(SoundEffect.SOUND_BONE_DROP)
		else
			data.thanatophiliaForgottenDownTime = downTime - 1
		end
		local color = Color.Lerp(entity:GetColor(), Color(0.5, 0.5, 0.5, 1.0), 0.06)
		entity:SetColor(color, 2, 1, false, false)
		return
	elseif (player:GetSprite():IsPlaying("Happy") or player:GetSprite():IsPlaying("Sad")) and player:GetSprite():GetFrame() <= 1 then
		mod:PlayForgottenAnim(entity, player:GetSprite():GetAnimation())
	end
	
	if data.forgottenAnim ~= "Trapdoor" then
		local speedLimit = (isSpecialForgotten and player:GetData().wraithActive) and 30 or 15
		if isSpecialForgotten then
			speedLimit = speedLimit * player.MoveSpeed
		end
		if entity.Velocity:Length() > speedLimit then
			entity.Velocity = entity.Velocity:Resized(speedLimit)
		end
	end
	
	if data.iFrames then
		data.iFrames = data.iFrames - 1
		if data.iFrames % 4 == 0 then
			entity:SetColor(lib.InvisibleColor, 2, 0, false, false)
		end
		if data.iFrames <= 0 then
			data.iFrames = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.ThanatophiliaForgottenUpdate, FamiliarVariant.MINISAAC)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if not game:IsPaused() then return end
	
	for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC, 99)) do
		if ent:GetData().isThanatophiliaMinion then
			local player = ent:ToFamiliar().Player
			if player and player:GetSprite():IsPlaying("LightTravel") and player:GetSprite():GetFrame() == 0 then
				ent:Update()
			end
		end
	end
end)

function mod:ThanatophiliaForgottenBone(knife)
	if knife.SpawnerType == EntityType.ENTITY_FAMILIAR and knife.SpawnerEntity
			and knife.SpawnerEntity.Variant == FamiliarVariant.MINISAAC 
			and knife.SpawnerEntity.SubType == 99 and knife.SpawnerEntity:GetData().isThanatophiliaMinion then
		if knife.SpawnerEntity:GetData().samaelContractForgotten and knife.SpawnerEntity:ToFamiliar() and knife.SpawnerEntity:ToFamiliar().Player then
			knife.CollisionDamage = knife.SpawnerEntity:ToFamiliar().Player.Damage
		else
			knife.CollisionDamage = 3.5
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.ThanatophiliaForgottenBone, 4)

function mod:ThanatophiliaForgottenCollision(entity)
	if entity:GetData().isThanatophiliaMinion and entity:GetData().thanatophiliaForgottenDown then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_FAMILIAR_COLLISION, mod.ThanatophiliaForgottenCollision, FamiliarVariant.MINISAAC)

function mod:ThanatophiliaIsaacDeath(entity)
	if entity.Variant == 0 and FindPlayerWithThanatophilia() then
		entity:BloodExplode()
		entity:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.ThanatophiliaIsaacDeath, EntityType.ENTITY_ISAAC)

-------------------- SHELL HEAD --------------------

function mod:ShellHeadUpdate(entity)
	if entity.Variant ~= MINIONS.ShellHead.Variant then return end
	
	ForceMinion(entity)
	
	local data = entity:GetData()
	
	if entity.State == 8 then
		if not data.isAttacking then
			sfxManager:Stop(SoundEffect.SOUND_MAGGOTCHARGE)
			data.isAttacking = true
		end
	else
		data.isAttacking = false
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.ShellHeadUpdate, MINIONS.ShellHead.Type)

-------------------- DEATH (BONY) --------------------

function mod:DeathBonyUpdate(entity)
	if entity.Variant ~= MINIONS.DeathBony.Variant then return end
	
	ForceMinion(entity)
	
	if entity:GetSprite():IsEventTriggered("ShootScythe") then
		local target = entity.Target or entity:GetPlayerTarget()
		
		if target:ToPlayer() then return end
		
		sfxManager:Play(SoundEffect.SOUND_MONSTER_GRUNT_0)
		
		local projVel = (target.Position - entity.Position):Resized(10)
		local tear = Isaac.Spawn(EntityType.ENTITY_TEAR, TearVariant.SCHYTHE, 0, entity.Position, projVel, entity):ToTear()
		tear:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		tear:AddTearFlags(TearFlags.TEAR_PIERCING)
		tear.Parent = entity
		tear.CollisionDamage = 13
		tear.FallingSpeed = 0.1
		tear.Scale = 1.5
		tear:GetData().fromThanatophiliaMinion = true
	end
	
	local player = GetPlayerParent(entity)
	if player:HasCollectible(CollectibleType.COLLECTIBLE_DOGMA) then
		entity.HitPoints = entity.MaxHitPoints
		entity:Morph(EntityType.ENTITY_DEATH, 0, 0, -1)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.DeathBonyUpdate, MINIONS.DeathBony.Type)

-------------------- PILE --------------------

function mod:PileUpdate(entity)
	local data = entity:GetData()
	
	if entity.Variant ~= 1 or not data.isThanatophiliaMinion then return end
	
	if entity.State == 13 then
		entity.State = 1
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.PileUpdate, EntityType.ENTITY_POLYCEPHALUS)

-------------------- DEATH (RIDER) --------------------

function mod:DeathBehaviour(entity)
	local data = entity:GetData()
	
	-- Scythes
	if entity.Variant == 10 then
		if entity.SpawnerEntity and entity.SpawnerEntity:GetData().isThanatophiliaMinion and entity:GetPlayerTarget():ToPlayer() and Isaac.CountEnemies() == 0 then
			entity:Kill()
		end
		return
	end
	
	if not data.isThanatophiliaMinion then return end
	
	local target = entity.Target or entity:GetPlayerTarget()
	local player = GetPlayerParent(entity)
	
	if entity.State == 8 then
		entity.State = 13
	end
	if target:ToPlayer() and (entity.State == 13 or entity.State == 14) then
		entity.State = 4
	end
	
	local targetPos = player.Position + (entity.Position - player.Position):Resized(50)
	entity.Velocity = (targetPos - entity.Position):Resized(math.min(9, entity.Position:Distance(targetPos) * 0.1))
	
	if not target:ToPlayer() then
		entity:GetSprite().FlipX = target.Position.X < entity.Position.X
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.DeathBehaviour, EntityType.ENTITY_DEATH)

-------------------- ULTRA DEATH --------------------

-- 9 = skulls/leavin
-- 10 = destroySkulls
-- 16 = slashin
-- 3 = idle
-- 8 = scythes

function mod:UltraDeathBehaviour(entity)
	-- Death
	if entity.Variant == 40 and entity:GetData().isThanatophiliaMinion then
		local sprite = entity:GetSprite()
		--print(entity.State .. ", " .. entity.I1 .. ", " .. entity.I2 .. " ... " .. entity:GetSprite():GetAnimation())
		
		entity.CollisionDamage = 0
		
		if entity.State == 9 then
			entity.State = 10
			entity:GetSprite():Play("SwingPrepare", true)
			entity.TargetPosition = entity.Position
			entity.Velocity = lib.ZeroVector
		end
		
		if entity.State == 16 or entity.State == 7 then
			game:Darken(0.5, 1)
		end
		
		if sprite:IsPlaying("Swing") and sprite:IsEventTriggered("Shoot") then
			for _, proj in pairs(Isaac.FindInRadius(entity.Position, 250, EntityPartition.BULLET)) do
				if proj.Variant == 14 then
					proj:Remove()
				else
					proj:Die()
				end
			end
		end
		
		local targetIsBeast = entity.Target and entity.Target.Type == EntityType.ENTITY_BEAST and entity.Target.Variant == 0
		
		local room = game:GetRoom()
		local clampedPos = room:GetClampedPosition(entity.Position, 0)
		
		if entity.State ~= 9 and entity.State ~= 7 and entity.State ~= 16 and clampedPos:Distance(entity.Position) > 0 then
			entity.Velocity = clampedPos - entity.Position
		elseif (entity.State == 3 or entity.State == 8) and targetIsBeast then
			local targetPos = (entity.Target.Position - room:GetCenterPos()):Rotated(180) + room:GetCenterPos()
			targetPos = room:GetClampedPosition(targetPos, 50)
			local maxSpeed = 30
			local targetVel = targetPos - entity.Position
			if targetVel:Length() > maxSpeed then
				targetVel = targetVel:Resized(maxSpeed)
			end
			entity.Velocity = lib.Lerp(entity.Velocity, targetVel, 0.2)
		end
	end
	
	-- Scythes
	if entity.Variant == 41 and entity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)	then
		if game:GetRoom():GetFrameCount() <= 1 then
			entity:Remove()
		end
		
		if entity.Target and not entity:GetData().thanatophiliaOverrodeTargetPos then
			entity.TargetPosition = (entity.Target.Position - entity.Position):Resized(2)
			entity:GetData().thanatophiliaOverrodeTargetPos = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.UltraDeathBehaviour, EntityType.ENTITY_BEAST)

function mod:UltraDeathDamage(tookDamage, damage, damageFlags, damageSourceRef)
	if damageSourceRef.Type == EntityType.ENTITY_BEAST and damageSourceRef.Variant == 40 and damageSourceRef.Entity and damageSourceRef.Entity:ToNPC()
			and damageSourceRef.Entity:GetData().isThanatophiliaMinion and damageSourceRef.Entity:ToNPC().State == 16 then
		tookDamage:TakeDamage(66, DamageFlag.DAMAGE_IGNORE_ARMOR, EntityRef(Isaac.GetPlayer(0)), 0)
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.UltraDeathDamage)

function mod:UltraDeathScytheCollision(entity, collider)
	if entity.Variant ~= 41 or not entity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then return end
	
	if collider:IsActiveEnemy() and not collider:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
		entity:Kill()
		collider:TakeDamage(66, 0, EntityRef(Isaac.GetPlayer(0)), 0)
	end
	
	if collider.Type == 9 then
		entity:Kill()
		if collider.Variant == 14 then
			collider:Remove()
		else
			collider:Die()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.UltraDeathScytheCollision, EntityType.ENTITY_BEAST)

mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, function()
	for _, death in pairs(Isaac.FindByType(EntityType.ENTITY_BEAST, 40)) do
		if death:GetData().isThanatophiliaMinion then
			death:Remove()
		end
	end
end)

-------------------- REVELATIONS RAGFOLK --------------------

if REVEL then
	local RAG_FOLK_BY_NAME = {
		RAG_GAPER = {},
		RAG_TAG = {},
		RAG_BONY = {},
		WRETCHER = {},
		--RAG_FATTY = {Minion=MINIONS.HeadlessBigBony},
		RAG_TRITE = {Minion=MINIONS.RagNecro, ExData={Scale=0.85}},
		RAGMA = {BuffedOnly=true, ExData={Scale=1.25}},
	}
	local RAG_FOLK_BY_KEY = {}
	
	for name, tab in pairs(RAG_FOLK_BY_NAME) do
		local id = REVEL.ENT[name].id
		local var = REVEL.ENT[name].variant
		tab.Type = id
		tab.Variant = var
		RAG_FOLK_BY_KEY[id.."."..var] = tab
	end
	
	if FiendFolio then
		local id = FiendFolio.FF.Ragurge.ID
		local var = FiendFolio.FF.Ragurge.Var
		RAG_FOLK_BY_KEY[id ..".".. var] = {
			Type = id,
			Variant = var,
			TombOnly = true,
		}
	end
	
	function mod:ThanatophiliaRagFolk(npc)
		local key = npc.Type ..".".. npc.Variant
		local ragInfo = RAG_FOLK_BY_KEY[key]
		
		if not ragInfo or (ragInfo.TombOnly and not REVEL.STAGE.Tomb:IsStage()) then return end
		
		local data = npc:GetData()
		
		data.noNormalThanatophiliaSpawn = true
		
		if data.isThanatophiliaMinion then
			if not data.Buffed and data.thanatophiliaExtraData and data.thanatophiliaExtraData.RagBuffed then
				data.Buffed = true
			end
			data.NoRags = true
		elseif npc:IsDead() and not data.samaelCheckedRagFolk and not (ragInfo.BuffedOnly and not data.Buffed) then
			local thanatophiliaPlayer = FindPlayerWithThanatophilia()
			if thanatophiliaPlayer then
				local minion = mod.SpawnThanatophiliaMinion(ragInfo.Minion or MINIONS.RagBony, thanatophiliaPlayer, npc, ragInfo.ExData)
				if minion then
					data.SpawnedRag = true
					data.NoRags = true
					lib.GetOrInit(minion:GetData(), "thanatophiliaExtraData").RagBuffed = data.Buffed
					data.Buffed = true
				end
			end
			data.samaelCheckedRagFolk = true
		end
	end
	
	for _, tab in pairs(RAG_FOLK_BY_KEY) do
		mod:AddPriorityCallback(ModCallbacks.MC_NPC_UPDATE, CallbackPriority.EARLY, mod.ThanatophiliaRagFolk, tab.Type)
	end
end

-------------------- MEAT CLEAVER FOR SOME REASON --------------------

mod:AddCallback(ModCallbacks.MC_USE_ITEM, function(_, _, _, player, flags, slot)
	for _, entity in pairs(Isaac.GetRoomEntities()) do
		local data = entity:GetData()
		if entity:Exists() and entity:ToNPC() and data.isThanatophiliaMinion and not data.isSirenBonyMinion and entity:ToNPC().Scale > 0.5 then
			entity = entity:ToNPC()
			local newMinion = SummonMinion(player, entity, true)
			if newMinion then
				local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.CLEAVER_SLASH, 0, entity.Position, lib.ZeroVector, nil):ToEffect()
				eff:FollowParent(entity)
				
				lib.BoneGibsBurst(entity.Position)
				
				local exData = lib.GetOrInit(data, "thanatophiliaExtraData")
				exData.Scale = (exData.Scale or entity.Scale) * (2/3)
				entity.Scale = exData.Scale
				
				lib.GetOrInit(newMinion:GetData(), "thanatophiliaExtraData").Scale = exData.Scale
				newMinion.Scale = exData.Scale
			end
		end
	end
end, CollectibleType.COLLECTIBLE_MEAT_CLEAVER)
