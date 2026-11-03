if not FiendFolio or not FiendFolio.EMOJI then return end

local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local emojiGlassesSprite

local EmojiSigils
if FiendFolio then
	EmojiSigils = {
		FiendFolio.EMOJI.JOY,
		FiendFolio.EMOJI.CROC,
		--FiendFolio.EMOJI.CHICK,
		FiendFolio.EMOJI.SUNGLASSES,
		FiendFolio.EMOJI.IMP,
		FiendFolio.EMOJI.GRIMACE,
		FiendFolio.EMOJI.WALK,
		FiendFolio.EMOJI.WEIRD,
		FiendFolio.EMOJI.SICK,
		--FiendFolio.EMOJI.BOOT,
		FiendFolio.EMOJI.SAX,
		FiendFolio.EMOJI.BUG,
		FiendFolio.EMOJI.GIFT,
		FiendFolio.EMOJI.HORSE,
		FiendFolio.EMOJI.NERD,
		FiendFolio.EMOJI.HEARTEYES,
		FiendFolio.EMOJI.KAWAII,
		FiendFolio.EMOJI.THINK,
		--FiendFolio.EMOJI.INVISIBLE,
		FiendFolio.EMOJI.GUN,
	}
end

function mod:EmojiGlassesSigilUpdate(sigil)
	local sigilData = sigil:GetData()
	local player = sigil.Parent:ToPlayer()
	local emoji = sigilData.EmojiGlassesEffect
	
	sigil.Visible = false
	
	if not sigilData.EmojiGlassesEffect then
		sigilData.EmojiGlassesEffect = lib.PickRandom(EmojiSigils)
	end
	
	if emoji == FiendFolio.EMOJI.JOY and sigil.FrameCount % 4 == 0 then
		if not sigilData.JoyEmojiStartDir then
			sigilData.JoyEmojiStartDir = RandomVector()
		end
		local vec = sigilData.JoyEmojiStartDir:Resized(9):Rotated(90 + (sigil.FrameCount * 10))
		local tear = Isaac.Spawn(2, 0, 0, sigil.Position + Vector(0, 20), vec, sigil):ToTear()
		tear.CollisionDamage = player.Damage / 3
		--tear.Scale = 1.0
	elseif emoji == FiendFolio.EMOJI.CROC then
		if not sigilData.mementoMoriSigilEmojiMovement then
			sigilData.mementoMoriSigilEmojiMovement = RandomVector() * 1
			sigilData.emojiSigilTurnCountDown = 400
		end
	elseif emoji == FiendFolio.EMOJI.IMP then
		sigilData.sigilForceCrit = true
	elseif emoji == FiendFolio.EMOJI.GRIMACE then
		sigilData.sigilForceBruise = true
	elseif emoji == FiendFolio.EMOJI.WALK then
		if not sigilData.mementoMoriSigilEmojiMovement then
			sigilData.mementoMoriSigilEmojiMovement = RandomVector() * 2
		end
		if sigil.FrameCount % 30 == 0 and Random() % 2 == 0 then
			local nearby = lib.PickRandom(Isaac.FindInRadius(sigil.Position, 200, EntityPartition.ENEMY))
			if nearby and nearby:Exists() then
				sigilData.mementoMoriSigilEmojiMovement = (nearby.Position - sigil.Position):Resized(2)
			end
		end
	elseif emoji == FiendFolio.EMOJI.WEIRD then
		if not sigilData.mementoMoriSigilEmojiMovement then
			sigilData.mementoMoriSigilEmojiMovement = Vector.One:Rotated(90 * (Random() % 4) + ((Random() % 30) - 15)):Resized(5)
			sigilData.mementoMoriSigilBounce = true
		end
	elseif emoji == FiendFolio.EMOJI.SICK then
		sigilData.linkLaser:AddTearFlags(TearFlags.TEAR_MYSTERIOUS_LIQUID_CREEP)
	elseif emoji == FiendFolio.EMOJI.BUG then
		if not sigilData.mementoMoriSigilEmojiMovement then
			sigilData.mementoMoriSigilEmojiMovement = RandomVector() * 5
		end
	elseif emoji == FiendFolio.EMOJI.HORSE then
		if not sigilData.mementoMoriSigilEmojiMovement then
			if Random()%2==0 then
				sigilData.mementoMoriSigilEmojiMovement = Vector(20, 0)
			else
				sigilData.mementoMoriSigilEmojiMovement = Vector(-20, 0)
			end
			sigilData.mementoMoriSigilBounce = true
		end
	elseif emoji == FiendFolio.EMOJI.NERD then
		local closest
		for _, nearby in pairs(Isaac.FindInRadius(sigil.Position, 9999, EntityPartition.ENEMY)) do
			if not closest or nearby.Position:Distance(sigil.Position) < closest.Position:Distance(sigil.Position) then
				closest = nearby
			end
		end
		if closest then
			sigilData.mementoMoriSigilEmojiMovement = (closest.Position - sigil.Position):Resized(3)
		elseif not sigilData.mementoMoriSigilEmojiMovement then
			sigilData.mementoMoriSigilEmojiMovement = RandomVector() * 3
		end
		sigil.SpriteOffset = Vector(4 * math.sin(math.pi * sigil.FrameCount / 4), 0)
		sigilData.emojiSigilNoFlip = true
	elseif emoji == FiendFolio.EMOJI.THINK then
		sigilData.mementoMoriSigilForceLudo = true
	elseif emoji == FiendFolio.EMOJI.KAWAII then
		if not sigil:GetSprite():IsPlaying("Death") and (not sigilData.emojiSigilKawaii or not sigilData.emojiSigilKawaii:Exists()) then
			local lure = Isaac.Spawn(EntityType.ENTITY_GUSHER, 1, 617, sigil.Position, lib.ZeroVector, player):ToNPC()
			lure:AddEntityFlags(EntityFlag.FLAG_FRIENDLY | EntityFlag.FLAG_CHARM | EntityFlag.FLAG_BAITED | EntityFlag.FLAG_NO_STATUS_EFFECTS)
			lure:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			lure.GridCollisionClass = 0
			lure.CollisionDamage = 0
			lure.Parent = sigil
			lure.SpawnerEntity = player
			lure.Visible = false
			lure:GetData().emojiSigilKawaii = true
			sigilData.emojiSigilKawaii = lure
		end
	elseif emoji == FiendFolio.EMOJI.GUN then
		if not sigil:GetSprite():IsPlaying("Death") and sigil.FrameCount % 10 == 9 then
			local enemy = FiendFolio.FindClosestEnemy(sigil.Position, 300, nil, nil, nil, EntityCollisionClass.ENTCOLL_PLAYEROBJECTS)
			if enemy then
				local shootDir = (enemy.Position - sigil.Position):Normalized()
				local bullet = Isaac.Spawn(2, TearVariant.M90_BULLET, 0, sigil.Position + Vector(0, 20) + shootDir:Resized(30), shootDir:Resized(13), sigil):ToTear()
				bullet.CollisionDamage = player.Damage / 2
				sigilData.emojiSigilShootAngle = shootDir:GetAngleDegrees() + 180
				sigilData.emojiGunFlip = shootDir.X > 0
			end
		end
	end
