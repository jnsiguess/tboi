local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local FERRYMAN = mod.ENTITIES.FERRYMAN.Var

local FERRYMAN_EFFECT = {
	VARIANT = mod.ENTITIES.FERRYMAN_EFFECT.Var,
	BOAT_BACK = 0,
	LEAVING = 1,
}

local FERRYMANS_OBOLS = mod.ITEMS.FERRYMANS_OBOLS

local LOST_SOUL_BEGGAR = mod.ENTITIES.LOST_SOUL_BEGGAR.Var

local kBoatSpeed = 3
local kTeleportDist = 200

local kPassengerBaseOffset = Vector(20, -4)
local kPassengerAdditionalOffset = Vector(0, -30)

local doingFerrymanTeleport = false
local activeFerryman

function mod:SpawnFerryman(prepaid, pos)
	local ferryman = Isaac.Spawn(EntityType.ENTITY_SLOT, FERRYMAN, prepaid and 1 or 0, pos, lib.ZeroVector, nil)
	mod:FerrymanUpdate(ferryman)
	return ferryman
end

local function DoRoomTransition()
	if activeFerryman then
		activeFerryman:Remove()
		activeFerryman = nil
	end
	
	if mod:IsFragmentRoom() then
		local returnIdx = mod:GetFragmentData().returnIdx or mod:GetLastKnownGridIndex()
		if mod:IsFragmentRoom(returnIdx) or mod:IsDeathDealRoom(returnIdx) then
			returnIdx = game:GetLevel():GetStartingRoomIndex()
		end
		game:StartRoomTransition(returnIdx, 0, RoomTransitionAnim.FADE)
	else
		mod:GoToFragment()
	end
	
	doingFerrymanTeleport = true
end

local function IsCharonOrGoldMemberPresent()
	for _, player in pairs(lib.GetPlayers()) do
		if player:GetTrinketMultiplier(mod.ITEMS.CHARON_CLUB_CARD) >= 2
				or (REVEL and REVEL.Dante and (REVEL.Dante.IsCharon(player) or REVEL.Dante.IsMerged(player))) then
			return true
		end
	end
	return false
end

