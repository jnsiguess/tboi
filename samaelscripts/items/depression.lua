local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local DEPRESSION = mod.ITEMS.DEPRESSION
local DEPRESSION_FOSSIL = mod.ITEMS.DEPRESSION_FOSSIL

local antiRecursionBit = false

function mod:DepressionDamage(player, damage, damageFlags, damageSourceRef)
	local player = player:ToPlayer()
	local data = player:GetData()
	
	local isSelfDamage = damageSourceRef.Type == EntityType.ENTITY_PLAYER
	local isCurseRoomDamage = damageFlags & DamageFlag.DAMAGE_CURSED_DOOR ~= 0
	local isRedHeartDamage = damageFlags & DamageFlag.DAMAGE_RED_HEARTS ~= 0
	local isFakeDamage = damageFlags & DamageFlag.DAMAGE_FAKE ~= 0
	
	if lib.HasItem(player, DEPRESSION) and not data.fakeDeathPillActive and ((not isSelfDamage and not isRedHeartDamage and not isCurseRoomDamage) or isFakeDamage) and not antiRecursionBit then
		local twin = player:GetOtherTwin() or data.sodom or data.gomorrah
		if twin and twin:GetData().fakeDeathPillActive then return end
		data.samaelTriggerDepressionEffect = true
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, mod.DepressionDamage, EntityType.ENTITY_PLAYER)

-- Keyframes for player sprite warping to simulate a shooting animation.
local DepressionShootAnim = {
	{ ScaleX=1, ScaleY=1, Duration=3 },
	{ ScaleX=1.3, ScaleY=0.8, Duration=3, Trigger=true  },
	{ ScaleX=0.8, ScaleY=1.3, Duration=3 },
	{ ScaleX=1.1, ScaleY=0.9, Duration=2 },
	{ ScaleX=1, ScaleY=1, Duration=10, End=true },
}

local function FireDepressionProjectiles(player)
	local data = player:GetData()
	local rng = player:GetCollectibleRNG(DEPRESSION)
	
	local mult = 1
	if player:HasTrinket(DEPRESSION_FOSSIL) then
		mult = 0.33 + (FiendFolio and FiendFolio.GetGolemTrinketPower(player, DEPRESSION_FOSSIL) or player:GetTrinketMultiplier(DEPRESSION_FOSSIL))
	end
	if lib.HasItem(player, DEPRESSION) then
		mult = mult + 1
	end
	
	local numTears = math.ceil(lib.CalcUnmodifiedTears(player) * mult) * lib.GetNumProjectiles(player)
	local startingAngle = Vector(1,0):Rotated(rng:RandomInt(360))
	if player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) then
		numTears = numTears + 14
	end
	for i=0, numTears-1 do
		local projVel = startingAngle:Rotated((360 / numTears) * i) * player.ShotSpeed * 10
		if player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) then
			player:FireBrimstone(projVel)
		elseif player:HasWeaponType(WeaponType.WEAPON_BOMBS) then
			player:FireBomb(player.Position, projVel)
		else
			player:FireTear(player.Position, projVel, true, false, false, player, 1.0)
		end
	end
	data.samaelDepressionShotsFired = data.samaelDepressionShotsFired + 1
end

