local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local THANATOSIS_PILL = mod.ITEMS.THANATOSIS

local kThanatosisVo = Isaac.GetSoundIdByName("SamaelThanatosisPill")
local kMegaThanatosisVo = Isaac.GetSoundIdByName("SamaelMegaThanatosisPill")

-- (TESTING) Force add the pill to the pool.
function mod:PillTesting()
	if game:GetFrameCount() ~= 10 then return end
	local pillColor = Isaac.AddPillEffectToPool(THANATOSIS_PILL)
	local room = game:GetRoom()
	Isaac.Spawn(5, 70, pillColor, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
	Isaac.Spawn(5, 70, pillColor + PillColor.PILL_GIANT_FLAG, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
end
--mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.PillTesting)

-- Table of alternate animations to use for characters besides "Death", or other overrides.
-- Primarily for modded characters who have more intense or unique death animations.
local FakeDeathAnimationOverrides = {}

function mod:AddFakeDeathAnimOverride(idOrName, isTainted, anim, exData)
	local pType = idOrName
	if type(idOrName) == "string" then
		pType = Isaac.GetPlayerTypeByName(idOrName, isTainted)
	end
	
	if pType > -1 then
		exData = exData or {}
		
		FakeDeathAnimationOverrides[pType] = {
			Animation = anim,
			LastFrame = exData.LastFrame,
			Sound = exData.Sound,
			DisableDepressionOffset = exData.DisableDepressionOffset,
			OnHurt = exData.OnHurt,
			Ultra = exData.Ultra,
		}
	end
end

local FakeDeathAnimOverridesInitialized = false
function mod:InitFakeDeathAnimOverrides()
	if not FakeDeathAnimOverridesInitialized then
		mod:AddFakeDeathAnimOverride(PlayerType.PLAYER_THELOST, false, "LostDeath", {Ultra=true})
		mod:AddFakeDeathAnimOverride(PlayerType.PLAYER_THESOUL, false, "LostDeath", {Ultra=true})
		mod:AddFakeDeathAnimOverride(PlayerType.PLAYER_THELOST_B, false, "LostDeath", {Ultra=true})
		mod:AddFakeDeathAnimOverride(PlayerType.PLAYER_THESOUL_B, false, "LostDeath", {Ultra=true})
		
		mod:AddFakeDeathAnimOverride("Samael", false, "FakeDeath")
		mod:AddFakeDeathAnimOverride("Samael", true, "FakeDeath", {
				OnHurt = mod.TaintedSamaelHurtSound,
				Sound = SoundEffect.SOUND_NULL,
				DisableDepressionOffset = true})
		mod:AddFakeDeathAnimOverride("Samael ", false, "FakeDeath", {
				OnHurt = mod.OtherSamaelHurtSound,
				Sound = SoundEffect.SOUND_NULL,
				DisableDepressionOffset = true})
		mod:AddFakeDeathAnimOverride(" Edith", false, "DeathTeleport", {DisableDepressionOffset = true})
		mod:AddFakeDeathAnimOverride("Sodom", false, "Death", {LastFrame = 17})
		mod:AddFakeDeathAnimOverride("Gomorrah", false, "Death", {LastFrame = 17})
		mod:AddFakeDeathAnimOverride("Bela", false, "DeathTeleport")
		mod:AddFakeDeathAnimOverride("Bertran", false, "Death", {Sound = Isaac.GetSoundIdByName("b_die")})
		mod:AddFakeDeathAnimOverride("Bertran", true, "Death", {Sound = Isaac.GetSoundIdByName("b_die")})
		mod:AddFakeDeathAnimOverride("Fiend", false, "Death", {Sound = Isaac.GetSoundIdByName("FiendDies")})
		mod:AddFakeDeathAnimOverride("Fiend", true, "Death", {Sound = Isaac.GetSoundIdByName("BiendFreakingDies")})
		mod:AddFakeDeathAnimOverride("Golem", false, "Death", {Sound = Isaac.GetSoundIdByName("golemDeath")})
		mod:AddFakeDeathAnimOverride("China", false, "DeathTeleport", {Sound = SoundEffect.SOUND_MIRROR_BREAK})
		FakeDeathAnimOverridesInitialized = true
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.InitFakeDeathAnimOverrides)

