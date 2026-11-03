local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local wisp
local doingTeaser = nil
local pulls = 0
local frames = 0

local eType = 1000
local eVariant = 6627
local eSubType = 8

local tvPos = Vector(320, 160)

local couch = false

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	doingTeaser = nil
end)

function mod:StartTeaser()
	for _, ent in pairs(Isaac.FindByType(960, 4)) do
		ent:Remove()
	end
	
	local tv = Isaac.FindByType(950,1)[1] or Isaac.Spawn(950, 1, 0, tvPos, Vector.Zero, nil)
	tv.MaxHitPoints = 1
	tv.HitPoints = 1
	tv.SpriteOffset = Vector(0, -10)
	tv:AddEntityFlags(EntityFlag.FLAG_DONT_COUNT_BOSS_HP)
	
	Isaac.GetPlayer().Position = Vector(320, 227)
	
	for _, ent in pairs(Isaac.FindByType(eType, eVariant, eSubType)) do
		ent:Remove()
	end
	--mod:GetFragmentData().CurrentRoomSeed = 0
	--local soul = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, game:GetRoom():GetCenterPos() + Vector(200,-125))
	local soul = Isaac.Spawn(eType, eVariant, eSubType, game:GetRoom():GetCenterPos() + Vector(200, -125), Vector.Zero, nil)
	local sprite = soul:GetSprite()
	sprite:Play("TeaserPull", true)
	sprite:PlayOverlay("TeaserBody", true)
	sprite:SetOverlayRenderPriority(true)
	pulls = 0
	soul:GetData().tvdrag = true
	
	--[[local shadow = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, game:GetRoom():GetCenterPos()):ToEffect()
	shadow:GetSprite():Play("TeaserShadow", true)
	shadow:GetSprite():PlayOverlay("TeaserShadowBody", true)
	shadow:GetSprite():SetOverlayRenderPriority(true)
	shadow:GetSprite().FlipY = true
	shadow:GetSprite():Reload()
	--shadow:AddEntityFlags(EntityFlag.FLAG_RENDER_FLOOR | EntityFlag.FLAG_NO_REMOVE_ON_TEX_RENDER)
	shadow:FollowParent(soul)]]
	
	game:GetHUD():SetVisible(false)
	
	mod:ManuallyApplyFragmentBackdrop({
		[9] = 1,
		[11] = 0,
		[5] = true,
		[7] = true,
	})
	
	doingTeaser = 1
end

function mod:StartTeaser2()
	for _, ent in pairs(Isaac.FindByType(eType, eVariant, eSubType)) do
		ent:Remove()
	end
	--mod:GetFragmentData().CurrentRoomSeed = 0
	--local soul = lib.Spawn(mod.ENTITIES.LOST_SOUL_EFFECT, game:GetRoom():GetCenterPos() + Vector(0,-125))
	local soul = Isaac.Spawn(eType, eVariant, eSubType, game:GetRoom():GetCenterPos() + Vector(0,-125), Vector.Zero, nil)
	local sprite = soul:GetSprite()
	sprite:Play("TeaserPull", true)
	sprite:PlayOverlay("TeaserBody", true)
	sprite:SetOverlayRenderPriority(true)
	pulls = 0
	soul:GetData().atdoor = true
	
	game:GetRoom():RemoveDoor(1)
	game:GetHUD():SetVisible(false)
	
	mod:ManuallyApplyFragmentBackdrop({
		[2] = true,
		[4] = true,
		[7] = 7,
		ALT = {[2]=true},
	})
	
	doingTeaser = 2
end

local function SpawnSigil(pos)
	mod:SpawnSigil(Isaac.GetPlayer(), false, pos)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP_WEAK)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP_STRONG)
	sfxManager:Stop(SoundEffect.SOUND_REDLIGHTNING_ZAP_BURST)
end

