local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local kDefaultParticleSpeed = 0.3
local kDefaultParticleAngle = 0
local kBaseParticleTimeoutMult = 2

local FadingSprites = {}

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	FadingSprites = {}
end)

local function GetLeftEdge(layerData)
	return layerData.PosX - layerData.PivotX
end

local function GetRightEdge(layerData)
	return layerData.PosX + layerData.Width - layerData.PivotX
end

local function GetTopEdge(layerData)
	return layerData.PosY - layerData.PivotY --+ 1
end

local function GetBottomEdge(layerData)
	return layerData.PosY + layerData.Height - layerData.PivotY
end

local TEMPLATES = {
	MOTHER = {
		Anm2 = "gfx/912.000_witness.anm2",
		Animation = "Idle",
		PlayAnimation = false,
		Layers = {
			{
				Layer = 1,
				Width = 456,
				Height = 143,
				PivotX = 256,
				PivotY = 0,
				PosX = 20,
				PosY = -180,
			},
			{
				Layer = 0,
				Width = 192,
				Height = 192,
				PivotX = 90,
				PivotY = 96,
				PosX = 0,
				PosY = -82,
			},
			{
				Layer = 2,
				Width = 180,
				Height = 216,
				PivotX = 90,
				PivotY = 85,
				PosX = -116,
				PosY = -94,
			},
			{
				Layer = 3,
				Width = 180,
				Height = 216,
				PivotX = 90,
				PivotY = 84,
				PosX = 116,
				PosY = -94,
			},
		},
		Direction = Direction.LEFT,
	},
	ISAAC = {
		Anm2 = "gfx/001.000_player.anm2",
		Animation = "Happy",
		WaitForAnimation = true,
		Delay = -80,
		--ParticleSpeed = 1.5,
		--ParticleAngle = 80,
		--ParticleTimeoutMult = 2,
		Layers = {
			{
				Layer = 12,
				Width = 64,
				Height = 64,
				PivotX = 32,
				PivotY = 56,
				PosX = 0,
				PosY = 1,
			},
		},
		Direction = Direction.DOWN,
	},
	REAPER = {
		Anm2 = "gfx/samael_entities/reaper_statue.anm2",
		PlayAnimation = false,
		Layers = {
			{
				Layer = 0,
				Width = 96,
				Height = 96,
				PivotX = 53,
				PivotY = 85,
				PosX = 0,
				PosY = 0,
			},
			{
				Layer = 1,
				Width = 96,
				Height = 96,
				PivotX = 53,
				PivotY = 85,
				PosX = 0,
				PosY = 0,
			},
		},
		Direction = Direction.DOWN,
	},
	ULTRA_GREED = {
		Anm2 = "gfx/406.000_ultragreed.anm2",
		Animation = "Idle",
		PlayAnimation = false,
		Layers = {
			{
				Layer = 0,
				Width = 106,
				Height = 112,
				PivotX = 54,
				PivotY = 96,
				PosX = 0,
				PosY = 0,
			},
			{
				Layer = 2,
				Width = 48,
				Height = 48,
				PivotX = 16,
				PivotY = 16,
				PosX = -2,
				PosY = -44,
			},
			{
				Layer = 1,
				Width = 80,
				Height = 80,
				PivotX = 44,
				PivotY = 64,
				PosX = 9,
				PosY = -44,
			},
		},
		Direction = Direction.RIGHT,
	},
	ITEM = {
		Anm2 = "gfx/005.100_collectible.anm2",
		Animation = "ShopIdle",
		Layers = {
			{
				Layer = 1,
				Width = 32,
				Height = 32,
				PivotX = 16,
				PivotY = 32,
				PosX = 0,
				PosY = 8,
			},
		},
		Direction = Direction.DOWN,
	},
}

