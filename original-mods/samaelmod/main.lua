---- Samael Character Mod ---
-- By Connor, aka Ghostbroster

-- Main file contains some primary mod setup/functionality and loads other files.

SamaelMod = RegisterMod("Samael", 1)
local mod = SamaelMod
mod.VERSION = 3.27

if StageAPI and StageAPI.Loaded then
	StageAPI.UnregisterCallbacks("Samael")
end

REPENTANCE = REPENTANCE or REPENTANCE_PLUS

local game = Game()
mod.Game = game
mod.SfxManager = SFXManager()
mod.MusicManager = MusicManager()
mod.HiddenItemManager = include("samaelscripts/hidden_item_manager"):Init(mod)
mod.HiddenItemManager:HideCostumes()

-- Warning if the mod failed to load.
function mod:FailedLoadWarning()
	local currentFrame = game:GetFrameCount()
	
	if currentFrame < 1 then return end
	
	local err
	if not CallbackPriority then
		err = "Isaac out of date! Samael requires Repentance v1.7.9b.J835!"
	elseif not mod.SuccessfullyLoaded then
		err = "Samael failed to load."
	elseif not mod.SAVE_STATE then
		err = "Samael failed to read or initialize savedata."
	end
	
	if not err then return end
	
	if not mod.WarningStartFrame then
		mod.WarningStartFrame = currentFrame
	end
	
	local pType = Isaac.GetPlayer():GetPlayerType()
	local playerIsSamael = pType == Isaac.GetPlayerTypeByName("Samael", false) or pType == Isaac.GetPlayerTypeByName("Samael", true)
	local duration = 360
	local startFrame = mod.WarningStartFrame
	local endFrame = startFrame + duration
	if playerIsSamael or currentFrame < endFrame then
		local a = 1.0
		local fadeStart = math.floor(duration * 0.7)
		if not playerIsSamael and currentFrame - startFrame >= fadeStart then
			a = 1 - ((currentFrame - startFrame - fadeStart) / (endFrame - startFrame - fadeStart))
		end
		Isaac.RenderText("ERROR: " .. err, 66, 200, 1, 0, 0, a)
		Isaac.RenderText("Achievement progress will not be saved.", 66, 210, 1, 0, 0, a)
	end
end
if CallbackPriority then
	mod:AddPriorityCallback(ModCallbacks.MC_POST_RENDER, CallbackPriority.EARLY, mod.FailedLoadWarning)
else
	mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.FailedLoadWarning)
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
	mod.WarningStartFrame = nil
end)

mod.Lib = include("samaelscripts/lib")
local lib = mod.Lib

mod.Path = lib.GetCurrentModPath()

if EID then
	include("samaelscripts/eid_samael_synergies")
end

include("samaelscripts/vanilla_unlock_checker")

--------------------------------------------------
---- SAVEDATA STUFF (Thanks AgentCucco) 
--------------------------------------------------

-- Save Data
mod.PERSISTENT_DATA = {}

-- Custom ""callbacks"".
mod.PRE_SAVE = {}
mod.PRE_GAME_STARTED = {}

local json = include("json")

local function GetPlayerSaveDataKey(player)
	local rng = player:GetCollectibleRNG(Isaac.GetItemIdByName("Tainted Samael Tractor Beam"))
	local key = tostring(rng:GetSeed())
	
	if player:GetPlayerType() == PlayerType.PLAYER_LAZARUS2_B then
		key = "LAZ" .. key
	end
	
	return key
end

local function SavePersistentPlayerData()
	local tab = {}
	
	for _, player in pairs(lib.GetPlayers()) do
		local data = player:GetData().SAMAEL_PERSISTENT
		if data then
			local key =  GetPlayerSaveDataKey(player)
			
			if tab[key] then
				lib.LogErr("Tried to save player data twice with the same key!")
			else
				tab[key] = data
			end
		end
	end
	
	return tab