end

function mod:EmojiSigilTrigger(sigil)
	local sigilData = sigil:GetData()
	local player = sigil.Parent:ToPlayer()
	local emoji = sigilData.EmojiGlassesEffect
	
	if emoji == FiendFolio.EMOJI.SAX then
		FiendFolio:emojiGlassesPreTear(nil, nil, {EmojiGlassesEffect=FiendFolio.EMOJI.SAX})
	elseif emoji == FiendFolio.EMOJI.HEARTEYES then
		game:CharmFart(sigil.Position, 80, player)
	elseif emoji == FiendFolio.EMOJI.GIFT then
		local summonCount = FiendFolio.GetEntityCount(3, 43) + FiendFolio.GetEntityCount(3, 73) + FiendFolio.GetEntityCount(3, 1026)
		if summonCount < 5 then
			sfxManager:Play(SoundEffect.SOUND_CHEST_OPEN, 1, 0, false, 1.3)
			local rand = math.random(3)
			if rand == 1 then
				local afly = Isaac.Spawn(3, 43, 0, sigil.Position, lib.ZeroVector, sigil)
				afly:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
				afly:Update()
			elseif rand == 2 then
				Isaac.GetPlayer(0):ThrowBlueSpider(sigil.Position, sigil.Position+RandomVector()*25)
			elseif rand == 3 then
				local skuzz = Isaac.Spawn(3, FamiliarVariant.ATTACK_SKUZZ, 0, sigil.Position, lib.ZeroVector, sigil)
				skuzz:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
				skuzz:Update()
			end
		end
	end
end

