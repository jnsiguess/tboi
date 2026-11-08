local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local THE_REAPER_CHALLENGE = mod.CHALLENGES.THE_REAPER.ID

local function GetChallengeData()
	return mod:GetPersistentData("REAPER_CHALLENGE")
end

mod:AddCallback(ModCallbacks.MC_PRE_PICKUP_COLLISION, function(_, pickup, collider)
	if collider:ToPlayer() and game.Challenge == THE_REAPER_CHALLENGE then
		mod.ContentManager:GrantCustomAchievement(mod.ACHIEVEMENTS.THE_REAPER, true)
		
		local chData = GetChallengeData()
		local runData = mod:GetAllRunData()
		
		local soulsKilled = (runData.lostSoulsKilled or 0)
		local soulsSaved = (runData.lostSoulsSaved or 0)
		
		chData.leastSoulsKilled = math.min(chData.leastSoulsKilled or soulsKilled, soulsKilled)
		chData.mostSoulsSaved = math.max(chData.mostSoulsSaved or 0, soulsSaved)
	end
end, PickupVariant.PICKUP_TROPHY)

mod:AddCallback(ModCallbacks.MC_POST_PLAYER_INIT, function(_, player)
	if game.Challenge == THE_REAPER_CHALLENGE then
		lib.ScheduleForUpdate(function()
			if player and player:Exists() and player:GetPlayerType() ~= lib.OtherSamaelId then
				if (game:GetFrameCount() == 0) then
					Isaac.ExecuteCommand("restart " .. lib.OtherSamaelId)
				else
					player:ChangePlayerType(lib.OtherSamaelId)
					newPlayer:AddBombs(-1)
				end
			end
		end, 0, ModCallbacks.MC_POST_RENDER, true)
	end
end, 0)

mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, function(_, player)
	if player:GetPlayerType() ~= lib.OtherSamaelId then return end
	
	local data = mod:GetPersistentPlayerData(player)
	
	if lib.HasItem(player, CollectibleType.COLLECTIBLE_BIRTHRIGHT) then
		if not data.improvedSamael then
			data.improvedSamael = true
			local pos = game:GetRoom():FindFreePickupSpawnPosition(player.Position, 40)
			Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, CollectibleType.COLLECTIBLE_LOST_CONTACT, pos, lib.ZeroVector, nil)
		end
		if player:GetActiveItem(ActiveSlot.SLOT_POCKET) == 0 then
			player:SetPocketActiveItem(CollectibleType.COLLECTIBLE_SPINDOWN_DICE)
		end
	end
end)

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	local roomData = mod:GetAllCurrentRoomData()
	local room = game:GetRoom()
	
	if game.Challenge == THE_REAPER_CHALLENGE and not room:IsClear() and not roomData.spawnedChallengeSoul then
		roomData.spawnedChallengeSoul = true
		Isaac.Spawn(mod.ENTITIES.LOST_SOUL.ID, mod.ENTITIES.LOST_SOUL.Var, 1, Isaac.GetPlayer().Position, lib.ZeroVector, nil)
	end
end)

function mod:ReaperChallengeSoulReward(pos)
	local soulsSaved = mod:GetAllRunData().lostSoulsSaved or 0
	if soulsSaved == 4 or soulsSaved % 8 == 0 then
		local itemPos = game:GetRoom():FindFreePickupSpawnPosition(pos, 10)
		local itemID = game:GetItemPool():GetCollectible(ItemPoolType.POOL_TREASURE, true, game:GetRoom():GetAwardSeed())
		Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, itemID, pos, lib.ZeroVector, nil):ToPickup()
		mod.SfxManager:Play(SoundEffect.SOUND_THUMBSUP)
	else
		Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, 3, pos, RandomVector() * 2, nil):ToPickup()
		mod.SfxManager:Play(SoundEffect.SOUND_SLOTSPAWN)
	end
end

function mod:OtherSamaelHurtSound(player)
	lib.SuppressSound(SoundEffect.SOUND_ISAAC_HURT_GRUNT)
	
	mod.SfxManager:Play(500, 1.0, 0, false, 1.5)
	mod.SfxManager:Play(SoundEffect.SOUND_BISHOP_HIT, 1.0, 0, false, 2)
	mod.SfxManager:Play(SoundEffect.SOUND_POISON_HURT, 1.0, 0, false, 2)
end

local function GetScreenTopLeft()
	local hudOffset = Options.HUDOffset
	local offset = Vector(hudOffset * 20, hudOffset * 12)

	return Vector.Zero + offset
end

local SAVED_ICON = Sprite()
SAVED_ICON:Load("gfx/ui/samael_reaper_challenge.anm2", true)
SAVED_ICON:Play("Saved", true)
--local SAVED_OFFSET = Vector(45,45)
local SAVED_OFFSET = Vector(140,6)

local LOST_ICON = Sprite()
LOST_ICON:Load("gfx/ui/samael_reaper_challenge.anm2", true)
LOST_ICON:Play("Lost", true)
--local LOST_OFFSET = Vector(0, 20)
local LOST_OFFSET = Vector(0, 15)

local HUD_FONT = Font()
HUD_FONT:Load("font/pftempestasevencondensed.fnt")
local FONT_COLOR = KColor(1,1,1,1,0,0,0)
local FONT_OFFSET = Vector(13, -5.5)

mod:AddPriorityCallback(ModCallbacks.MC_POST_RENDER, CallbackPriority.EARLY, function()
	if game.Challenge ~= THE_REAPER_CHALLENGE then return end
	
	local level = game:GetLevel()
	local isFirstRoom = level:GetStage() == LevelStage.STAGE1_1 and level:GetCurrentRoomIndex() == level:GetStartingRoomIndex()
	
	local chData = GetChallengeData()
	local runData = mod:GetAllRunData()
	
	local numSaved = runData.lostSoulsSaved or 0
	local savedPos = GetScreenTopLeft() + SAVED_OFFSET
	SAVED_ICON:Render(savedPos, lib.ZeroVector, lib.ZeroVector)
	local savedStr = isFirstRoom and ("MOST SAVED: " .. (chData.mostSoulsSaved or "N/A")) or (""..numSaved)
	HUD_FONT:DrawString(savedStr, savedPos.X + FONT_OFFSET.X, savedPos.Y + FONT_OFFSET.Y, FONT_COLOR, 0, true)
	
	local numLost = runData.lostSoulsKilled or 0
	local lostPos = savedPos + LOST_OFFSET
	LOST_ICON:Render(lostPos, lib.ZeroVector, lib.ZeroVector)
	local lostStr = isFirstRoom and ("LEAST LOST: " .. (chData.leastSoulsKilled or "N/A")) or (""..numLost)
	HUD_FONT:DrawString(lostStr, lostPos.X + FONT_OFFSET.X, lostPos.Y + FONT_OFFSET.Y, FONT_COLOR, 0, true)
end)
