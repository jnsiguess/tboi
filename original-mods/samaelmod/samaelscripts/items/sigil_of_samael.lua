local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local SIGIL_OF_SAMAEL = mod.ITEMS.SIGIL_OF_SAMAEL
local SCYTHE_FOSSIL = mod.ITEMS.SCYTHE_FOSSIL
local DUMMY = mod.ENTITIES.DUMMY_ENEMY

local function IsDummy(entity)
	return entity and (entity:GetData().sigilOfSamaelDeathDummy or (entity.Type == DUMMY.ID and entity.Variant == DUMMY.Var and entity.SubType == DUMMY.Sub))
end

-- Detect if Death's List is actively targeting an entity.
function mod:DeathMarkUpdate(eff)
	if eff.Target then
		if IsDummy(eff.Target) then
			eff:Remove()
			return
		end
		eff.Target:GetData().deathsListMark = eff
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.DeathMarkUpdate, EffectVariant.DEATH_SKULL)

local function AllowExtraDeaths(entity)
	return entity and not IsDummy(entity) and lib.AllowOnDeathEffect(entity, true)
end

function mod:SigilOfSamaelEffect(entity)
	entity = entity:ToNPC()
	
	if not AllowExtraDeaths(entity) then return end
	
	local extraDeaths = 0
	
	local player = (lib.Pointee(entity:GetData().samaelLastPlayerDamageSource) or Isaac.GetPlayer(0)):ToPlayer()
	
	for _, p in pairs(lib.GetPlayers()) do
		if p:HasTrinket(SIGIL_OF_SAMAEL) or p:HasTrinket(SCYTHE_FOSSIL) then
			if not (player:HasTrinket(SIGIL_OF_SAMAEL) or player:HasTrinket(SCYTHE_FOSSIL)) then
				player = p
			end
			local sigilOfSamaelPower = p:GetTrinketMultiplier(SIGIL_OF_SAMAEL)
			local scytheFossilPower = FiendFolio and FiendFolio.GetGolemTrinketPower(p, SCYTHE_FOSSIL) or p:GetTrinketMultiplier(SCYTHE_FOSSIL)
			extraDeaths = extraDeaths + sigilOfSamaelPower + scytheFossilPower
		end
	end
	
	local bonusChance = extraDeaths % 1
	extraDeaths = math.floor(extraDeaths)
	if bonusChance > 0 and player:GetTrinketRNG(SIGIL_OF_SAMAEL):RandomFloat() <= bonusChance then
		extraDeaths = extraDeaths + 1
	end
	
	if extraDeaths > 0 then
		local playerRef = EntityRef(player)
		
		for i=1, extraDeaths do
			local pos = entity.Position - Vector(0, entity.Size * 0.5) + RandomVector()*entity.Size
			local dummy = Isaac.Spawn(DUMMY.ID, DUMMY.Var, DUMMY.Sub, pos, lib.ZeroVector, nil):ToNPC()
			dummy:AddEntityFlags(entity:GetEntityFlags())
			dummy:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			if dummy:HasEntityFlags(EntityFlag.FLAG_ICE_FROZEN) then
				dummy:ClearEntityFlags(EntityFlag.FLAG_ICE_FROZEN)
				dummy:AddEntityFlags(EntityFlag.FLAG_ICE)
			end
			dummy.SplatColor = entity.SplatColor
			dummy.MaxHitPoints = entity.MaxHitPoints
			dummy.HitPoints = entity:GetData().samaelLastHp or 1
			if entity:IsChampion() and not entity:IsBoss() then
				dummy:MakeChampion(dummy.InitSeed, entity:GetChampionColorIdx(), true)
			end
			dummy:GetData().noJarOfScythes = entity:GetData().lastDamageWasJarOfScythes
			dummy:GetData().sigilOfSamaelDeathDummy = true
			dummy:GetData().samaelPlayerRef = playerRef
			dummy.TargetPosition = pos
			dummy.EntityCollisionClass = 0
			dummy.GridCollisionClass = 0
			
			if entity:GetData().deathsListMark and entity:GetData().deathsListMark:Exists() and entity:GetData().deathsListMark.Target
					and entity:GetData().deathsListMark.Target.InitSeed == entity.InitSeed then
				-- Only method I found to avoid breaking Death's List.
				dummy:AddEntityFlags(EntityFlag.FLAG_FRIENDLY)
			end
			
			dummy.I1 = 4 * i
			dummy.I2 = 0
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.SigilOfSamaelEffect)