end

function mod:SaveModData()
	if not mod.SuccessfullyLoaded then
		local err = "[Samael] ERROR: Mod failed to fully load - will not overwrite savedata."
		print(err)
		Isaac.DebugString(err)
		return
	end
	
	if not mod.SAVE_STATE then
		local err = "[Samael] ERROR: Failed to read or initialize savedata - will not overwrite savedata."
		print(err)
		Isaac.DebugString(err)
		return
	end
	
	lib.Log("Running pre-save callbacks...")
	for _, func in pairs(mod.PRE_SAVE) do
		func()
	end
	mod:GetAllRunData().HIDDEN_ITEMS = mod.HiddenItemManager:GetSaveData()
	mod.PERSISTENT_DATA.VERSION = mod.VERSION
	
	-- Save mod data.
	lib.Log("Saving...")
	mod.SAVE_STATE.PERSISTENT_DATA = mod.PERSISTENT_DATA
	mod.SAVE_STATE.PERSISTENT_DATA.PLAYER = SavePersistentPlayerData()
	mod:SaveData(json.encode(mod.SAVE_STATE))
	lib.Log("Save completed.")
end
mod:AddPriorityCallback(ModCallbacks.MC_PRE_MOD_UNLOAD, -1, function(_, modToBeUnloaded)
	if mod.InitFinished and mod.GameStarted and modToBeUnloaded.Name == mod.Name then
		mod:SaveModData()
	end
end)

mod:AddPriorityCallback(ModCallbacks.MC_POST_NEW_LEVEL, -1, function()
	if mod.PERSISTENT_DATA.RUN then
		mod.PERSISTENT_DATA.RUN.FLOOR = {}
	end
	-- Save every floor.
	mod:SaveModData()
end)

mod:AddPriorityCallback(ModCallbacks.MC_PRE_GAME_EXIT, -1, function(_, shouldSave)
	-- Save when exiting a run.
	mod:SaveModData()
	mod.GameStarted = false
end)

local function ReadJsonData()
	if Isaac.HasModData(mod) then
		local jsonString = mod:LoadData()
		if jsonString ~= "" then
			jsonString = jsonString:gsub('-nan%(ind%)', '0')
			return json.decode(jsonString)
		end
	end
	return nil
end

local function LoadSaveData()
	lib.Log("Loading save data...")
	mod.SAVE_STATE = ReadJsonData()
	if mod.SAVE_STATE and type(mod.SAVE_STATE) == "table" then
		lib.Log("Save data loaded.")
		mod.PERSISTENT_DATA = mod.SAVE_STATE.PERSISTENT_DATA or {}
		if not mod.PERSISTENT_DATA.VERSION then
			-- Loaded a save file from before version 3.0
			mod.PERSISTENT_DATA.ShowLegacySamaelPopup = true
			mod.PERSISTENT_DATA.ShowTaintedSamaelPopup = true
		end
	else
		lib.Log("No save data to load. Initializing...")
		mod.SAVE_STATE = {}
		mod.PERSISTENT_DATA = {}
	end
	
	mod.HiddenItemManager:LoadData(mod:GetAllRunData().HIDDEN_ITEMS)
	
	lib.Log("Load completed. Checking unlocks...")
	mod:LoadUnlockData()
	lib.Log("Done checking unlocks.")
end