local function GetFakeDeathAnimation(player)
	local override = FakeDeathAnimationOverrides[player:GetPlayerType()]
	local isUltra = player:GetData().fakeDeathUltra
	if override and override.Animation and ((not isUltra and not override.Ultra) or (isUltra and override.Ultra)) then
		return override.Animation
	end
	return "Death"
end

local function GetFakeDeathAnimationLastFrame(player)
	local override = FakeDeathAnimationOverrides[player:GetPlayerType()]
	if override and override.LastFrame then
		return override.LastFrame
	end
	return 21
end

local function GetFakeDeathSound(player)
	if player:GetData().MaliceMinion then
		return 0
	end
	local override = FakeDeathAnimationOverrides[player:GetPlayerType()]
	if override and override.Sound then
		return override.Sound
	end
	return SoundEffect.SOUND_ISAACDIES
end

local function FakeDeathOnHurt(player)
	local override = FakeDeathAnimationOverrides[player:GetPlayerType()]
	if override and override.OnHurt then
		override.OnHurt(player)
	end
end

-- Helper for Spirit of Depression.
function mod:ShouldDisableDepressionOffset(player)
	local override = FakeDeathAnimationOverrides[player:GetPlayerType()]
	if override and override.DisableDepressionOffset then
		return true
	end
	return false
end

local addedCallback = false
function mod:ForcePlayerInvisible(player)
	if player:GetData().fakeDeathPillActive then
		player.Color = lib.InvisibleColor
		player:GetSprite().Scale = lib.ZeroVector
	end
end

-- Post G-FUEL era pill color detection.
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, function(_, player)
	player:GetData().samaelCurrentPill = player:GetPill(0)
end)

-- Players currently in the fake death state.
-- Only maintained to make it faster to check for from NPC updates.
local FakeDeathPlayers = {}

local fakeDeathTwinBit = false

-- Activate fake death effect.
function mod:FakeDeathPill(player, useFlags, forceHorsePill, depressionEffect)
	local pData = player:GetData()
	local pSprite = player:GetSprite()
	
	--local pillColor = player:GetPill(0) -- BROKEN BY THE G-FUEL UPDATE
	local pillColor = player:GetData().samaelCurrentPill
	local isHorsePill = forceHorsePill or (
			not depressionEffect and pillColor and pillColor > 0
			and game:GetItemPool():GetPillEffect(pillColor, player) == THANATOSIS_PILL
			and pillColor >= PillColor.PILL_GIANT_FLAG)
	pData.fakeDeathUltra = isHorsePill
	
	player.ControlsEnabled = false
	pData.fakeDeathPillActive = true
	pData.fakeDeathPillTime = 0
	pData.fakeDeathUltraMoveCount = 0
	pData.samaelDepressionActive = depressionEffect
	local c = player.Color
	c:SetTint(c.R, c.G, c.B, math.max(c.A, 0.5))
	pData.fakeDeathAnimOriginalColor = c
	pData.fakeDeathAnimOriginalScale = player.SpriteScale
	if pData.MaliceMinion then
		pData.fakeDeathAnimOriginalScale = pData.fakeDeathAnimOriginalScale * 0.5
	end
	FakeDeathPlayers[player.InitSeed] = player
	
	-- Trigger iFrames and on-damage effects.
	if XalumMods and XalumMods.SodomAndGomorrah and player:GetName() == "Sodom" and pData.heart then
		pData.heart:TakeDamage(1, DamageFlag.DAMAGE_FAKE, EntityRef(player), 0)
	end
	lib.TriggerOnDamageEffects(player)
	FakeDeathOnHurt(player)
	
	-- Store how many iframes the player should have when the effect ends.
	-- The player's DamageCooldown is frozen at this value until the effect ends.
	pData.fakeDeathDamageCooldown = math.floor(player:GetDamageCooldown())
	
	local fakeDeathAnim = GetFakeDeathAnimation(player)
	
	if not pData.fakeDeathUltra and fakeDeathAnim == "Death" then
		pData.fakeDeathPillLastFrame = GetFakeDeathAnimationLastFrame(player)
	else
		-- Find the last frame of the animation.
		pSprite:Play(fakeDeathAnim, true)
		pSprite:SetLastFrame()
		local lastFrame = pSprite:GetFrame()-1
		pData.fakeDeathPillLastFrame = lastFrame
	end
	
	player:AnimateAppear()
	
	if not depressionEffect then
		sfxManager:Play(SoundEffect.SOUND_DEATH_BURST_SMALL, 0.8, 0, false, 1.0, 0)
	end
	
	-- For Jacob&Esau / Forgotten / Sodom&Gomorrah, proc the effect for both characters.
	local twin = player:GetOtherTwin() or pData.sodom or pData.gomorrah
	if twin and not fakeDeathTwinBit then
		fakeDeathTwinBit = true
		mod:FakeDeathPill(twin, useFlags, isHorsePill, depressionEffect)
		fakeDeathTwinBit = false
	end
	
	-- BIEND
	if pData.MaliceMinions then
		fakeDeathTwinBit = true
		for i, minion in ipairs(pData.MaliceMinions) do
			if minion:Exists() then
				mod:FakeDeathPill(minion, useFlags, isHorsePill, depressionEffect)
			end
		end
		fakeDeathTwinBit = false
	end
	
	-- Announcer voice
	if not depressionEffect and not fakeDeathTwinBit and useFlags & (UseFlag.USE_NOHUD | UseFlag.USE_MIMIC) == 0 then
		local voiceMode = Options.AnnouncerVoiceMode
		if voiceMode == 2 or (voiceMode == 0 and Random() % 2 == 0) then
			if isHorsePill then
				--lib.PlayDelayedSound(kMegaThanatosisVo, 30, 0.7)
				sfxManager:Play(kMegaThanatosisVo, 0.7)
			else
				lib.PlayDelayedSound(kThanatosisVo, 30, 0.7)
				--sfxManager:Play(kThanatosisVo, 0.7)
			end
		end
	end
	
	if not addedCallback then
		mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.ForcePlayerInvisible)
		addedCallback = true
	end