function mod:HandleEmojiSigilMovement(sigil)
	local sigilData = sigil:GetData()
	local player = sigil.Parent:ToPlayer()
	local emoji = sigilData.EmojiGlassesEffect
	
	sigil.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_GROUND
	
	if emoji == FiendFolio.EMOJI.BUG then
		local randomvalue = ((math.random(100) - 50))
		sigilData.mementoMoriSigilEmojiMovement = sigilData.mementoMoriSigilEmojiMovement:Rotated(randomvalue)
		sigil.Velocity = sigilData.mementoMoriSigilEmojiMovement
		return
	end
	
	local hitHori = false
	local hitVert = false
	local shouldTurn = false
	
	if sigilData.mementoMoriSigilLastPos then
		hitHori = math.abs(sigilData.mementoMoriSigilLastPos.X - sigil.Position.X) < math.abs(sigilData.mementoMoriSigilEmojiMovement.X) * 0.5
		hitVert = math.abs(sigilData.mementoMoriSigilLastPos.Y - sigil.Position.Y) < math.abs(sigilData.mementoMoriSigilEmojiMovement.Y) * 0.5
		shouldTurn = hitHori or hitVert
	end
	
	if sigilData.emojiSigilTurnCountDown then
		sigilData.emojiSigilTurnCountDown = sigilData.emojiSigilTurnCountDown - 1
		if sigilData.emojiSigilTurnCountDown <= 0 then
			shouldTurn = true
		end
	end
	
	if (sigilData.emojiSigilTurnCooldown or 0) > 0 then
		shouldTurn = false
	end
	
	if shouldTurn then
		if sigilData.mementoMoriSigilBounce then
			if hitHori then
				sigilData.mementoMoriSigilEmojiMovement = sigilData.mementoMoriSigilEmojiMovement * Vector(-1, 1)
			else
				sigilData.mementoMoriSigilEmojiMovement = sigilData.mementoMoriSigilEmojiMovement * Vector(1, -1)
			end
			sigil.Velocity = sigilData.mementoMoriSigilEmojiMovement
		else
			sigilData.mementoMoriSigilEmojiMovement = sigilData.mementoMoriSigilEmojiMovement:Rotated(Random() % 360)
		end
		
		if sigilData.emojiSigilTurnCountDown then
			sigilData.emojiSigilTurnCountDown = 150 + Random() % 250
		end
		sigilData.emojiSigilTurnCooldown = 10
	end
	
	if sigilData.emojiSigilTurnCooldown and sigilData.emojiSigilTurnCooldown > 0 then
		sigilData.emojiSigilTurnCooldown = sigilData.emojiSigilTurnCooldown - 1
	end
	
	sigilData.mementoMoriSigilLastPos = sigil.Position
	sigil.Velocity = lib.Lerp(sigil.Velocity, sigilData.mementoMoriSigilEmojiMovement, 0.25)
end

function mod:EmojiSigilRender(sigil)
	local data = sigil:GetData()
	if data.EmojiGlassesEffect then
		if not emojiGlassesSprite then
			emojiGlassesSprite = Sprite()
			emojiGlassesSprite:Load("gfx/projectiles/emoji_tear.anm2", true)
			emojiGlassesSprite:Play("Idle", true)
			emojiGlassesSprite:Stop()
		end
		
		emojiGlassesSprite.Offset = sigil.SpriteOffset
		emojiGlassesSprite.Rotation = data.emojiSigilShootAngle or 0
		
		if data.emojiGunFlip then
			emojiGlassesSprite.Rotation = emojiGlassesSprite.Rotation * -1 + 180
			emojiGlassesSprite.FlipX = true
		else
			emojiGlassesSprite.FlipX = not data.emojiSigilNoFlip and (data.mementoMoriSigilEmojiMovement or sigil.Velocity).X > 0
		end
		
		local scaleX = 2.0
		local scaleY = 2.0
		local alpha = 1.0
		
		local sprite = sigil:GetSprite()
		if sprite:GetAnimation() == "Appear" then
			local n = math.min(sprite:GetFrame() / 9, 1)
			alpha = lib.Lerp(0, 1.0, n)
			scaleX = lib.Lerp(3, 2, n)
			scaleY = lib.Lerp(3, 2, n)
		elseif sprite:GetAnimation() == "Death" then
			local n = math.min(sprite:GetFrame() / 15, 1)
			alpha = lib.Lerp(1.0, 0, n)
			scaleX = lib.Lerp(2, 3, n)
			scaleY = lib.Lerp(2, 3, n)
		end
		
		emojiGlassesSprite.Color = Color(1,1,1, alpha)
		emojiGlassesSprite.Scale = Vector(scaleX, scaleY)
		
		emojiGlassesSprite:SetFrame(data.EmojiGlassesEffect)
		local pos = Isaac.WorldToScreen(sigil.Position)
		emojiGlassesSprite:Render(pos, lib.ZeroVector, lib.ZeroVector)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_FAMILIAR_RENDER, mod.EmojiSigilRender, mod.ENTITIES.MEMENTO_MORI_SIGIL.Var)