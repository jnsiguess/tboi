local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local PUNISHMENT_OF_THE_GRAVE = mod.ITEMS.PUNISHMENT_OF_THE_GRAVE

local LIGHT_FROM_ABOVE = mod.ENTITIES.LIGHT_FROM_ABOVE.Var

local REVIVE_TIMELINE = {
	GETUP = 30,
	REWARD = 60,
	END = 80,
}

-- # of hearts to revive players with.
local kReviveHearts = 4

local doingPotgRevive = nil
local potgWasPaused = false

-- Returns true if the player has taken devil deals, and has not been forgiven for it.
local function HasPlayerBeenEvil()
	if game:GetDevilRoomDeals() == 0 then
		return false
	end
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasCollectible(CollectibleType.COLLECTIBLE_ACT_OF_CONTRITION) or player:HasCollectible(CollectibleType.COLLECTIBLE_REDEMPTION) then
			return false
		end
	end
	
	return true
end

local function GetTotalHealth(player)
	return player:GetHearts() + player:GetSoulHearts() + player:GetEternalHearts() + player:GetBoneHearts()
end

-- Sets the player's health to the intended post-revive value.
local function PotgReviveHealth(player)
	player:AddBlackHearts(-999)
	player:AddBoneHearts(-999)
	player:AddEternalHearts(-999)
	player:AddGoldenHearts(-999)
	player:AddMaxHearts(-999)
	player:AddSoulHearts(-999)
	player:AddHearts(-999)
	
	if HasPlayerBeenEvil() then
		player:AddBlackHearts(kReviveHearts * 2)
		if player:GetSoulHearts() == 0 then
			player:AddBoneHearts(kReviveHearts)
		end
	else
		local hearts = (kReviveHearts-1) * 2
		player:AddMaxHearts(hearts)
		player:AddSoulHearts(2)
		player:AddEternalHearts(1)
		if player:GetMaxHearts() == 0 and player:GetSoulHearts() == 2 then
			player:AddSoulHearts(hearts)
		end
		if player:GetMaxHearts() == hearts and player:GetSoulHearts() == 0 then
			player:AddMaxHearts(2)
		end
		player:SetFullHearts()
	end
end

-- Darkness overlay.
local dark = Sprite()
dark:Load("gfx/ui/bossoverlay_whiteout.anm2", true)
dark.Color = Color(0,0,0,0.5,-1,-1,-1)
dark.Scale = Vector(10,10)
dark:Play("Fade", true)
dark:SetFrame(15)
dark:Stop()

-- Make a "flash" on the screen.
local flash
function mod:FlashFade()
	flash = Sprite()
	flash:Load("gfx/ui/bossoverlay_whiteout.anm2", true)
	flash.PlaybackSpeed = 2
	flash.Color = Color(1,1,1,1)
	flash.Scale = Vector(10,10)
	flash:Play("Fade", true)
	flash:SetFrame(15)
end

-- Renders effects for the revival animation.
function mod:PotgRender()
	if not doingPotgRevive then return end
	
	local darkFadeDuration = 10
	local a = 0.5
	if doingPotgRevive > REVIVE_TIMELINE.END - darkFadeDuration then
		a = lib.Lerp(0.5, 0, 1 - (REVIVE_TIMELINE.END - doingPotgRevive) / darkFadeDuration)
	end
	dark.Color = Color(0,0,0, a ,-1,-1,-1)
	dark:Render(lib.ZeroVector, lib.ZeroVector, lib.ZeroVector)
	
	for _, player in pairs(lib.GetPlayers()) do
		local light = player:GetData().potgReviveLight
		if light then
			local renderPos = lib.WorldToScreen(light.Position)
			light:GetSprite():Render(renderPos, lib.ZeroVector, lib.ZeroVector)
		end
	end
	
	for _, player in pairs(lib.GetPlayers()) do
		local renderPos = lib.WorldToScreen(player.Position + player:GetFlyingOffset())
		player:GetSprite():Render(renderPos, lib.ZeroVector, lib.ZeroVector)
	end
	
	if flash then
		if flash:IsFinished() then
			flash = nil
		else
			flash:Render(lib.ZeroVector,lib.ZeroVector,lib.ZeroVector)
			if not game:IsPaused() then
				flash:Update()
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.PotgRender)