function mod:StartTeaser3()
	for _, ent in pairs(Isaac.FindByType(960, 4)) do
		ent:Remove()
	end
	
	couch = false
	
	local tv = Isaac.Spawn(950, 1, 0, tvPos, Vector.Zero, nil)
	tv.MaxHitPoints = 1
	tv.HitPoints = 1
	tv.SpriteOffset = Vector(0, -10)
	tv:AddEntityFlags(EntityFlag.FLAG_DONT_COUNT_BOSS_HP)
	
	for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.WISP)) do
		ent:Remove()
	end
	
	game:GetHUD():SetVisible(false)
	
	mod:ManuallyApplyFragmentBackdrop({
		[9] = 1,
		[11] = 0,
		[5] = true,
		[7] = true,
	})
	
	--[[local sigils = {
		Vector(445.6, 280),
		Vector(118.5, 171.7),
		Vector(527.1, 190.2),
		Vector(193.17, 280),
	}]]
	
	local sigils = {
		Vector(320 + 70, 240),
		Vector(320 - 140, 170),
		Vector(320 + 140, 170),
		Vector(320 - 70, 240),
	}
	
	local startPause = 100
	local step = 8
	local endPause = 15
	
	for i=1, #sigils do
		lib.ScheduleForUpdate(function()
			if i == 1 then
				SpawnSigil(Vector(592.5, 280))
			end
			SpawnSigil(sigils[i])
		end, startPause + step*i)
	end
	
	--SpawnSigil(Vector(52.4, 280))
	
	frames = (startPause + (step * #sigils) + endPause) * 2
	
	lib.ScheduleForUpdate(function()
		game:GetRoom():GetDoor(0):Close(true)
		game:GetRoom():GetDoor(2):Close(true)
	end, 60)
	
	doingTeaser = 3
end

mod:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN, function(_, t, v, s, pos, vel, spawner)
	if doingTeaser and t == 999 then
		if v == 99 or v == 5 then
			return {999, SamaelMod.ENTITIES.DUMMY.Var, 0}
		end
	end
end)

mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, function(_, eff)
	if doingTeaser and (eff.Variant == 99 or eff.Variant == 5) then
		eff:Remove()
	end
end)

function mod:StartTeaser4()
	local bed = Isaac.FindByType(5, PickupVariant.PICKUP_BED)[1]:GetSprite()
	bed:ReplaceSpritesheet(0, "gfx/items/pick ups/isaacbed_barren.png")
	bed:LoadGraphics()
	
	local carpet = Isaac.FindByType(1000, EffectVariant.ISAACS_CARPET)[1]:GetSprite()
	carpet:ReplaceSpritesheet(0, "gfx/effects/isaaccarpet_barren.png")
	carpet:LoadGraphics()
	carpet:Play("Broken", true)
	
	local room = game:GetRoom()
	for i=0, room:GetGridSize() do
		local gridEntity = game:GetRoom():GetGridEntity(i)
		if gridEntity and not gridEntity:ToDoor() then
			room:RemoveGridEntity(i, 0, false)
			gridEntity:Update()
		end
		room:SetGridPath(i, 0)
	end
	
	local chest = Isaac.FindByType(5, PickupVariant.PICKUP_LOCKEDCHEST)[1]
	if chest then
		chest:ToPickup():Morph(5, PickupVariant.PICKUP_OLDCHEST, 0)
	end
	
	game:ShowHallucination(-1, 22)
	
	Isaac.GridSpawn(GridEntityType.GRID_TRAPDOOR, 0, game:GetRoom():GetCenterPos(), true)
	
	mod:ManuallyApplyFragmentBackdrop({
		[10] = 2,
	})
	
	for i=0, 6 do
		local backdrop = mod:ManuallyApplyFragmentBackdrop({
			[3] = true,
			ALT = {
				[1] = true,
			},
		}, Vector(-25 * i, 0))
		
		backdrop.RenderZOffset = -5 - i * 10
	end
	
	mod:ManuallyApplyFragmentBackdrop({
		[11] = 0,
	})
	
	game:GetHUD():SetVisible(false)
	
	doingTeaser = 4
end

mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, function(_, sigil)
	if doingTeaser and sigil:GetData().mementoMoriSigilIndex == 1 then
		sigil.Visible = false
	end
end, mod.ENTITIES.MEMENTO_MORI_SIGIL.Var)

mod:AddPriorityCallback(ModCallbacks.MC_POST_LASER_UPDATE, CallbackPriority.LATE, function(_, laser)
	if doingTeaser then
		laser.Visible = false
	end
end)

local lastRockUpdate = 0
function mod:HandleTeaserFragmentRocks()
	if doingTeaser ~= 4 then return end
	
	--local currentFrame = game:GetFrameCount()
	--local currentFrame = game:GetFrameCount()
	--local currentFrame = math.floor(Isaac.GetFrameCount() * 0.5)
	
	--if lastRockUpdate < currentFrame then
		mod:DrawFragmentBackdrop()
		
		local centerPos = game:GetRoom():GetCenterPos()
		mod:SpawnFragmentChunk(1, centerPos + Vector(175, -250), 4, 1.0)
		--mod:SpawnFragmentChunk(2, centerPos + Vector(250, 30), 3, 0.75) -- Big one
		mod:SpawnFragmentChunk(3, centerPos + Vector(150, -125), 3, 1.0)
		mod:SpawnFragmentChunk(4, centerPos + Vector(275, -90), 5, 1.1)
		mod:SpawnFragmentChunk(5, centerPos + Vector(210, -45), 4, 1.0)
		
		--top
		mod:SpawnFragmentChunk(6, centerPos + Vector(50, -260), 4, 1.0)
		
		mod:SpawnFragmentChunk(7, centerPos + Vector(15, -240), 4, 1.1)
		mod:SpawnFragmentChunk(8, centerPos + Vector(180, -90), 4, 1.1)
		
		mod:SpawnFragmentChunk(8, centerPos + Vector(240, 80), 4, 1.1, true)
		mod:SpawnFragmentChunk(4, centerPos + Vector(185, -1), 5, 1.1, true)
		
		
		mod:SpawnFragmentChunk(3, centerPos + Vector(-300, 55), 3, 1.0, true)
		--mod:SpawnFragmentChunk(8, centerPos + Vector(-265, 90), 4, 1.1, true)
		mod:SpawnFragmentChunk(7, centerPos + Vector(-286, 120), 4, 1.1, true)
	--end
	
	--lastRockUpdate = currentFrame
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.HandleTeaserFragmentRocks)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.HandleTeaserFragmentRocks)

local function BreakDoor(slot)
	game:GetRoom():GetDoor(slot):Open()
	game:GetRoom():GetDoor(slot):GetSprite():Play("Break", true)
	
	local pos = game:GetRoom():GetDoor(slot).Position
	local numWoodParticles = 10
	for i=0, numWoodParticles-1 do
		local percent = i / (numWoodParticles-1)
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.WOOD_PARTICLE, 0, pos, RandomVector() * 2 + Vector(3,0), nil)
	end
	
	sfxManager:Play(SoundEffect.SOUND_WOOD_PLANK_BREAK)
end

local thing = false