function mod:AddFadingSprite(params)
	local sprite = Sprite()
	sprite:Load(params.Anm2, true)
	if params.SpritesheetReplacements then
		for layer, spriteSheet in pairs(params.SpritesheetReplacements) do
			sprite:ReplaceSpritesheet(layer, spriteSheet)
		end
		sprite:LoadGraphics()
	end
	sprite:Play(params.Animation or sprite:GetDefaultAnimation(), true)
	
	sprite:SetLastFrame()
	local lastFrame = sprite:GetFrame()
	
	if params.Frame then
		sprite:SetFrame(params.Frame)
	else
		sprite:Play(sprite:GetAnimation(), true)
	end
	
	sprite.PlaybackSpeed = (params.PlaybackSpeed or 1) * 0.5
	if params.PlayAnimation == false then
		sprite:Stop()
	end
	
	local dir = params.Direction or Direction.DOWN
	local maxEdge = 0
	
	local layers = lib.ShallowCopy(params.Layers)
	
	for layer, layerData in pairs(layers) do
		layerData.Step = 0
		layerData.Delay = (layerData.Delay or 0) + (params.Delay or 0)
		
		if params.WaitForAnimation then
			layerData.Delay = layerData.Delay + lastFrame * 2
		end
		
		layerData.TopLeftClamp = lib.ZeroVector
		layerData.BottomRightClamp = lib.ZeroVector
		
		local n = 1.0
		
		if dir == Direction.DOWN then
			layerData.TopLeftClamp = Vector(0, n)
			layerData.xStart = GetLeftEdge(layerData)
			layerData.yStart = GetTopEdge(layerData)
			layerData.LineSize = layerData.Width
			layerData.EndStep = layerData.Height
			layerData.StartingEdge = layerData.yStart
		elseif dir == Direction.UP then
			layerData.BottomRightClamp = Vector(0, n)
			layerData.xStart = GetLeftEdge(layerData)
			layerData.yStart = GetBottomEdge(layerData)
			layerData.LineSize = layerData.Width
			layerData.EndStep = layerData.Height
			layerData.StartingEdge = layerData.yStart
		elseif dir == Direction.LEFT then
			layerData.BottomRightClamp = Vector(n, 0)
			layerData.xStart = GetRightEdge(layerData)
			layerData.yStart = GetBottomEdge(layerData)
			layerData.LineSize = layerData.Height
			layerData.EndStep = layerData.Width
			layerData.StartingEdge = layerData.xStart
		elseif dir == Direction.RIGHT then
			layerData.TopLeftClamp = Vector(n, 0)
			layerData.xStart = GetLeftEdge(layerData)
			layerData.yStart = GetBottomEdge(layerData)
			layerData.LineSize = layerData.Height
			layerData.EndStep = layerData.Width
			layerData.StartingEdge = layerData.xStart
		end
		
		maxEdge = math.max(maxEdge, math.abs(layerData.StartingEdge))
	end
	
	if params.SyncLayers ~= false then
		for layer, layerData in pairs(layers) do
			layerData.Delay = layerData.Delay + (maxEdge - math.abs(layerData.StartingEdge))
		end
	end
	
	local color = Color(1,1,1,1)
	if params.StartFlash then
		color:SetOffset(1,1,1)
	end
	
	table.insert(FadingSprites, {
		Sprite = sprite,
		Position = params.Position,
		Velocity = params.Velocity,
		Offset = lib.ZeroVector,
		Layers = layers,
		Direction = dir,
		ParticleSpeed = params.ParticleSpeed or kDefaultParticleSpeed,
		ParticleAngle = params.ParticleAngle or kDefaultParticleAngle,
		ParticleTimeoutMult = (params.ParticleTimeoutMult or 1) * kBaseParticleTimeoutMult,
		ParticleTimeoutVariance = params.ParticleTimeoutVariance,
		ParticleDepthOffset = params.ParticleDepthOffset,
		ParticleOffset = params.ParticleOffset or lib.ZeroVector,
		MaxDelay = params.MaxDelay or 0,
		ParticleDivide = params.ParticleDivide or 1,
		Color = color,
		StartFlash = params.StartFlash,
		Sound = params.Sound,
	})
end

function mod:FadeReaperStatue(statue, delay)
	local params = TEMPLATES.REAPER
	params.Frame = statue:GetSprite():GetFrame()
	params.Position = statue.Position + statue:GetSprite().Offset + Vector(0, 20)
	params.MaxDelay = 1
	params.ParticleDivide = 3
	params.ParticleTimeoutMult = 0.25
	params.ParticleTimeoutVariance = 4
	params.Delay = delay
	mod:AddFadingSprite(params)
	statue:Remove()
end

function mod:FadeSamaelSpecial(player)
	local pSprite = player:GetSprite()
	local params = TEMPLATES.ISAAC
	params.Anm2 = pSprite:GetFilename()
	params.Animation = "FakeDeath"
	params.WaitForAnimation = false
	params.ParticleOffset = Vector(0, 2.5)
	--params.ParticleSpeed = 0.2
	--params.PlayAnimation = true
	--params.Frame = 14--pSprite:GetFrame()
	params.Position = player.Position + player:GetSprite().Offset + player:GetFlyingOffset()
	--params.Offset = Vector(20, -20)
	params.MaxDelay = 2
	params.Delay = -80
	params.ParticleDepthOffset = 255
	--params.ParticleDivide = 3
	--params.ParticleTimeoutMult = 0.25
	--params.ParticleTimeoutVariance = 4
	--params.Delay = delay
	mod:AddFadingSprite(params)
end

function mod:AddFadingSpriteForItem(collectibleType, pos, vel)
	local gfx = Isaac.GetItemConfig():GetCollectible(collectibleType).GfxFileName
	local params = TEMPLATES.ITEM
	params.SpritesheetReplacements = { [1] = gfx }
	params.Position = pos
	params.Velocity = vel or Vector(0, -3.5)
	params.Delay = 30
	params.StartFlash = true
	params.ParticleDepthOffset = 255
	params.Sound = SoundEffect.SOUND_CANDLE_LIGHT
	mod:AddFadingSprite(params)
end

local bit = false

function mod:FadingSpritesTesting()
	if not bit then
		--[[local params = TEMPLATES.ULTRA_GREED
		params.Position = game:GetRoom():GetCenterPos()
		mod:AddFadingSprite(params)]]
		--mod:AddFadingSpriteForItem(game:GetItemPool():GetCollectible(0), game:GetRoom():GetCenterPos(), Vector(0, -1.5))
		bit = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.FadingSpritesTesting)