-- Start the revival animation.
function mod:TriggerPotg(player)
	local data = player:GetData()
	
	game:GetLevel():AddCurse(LevelCurse.CURSE_OF_THE_UNKNOWN)
	PotgReviveHealth(player)
	
	data.potgReviveSource = true
	data.samaelPotgRevive = 0
	player.Visible = false
	if player:GetOtherTwin() then
		player:GetOtherTwin():GetData().samaelPotgRevive = 0
		player.Visible = false
	end
	
	sfxManager:Play(SoundEffect.SOUND_FLASHBACK)
	mod:FlashFade()
	doingPotgRevive = 0
	potgWasPaused = true
	
	Isaac.GetPlayer():UseActiveItem(CollectibleType.COLLECTIBLE_PAUSE, UseFlag.USE_NOANIM)
	mod:ClearEntitiesForPause()
	
	player:AnimateAppear()
end

-- Spawns an angel item.
local function SpawnAngelItem(player)
	local pos = game:GetRoom():FindFreePickupSpawnPosition(player.Position, 35)
	local item = game:GetItemPool():GetCollectible(ItemPoolType.POOL_ANGEL, true, player:GetCollectibleRNG(PUNISHMENT_OF_THE_GRAVE):Next())
	Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, item, pos, lib.ZeroVector, nil)
	Isaac.Spawn(EntityType.ENTITY_EFFECT, LIGHT_FROM_ABOVE, 0, pos, lib.ZeroVector, nil)
end

function mod:HandleOngoingPotgRevive(player)
	local data = player:GetData()
	
	if data.samaelPotgRevive then
		data.samaelPotgRevive = data.samaelPotgRevive + 1
		local frame = data.samaelPotgRevive
		
		player:SetMinDamageCooldown(60)
		player.ControlsEnabled = false
		
		if frame < REVIVE_TIMELINE.GETUP then
			player:AnimateAppear()
		elseif frame == REVIVE_TIMELINE.GETUP then
			data.potgReviveLight = Isaac.Spawn(EntityType.ENTITY_EFFECT, LIGHT_FROM_ABOVE, 0, player.Position, lib.ZeroVector, nil):ToEffect()
		elseif frame == REVIVE_TIMELINE.REWARD then
			local evil = HasPlayerBeenEvil()
			if evil then
				player:AnimateSad()
			else
				player:AnimateHappy()
			end
			if data.potgReviveSource then
				if evil then
					local baby = Isaac.Spawn(EntityType.ENTITY_BABY, 1, (Random()%3)%2, player.Position - Vector(20, 45), lib.ZeroVector, player):ToNPC()
					--baby:MakeChampion(baby.InitSeed, ChampionColor.WHITE, true)
					local angelType = EntityType.ENTITY_URIEL
					if game:GetLevel():GetAbsoluteStage() >= LevelStage.STAGE3_1 then
						angelType = EntityType.ENTITY_GABRIEL
					end
					local angel = Isaac.Spawn(angelType, 0, 0, player.Position - Vector(0, 50), lib.ZeroVector, nil)
					angel:GetData().fromPunishmentOfTheGrave = true
					angel:Update()
				else
					sfxManager:Play(SoundEffect.SOUND_SUPERHOLY)
					SpawnAngelItem(player)
					
					local baby1 = Isaac.Spawn(EntityType.ENTITY_BABY, 1, 0, player.Position - Vector(35, 0), lib.ZeroVector, player)
					baby1:AddCharmed(EntityRef(player), -1)
					local baby2 = Isaac.Spawn(EntityType.ENTITY_BABY, 1, 1, player.Position + Vector(35, 0), lib.ZeroVector, player)
					baby2:AddCharmed(EntityRef(player), -1)
				end
				data.potgSpawnedRewards = true
			end
			game:GetLevel():RemoveCurses(LevelCurse.CURSE_OF_THE_UNKNOWN)
		elseif frame >= REVIVE_TIMELINE.END then
			data.samaelPotgRevive = nil
			player:GetSprite():SetLastFrame()
			player.ControlsEnabled = true
			player.Visible = true
			data.potgReviveSource = nil
			data.potgSpawnedRewards = nil
			game:GetLevel():RemoveCurses(LevelCurse.CURSE_OF_THE_UNKNOWN)
		end
	end
end

-- Ignore player inputs during revive animation.
-- Forcibly break the "pause" effect when the revive animation ends.
function mod:PotgInputs(entity, hook, action)
	if doingPotgRevive then
		if action ~= ButtonAction.ACTION_CONSOLE then
			return 0
		end
	elseif (potgWasPaused or mod.ForceUnpause) and action == ButtonAction.ACTION_SHOOTDOWN then
		if entity:ToPlayer() and not entity:ToPlayer().ControlsEnabled then
			-- If the player has controls disabled we can't get the game to unpause.
			-- But we can't do it this MC_INPUT_ACTION anyway, so enable controls and wait until the next run.
			entity:ToPlayer().ControlsEnabled = true
			return
		end
		if potgWasPaused then
			mod:ClearEntitiesForPause()
		end
		potgWasPaused = false
		mod.ForceUnpause = false
		return 0.1
	end