function mod:FerrymanUpdate(entity)
	local data = entity:GetData()
	local sprite = entity:GetSprite()
	
	local charonPresent = IsCharonOrGoldMemberPresent()
	
	if entity.GridCollisionClass == EntityGridCollisionClass.GRIDCOLL_GROUND then
		for _, pickup in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP)) do
			if pickup.FrameCount <= 1 and pickup.Position:Distance(entity.Position) < 20
					and (pickup.SpawnerType == 0 or pickup.SpawnerType == EntityType.ENTITY_SLOT) then
				pickup:Remove()
			end
		end
		for _, bomb in pairs(Isaac.FindByType(EntityType.ENTITY_BOMB)) do
			if bomb.FrameCount <= 1 and bomb.Position:Distance(entity.Position) < 20
					and (bomb.SpawnerType == 0 or bomb.SpawnerType == EntityType.ENTITY_SLOT) then
				bomb:Remove()
			end
		end
		
		if not mod:IsFragmentRoom() then
			local numWoodParticles = 20
			local radius = 100
			for i=0, numWoodParticles-1 do
				local percent = i / (numWoodParticles-1)
				local pos = Vector(entity.Position.X - (radius * 0.5) + (radius * percent), entity.Position.Y)
				Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.WOOD_PARTICLE, 0, pos, RandomVector() * 2, nil)
			end
			
			local poofPos = entity.Position
			if sprite.FlipX then
				poofPos = poofPos + Vector(15, -20)
			else
				poofPos = poofPos + Vector(-15, -20)
			end
			for i=1,2 do
				local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, i, poofPos, lib.ZeroVector, nil):ToEffect()
				poof:GetSprite().Scale = Vector(0.5, 0.5)
				poof:GetSprite().Color = lib.NewColor(0,0,0,0.5,0,0,0)
			end
			
			lib.DustGibsBurst(entity.Position)
			
			sfxManager:Play(SoundEffect.SOUND_WOOD_PLANK_BREAK)
		end
		
		entity:Remove()
		return
	end
	
	if not data.samaelFerrymanInitialized then
		if entity.Position.X < game:GetRoom():GetCenterPos().X then
			sprite.FlipX = true
		end
		
		entity.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
		entity.SizeMulti = Vector(2, 1)
		
		local origSubType = entity.SubType
		
		if charonPresent or mod:IsFragmentRoom() then
			entity.SubType = 1
		end
		
		if entity.SubType == 1 and ((charonPresent and origSubType == 1) or mod:IsFragmentRoom()) then
			sprite:Play("Wait", true)
		end
		
		if sprite:GetAnimation() == "Wait" and charonPresent then
			sprite:Play("CharonSaluteOar", true)
		end
		
		data.samaelFerrymanInitialized = true
	end
	
	if not data.samaelFerrymanBoatBack or not data.samaelFerrymanBoatBack:Exists() then
		data.samaelFerrymanBoatBack = Isaac.Spawn(EntityType.ENTITY_EFFECT, FERRYMAN_EFFECT.VARIANT, FERRYMAN_EFFECT.BOAT_BACK, entity.Position, lib.ZeroVector, entity):ToEffect()
		data.samaelFerrymanBoatBack:FollowParent(entity)
	end
	data.samaelFerrymanBoatBack:Update()
	
	if mod:IsFragmentEntrance() then
		local fragmentData = mod:GetFragmentData()
		local souls = fragmentData.soulsInBoat or {}
		for i, head in pairs(souls) do
			if not data["soul"..i] then
				data["soul"..i] = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, entity.Position, lib.ZeroVector, entity):ToEffect()
				data["soul"..i].State = i
				data["soul"..i]:GetData().lostSoulOverrideHead = head
			end
			--[[if i <= numSouls and not data["soul"..i] then
				data["soul"..i] = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, entity.Position, lib.ZeroVector, entity):ToEffect()
				data["soul"..i].State = i
			elseif i > numSouls and data["soul"..i] then
				data["soul"..i]:Remove()
				data["soul"..i] = nil
			end]]
		end
	end
	
	if sprite:IsFinished("CharonSalutePrep") then
		sprite:Play("Prep", true)
		sprite:SetFrame(22)
	elseif sprite:IsFinished("CharonSaluteOar") then
		sprite:Play("Wait", true)
	end
	
	local isPaid = entity.SubType == 1
	
	if isPaid and (sprite:IsPlaying("Lookup") or sprite:IsPlaying("Read")) then
		local anim = charonPresent and "CharonSalutePrep" or "LookupToPrep"
		sprite:Play(anim, true)
	elseif sprite:IsFinished("LookupToPrep") then
		sprite:Play("Prep", true)
	elseif sprite:IsFinished("Lookup") or sprite:IsFinished("ReadFlip") or sprite:IsFinished("KeepReading") then
		sprite:Play("Read", true)
	elseif sprite:IsFinished("Read") then
		if Random() % 2 == 0 then
			sprite:Play("ReadFlip", true)
		else
			sprite:Play("Read", true)
		end
	elseif sprite:IsFinished("Pay5") or sprite:IsFinished("Pay10") then
		if isPaid then
			sprite:Play("Prep", true)
		else
			sprite:Play("KeepReading", true)
		end
	elseif sprite:IsFinished("Prep") then
		sprite:Play("Wait", true)
	end
	
	local cost = 10
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(mod.ITEMS.CHARON_CLUB_CARD) then
			cost = 5
			break
		end
	end
	
	if not isPaid then
		if sprite:GetAnimation() ~= "Pay5" and sprite:GetAnimation() ~= "Pay10" then
			for _, player in pairs(Isaac.FindInRadius(entity.Position, 15, EntityPartition.PLAYER)) do
				player = player:ToPlayer()
				if player:Exists() and player:GetNumCoins() >= cost then
					player:AddCoins(-cost)
					sprite:Play("Pay" .. cost, true)
					entity.SubType = 1
					sfxManager:Play(SoundEffect.SOUND_SCAMPER)
					break
				end
			end
		end
	end
	
	if not activeFerryman and sprite:GetAnimation() == "Wait" and game:GetRoom():GetFrameCount() > 30 then
		for _, nearbyPlayer in pairs(Isaac.FindInRadius(entity.Position, 15, EntityPartition.PLAYER)) do
			if nearbyPlayer:Exists() then
				local playerOffset = kPassengerBaseOffset
				local passengerNum = 1
				for _, player in pairs(lib.GetPlayers()) do
					local pData = player:GetData()
					pData.samaelFerrymanOriginalEcc = player.EntityCollisionClass
					pData.samaelFerrymanOriginalGcc = player.GridCollisionClass
					pData.samaelFerrymanOriginalDepthOffset = player.DepthOffset
					if pData.MaliceHidden then
						pData.samaelFerrymanPassengerOffset = kPassengerBaseOffset
					else
						pData.samaelFerrymanPassengerOffset = playerOffset
						local scale = player.SpriteScale.X
						if pData.MaliceMinion then
							scale = scale * 0.5
						end
						playerOffset = playerOffset + kPassengerAdditionalOffset * scale
					end
					passengerNum = passengerNum + 1
					player:AnimateAppear()
					player:GetSprite():Play("Jump", true)
				end
				
				data.samaelFerrymanOriginalPosition = entity.Position
				activeFerryman = entity
				break
			end
		end
	end
	
	if not isPaid or sprite:GetOverlayAnimation() ~= "" then
		local currentOverlay = sprite:GetOverlayAnimation()
		local desiredOverlay = "Sign" .. cost
		
		if isPaid then
			desiredOverlay = desiredOverlay .. "Disappear"
		end
		if sprite.FlipX then
			desiredOverlay = desiredOverlay .. "_Flipped"
		end
		
		if currentOverlay ~= desiredOverlay then
			sprite:PlayOverlay(desiredOverlay, true)
		end
	end
	
	if activeFerryman and GetPtrHash(activeFerryman) == GetPtrHash(entity) then
		if not sprite:IsPlaying("Row") then
			sprite:Play("Row", true)
		end
		
		entity.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		
		local speed = data.samaelFerrymanSpeed or 0
		local targetSpeed = kBoatSpeed
		if sprite.FlipX then
			targetSpeed = targetSpeed * -1
		end
		speed = lib.Lerp(speed, targetSpeed, 0.05)
		entity.TargetPosition = entity.TargetPosition + Vector(speed, 0)
		data.samaelFerrymanSpeed = speed
		
		local room = game:GetRoom()
		
		local clampedPos = room:GetClampedPosition(entity.Position, 0)
		local dist = entity.Position:Distance(clampedPos)
		
		if dist > 0 then
			local x = 1 - (dist / kTeleportDist)
			local c = Color(x,x,x,1)
			entity:SetColor(c, 2, 1, false, true)
			data.samaelFerrymanBoatBack:SetColor(c, 2, 1, false, true)
			data.samaelFerrymanFade = x
			
			if dist > kTeleportDist then
				entity.TargetPosition = data.samaelFerrymanOriginalPosition
				DoRoomTransition()
			end
		end
		
		-- Keep doors closed.
		for i=0, 7 do
			local door = room:GetDoor(i)
			if door and door:IsOpen() then
				door:Close(true)
			end
		end
		room:KeepDoorsClosed()
	else
		entity.EntityCollisionClass = EntityCollisionClass.ENTCOLL_PLAYERONLY
		
		if sprite:IsPlaying("Row") then
			sprite:Play("Wait", true)
		end
	end