function mod:LoadSaveDataOnInit(player)
	-- Check how many players are in-game.
	local numPlayers = #Isaac.FindByType(EntityType.ENTITY_PLAYER, -1, -1, false, false)
	
	-- If it's the first player to spawn (if the run just started).
	if numPlayers == 0 then
		lib.Log("Beginning MC_POST_PLAYER_INIT initialization.")
		mod.GameStarted = false
		mod.InitFinished = false
		-- Load old data.
		LoadSaveData()
		-- If the total run's duration is 0 (it's a new game).
		if game:GetFrameCount() == 0 then
			lib.Log("Resetting data for new run.")
			mod.PERSISTENT_DATA.RUN = {}
			mod:SaveModData()
		end
		
		for _, func in pairs(mod.PRE_GAME_STARTED) do
			func()
		end
		lib.Log("MC_POST_PLAYER_INIT Initialization completed.")
	end
	
	-- Load persistent player data, if any.
	local data = mod.PERSISTENT_DATA.PLAYER
	local key = GetPlayerSaveDataKey(player)
	
	if data and data[key] then
		player:GetData().SAMAEL_PERSISTENT = data[key]
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_PLAYER_INIT, -1, mod.LoadSaveDataOnInit)

function mod:PostGameStarted(isContinued)
	mod.GameStarted = true
	
	if not mod:GetAllRunData().VANILLA_UNLOCKS then
		mod:CheckVanillaUnlocks()
	end
	
	if Isaac.GetPlayer():GetPlayerType() == lib.TaintedSamaelId
			and mod.PERSISTENT_DATA.ShowTaintedSamaelPopup
			and mod.ContentManager:CharacterLocked(lib.TaintedSamaelId) then
		lib.ScheduleForUpdate(function()
			DeadSeaScrollsMenu.QueueMenuOpen("Samael", "taintedsamaelpopup", 1, true)
		end, 90)
	elseif mod.PERSISTENT_DATA.ShowLegacySamaelPopup then
		if not mod.ContentManager:CanRunUnlockAchievements() then
			mod.PERSISTENT_DATA.ShowLegacySamaelPopup = nil
			return
		end
		for _, player in pairs(lib.GetPlayers()) do
			if player:GetPlayerType() == lib.SamaelId then
				DeadSeaScrollsMenu.QueueMenuOpen("Samael", "samaelpopup", 1, true)
				break
			end
		end
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_GAME_STARTED, -1, mod.PostGameStarted)

function mod:GetLastKnownGridIndex()
	return mod:GetAllFloorData().LastKnownGridIndex or game:GetLevel():GetStartingRoomIndex()
end

function mod:GetLastRoomID()
	return mod:GetAllFloorData().LastRoomID
end

function mod:CheckInitializationsFinished()
	if not mod.FirstUpdate then
		mod.FirstUpdate = true
	else
		mod.GameStarted = true
	end
	mod.InitFinished = true
end
mod:AddPriorityCallback(ModCallbacks.MC_POST_UPDATE, -1, mod.CheckInitializationsFinished)

function mod:GetPersistentData(key)
	return lib.GetOrInit(mod.PERSISTENT_DATA, key)
end

function mod:GetPersistentPlayerData(player)
	if player and player:ToPlayer() then
		return lib.GetOrInit(player:GetData(), "SAMAEL_PERSISTENT")
	end
end

function mod:GetOptions()
	return mod:GetPersistentData("OPTIONS")
end

function mod:GetAllRunData()
	return lib.GetOrInit(mod.PERSISTENT_DATA, "RUN")
end

function mod:GetRunData(key)
	return lib.GetOrInit(mod:GetAllRunData(), key)
end

function mod:GetAllFloorData()
	return lib.GetOrInit(mod:GetAllRunData(), "FLOOR")
end

function mod:GetFloorData(key)
	return lib.GetOrInit(mod:GetAllFloorData(), key)
end

function mod:GetRoomData(roomIdx)
	return lib.GetOrInit(mod:GetFloorData("ROOMS"), ""..roomIdx)
end

function mod:GetAllCurrentRoomData()
	local roomIdx = game:GetLevel():GetCurrentRoomDesc().ListIndex
	return mod:GetRoomData(roomIdx)
end

function mod:GetCurrentRoomData(key)
	return lib.GetOrInit(mod:GetAllCurrentRoomData(), key)
end

function mod:VanillaUnlocks()
	return mod:GetRunData("VANILLA_UNLOCKS")