end
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION, mod.PotgInputs, InputHook.GET_ACTION_VALUE)

function mod:PotgUpdate()
	if doingPotgRevive then
		doingPotgRevive = doingPotgRevive + 1
		
		if doingPotgRevive >= REVIVE_TIMELINE.END then
			doingPotgRevive = nil
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.PotgUpdate)

-- Handle potential issues if the player tries the exit the game during the revival animation.
mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, function()
	game:GetLevel():RemoveCurses(LevelCurse.CURSE_OF_THE_UNKNOWN)
	for _, player in pairs(lib.GetPlayers()) do
		local data = player:GetData()
		if (doingPotgRevive or data.triggerPotg) and data.potgReviveSource and not data.potgSpawnedRewards and not HasPlayerBeenEvil() then
			SpawnAngelItem(player)
		end
	end
end)

-- Spawns a devil item.
local function SpawnEvilReward(pos, seed)
	local room = game:GetRoom()
	local itemPool = game:GetItemPool()
	local pos = room:FindFreePickupSpawnPosition(pos, 5)
	local item = itemPool:GetCollectible(ItemPoolType.POOL_DEVIL, true, seed)
	local pedestal = Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, item, pos, lib.ZeroVector, nil)
	pedestal:SetColor(Color(1,0,0,1,1), 20, 99, true, false)
	local light = Isaac.Spawn(EntityType.ENTITY_EFFECT, LIGHT_FROM_ABOVE, 0, pos, lib.ZeroVector, nil)
	light.Color = Color(1,0,0,1,1,0,0)
end

-- When the hostile angel dies, spawn two devil items.
function mod:PotgAngelDeath(angel)
	local data = angel:GetData()
	
	if data.fromPunishmentOfTheGrave then
		local offset = Vector(0, 40)
		local angle = 65
		SpawnEvilReward(angel.Position + offset:Rotated(-angle), angel.InitSeed)
		SpawnEvilReward(angel.Position + offset:Rotated(angle), angel.InitSeed)
		sfxManager:Play(SoundEffect.SOUND_SATAN_GROW)
		sfxManager:Play(SoundEffect.SOUND_SATAN_ROOM_APPEAR)
		data.fromPunishmentOfTheGrave = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.PotgAngelDeath, EntityType.ENTITY_URIEL)
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.PotgAngelDeath, EntityType.ENTITY_GABRIEL)

----------------------------------------------------------------------------------------------------
---- REVIVAL DETECTION / HANDLING ------------------------------------------------------------------
----------------------------------------------------------------------------------------------------