end

function mod:FakeDeathPillCallback(_, player, useFlags)
	mod:FakeDeathPill(player, useFlags, false, false)
end
mod:AddCallback(ModCallbacks.MC_USE_PILL, mod.FakeDeathPillCallback, THANATOSIS_PILL)

-- Special compatibility code for Sodom & Gomorrah
function mod:SodomGomorrahHeartUpdate(heart)
	if not XalumMods or not XalumMods.SodomAndGomorrah or heart.SubType ~= 179 then return end
	
	local sodom = heart.Player
	if not sodom or not sodom:GetName() == "Sodom" then return end
	
	if sodom:GetData().fakeDeathPillActive and not sodom:GetData().samaelDepressionActive then
		if sodom:GetData().fakeDeathUltra then
			if not heart:GetData().fakeDeathPillActive then
				-- "Kill" the heart.
				local rng = sodom:GetPillRNG(THANATOSIS_PILL)
				for i=0, 6 do
					local particle = Isaac.Spawn(1000, EffectVariant.BLOOD_PARTICLE, 0, heart.Position, Vector(rng:RandomFloat()*6 - 3, rng:RandomFloat()*6 - 3), sodom):ToEffect()
				end
				heart.Visible = false
				heart:GetData().fakeDeathPillActive = true
			end
		else
			-- Make S&G's heart flash white a little bit during the fake death invincibility.
			local n = math.sin(0.05 * math.pi * heart.Player:GetData().fakeDeathPillTime) * 0.2 + 0.2
			heart:SetColor(Color(1, 1, 1, 1, n, n, n), 2, 1, false, false)
		end
	elseif heart:GetData().fakeDeathPillActive then
		heart.Visible = true
		heart:GetData().fakeDeathPillActive = false
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.SodomGomorrahHeartUpdate, FamiliarVariant.ISAACS_HEART)

local fakeDeathScreen = nil
local fakeDeathScreenBg = nil
local fakeDeathScreenExtra = nil