-- Handle frozen enemies.
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	for _, npc in pairs(Isaac.FindByType(EntityType.ENTITY_FROZEN_ENEMY)) do
		if npc:GetData().sigilOfSamaelDeathDummy then
			npc:Kill()
		end
	end
end)

function mod:SigilOfSamaelDummy(dummy)
	if dummy.Variant ~= DUMMY.Var or dummy.SubType ~= DUMMY.Sub then return end
	
	if dummy:GetData().sigilOfSamaelDeathDummyGhost then
		local spr = dummy:GetSprite()
		--spr.Color = Color(1,1,1, 0.5)
		
		if spr:IsFinished("Appear") then
			spr:Play("Die", true)
		elseif spr:IsFinished("Die") then
			dummy:Kill()
		end
		return
	end
	
	if dummy.I1 > 0 then
		dummy.I1 = dummy.I1 - 1
	else
		dummy.Position = dummy.TargetPosition
		if dummy.I2 == 1 or dummy:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
			dummy:Kill()
		else
			dummy:TakeDamage(dummy.HitPoints, 0, dummy:GetData().samaelPlayerRef or EntityRef(Isaac.GetPlayer(0)), 0)
			dummy.I2 = 1
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfSamaelDummy, DUMMY.ID)

function mod:SigilOfSamaelDummyCollision(dummy, collider)
	if dummy.Variant == DUMMY.Var and dummy.SubType == DUMMY.Sub then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.SigilOfSamaelDummyCollision, DUMMY.ID)

function mod:TrackLastHp(entity)
	if entity.HitPoints > 0 then
		entity:GetData().samaelLastHp = entity.HitPoints
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.TrackLastHp)

function mod:ScytheFossilCrushEffect()
	mod:TriggerDyingGhosts()
	
	local data = mod:GetAllRunData()
	data.scytheFossilTriggers = (data.scytheFossilTriggers or 0) + 4
end

function mod:ScytheFossilSpawnDyingGhost()
	local pos = game:GetRoom():FindFreeTilePosition(game:GetRoom():GetRandomPosition(20), 0)
	local dummy = Isaac.Spawn(DUMMY.ID, DUMMY.Var, DUMMY.Sub, pos, lib.ZeroVector, nil):ToNPC()
	dummy:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
	--dummy:AddEntityFlags(EntityFlag.FLAG_FRIENDLY)
	dummy:GetData().sigilOfSamaelDeathDummy = true
	dummy:GetData().sigilOfSamaelDeathDummyGhost = true
	dummy:GetData().samaelPlayerRef = EntityRef(Isaac.GetPlayer())
	dummy.TargetPosition = pos
	dummy.EntityCollisionClass = 0
	dummy.GridCollisionClass = 0
	dummy.MaxHitPoints = 5 + Random() % 16
	dummy.HitPoints = dummy.MaxHitPoints
	
	local spr = dummy:GetSprite()
	spr:Load("gfx/1000.186_hungry soul.anm2", false)
	spr:ReplaceSpritesheet(0, "gfx/effects/spookier_ghost_white.png")
	spr:LoadGraphics()
	spr:Play("Appear", true)
	
	spr.Color = Color(1,1,1, 0.5)
	
	dummy.SplatColor = Color(0,0,0, 0.3, 0.8, 0.8, 0.8)
end

function mod:TriggerDyingGhosts()
	mod:GetAllCurrentRoomData().scytheFossilTriggered = true
	for i=1, 13 do
		lib.ScheduleForUpdate(function()
			mod:ScytheFossilSpawnDyingGhost()
		end, i * 3)
	end
end

function mod:HandleScytheFossilPostCrush()
	local roomData = mod:GetAllCurrentRoomData()
	if not roomData.scytheFossilTriggered and not game:GetRoom():IsClear() then
		local data = mod:GetAllRunData()
		local triggers = (data.scytheFossilTriggers or 0)
		if triggers > 0 then
			data.scytheFossilTriggers = triggers - 1
			mod:TriggerDyingGhosts()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.HandleScytheFossilPostCrush)