-- MC_POST_PLAYER_UPDATE runs the entire time the player is "dying".
-- MC_POST_PEFFECT_UPDATE doesn't, but if the player revives, it will only run ONCE while the death animation is still playing, at the end before revival.
-- Detecting death/revival via the death animation still playing on that single PEFFECT frame could potentially be inconsistent.
-- So the method implemented here is described as follows:
--
-- On MC_POST_PEFFECT_UPDATE, if the player has our item and won't currently revive, we add the "Soul of Lazarus" effect, and keep track of the fact WE added it.
-- The "Soul of Lazarus" effect persists on quit and continue, so keeping track of the fact WE added it should be done in persistent player savedata.
-- We'll also remove our "Soul of Lazarus" effect if the player loses the item without dying, or if they get more than one "Soul of Lazarus" stack.
-- 
-- On MC_POST_PLAYER_UPDATE, we detect if the player is playing their death animation, will revive, AND that WE previously added the "Soul of Lazarus" effect.
-- If so, we're fairly confident that we're responsible for the revival, so we populate a GetData boolean (this doesn't need to be savedata).
--
-- On the next MC_POST_PEFFECT_UPDATE (which will only trigger post-revive) we'll see that we set that boolean, and trigger any post-revival effects (like removing our item).
--
-- The "Soul of Lazarus" null effect seems to take priority over other forms of revival.
--
-- This logic was originally based on "Crystal Skull" from Tainted Treasures, so thanks JD for that and for helping figure out how to solve compatability issues with this method.

-- On MC_POST_PLAYER_UPDATE, detect that the player is dying and will revive.
-- (See top of this section for more detail.)
function mod:PotgPlayerUpdate(player)
	local data = player:GetData()
	local savedata = mod:GetPersistentPlayerData(player)
	
	local isPlayingDeathAnimation = player:GetSprite():GetAnimation():sub(-#"Death") == "Death"  -- Does their current animation end with "Death"?
	local framesSinceLastPeffectUpdate = game:GetFrameCount() - (data.LastPeffectUpdate or 0)  -- PEFFECT doesn't run while dying.
	
	if isPlayingDeathAnimation and framesSinceLastPeffectUpdate > 0 and player:WillPlayerRevive()
			and savedata.potgAddedLazSoulEffect and not data.samaelQueuedPotgRevive
			and player:GetEffects():HasNullEffect(NullItemID.ID_LAZARUS_SOUL_REVIVE) then
		-- Pretty sure the player is reviving using OUR Soul of Lazarus effect.
		-- Trigger the revival effects on the next PEFFECT update.
		data.samaelQueuedPotgRevive = true
	end
	
	-- This is specific to Punishment of the Grave, I just needed to catch a single frame where you can see the player standing up / can shoot.
	if data.samaelQueuedPotgRevive then
		local frame = player:GetSprite():GetFrame()
		if frame ~= data.samaelPotgLastAnimFrame then
			data.samaelPotgLastAnimFrame = frame
			data.samaelPothAnimFrameChanged = Isaac.GetFrameCount()
		end
		if Isaac.GetFrameCount() - data.samaelPothAnimFrameChanged >= 3 then
			player.Visible = false
		end
		player.ControlsEnabled = false
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.PotgPlayerUpdate)

-- On MC_POST_PEFFECT_UPDATE, handle adding/removing the Soul of Lazarus effect and any post-revival effects (after detecting death+revival above).
-- (See top of this section for more detail.)
function mod:PotgPeffectUpdate(player)
	local peffects = player:GetEffects()
	local data = player:GetData()
	local savedata = mod:GetPersistentPlayerData(player)
	local playerHoldingSoulOfLazarus = player:GetCard(0) == Card.CARD_SOUL_LAZARUS or player:GetCard(1) == Card.CARD_SOUL_LAZARUS
	
	-- PEFFECT doesn't run while dying, so we can refer to this to more accurately detect death.
	data.LastPeffectUpdate = game:GetFrameCount()
	
	if not player:HasCollectible(PUNISHMENT_OF_THE_GRAVE) then
		if savedata.potgAddedLazSoulEffect then
			-- The player HAD this item and we added the Soul of Lazarus effect.
			-- But they lost the item without reviving using it, so remove a stack.
			peffects:RemoveNullEffect(NullItemID.ID_LAZARUS_SOUL_REVIVE)
			savedata.potgAddedLazSoulEffect = nil
		end
		return
	end
	
	if data.samaelQueuedPotgRevive then
		-- We detected the player dying in MC_POST_PLAYER_UPDATE, and we assumed it was our revive because WE added a Soul of Lazarus effect.
		-- Remove our revive item and its associated booleans.
		data.samaelQueuedPotgRevive = nil
		savedata.potgAddedLazSoulEffect = nil
		player:RemoveCollectible(PUNISHMENT_OF_THE_GRAVE)
		
		-- Trigger the actual revival effects.
		mod:TriggerPotg(player)
	elseif not playerHoldingSoulOfLazarus and not peffects:HasNullEffect(NullItemID.ID_LAZARUS_SOUL_REVIVE) then
		-- If the player doesn't already have a stack of the Lazarus effect, add it and keep track of the fact we did so in player savedata.
		peffects:AddNullEffect(NullItemID.ID_LAZARUS_SOUL_REVIVE)
		savedata.potgAddedLazSoulEffect = true
	elseif savedata.potgAddedLazSoulEffect and (peffects:GetNullEffectNum(NullItemID.ID_LAZARUS_SOUL_REVIVE) > 1 or playerHoldingSoulOfLazarus) then
		-- We previously added a "Soul of Lazarus" stack, but now there's more than one (or they have the actual Soul Stone).
		-- To be safe, remove ours. The player can revive using the other one.
		peffects:RemoveNullEffect(NullItemID.ID_LAZARUS_SOUL_REVIVE)
		savedata.potgAddedLazSoulEffect = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, function(_, player)
	mod:PotgPeffectUpdate(player)
	mod:HandleOngoingPotgRevive(player)
end)