end

----------------------------------------------------------------------------------------------------
---- Quick REPENTOGON support patch for main menu completion marks
----------------------------------------------------------------------------------------------------

local function GetMarkFrame(allMarks, markToCheck)
	local mark = allMarks[markToCheck] or allMarks[tostring(markToCheck)]
	if mark then
		if mark.Hard then return 2 end
		if mark.Unlock then return 1 end
	end
	return 0
end

local function SyncCompletionMarksWithRepentogon(playerType, marks)
	if not REPENTOGON or not playerType or not marks then return end

	Isaac.SetCompletionMarks({
		PlayerType =	playerType,
		MomsHeart =		GetMarkFrame(marks, mod.ContentManager.VanillaMarks.MOMS_HEART),
		Isaac =			GetMarkFrame(marks, mod.ContentManager.VanillaMarks.ISAAC),
		Satan =			GetMarkFrame(marks, mod.ContentManager.VanillaMarks.SATAN),
		BossRush =		GetMarkFrame(marks, mod.ContentManager.VanillaMarks.BOSS_RUSH),
		BlueBaby =		GetMarkFrame(marks, mod.ContentManager.VanillaMarks.BLUE_BABY),
		Lamb =			GetMarkFrame(marks, mod.ContentManager.VanillaMarks.LAMB),
		MegaSatan =		GetMarkFrame(marks, mod.ContentManager.VanillaMarks.MEGA_SATAN),
		UltraGreed =	GetMarkFrame(marks, mod.ContentManager.VanillaMarks.GREED),
		Hush =			GetMarkFrame(marks, mod.ContentManager.VanillaMarks.HUSH),
		Delirium =		GetMarkFrame(marks, mod.ContentManager.VanillaMarks.DELIRIUM),
		Mother =		GetMarkFrame(marks, mod.ContentManager.VanillaMarks.MOTHER),
		Beast =			GetMarkFrame(marks, mod.ContentManager.VanillaMarks.BEAST),
	})
end

if REPENTOGON then
	function mod:RepentogonSync()
		SyncCompletionMarksWithRepentogon(lib.SamaelId, mod.ContentManager:GetVanillaMarks(lib.SamaelId))
		SyncCompletionMarksWithRepentogon(lib.TaintedSamaelId, mod.ContentManager:GetVanillaMarks(lib.TaintedSamaelId))
	end
	table.insert(mod.PRE_SAVE, mod.RepentogonSync)

	mod:AddCallback(ModCallbacks.MC_POST_SAVESLOT_LOAD, function(_, saveSlot, isSlotSelected)
		if isSlotSelected then
			local savedata = ReadJsonData()
			if savedata and type(savedata) == "table" and savedata.PERSISTENT_DATA then
				SyncCompletionMarksWithRepentogon(lib.SamaelId, savedata.PERSISTENT_DATA.SamaelUnlockData)
				SyncCompletionMarksWithRepentogon(lib.TaintedSamaelId, savedata.PERSISTENT_DATA.TaintedSamaelUnlockData)
			end
		end
	end)
end

--------------------------------------------------
---- LOAD OTHER FILES
--------------------------------------------------

local SCRIPTS = {
	"constants",
	"callbacks",
	"backdrop_data",
	"fade",
	"bag_o_bones",
	"death_pool",
	"mod_content",
	"active_item_rendering",
	
	"dss/dssmenu",
	"dss/changelogs",
	
	"fragment/fragment",
	"fragment/ferryman",
	"fragment/lost_soul",
	"fragment/custom_portal",
	"fragment/ferryman_room",
	
	--"special/teaser",
	
	"chars/samael",
	"chars/samael_ending",
	"chars/samael_birthright",
	"chars/memento_mori",
	"chars/memento_mori_emoji_glasses",
	
	"api_override",
	
	"items/denial",
	"items/anger",
	"items/bargaining",
	"items/depression",
	"items/acceptance",
	"items/punishment_of_the_grave",
	"items/remembrance_of_death",
	"items/remembrance_of_the_forgotten",
	"items/sigil_of_lilith",
	"items/sigil_of_samael",
	"items/soul_of_samael_and_reaper_bum",
	"items/thanatophilia_def",
	"items/thanatophilia",
	"items/thanatophobia",
	"items/thanatosis",
	"items/xiii",
	"items/xiii_reversed",
	"items/thanatos",
	"items/fragment_fragment",
	"items/jar_of_scythes",
	"items/wraith_skull",
	"items/trumpet_of_woe",
	
	"challenges/the_reaper",
	
	"extra_room_stuff",
}