function mod:TeaserWisp(player)
	if not (doingTeaser == 1 or doingTeaser == 2 or doingTeaser == 3) then
		if wisp then
			player.Visible = true
			player:GetData().Vel = nil
			if player:GetData().samaelScythe then
				player:GetData().samaelScythe.Visible = true
				player:GetData().samaelScythe:GetData().samaelScytheBlade.Visible = true
			end
			wisp:Remove()
			wisp = nil
		end
		return
	end
	
	if doingTeaser == 3 then
		if frames > 1 then
			thing = false
		end
		if frames == 1 then
			local pos = player.Position
			player.Position = Vector(52.4, 280)
			player:GetData().mementoMoriForceStartPos = player.Position
			mod:ForceActivateMementoMori(player)
			player.Position = pos
			BreakDoor(0)
			thing = true
		elseif not player:GetData().mementoMoriActive and thing then
			thing = false
			BreakDoor(2)
		end
		frames = frames - 1
	else
		if not wisp or not wisp:Exists() then
			for _, ent in pairs(Isaac.FindByType(EntityType.ENTITY_FAMILIAR, FamiliarVariant.WISP)) do
				ent:Remove()
			end
			wisp = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FamiliarVariant.WISP, 0, player.Position, Vector.Zero, player):ToFamiliar()
			wisp:RemoveFromOrbit()
			wisp.Color = mod.IsaacWispColor
		end
		wisp.Position = player.Position
	end
	
	player.Visible = false
	if player:GetData().samaelScythe then
		player:GetData().samaelScythe.Visible = false
		player:GetData().samaelScythe:GetData().samaelScytheBlade.Visible = false
	end
	if player:GetData().Vel then
		player.Velocity = lib.Lerp(player:GetData().Vel, player.Velocity, 0.3)
		local maxSpeed = 2.5
		if player.Velocity:Length() > maxSpeed then
			player.Velocity = player.Velocity:Resized(maxSpeed)
		end
	end
	player:GetData().Vel = player.Velocity
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.TeaserWisp)

mod:AddCallback(ModCallbacks.MC_POST_PLAYER_INIT, function()
	doingTeaser = nil
end)

mod:AddCallback(ModCallbacks.MC_PRE_PLAYER_COLLISION, function()
	if doingTeaser then return true end
end)
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, function()
	if doingTeaser then return false end
end, EntityType.ENTITY_PLAYER)

mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-2, function(_, ent, damage, flags, source)
	if doingTeaser and source and source.Entity and source.Entity:ToFamiliar() then
		local combo = source.Entity:ToFamiliar().Player:GetData().mementoMoriCombo
		--print(combo)
		if combo == 2 then
			couch = true
		else
			return false
		end
	end
end, 950)

mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
	if npc.Variant == 3 and doingTeaser and couch then
		npc:GetSprite():Play("Idle", true)
	end
end, 960)

local SOUL_OFFSET = Vector(0, -5)
local ler = 0.001
local ler2 = 1.12

