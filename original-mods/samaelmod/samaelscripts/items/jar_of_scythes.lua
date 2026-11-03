local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local JAR_OF_SCYTHES = mod.ITEMS.JAR_OF_SCYTHES

local NUM_KILLS_PER_STOCK = 1
local MAX_SCYTHE_STOCKS = 20
local MAX_ACTIVATION_SCYTHES = 4

local SLASH_COLOR = Color(1,0,0,1)

local catchScytheDamage = false

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	catchScytheDamage = false
end)

function mod:JarOfScythes(_, rng, player, useFlags, slot)
	local data = mod:GetPersistentPlayerData(player)
	if (data.jarOfScythesStocks or 0) > 0 then
		local scythesToSpawn = math.min(data.jarOfScythesStocks, MAX_ACTIVATION_SCYTHES)
		if player:HasCollectible(CollectibleType.COLLECTIBLE_CAR_BATTERY) then
			scythesToSpawn = scythesToSpawn * 2
		end
		mod:TriggerSpinningScythes(player, scythesToSpawn)
		data.jarOfScythesStocks = math.max(data.jarOfScythesStocks - MAX_ACTIVATION_SCYTHES, 0)
		return {ShowAnim = true}
	end
end
mod:AddCallback(ModCallbacks.MC_USE_ITEM, mod.JarOfScythes, JAR_OF_SCYTHES)

function mod:JarOfScythesKill(npc)
	if not npc:GetData().lastDamageWasJarOfScythes and not  npc:GetData().noJarOfScythes and lib.AllowOnDeathEffect(npc) then
		for _, player in pairs(lib.GetPlayers()) do
			if player:HasCollectible(JAR_OF_SCYTHES) then
				local data = mod:GetPersistentPlayerData(player)
				data.jarOfScythesKills = (data.jarOfScythesKills or 0) + 1
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE, mod.JarOfScythesKill)

function mod:JarOfScythesUpdate(player)
	if player:HasCollectible(JAR_OF_SCYTHES) then
		local data = mod:GetPersistentPlayerData(player)
		
		local kills = data.jarOfScythesKills or 0
		
		while kills >= NUM_KILLS_PER_STOCK do
			kills = kills - NUM_KILLS_PER_STOCK
			data.jarOfScythesStocks = math.min((data.jarOfScythesStocks or 0) + 1, MAX_SCYTHE_STOCKS)
		end
		
		data.jarOfScythesKills = kills
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.JarOfScythesUpdate)

mod.RenderActive:Add({
	ItemID = JAR_OF_SCYTHES,
	Anm2Filename = "gfx/samael_counter.anm2",
	Update = function(player, slot, data)
		local frame = math.min(mod:GetPersistentPlayerData(player).jarOfScythesStocks or 0, 20)
		data.Sprite:SetFrame(frame)
	end,
})

function mod:SlashEnemy(player, enemy, damage)
	catchScytheDamage = true
	enemy:TakeDamage(damage, DamageFlag.DAMAGE_IGNORE_ARMOR, EntityRef(player), 0)
	catchScytheDamage = false
	
	sfxManager:Play(SoundEffect.SOUND_TOOTH_AND_NAIL, 0.85, 0, false, 1.5)
	
	local slashSizeFactor = enemy.Size
	local pos = enemy.Position - Vector(0, slashSizeFactor * 0.5) + lib.NormalVector:Resized(((Random()%101)/101)*slashSizeFactor*0.75):Rotated(Random() % 360)
	local v = Vector(0, 20 + slashSizeFactor * 0.75):Rotated(Random() % 360)
	local slashWidth = 0.85
	mod:SpawnSlashEffect(player, pos, v, slashWidth, enemy, true, (not lib.IsSamael(player)) and SLASH_COLOR or nil)
end

local function IsSamaelSpinningScythe(npc)
	return npc and npc.Type == 951 and npc.Variant == 41 and npc.SubType == 1 and npc.Parent and npc.Parent:ToPlayer() and npc:GetData().isSamaelSpinningScythe
end

function mod:SpinningScytheCollision(npc, collider)
	if IsSamaelSpinningScythe(npc) or IsSamaelSpinningScythe(collider) then
		return true
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_PRE_NPC_COLLISION, CallbackPriority.IMPORTANT, mod.SpinningScytheCollision)

mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, function(_, npc, damageAmount, damageFlags, damageSource)
	if IsSamaelSpinningScythe(npc) then
		return false
	end
	
	npc:GetData().lastDamageWasJarOfScythes = catchScytheDamage
end)

local function CalcSpinningScytheDamage(player, entity)
	return lib.CalcUnmodifiedDps(player) --* 0.5
end

local antiRecursion = false

function mod:SpinningScytheUpdate(npc)
	if not IsSamaelSpinningScythe(npc) then return end
	
	--[[if not antiRecursion then
		antiRecursion = true
		npc:Update()
		antiRecursion = false
	end]]
	
	if npc.FrameCount < 4 then return end
	
	local player = npc.Parent:ToPlayer()
	
	for _, entity in pairs(Isaac.FindInRadius(npc.Position, 60, EntityPartition.ENEMY)) do
		local d = entity:GetData()
		if not d.hitBySamaelSpinningScythes then
			d.hitBySamaelSpinningScythes = {}
		end
		local tab = d.hitBySamaelSpinningScythes
		local key = npc.InitSeed --npc:GetData().samaelSpinningScytheKey
		
		if entity:IsVulnerableEnemy() and not entity:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) and not tab[key] then
			local damage = CalcSpinningScytheDamage(player, entity)
			mod:SlashEnemy(player, entity, damage)
			tab[key] = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SpinningScytheUpdate, 951)

local queuedSpinningScythesPlayer
function mod:HandleQueuedSpinningScythes()
	if game:GetRoom():GetFrameCount() > 1 and queuedSpinningScythesPlayer then
		mod:TriggerSpinningScythes(queuedSpinningScythesPlayer)
		queuedSpinningScythesPlayer = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.HandleQueuedSpinningScythes)

function mod:TriggerSpinningScythes(player, numScythes)
	player = player or Isaac.GetPlayer(0)
	numScythes = numScythes or 3
	
	if game:GetRoom():GetFrameCount() <= 1 then
		queuedSpinningScythesPlayer = player
		return
	end
	
	local startAngle = Random() % 360
	
	for i=1, numScythes do
		local scythe = Isaac.Spawn(951, 41, 1, player.Position, lib.ZeroVector, player):ToNPC()
		scythe:AddEntityFlags(EntityFlag.FLAG_FRIENDLY)
		scythe:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		--scythe.TargetPosition = Vector(60, -90 + 120 * (i-1))
		local angle = startAngle + 360 * (i-1) / numScythes
		scythe.TargetPosition = Vector(60, angle)
		scythe.Parent = player
		scythe.V1 = Vector(480, 280)
		scythe.EntityCollisionClass = 0
		--scythe.V2 = Vector(0.1, 0)
		--scythe.State = 3
		
		local data = scythe:GetData()
		--data.samaelSpinningScytheKey = game:GetFrameCount()
		data.isSamaelSpinningScythe = true
		
		--scythe:Update()
		scythe:SetColor(lib.InvisibleColor, 13, 1, true, true)
	end
	
	sfxManager:Play(SoundEffect.SOUND_DEATH_SKULL_SUMMON_END, 0.5, 0, false, 1)
end

function mod:TestSpinningScythes()
	for i=1, 10 do
		lib.ScheduleForUpdate(mod.TriggerSpinningScythes, i*5)
	end
end