lib.Log("Loading scripts...")
for _, script in ipairs(SCRIPTS) do
	include("samaelscripts/"..script)
end
lib.Log("Finished loading scripts.")

--------------------------------------------------
---- RUN LAST
--------------------------------------------------

function mod:TrackLastKnownGridIndex()
	local level = game:GetLevel()
	local index = level:GetCurrentRoomIndex()
	if index >= 0 then
		mod:GetAllFloorData().LastKnownGridIndex = index
	end
	mod:GetAllFloorData().LastRoomID = level:GetCurrentRoomDesc().Data.Variant
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.TrackLastKnownGridIndex)

-- If the player loses holy mantle when I think they shouldn't have, give it back and try to hide the fact it happened.
-- UPDATE: Turns out this is all super unecessary. Just use player:SetMinDamageCooldown(n) for your invuln!!!
-- Leaving this in anyway just in case.
function mod:FixHolyMantle(player)
	local pData = player:GetData()
	local pEffects = player:GetEffects()
	
	local prevMantles = pData.samaelHolyMantleCount or 0
	local numMantles = pEffects:GetCollectibleEffectNum(CollectibleType.COLLECTIBLE_HOLY_MANTLE)
	pData.samaelHolyMantleCount = numMantles
	
	if numMantles >= prevMantles then return end
	
	local playerIsInvincible = (pData.mementoMoriActive and not pData.disjointedMementoMori) or (pData.mementoMoriIFrames or 0) > 0
			or pData.wraithActive or (pData.wraithCooldown or 0) > 0 or pData.doingIsaacKillAnimation
			or pData.fakeDeathPillActive or (pData.fakeDeathPillRecovery or 0) > 0 or pData.samaelPotgRevive
	
	if playerIsInvincible then
		for _, eff in pairs(Isaac.FindByType(EntityType.ENTITY_EFFECT, 16, 11)) do
			eff:Remove()
		end
		lib.SuppressSound(SoundEffect.SOUND_HOLY_MANTLE)
		pEffects:AddCollectibleEffect(CollectibleType.COLLECTIBLE_HOLY_MANTLE)
		player:ResetDamageCooldown()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, mod.FixHolyMantle)

--------------------------------------------------
---- CONSOLE COMMANDS
--------------------------------------------------

function mod:RetroactiveSamaelUnlocks()
	mod.ContentManager:GrantAllVanillaMarks(lib.SamaelId)
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:GetPlayerType() == lib.SamaelId then
			mod.ContentManager:GrantCustomMarkToOnePlayer(player, mod.ACHIEVEMENTS.SAMAEL_ENDING, true, true)
			break
		end
	end
	
	lib.Log("Granted all completion marks for Samael (but not for Tainted Samael)", true)
end

function mod:UnlockTaintedSamael()
	mod.ContentManager:UnlockTaintedCharacter(lib.TaintedSamaelId)
	
	lib.Log("Unlocked Tainted Samael", true)
end