end
mod:AddPostSlotUpdateFunc(mod.FerrymanUpdate, FERRYMAN)

mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, function()
	for _, ferryman in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT, FERRYMAN)) do
		local pos = ferryman:GetData().samaelFerrymanOriginalPosition
		if pos then
			ferryman.Position = pos
			ferryman.TargetPosition = pos
		end
	end
end)

function mod:FerrymanBoatBackInit(eff)
	if eff.SubType ~= FERRYMAN_EFFECT.BOAT_BACK then return end
	
	eff:GetSprite():Play("BoatBack", true)
	eff.DepthOffset = -10
	
	if mod:IsFragmentRoom() then
		eff:GetSprite():ReplaceSpritesheet(12, "gfx/samael_null.png")
		eff:GetSprite():LoadGraphics()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.FerrymanBoatBackInit, FERRYMAN_EFFECT.VARIANT)

function mod:FerrymanBoatBack(eff)
	if eff.SubType ~= FERRYMAN_EFFECT.BOAT_BACK then return end
	
	if not eff.Parent or not eff.Parent:Exists() or not (
			(eff.Parent.Type == EntityType.ENTITY_SLOT and eff.Parent.Variant == FERRYMAN)
			or (eff.Parent.Type == EntityType.ENTITY_EFFECT and eff.Parent.Variant == FERRYMAN_EFFECT.VARIANT)) then
		eff:Remove()
		return
	end
	
	local sprite = eff:GetSprite()
	sprite.FlipX = eff.Parent:GetSprite().FlipX
	if sprite:GetAnimation() ~= "BoatBack" then
		sprite:Play("BoatBack", true)
	end
	eff.DepthOffset = -10
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.FerrymanBoatBack, FERRYMAN_EFFECT.VARIANT)