function mod:RenderFadingSprites()
	for spriteKey, data in pairs(FadingSprites) do
		if not game:IsPaused() then
			if data.Velocity then
				data.Offset = data.Offset + data.Velocity
				data.Velocity = data.Velocity * 0.94
			end
			if data.StartFlash then
				local c = data.Color
				local m = 0.96
				c:SetOffset(c.RO * m, c.GO * m, c.BO * m)
				data.Sprite.Color = c
			end
		end
		
		local pos = data.Position
		local screenPos = lib.WorldToScreen(pos)
		
		local isDone = true
		local lineStep = data.ParticleDivide or 1
		
		for layerKey, layerData in pairs(data.Layers) do
			isDone = false
			
			local lineStart = layerData.Step % lineStep
			
			-- Render this layer
			data.Sprite.Offset = data.Offset
			data.Sprite:RenderLayer(layerData.Layer, screenPos, layerData.TopLeftClamp * layerData.Step, layerData.BottomRightClamp * layerData.Step)
			data.Sprite.Offset = lib.ZeroVector
			
			if layerData.Delay <= 0 and not game:IsPaused() then
				layerData.Step = layerData.Step + 1
			end
			
			if not game:IsPaused() then
				if layerData.Delay <= 0 then
					if data.Sound then
						sfxManager:Play(data.Sound, 2.0, 10, false, 0.75)
						data.Sound = nil
					end
					
					-- Incrementally crop more of the sprite and spawn particles along the way.
					for i=lineStart, layerData.LineSize, lineStep do
						local pixelPos
						if data.Direction == Direction.DOWN then
							pixelPos = Vector(layerData.xStart + i, layerData.yStart + layerData.Step)
						elseif data.Direction == Direction.UP then
							pixelPos = Vector(layerData.xStart + i, layerData.yStart - layerData.Step)
						elseif data.Direction == Direction.LEFT then
							pixelPos = Vector(layerData.xStart - layerData.Step, layerData.yStart - i)
						elseif data.Direction == Direction.RIGHT then
							pixelPos = Vector(layerData.xStart + layerData.Step, layerData.yStart - i)
						end
						local pixel = data.Sprite:GetTexel(pixelPos, lib.ZeroVector, 1.0, layerData.Layer)
						if pixel.Alpha > 0 then
							local particleSpeed = data.ParticleSpeed
							local particleVel = lib.DirectionalVector(data.Direction, particleSpeed):Rotated(180 + data.ParticleAngle)
							local particlePos = pos + data.ParticleOffset --[[+ data.Offset * 1.54]] + pixelPos * 1.54 - particleVel
							local particle = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.EMBER_PARTICLE, 617, particlePos, particleVel, nil):ToEffect()
							particle.SpriteScale = Vector(0.5, 0.5)
							local c = Color(1,1,1,1)
							local m = 1
							c:SetColorize(pixel.Red * m, pixel.Green * m, pixel.Blue * m, 1)
							particle.Color = c
							particle:GetData().fadingSpriteParticleTimeoutMult = data.ParticleTimeoutMult
							particle:GetData().fadingSpriteParticleTimeoutVariance = data.ParticleTimeoutVariance
							particle.SpriteOffset = data.Offset
							if data.ParticleDepthOffset then
								particle.DepthOffset = data.ParticleDepthOffset
							end
							particle:AddEntityFlags(EntityFlag.FLAG_NO_QUERY)
						end
					end
					layerData.Delay = data.MaxDelay
				else
					layerData.Delay = layerData.Delay - 1
				end
			end
			
			if layerData.Step >= layerData.EndStep then
				data.Layers[layerKey] = nil
			end
		end
		
		if isDone then
			FadingSprites[spriteKey] = nil
		elseif not game:IsPaused() then
			data.Sprite:Update()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.RenderFadingSprites)

function mod:FadingSpritesParticle(particle)
	if particle.SubType ~= 617 then return end
	
	local data = particle:GetData()
	
	if not data.fadingSpriteParticleLifeSpan and data.fadingSpriteParticleTimeoutMult then
		local lifespan = particle.Timeout * data.fadingSpriteParticleTimeoutMult
		if data.fadingSpriteParticleTimeoutVariance then
			lifespan = lifespan * (1 + (Random() % data.fadingSpriteParticleTimeoutVariance))
		end
		lifespan = math.ceil(lifespan)
		data.fadingSpriteParticleLifeSpan = lifespan
		particle.Timeout = lifespan
	end
	
	if data.fadingSpriteParticleLifeSpan and particle.Timeout <= data.fadingSpriteParticleLifeSpan * 0.5 then
		local c = particle.Color
		c:SetTint(c.R, c.G, c.B, particle.Timeout / (data.fadingSpriteParticleLifeSpan * 0.5))
		particle.Color = c
	end
	
	return false
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, CallbackPriority.IMPORTANT, mod.FadingSpritesParticle, EffectVariant.EMBER_PARTICLE)