function mod:ConsoleCommands(command, params)
	command = string.lower(command)
	params = string.lower(params)
	
	if command ~= "samael" then return end
	
	if params == "testcards" then
		local pillColor = Isaac.AddPillEffectToPool(mod.ITEMS.THANATOSIS)
		local room = game:GetRoom()
		Isaac.Spawn(5, 70, pillColor, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
		Isaac.Spawn(5, 300, mod.ITEMS.XIII, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
		Isaac.Spawn(5, 300, mod.ITEMS.XIII_REVERSED, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
		Isaac.Spawn(5, 300, mod.ITEMS.SOUL_OF_SAMAEL, room:FindFreePickupSpawnPosition(room:GetCenterPos()), lib.ZeroVector, nil)
	elseif params == "unlockall" or params == "unlock all" then
		mod:RetroactiveSamaelUnlocks()
	elseif params == "unlocktainted" or params == "unlock tainted" then
		mod:UnlockTaintedSamael()
	end
end
mod:AddCallback(ModCallbacks.MC_EXECUTE_CMD, mod.ConsoleCommands)

--------------------------------------------------
---- STAGEAPI COMPAT
--------------------------------------------------

if StageAPI and StageAPI.Loaded then
	StageAPI.AddPlayerGraphicsInfo(lib.SamaelId, {
		Portrait = "gfx/ui/playerportrait_samael.png",
		Name = "gfx/ui/boss/playername_samael.png",
		PortraitBig = "gfx/ui/playerportrait_samael.png",
		NoShake = false,
	})
	StageAPI.AddPlayerGraphicsInfo(lib.TaintedSamaelId, {
		Portrait = "gfx/ui/playerportrait_samael_b.png",
		Name = "gfx/ui/boss/playername_samael.png",
		PortraitBig = "gfx/ui/playerportrait_samael_b.png",
		NoShake = true,
	})
	StageAPI.AddPlayerGraphicsInfo(lib.OtherSamaelId, {
		Portrait = "gfx/ui/playerportrait_samael_i.png",
		Name = "gfx/ui/boss/playername_samael.png",
		PortraitBig = "gfx/ui/playerportrait_samael_i.png",
		NoShake = true,
	})
end

--------------------------------------------------
---- SPECIALIST DANCE COMPATABILITY + FIX
--------------------------------------------------

local function UseAltDance()
	return game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()) % 100 < 4
end

local function GetDanceMusic()
	if UseAltDance() then
		local id = Isaac.GetMusicIdByName("(Samael) Mass Destruction")
		if id and id > 0 then
			return id
		end
	end
	return Isaac.GetMusicIdByName("specialist")
end

local function GetDanceCostume()
	if UseAltDance() then
		local id = Isaac.GetCostumeIdByPath("gfx/characters/samael_dance_alt.anm2")
		if id and id > 0 then
			return id
		end
	end
	return Isaac.GetCostumeIdByPath("gfx/characters/samael_dance.anm2")
end

include("samaelscripts/specialist_fix")

mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, function()
	if SpecialistModAPI then
		local costume = GetDanceCostume()
		if costume and costume > 0 then
			SpecialistModAPI:AddDanceCostume(lib.SamaelId, costume, true)
			SpecialistModAPI:AddDanceCostume(lib.TaintedSamaelId, costume, true)
			SpecialistModAPI:AddDanceCostume(lib.OtherSamaelId, costume, true)
		end
		if SpecialistModdedCharFix then
			SpecialistModdedCharFix.CUSTOM_MUSIC[lib.SamaelId] = GetDanceMusic()
			SpecialistModdedCharFix.CUSTOM_MUSIC[lib.TaintedSamaelId] = GetDanceMusic()
			SpecialistModdedCharFix.CUSTOM_MUSIC[lib.OtherSamaelId] = GetDanceMusic()
		end
	end
end)

--------------------------------------------------
---- FINISH UP
--------------------------------------------------

-- LUAMOD fix
if game:GetFrameCount() > 0 then
	LoadSaveData()
end

mod.SuccessfullyLoaded = true

lib.Log("Samael finished loading! Version " .. mod.VERSION)