function mod:FerrymanNewRoom()
	if doingFerrymanTeleport then
		doingFerrymanTeleport = false
		
		if not mod:IsFragmentRoom() then
			local room = game:GetRoom()
			local vec = Isaac.GetPlayer(0).Position - room:GetCenterPos()
			local offset = vec:Resized(50) * Vector(1.0, 0.75)
			local pos = room:GetClampedPosition(room:GetCenterPos() + vec:Resized(9999), 0) + offset
			local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, FERRYMAN_EFFECT.VARIANT, FERRYMAN_EFFECT.LEAVING, pos, lib.ZeroVector, nil)
			local back = Isaac.Spawn(EntityType.ENTITY_EFFECT, FERRYMAN_EFFECT.VARIANT, FERRYMAN_EFFECT.BOAT_BACK, eff.Position, lib.ZeroVector, eff):ToEffect()
			back:FollowParent(eff)
			eff:GetData().samaelFerrymanBoatBack = back
			
			local fragmentData = mod:GetFragmentData()
			if fragmentData.soulsInBoat then
				for i, head in pairs(fragmentData.soulsInBoat) do
					local soul = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, eff.Position, lib.ZeroVector, eff):ToEffect()
					soul.State = i
					soul:GetData().lostSoulOverrideHead = head
				end
			end
			fragmentData.soulsInBoat = {}
		end
	end
	
	for _, entity in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT, FERRYMAN)) do
		mod:FerrymanUpdate(entity)
	end
	
	if activeFerryman then
		activeFerryman = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.FerrymanNewRoom)

function mod:FerrymanLeavingInit(eff)
	if eff.SubType ~= FERRYMAN_EFFECT.LEAVING then return end
	
	local sprite = eff:GetSprite()
	
	if IsCharonOrGoldMemberPresent() then
		sprite:Play("CharonSaluteOar", true)
	else
		sprite:Play("Wave", true)
	end
	
	if Isaac.GetPlayer(0).Position.X < game:GetRoom():GetCenterPos().X then
		sprite.FlipX = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.FerrymanLeavingInit, FERRYMAN_EFFECT.VARIANT)

function mod:FerrymanLeaving(eff)
	if eff.SubType ~= FERRYMAN_EFFECT.LEAVING then return end
	
	local data = eff:GetData()
	local sprite = eff:GetSprite()
	
	if sprite:IsFinished("Wave") or sprite:IsFinished("CharonSaluteOar") then
		sprite:Play("Row", true)
	end
	
	if sprite:IsPlaying("Row") then
		local targetVel
		if sprite.FlipX then
			targetVel = Vector(-kBoatSpeed, 0)
		else
			targetVel = Vector(kBoatSpeed, 0)
		end
		eff.Velocity = lib.Lerp(eff.Velocity, targetVel, 0.1)
		
		local room = game:GetRoom()
		
		local clampedPos = room:GetClampedPosition(eff.Position, 0)
		local dist = eff.Position:Distance(clampedPos)
		
		if dist > 0 then
			local x = 1 - (dist / kTeleportDist)
			x = lib.Lerp(data.samaelFerrymanFade or 1, x, 0.3)
			data.samaelFerrymanFade = x
			
			local c = Color(x,x,x,1)
			eff:SetColor(c, 2, 1, false, true)
			if eff:GetData().samaelFerrymanBoatBack then
				eff:GetData().samaelFerrymanBoatBack:SetColor(c, 2, 1, false, true)
			end
			
			if dist > kTeleportDist then
				eff:Remove()
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.FerrymanLeaving, FERRYMAN_EFFECT.VARIANT)