function mod:DepressionUpdate(player)
	local data = player:GetData()
	if not antiRecursionBit and data.samaelTriggerDepressionEffect then
		antiRecursionBit = true
		-- Much of this functionality is shared with Thanatosis pills.
		mod:FakeDeathPill(player, 0, false, true)
		antiRecursionBit = false
	end
	
	if data.samaelDepressionActive then
		if not data.samaelDepressionWasActive then
			data.samaelTriggerDepressionEffect = false
			data.samaelOriginalSpriteScale = Vector(player.SpriteScale.X, player.SpriteScale.Y)
			data.samaelDepressionFireCooldown = 25
			data.samaelDepressionShotsFired = 0
			data.samaelDepressionWasActive = true
		end
		
		-- Make the player shake.
		local x = 1 * math.sin(0.45 * math.pi * game:GetFrameCount())
		local t = 30
		local spriteOffsetOffset = Vector(-15, -4)
		if mod:ShouldDisableDepressionOffset(player) then
			spriteOffsetOffset = lib.ZeroVector
		elseif data.fakeDeathPillTime < t then
			spriteOffsetOffset = lib.Lerp(lib.ZeroVector, spriteOffsetOffset, data.fakeDeathPillTime / t)
		end
		player.SpriteOffset = Vector(x, 0) + spriteOffsetOffset
		
		if data.fakeDeathPillTime == 30 or data.fakeDeathPillTime == 120 then
			sfxManager:Play(SoundEffect.SOUND_SCARED_WHIMPER, 1, 0, false, 1.00)
		end
		
		if data.samaelDepressionFireCooldown and data.samaelDepressionFireCooldown > 0 then
			data.samaelDepressionFireCooldown = data.samaelDepressionFireCooldown - 1
		end
		
		if data.samaelDepressionFireCooldown == 0 then
			data.samaelDepressionState = 1
			data.samaelDepressionFrame = 0
			data.samaelDepressionFireCooldown = nil
		end
		
		if not data.samaelDepressionState then
			data.samaelDepressionState = #DepressionShootAnim
		end
		
		local currentKeyFrame = DepressionShootAnim[data.samaelDepressionState]
		local nextKeyFrame = DepressionShootAnim[data.samaelDepressionState+1]
		
		if currentKeyFrame and nextKeyFrame then
			local x = lib.Lerp(currentKeyFrame.ScaleX, nextKeyFrame.ScaleX, data.samaelDepressionFrame / currentKeyFrame.Duration)
			local y = lib.Lerp(currentKeyFrame.ScaleY, nextKeyFrame.ScaleY, data.samaelDepressionFrame / currentKeyFrame.Duration)
			data.fakeDeathScaleMult = Vector(x, y)
			
			if data.samaelDepressionFrame >= currentKeyFrame.Duration then
				data.samaelDepressionState = data.samaelDepressionState + 1
				data.samaelDepressionFrame = 0
			else
				data.samaelDepressionFrame = data.samaelDepressionFrame + 1
			end
		end
		
		if currentKeyFrame.End then
			if data.samaelDepressionShotsFired >= 4 then
				if data.samaelDepressionFrame >= currentKeyFrame.Duration then
					data.samaelEndDepressionEffect = true
				else
					data.samaelDepressionFrame = data.samaelDepressionFrame + 1
				end
			elseif not data.samaelDepressionFireCooldown then
				data.samaelDepressionFireCooldown = 8
			end
		elseif currentKeyFrame.Trigger and data.samaelDepressionFrame == 0 then
			-- Fire projectiles.
			FireDepressionProjectiles(player)
			lib.ScheduleForUpdate(function()
				FireDepressionProjectiles(player)
			end, 10)
		end
	elseif data.samaelDepressionWasActive then
		data.samaelDepressionState = nil
		player.SpriteOffset = lib.ZeroVector
		player.SpriteScale = data.samaelOriginalSpriteScale
		data.samaelDepressionWasActive = false
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.DepressionUpdate)

function mod:DepressionFix()
	for _, player in pairs(lib.GetPlayers()) do
		if player:GetData().samaelDepressionActive then
			player:GetData().samaelEndDepressionEffect = true
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.DepressionFix)

function mod:DepressionCache(player)
	if lib.HasItem(player, DEPRESSION) then
		local count = player:GetCollectibleNum(DEPRESSION)
		player.MaxFireDelay = lib.TearsUp(player.MaxFireDelay, 0.7 + 0.5 * (count - 1))
	end
	if player:HasTrinket(DEPRESSION_FOSSIL) then
		local power = FiendFolio and FiendFolio.GetGolemTrinketPower(player, DEPRESSION_FOSSIL) or player:GetTrinketMultiplier(DEPRESSION_FOSSIL)
		player.MaxFireDelay = lib.TearsUp(player.MaxFireDelay, 0.5 * power)
	end
	
	local ppData = mod:GetPersistentPlayerData(player)
	if (ppData.depressionFossilBoost or 0) > 0 then
		player.MaxFireDelay = lib.TearsUp(player.MaxFireDelay, ppData.depressionFossilBoost)
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.DepressionCache, CacheFlag.CACHE_FIREDELAY)

local DEPRESSION_FOSSIL_CRUSH_BOOST = 3
local DEPRESSION_FOSSIL_CRUSH_BOOST_DURATION = 2 * 30 * 60 -- 2 minutes

function mod.DepressionFossilCrushEffect(player, spawner)
	sfxManager:Play(SoundEffect.SOUND_GOODEATH)
	local ppData = mod:GetPersistentPlayerData(player)
	ppData.depressionFossilBoost = (ppData.depressionFossilBoost or 0) + DEPRESSION_FOSSIL_CRUSH_BOOST
	player:AddCacheFlags(CacheFlag.CACHE_FIREDELAY)
	player:EvaluateItems()
end

function mod:DepressionFossilUpdate(player)
	local persistentData = mod:GetPersistentPlayerData(player)
	
	if (persistentData.depressionFossilBoost or 0) > 0 then
		persistentData.depressionFossilBoost = persistentData.depressionFossilBoost - (DEPRESSION_FOSSIL_CRUSH_BOOST / DEPRESSION_FOSSIL_CRUSH_BOOST_DURATION)
		player:AddCacheFlags(CacheFlag.CACHE_FIREDELAY)
		player:EvaluateItems()
	end
	
	if not player:HasTrinket(DEPRESSION_FOSSIL) then return end
	
	local data = player:GetData()
	
	if data.fakeDeathPillActive then return end
	
	local timer = persistentData.depressionFossilCountdown
	
	if timer == 0 then
		data.samaelTriggerDepressionEffect = true
	end
	
	if not timer or timer <= 0 then
		local minSeconds = 15
		local maxSeconds = 60
		timer = math.ceil(30 * lib.Lerp(minSeconds, maxSeconds, player:GetTrinketRNG(DEPRESSION_FOSSIL):RandomFloat()))
	end
	
	persistentData.depressionFossilCountdown = timer - 1
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.DepressionFossilUpdate)