function mod:TeaserSoul(soul)
	if soul.SubType ~= eSubType then return end
	
	local sprite = soul:GetSprite()
	
	sprite.FlipX = soul:GetData().turned or false
	
	if sprite:IsPlaying("TeaserShadow") then
		return
	end
	
	sprite.Offset = SOUL_OFFSET
	
	soul.Velocity = lib.Lerp(soul.Velocity, lib.ZeroVector, 0.08)
	
	if sprite:IsEventTriggered("TeaserPull") then
		soul.Velocity = Vector(-1, 0)
		pulls = pulls + 1
		--sprite:Play("TeaserUnlock", true)
	end
	
	if sprite:IsPlaying("TeaserPull") and sprite:GetFrame() == 0 and pulls == 6 and soul:GetData().atdoor then
		sprite:Play("TeaserLift", true)
	end
	
	if sprite:IsFinished("TeaserLift") then
		sprite:Play("TeaserHold", true)
	end
	if sprite:IsFinished("TeaserHold") then
		sprite:Play("TeaserTurn", true)
	end
	if sprite:IsEventTriggered("TeaserTurn") then
		soul:GetData().turned = true
		sprite.FlipX = true
	end
	if sprite:IsFinished("TeaserTurn") then
		sprite:Play("TeaserUnlock", true)
	end
	if sprite:IsEventTriggered("TeaserUnlock") then
		Isaac.GetPlayer():UseCard(Card.CARD_SOUL_CAIN, UseFlag.USE_NOANIM)
		game:GetRoom():RemoveDoor(1)
		local sammy = Isaac.Spawn(eType, eVariant, eSubType, game:GetRoom():GetDoorSlotPosition(0) + Vector(-5, 8), Vector.Zero, nil)
		sammy:GetSprite():Play("SamaelWalk", true)
		local b = -0.6
		sammy:GetSprite().Color = Color(b,b,b,0)
		frames = 0
	end
	if sprite:IsFinished("TeaserUnlock") then
		sprite:Play("TeaserHappy", true)
	end
	if sprite:IsFinished("TeaserHappy") then
		local targetPos = game:GetRoom():GetCenterPos() + Vector(20, -150)
		local targetVel = targetPos - soul.Position
		if targetVel:Length() > 12 then
			targetVel = targetVel:Resized(12)
		end
		soul.Velocity = lib.Lerp(soul.Velocity, targetVel, 0.02)
	end
	
	if sprite:IsPlaying("TeaserPull") and sprite:GetFrame() == 0 and pulls % 4 == 0 then
		sprite:Play("TeaserPause", true)
	end
	
	if sprite:IsFinished("TeaserPause") then
		sprite:Play("TeaserPull", true)
	end
	
	if sprite:IsPlaying("SamaelWalk") or sprite:GetAnimation() == "SamaelTurn" then
		sprite.Offset = Vector(0, 0)
	end
	
	--[[if sprite:IsFinished("SamaelTurn") then
		sprite:Play("SamaelWalk", true)
		local b = -0.6
		sprite.Color = Color(b,b,b,0)
	end]]
	
	if sprite:IsPlaying("SamaelWalk") then
		frames = frames + 1
		if frames > 20 then
			sprite.Color = Color.Lerp(sprite.Color, Color(1,1,1,1), 0.08)
		end
		
		if frames > 120 then
			soul.Velocity = Vector.Zero
			sprite.PlaybackSpeed = 1.0
			frames = 0
			sprite:Play("SamaelTurn", true)
		elseif frames > 80 then
			--soul.Velocity = lib.Lerp(soul.Velocity, Vector.Zero, 0.1)
			sprite.PlaybackSpeed = 0
		elseif frames > 60 then
			--soul.Velocity = lib.Lerp(soul.Velocity, Vector.Zero, 0.1)
			sprite.PlaybackSpeed = 0.5
		elseif frames > 20 then
			soul.Velocity = Vector(1, 0)
		end
	end
	if sprite:IsPlaying("SamaelTurn") then
		local targetPos = Vector(276, 282)
		--local targetVel = (targetPos - Isaac.GetPlayer().Position)
		
		--targetVel = targetVel:Resized(math.min(1, targetVel:Length() * 0.5))
		
		--Isaac.GetPlayer().Velocity = lib.Lerp(Isaac.GetPlayer().Velocity, targetVel, ler)
		--ler = math.min(ler * ler2, 1.0)
		
		local player = Isaac.GetPlayer()
		local pos = player.Position
		local realTargetPos = lib.Lerp(pos, targetPos, 0.04)
		local targetVel = realTargetPos - pos
		player.Velocity = lib.Lerp(player.Velocity, targetVel, 0.3)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.TeaserSoul, eVariant)

--[[local tv

mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, prop)
	if prop.Variant == 4 and doingTeaser and (not tv or not tv:Exists()) then
		tv = Isaac.Spawn(950, 1, 0, prop.Position, Vector.Zero, nil)
		tv.MaxHitPoints = 1
		tv.HitPoints = 1
		tv.SpriteOffset = Vector(0, -10)
		tv:AddEntityFlags(EntityFlag.FLAG_DONT_COUNT_BOSS_HP)
		prop:Remove()
	end
end, 960)]]

local redkeygfx = Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_RED_KEY).GfxFileName
local RED_KEY_SPRITE = Sprite()
RED_KEY_SPRITE:Load("gfx/005.100_collectible.anm2", false)
RED_KEY_SPRITE:ReplaceSpritesheet(1, redkeygfx)
RED_KEY_SPRITE:LoadGraphics()
RED_KEY_SPRITE:Play("ShopIdle", true)
RED_KEY_SPRITE.Offset = Vector(12, 0)
RED_KEY_SPRITE.Rotation = 30
RED_KEY_SPRITE.FlipY = true

local SHADOW = Sprite()
SHADOW:Load("gfx/samael_entities/teaser.anm2", true)
SHADOW:Play("Shadow", true)
local shadowScale = 1.5
SHADOW.Scale = Vector(1, shadowScale)