function mod:FerrymanPlayerUpdate(player)
	local data = player:GetData()
	
	if activeFerryman and activeFerryman:Exists() then
		local offset = data.samaelFerrymanPassengerOffset or kPassengerBaseOffset
		if activeFerryman:GetSprite().FlipX then
			offset = offset * Vector(-1, 1)
		end
		
		local lerpStrength = 0.75
		if player:GetSprite():IsPlaying("Jump") then
			lerpStrength = 0.2
		elseif lib.IsTaintedSamael(player) then
			player:AnimateAppear()
			player:GetSprite():SetFrame(30)
		end
		
		player.Position = lib.Lerp(player.Position, activeFerryman.Position + offset, lerpStrength)
		player.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		player.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_NONE
		player.CanFly = true
		player.PositionOffset = -player:GetFlyingOffset()
		player.DepthOffset = -1
		if player:IsExtraAnimationFinished() then
			player:GetSprite():SetFrame(0)
		end
		
		local x = activeFerryman:GetData().samaelFerrymanFade
		if x then
			player:SetColor(Color(x,x,x,1), 1, 1, false, true)
		end
		
		if player.ControlsEnabled and REVEL and REVEL.Dante and REVEL.Dante.IsMerged(player) then
			REVEL.Dante.Callbacks.Partner_PostUpdate(player, data, Direction.RIGHT)
			--[[if x and data.CharonIncubus then
				data.CharonIncubus:SetColor(Color(x,x,x,1), 1, 1, false, true)
				--data.CharonIncubus.Color = Color(x,x,x,1)
			end]]
		end
		
		player.ControlsEnabled = false
		data.samaelFerrymanWasOnBoat = true
	elseif data.samaelFerrymanWasOnBoat then
		if data.samaelFerrymanOriginalEcc then
			player.EntityCollisionClass = data.samaelFerrymanOriginalEcc
			data.samaelFerrymanOriginalEcc = nil
		end
		if data.samaelFerrymanOriginalGcc then
			player.GridCollisionClass = data.samaelFerrymanOriginalGcc
			data.samaelFerrymanOriginalGcc = nil
		end
		if data.samaelFerrymanOriginalDepthOffset then
			player.DepthOffset = data.samaelFerrymanOriginalDepthOffset
			data.samaelFerrymanOriginalDepthOffset = nil
		end
		player.PositionOffset = lib.ZeroVector
		player.ControlsEnabled = true
		data.samaelFerrymanWasOnBoat = nil
		player:AddCacheFlags(CacheFlag.CACHE_FLYING)
		player:EvaluateItems()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.FerrymanPlayerUpdate)

--------------------------------------------------
-- FAKE TRAP DOOR
--------------------------------------------------