function mod:LoadFakeDeathScreen()
	fakeDeathScreen = Sprite()
	fakeDeathScreen:Load("gfx/ui/death screen.anm2", true)
	fakeDeathScreen:Play("Diary", true)
	fakeDeathScreen:SetLayerFrame(5, 149)
	fakeDeathScreen:SetLayerFrame(3, 146)
	fakeDeathScreen:SetLayerFrame(2, 100)
	fakeDeathScreen:SetLayerFrame(6, 24)
	
	fakeDeathScreenBg = Sprite()
	fakeDeathScreenBg:Load("gfx/ui/bossoverlay_whiteout.anm2", true)
	fakeDeathScreenBg.Color = Color(0,0,0, 0.6, -1,-1,-1)
	fakeDeathScreenBg.Scale = Vector(10,10)
	fakeDeathScreenBg:SetFrame("Fade", 15)
	
	fakeDeathScreenExtra = Sprite()
	fakeDeathScreenExtra:Load("gfx/samael_test.anm2", true)
	fakeDeathScreenExtra:Play("Test", true)
	
	MusicManager():Play(Music.MUSIC_GAME_OVER, Options.MusicVolume)
end

function mod:FakeDeathPlayerRender(player)
	local pData = player:GetData()
	local pSprite = player:GetSprite()
	local frame = Isaac.GetFrameCount()
	
	if pData.fakeDeathPillActive and (not pData.fakeDeathLastRender or pData.fakeDeathLastRender < frame) and not pData.MaliceSplit then
		local fakeDeathAnim = GetFakeDeathAnimation(player)
		local fakeDeathAnimLastFrame = pData.fakeDeathPillLastFrame
		local fakeDeathAnimFrame = math.min(math.floor(pData.fakeDeathPillTime * 0.5), fakeDeathAnimLastFrame)
		
		pSprite:Play(fakeDeathAnim, true)
		pSprite:SetFrame(fakeDeathAnimFrame)
		mod:SamaelDeathHandler(player)
		
		player.Color = pData.fakeDeathAnimOriginalColor or lib.NullColor
		pSprite.Scale = pData.fakeDeathAnimOriginalScale or lib.NormalVector
		if pData.fakeDeathScaleMult then
			pSprite.Scale = Vector(pSprite.Scale.X * pData.fakeDeathScaleMult.X, pSprite.Scale.Y * pData.fakeDeathScaleMult.Y)
		end
		pSprite:Render(Isaac.WorldToScreen(player.Position), lib.ZeroVector, lib.ZeroVector)
		mod:ForcePlayerInvisible(player)
		
		player:AnimateAppear()
		
		pData.fakeDeathLastRender = frame
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, mod.FakeDeathPlayerRender)

function mod:FakeDeathRender()
	if fakeDeathScreen then
		fakeDeathScreenBg:Render(lib.ZeroVector, lib.ZeroVector, lib.ZeroVector)
		local fakeDeathScreenRenderPos = lib.GetScreenSize() * 0.5 + Vector(16,16)
		fakeDeathScreen:Render(fakeDeathScreenRenderPos, lib.ZeroVector, lib.ZeroVector)
		fakeDeathScreenExtra:Render(fakeDeathScreenRenderPos - Vector(146, 73), lib.ZeroVector, lib.ZeroVector)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.FakeDeathRender)

function mod:FakeDeathPostGameStarted()
	fakeDeathScreenBg = nil
	fakeDeathScreen = nil
	FakeDeathPlayers = {}
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.FakeDeathPostGameStarted)

-- Returns true if the player is pressing any of the movement inputs.
local function PlayerMoving(player)
	local id = player.ControllerIndex
	return Input.IsActionPressed(ButtonAction.ACTION_LEFT, id)
		or Input.IsActionPressed(ButtonAction.ACTION_RIGHT, id)
		or Input.IsActionPressed(ButtonAction.ACTION_UP, id)
		or Input.IsActionPressed(ButtonAction.ACTION_DOWN, id)
end

-- Returns true if the player is pressing any of the attack inputs.
local function PlayerAttacking(player)
	local id = player.ControllerIndex
	return Input.IsActionPressed(ButtonAction.ACTION_SHOOTLEFT, id)
		or Input.IsActionPressed(ButtonAction.ACTION_SHOOTRIGHT, id)
		or Input.IsActionPressed(ButtonAction.ACTION_SHOOTUP, id)
		or Input.IsActionPressed(ButtonAction.ACTION_SHOOTDOWN, id)
end