function mod:TeaserSoulRender(soul)
	if soul.SubType ~= eSubType then return end
	
	if not soul:GetData().tvdrag then
		return
	end
	
	local sprite = soul:GetSprite()
	local pos = Isaac.WorldToScreen(soul.Position + Vector(-2,2))
	
	local limit = 75
	local truex = soul.Position.X - game:GetRoom():GetCenterPos().X
	local x = math.max(math.min(truex, limit), -limit)
	local i = 8 * ((x+limit) / (limit*2))
	local remainder = i % 1
	i = math.floor(i)
	
	local f = Isaac.GetFrameCount()
	--SHADOW.Color = Color(0,0,0, lib.Lerp(0, 0.2, 1-math.max(0, math.min(1, math.abs(x*1.5) / limit))) * 0.2 * math.sin(math.pi * f / 5))
	local y = math.max(-1*(truex*truex)/9999 + 1, 0)
	--local flicker = 1.1 * math.sin(math.pi * f / 5)
	local flicker = 1.0 + 0.3 * (0.5 * math.sin(math.pi * f / 5) + 0.5)
	SHADOW.Color = Color(0,0,0, y * 0.2 * flicker)
	
	local rot = 1.5
	SHADOW.Rotation = lib.Lerp(rot, -rot, remainder)
	--SHADOW.Offset = Vector(i - 4.5, 10 * shadowScale)
	SHADOW.Offset = Vector(8 * x / limit, 10 * shadowScale)
	SHADOW:SetFrame(i)
	SHADOW:Render(pos, Vector.Zero, Vector.Zero)
	
	--[[local sprite = soul:GetSprite()
	local pos = Isaac.WorldToScreen(soul.Position)
	sprite.FlipY = true
	sprite.Rotation = 0
	local shadowScale = Vector(1,4)
	sprite.Scale = shadowScale
	local shadowColor = Color(0,0,0,0.2)
	sprite.Color = shadowColor
	
	sprite.Offset = Vector(0, -2)
	sprite:RenderLayer(1, pos, Vector.Zero, Vector.Zero)
	sprite:RenderLayer(0, pos, Vector.Zero, Vector.Zero)
	sprite:RenderLayer(3, pos, Vector.Zero, Vector.Zero)
	sprite.Offset = sprite.Offset + Vector(-5, -4)
	--sprite.Offset = sprite.Offset + Vector(-12, -7)
	sprite.Rotation = sprite.Rotation -5
	--sprite.Scale = lib.Lerp(sprite.Scale, Vector(1, 1), 0.33)
	sprite:RenderLayer(2, pos, Vector.Zero, Vector.Zero)
	
	sprite.Offset = SOUL_OFFSET
	sprite.Rotation = 0
	sprite.Scale = Vector(1, 1)
	sprite.Color = Color(1,1,1,1)
	sprite.FlipY = false]]
	
	--RED_KEY_SPRITE.Scale = shadowScale
	--RED_KEY_SPRITE.Color = shadowColor
	--RED_KEY_SPRITE:Render(pos, Vector.Zero, Vector.Zero)
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_RENDER, mod.TeaserSoulRender, eVariant)

mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, function(_, player)
	if player:GetData().teaserTrapdoor then
		player:GetSprite():SetFrame(15)
		player:GetSprite().PlaybackSpeed = 0
		player:GetData().teaserTrapdoor = player:GetData().teaserTrapdoor - 1
		if player:GetData().teaserTrapdoor <= 0 then
			player:GetSprite().PlaybackSpeed = 1.0
			player:GetData().teaserTrapdoor = nil
		end
	end
end)

mod:AddCallback(ModCallbacks.MC_POST_RENDER, function()
	local player = Isaac.GetPlayer()
	if player:GetSprite():IsPlaying("Trapdoor") and player:GetSprite():GetFrame() == 14 then
		player:GetData().teaserTrapdoor = 120
		player.Visible = false
	end
end)