--[[local STATIC_HELPER = {
	TYPE = Isaac.GetEntityTypeByName("(Samael) Static Helper"),
	VARIANT = Isaac.GetEntityVariantByName("(Samael) Static Helper"),
	SUBTYPE = 617,
}

function mod:FakeTrapDoorInit(entity)
	if entity.Variant == STATIC_HELPER.VARIANT and entity.SubType == STATIC_HELPER.SUBTYPE then
		entity:GetSprite():Play("Start", true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.FakeTrapDoorInit, STATIC_HELPER.TYPE)

function mod:FakeTrapDoor(entity)
	if entity.Variant == STATIC_HELPER.VARIANT and entity.SubType == STATIC_HELPER.SUBTYPE then
		local data = entity:GetData()
		if not data.samaelTargetPos then
			data.samaelTargetPos = entity.Position
			sfxManager:Play(542, 0.5, 0, false, 1)
		end
		entity.Velocity = lib.ZeroVector
		entity.Position = data.samaelTargetPos
		
		local shakeStart = 10
		if entity.FrameCount >= shakeStart then
			local x = entity.FrameCount - shakeStart
			entity.SpriteOffset = Vector(x * math.sin(x*2) / 5, 0)
		end
		entity.DepthOffset = -100
		
		local sprite = entity:GetSprite()
		
		if sprite:IsFinished("Start") then
			Isaac.Spawn(EntityType.ENTITY_PORTAL, 0, 0, entity.Position, lib.ZeroVector, nil)
			sprite:Play("End", true)
		elseif sprite:IsFinished("End") then
			entity:Remove()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.FakeTrapDoor, STATIC_HELPER.TYPE)

function mod:FakeTrapDoorCollision(entity)
	if entity.Variant == STATIC_HELPER.VARIANT and entity.SubType == STATIC_HELPER.SUBTYPE then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_NPC_COLLISION, mod.FakeTrapDoorCollision, STATIC_HELPER.TYPE)]]

--------------------------------------------------
-- Lost Soul Beggar
--------------------------------------------------

function mod:LostSoulBeggarDeath(entity)
	for i=0, 3 do
		local pos = entity.Position
		local particle = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.BLOOD_PARTICLE, 0, pos, RandomVector() * 2, nil)
		particle.Color = Color(1,1,1,0.2,1,1,1)
	end
	
	local pos = entity.Position + entity.SpriteOffset
	
	local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 17, 0, pos, lib.ZeroVector, nil):ToEffect()
	poof:GetSprite().Scale = Vector(0.5, 0.5)
	poof:GetSprite().Color = Color(1,1,1,0.5,1,1,1)
	
	lib.DustGibsBurst(pos)
	
	sfxManager:Play(SoundEffect.SOUND_ISAACDIES, 1, 0, false, 1.2)
end

function mod:LostSoulBeggar(entity)
	local data = lib.GetOrInit(mod:GetFragmentData(), "LOST_SOUL")
	
	-- Death
	if entity.GridCollisionClass == EntityGridCollisionClass.GRIDCOLL_GROUND then
		for _, pickup in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP)) do
			if pickup.FrameCount <= 1 and pickup.Position:Distance(entity.Position) < 20
					and (pickup.SpawnerType == 0 or pickup.SpawnerType == EntityType.ENTITY_SLOT) then
				pickup:Remove()
			end
		end
		
		mod:LostSoulBeggarDeath(entity)
		
		data.Killed = true
		entity:Remove()
	end
	
	local sprite = entity:GetSprite()
	
	if sprite:GetAnimation() == "Idle" then
		if entity.SubType == 0 then
			for _, player in pairs(Isaac.FindInRadius(entity.Position, entity.Size, EntityPartition.PLAYER)) do
				player = player:ToPlayer()
				if player:Exists() and player:GetNumCoins() >= 5 then
					player:AddCoins(-5)
					sprite:Play("PayPrize", true)
					entity.SubType = 1
					sfxManager:Play(SoundEffect.SOUND_SCAMPER)
					data.Paid = true
					break
				end
			end
		elseif entity.SubType == 1 then
			sprite:Play("Prize", true)
		else
			if entity.SubType == 2 then
				data.Left = true
			end
			entity:Remove()
		end
	end
	
	if sprite:IsFinished("PayPrize") or sprite:IsFinished("PayNothing") then
		sprite:Play("Prize", true)
	end
	
	if sprite:IsEventTriggered("Prize") then
		sfxManager:Play(SoundEffect.SOUND_SLOTSPAWN)
		for i=1, 3 do
			Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, 3, entity.Position, RandomVector() * 3, nil)
		end
		data.PaidOut = true
		entity.SubType = 2
	end
	
	if sprite:IsFinished("Prize") then
		sprite:Play("Teleport", true)
	end
	
	if sprite:IsFinished("Teleport") then
		data.Left = true
		entity:Remove()
	end
