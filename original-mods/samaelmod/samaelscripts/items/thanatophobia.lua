local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local THANATOPHOBIA = mod.ITEMS.THANATOPHOBIA

local kYellEffectDuration = 20
local kMaxHealthToActivate = 12
local kEnemyKnockbackForce = 25
local kProjectileKnockbackForce = 5
--local kDealDamage = false

local function SpawnEffect(player, effectSubType, minRadius, maxRadius, lifeSpan)
	local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BIG_ATTRACT, effectSubType, player.Position, lib.ZeroVector, player):ToEffect()
	eff.MinRadius = minRadius
	eff.MaxRadius = maxRadius
	eff.LifeSpan = lifeSpan
	eff.Timeout = lifeSpan
	eff:FollowParent(player)
	eff.Visible = false
	eff:Update()
	eff.Visible = true
	
	return eff
end

local function DoYellEffect(player, scale)
	local rng = player:GetCollectibleRNG(CollectibleType.COLLECTIBLE_LARYNX)
	
	SpawnEffect(player, 10, 0, 10 * scale, kYellEffectDuration)
	SpawnEffect(player, 10, 5 * scale, 15 * scale, kYellEffectDuration)
	SpawnEffect(player, 10, 0, 25 * scale, kYellEffectDuration)
	
	for i=1, 2 + rng:RandomInt(3) + math.floor(scale) do
		SpawnEffect(player, 11, 10 * rng:RandomFloat(), 15 * scale + 15 * rng:RandomFloat(), 5 + math.floor(5 * scale) + rng:RandomInt(10))
	end
	
	game:ShakeScreen(math.ceil(6 * scale))
	
	local radius = 90 * scale
	
	for _, entity in pairs(Isaac.FindInRadius(player.Position, radius, EntityPartition.ENEMY)) do
		--[[if entity:IsVulnerableEnemy() and kDealDamage then
			local dist = math.min(entity.Position:Distance(player.Position), radius)
			local magnitude = (radius - dist) / radius
			local dmg = lib.Lerp(1, math.max(player.Damage * scale, 3), magnitude)
			entity:TakeDamage(dmg, 0, EntityRef(player), 0)
		else]]
		if entity.Type == EntityType.ENTITY_FIREPLACE then
			entity:Kill()
		end
	end
	
	game:ButterBeanFart(player.Position, radius, player, false, true)
	
	--sfxManager:Play(SoundEffect.SOUND_DEMON_HIT, 1.0, 0, false, 0.85)
	sfxManager:Play(SoundEffect.SOUND_ISAAC_ROAR, 1.0, 0, false, 1.0)
end

local function GetTotalHearts(player)
	return player:GetHearts() + player:GetSoulHearts() + player:GetBoneHearts()
end

function mod:ThanatophobiaOnDamage(player, damage, damageFlags, damageSourceRef)
	local player = player:ToPlayer()
	
	if player and lib.HasItem(player, THANATOPHOBIA) then
		local pData = player:GetData()
		local hearts = GetTotalHearts(player)
		
		pData.thanatophobiaPreDamageHearts = math.max(hearts, pData.thanatophobiaPreDamageHearts or 1)
		pData.thanatophobiaTookFakeDamage = damageFlags & DamageFlag.DAMAGE_FAKE ~= 0
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, mod.ThanatophobiaOnDamage, EntityType.ENTITY_PLAYER)

-- Not accounting for >12 hearts
local function GetThanatophobiaScale(player)
	local hearts = GetTotalHearts(player)
	if hearts > kMaxHealthToActivate then
		return 0
	else
		return lib.Lerp(3.0, 1.0, (GetTotalHearts(player)-1) / (kMaxHealthToActivate-1))
	end
end

local function GetThanatophobiaDamageBonus(player)
	local hasMantle = player:GetEffects():HasCollectibleEffect(CollectibleType.COLLECTIBLE_HOLY_MANTLE)
	if not lib.HasItem(player, THANATOPHOBIA) or hasMantle then
		return 0
	else
		return GetThanatophobiaScale(player) - 1
	end
end

function mod:ThanatophobiaUpdate(player)
	local pData = player:GetData()
	local hearts = GetTotalHearts(player)
	
	local hasMantle = player:GetEffects():HasCollectibleEffect(CollectibleType.COLLECTIBLE_HOLY_MANTLE)
	
	local lostHealth = pData.thanatophobiaPreDamageHearts and pData.thanatophobiaPreDamageHearts > hearts
	local tookFakeDamage = pData.thanatophobiaTookFakeDamage
	local lostMantle = pData.thanatophobiaHadHolyMantle and not hasMantle
	
	if lib.HasItem(player, THANATOPHOBIA) and (lostHealth or tookFakeDamage or lostMantle) and hearts <= kMaxHealthToActivate then
		DoYellEffect(player, GetThanatophobiaScale(player))
	end
	
	if pData.thanatophobiaAppliedDamageBonus ~= GetThanatophobiaDamageBonus(player) then
		player:AddCacheFlags(CacheFlag.CACHE_DAMAGE)
		player:EvaluateItems()
	end
	
	pData.thanatophobiaPreDamageHearts = nil
	pData.thanatophobiaTookFakeDamage = nil
	pData.thanatophobiaHadHolyMantle = hasMantle
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.ThanatophobiaUpdate)

function mod:ThanatophobiaDamageBonus(player)
	local pData = player:GetData()
	
	if lib.HasItem(player, THANATOPHOBIA) and GetTotalHearts(player) <= kMaxHealthToActivate then
		local bonus = GetThanatophobiaDamageBonus(player)
		player.Damage = player.Damage + bonus
		pData.thanatophobiaAppliedDamageBonus = bonus
	else
		pData.thanatophobiaAppliedDamageBonus = 0
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.ThanatophobiaDamageBonus, CacheFlag.CACHE_DAMAGE)