-- Returns true if the player is pressing any of the attack or movement inputs.
local function PlayerMovingOrAttacking(player)
	local id = player.ControllerIndex
	return PlayerMoving(player) or PlayerAttacking(player)
end

local function SetDoorsOpenState(open)
	local room = game:GetRoom()
	
	for i=0, 7 do
		local door = room:GetDoor(i)
		if door then
			if open then
				door:Open()
			else
				door:Close(false)
			end
		end
	end
end

function mod:FakeDeathNewRoom()
	for _, player in pairs(lib.GetPlayers()) do
		if player:GetData().fakeDeathPillActive and player:GetData().fakeDeathUltra then
			SetDoorsOpenState(true)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.FakeDeathNewRoom)

function mod:ClearEntitiesForPause()
	for _, ember in ipairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.FALLING_EMBER, -1)) do
		if ember:Exists() then
			ember:Remove()
		end
	end
	for _, rain in ipairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, EffectVariant.RAIN_DROP, -1)) do
		if rain:Exists() then
			rain:Remove()
		end
	end
	for _, proj in pairs(Isaac.FindByType(EntityType.ENTITY_PROJECTILE)) do
		if proj:Exists() then
			proj:Remove()
		end
	end
end

-- Update for while a player is in the fake death state.
function mod:FakeDeathUpdate(player)
	local pData = player:GetData()
	local pSprite = player:GetSprite()
	
	-- Render the flashing effect for the iframes after ending the effect, if needed.
	-- Only needed if something cancels the players natural iframes.
	if pData.fakeDeathPillRecovery and pData.fakeDeathPillRecovery > 0 then
		pData.fakeDeathPillRecovery = pData.fakeDeathPillRecovery - 1
		if player:GetDamageCooldown() == 0  and pData.fakeDeathPillRecovery % 6 == 0 then
			player:SetColor(lib.InvisibleColor, 2, 1, false, false)
		end
	end
	
	-- For when the fake death is active.
	if pData.fakeDeathPillActive then
		pData.fakeDeathPillTime = (pData.fakeDeathPillTime or 0) + 1
		pData.fakeDeathPauseTime = (pData.fakeDeathPauseTime or 0) + 1
		
		-- Freeze iframes until effect ends.
		player:SetMinDamageCooldown(pData.fakeDeathDamageCooldown or 60)
		
		if pData.fakeDeathPillTime == 20 and not pData.samaelDepressionActive then
			local deathSound = GetFakeDeathSound(player)
			if deathSound > 0 then
				sfxManager:Play(deathSound, 1, 0, false, 1, 0)
			end
		end
		
		local fakeDeathAnimLastFrame = pData.fakeDeathPillLastFrame
		local fakeDeathAnimFrame = math.min(math.floor(pData.fakeDeathPillTime * 0.5), fakeDeathAnimLastFrame)
		
		-- Horse Pill
		if pData.fakeDeathUltra then
			if fakeDeathAnimFrame >= fakeDeathAnimLastFrame - 1 and not fakeDeathScreen then
				player:UseActiveItem(CollectibleType.COLLECTIBLE_PAUSE, UseFlag.USE_NOANIM)
				mod:ClearEntitiesForPause()
				pData.fakeDeathPauseTime = 0
				mod:LoadFakeDeathScreen()
			end
			if pData.fakeDeathPillTime > 120 and not player.ControlsEnabled then
				player.ControlsEnabled = true
			end
			if game:GetRoom():GetFrameCount() == 1 then
				player:UseActiveItem(CollectibleType.COLLECTIBLE_PAUSE, UseFlag.USE_NOANIM)
				mod:ClearEntitiesForPause()
				pData.fakeDeathPauseTime = 0
			end
			if PlayerMoving(player) then
				pData.fakeDeathUltraMoveCount = (pData.fakeDeathUltraMoveCount or 0) + 1
			end
			if fakeDeathScreen and pData.fakeDeathUltraMoveCount > 90 then
				SetDoorsOpenState(true)
			end
		end
		
		local shouldEndFakeDeath = not pData.fakeDeathUltra and (pData.fakeDeathPillTime > 120 and PlayerMovingOrAttacking(player))
		
		if pData.samaelDepressionActive then
			shouldEndFakeDeath = pData.samaelEndDepressionEffect
		end
		
		local shouldEndUltraFakeDeath = pData.fakeDeathUltra and pData.fakeDeathPillTime > 120 and (PlayerAttacking(player) or pData.fakeDeathPauseTime > 60*30)
		
		if shouldEndFakeDeath or shouldEndUltraFakeDeath then
			-- End the fake death effect.
			player.ControlsEnabled = true
			pData.fakeDeathPillActive = false
			pData.fakeDeathPillTime = nil
			pData.fakeDeathPillRecovery = 60
			pData.fakeDeathScaleMult = nil
			pData.samaelDepressionActive = false
			player.SpriteOffset = lib.ZeroVector
			player.Color = pData.fakeDeathAnimOriginalColor or lib.NullColor
			pSprite.Scale = pData.fakeDeathAnimOriginalScale or lib.NormalVector
			player:AddCacheFlags(CacheFlag.CACHE_COLOR | CacheFlag.CACHE_SIZE)
			player:EvaluateItems()
			pData.samaelEndDepressionEffect = false
			FakeDeathPlayers[player.InitSeed] = nil
			fakeDeathScreen = nil
			fakeDeathScreenBg = nil
			
			if pData.fakeDeathUltra then
				mod:ClearEntitiesForPause()
				if not game:GetRoom():IsClear() then
					SetDoorsOpenState(false)
				end
				pData.fakeDeathUltra = false
			end
		else
			mod:ForcePlayerInvisible(player)
		end
		
		player:AnimateAppear()
		pSprite:SetLastFrame()
		player.Visible = true
	end
	
	-- T.Sodom funny sliding sound.
	if player:GetPlayerType() == Isaac.GetPlayerTypeByName("Sodom", true) then
		local sodomSlideSound = Isaac.GetSoundIdByName("SamaelStoneGrindLoop")
		if pData.fakeDeathPillActive and pData.fakeDeathUltra and PlayerMoving(player) and not pData.jump_velocity and player.Velocity:Length() > 0 then
			if not sfxManager:IsPlaying(sodomSlideSound) then
				sfxManager:Play(sodomSlideSound, 1, 0, true)
			else
			end
		elseif sfxManager:IsPlaying(sodomSlideSound) then
			sfxManager:Stop(sodomSlideSound)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.FakeDeathUpdate)