end
mod:AddPostSlotUpdateFunc(mod.LostSoulBeggar, LOST_SOUL_BEGGAR)

--------------------------------------------------
-- Obols
--------------------------------------------------

function mod:FerrymansObols(_, player, useFlags)
	if mod:IsFragmentRoom() then
		lib.RefundInvalidCardUse(player, FERRYMANS_OBOLS, useFlags)
		return false
	end
	
	local pos = Isaac.GetFreeNearPosition(player.Position, 30)
	mod:SpawnFerryman(true, pos)
	
	sfxManager:Play(SoundEffect.SOUND_SUMMONSOUND)
	Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, pos, lib.ZeroVector, nil)
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_CARD, CallbackPriority.EARLY, mod.FerrymansObols, FERRYMANS_OBOLS)

--------------------------------------------------
-- Static Gaper
--------------------------------------------------

--[[local STATIC_GAPER = {
	TYPE = Isaac.GetEntityTypeByName("(Samael) Static Gaper"),
	VARIANT = Isaac.GetEntityVariantByName("(Samael) Static Gaper"),
	SUBTYPE = 618,
}

function mod:StaticGaper(entity)
	if entity.Variant == STATIC_GAPER.VARIANT and entity.SubType == STATIC_GAPER.SUBTYPE then
		entity:GetSprite():Play("HeadTransform", true)
		entity.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_GROUND
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.StaticGaper, STATIC_GAPER.TYPE)

function mod:StaticGaper(entity)
	if entity.Variant == STATIC_GAPER.VARIANT and entity.SubType == STATIC_GAPER.SUBTYPE then
		local data = entity:GetData()
		
		if entity.FrameCount > 60 then
			data.staticGaperTransforming = true
		end
		
		if entity.FrameCount > 200 then
			game:RerollEnemy(entity)
			return
		end
		
		local target = entity:GetPlayerTarget()
		
		local hasDirectPath = game:GetRoom():CheckLine(entity.Position, target.Position, 1)
		if hasDirectPath then
			targetVel = (target.Position - entity.Position):Resized(3.5)
			entity.Velocity = lib.Lerp(entity.Velocity, targetVel, 0.1)
		else
			entity.Pathfinder:FindGridPath(target.Position, 0.5, 0, true)
		end
		
		local sprite = entity:GetSprite()
		local dir = lib.GetDirectionFromVector(entity.Velocity)
		
		if data.staticGaperTransforming and (sprite:IsFinished("WalkVertTransform") or sprite:IsFinished("WalkVertTransform")) then
			data.staticGaperTransformed = true
		end
		
		local anim
		local flipped = false
		
		if dir == Direction.UP then
			anim = "WalkVert"
		elseif dir == Direction.DOWN then
			anim = "WalkVert"
		elseif dir == Direction.LEFT then
			anim = "WalkHori"
			flipped = true
		elseif dir == Direction.RIGHT then
			anim = "WalkHori"
		end
		
		if data.staticGaperTransformed then
			anim = anim .. "Transformed"
		elseif data.staticGaperTransforming then
			anim = anim .. "Transform"
		end
		
		if sprite:GetAnimation() ~= anim then
			local frame = sprite:GetFrame()
			sprite:Play(anim, true)
			sprite:SetFrame(frame)
			sprite.FlipX = flipped
		end
		
		local overlay = "Head"
		
		if data.staticGaperTransformed then
			overlay = overlay .. "Transformed"
		elseif data.staticGaperTransforming then
			overlay = overlay .. "Transform"
		end
		
		if sprite:GetOverlayAnimation() ~= overlay then
			sprite:PlayOverlay(overlay, true)
		end
		
		entity.State = 4
		
		if data.staticGaperTransformed then
			if entity.FrameCount % 6 == 0 then
				local creep = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.CREEP_STATIC, 0, entity.Position, lib.ZeroVector, entity):ToEffect()
				creep.Timeout = 25
			end
		end
		
		--return true
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.StaticGaper, STATIC_GAPER.TYPE)]]