-- Resist damage while in the fake death state.
function mod:FakeDeathInvincibility(player, damage, damageFlags, damageSourceRef)
	player = player:ToPlayer()
	local pData = player:GetData()
	
	if (pData.fakeDeathPillActive or (pData.fakeDeathPillRecovery or 0) > 0) and damageFlags & DamageFlag.DAMAGE_FAKE == 0 then
		if player:GetName() == "Bertran" and not pData.samaelDepressionActive then
			sfxManager:Stop(Isaac.GetSoundIdByName("b_hurt"))
			sfxManager:Stop(SoundEffect.SOUND_THUMBS_DOWN)
		end
		return false
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.IMPORTANT-1, mod.FakeDeathInvincibility, EntityType.ENTITY_PLAYER)

-- Confuse all enemies as long as any player is in the fake death state.
function mod:FakeDeathConfuseEnemies(npc)
	if not npc:IsEnemy() then return end
	
	for idx, player in pairs(FakeDeathPlayers) do
		local pData = player:GetData()
		if pData.fakeDeathPillActive and not pData.samaelDepressionActive then
			if pData.fakeDeathUltra then
				npc:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			elseif npc:HasEntityFlags(EntityFlag.FLAG_CONFUSION) then
				npc:AddConfusion(EntityRef(player), 1, false)
			else
				npc:AddConfusion(EntityRef(player), 2, false)
			end
			return
		else
			FakeDeathPlayers[idx] = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.FakeDeathConfuseEnemies)

-- Stoneys can't be confused, so just make them stop moving.
function mod:StoneyUpdate(stoney)
	for idx, player in pairs(FakeDeathPlayers) do
		if player:GetData().fakeDeathPillActive and not pData.samaelDepressionActive then
			stoney.State = 16
			stoney:GetSprite():SetFrame("WalkVert", 0)
			return
		else
			FakeDeathPlayers[idx] = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.StoneyUpdate, EntityType.ENTITY_STONEY)
