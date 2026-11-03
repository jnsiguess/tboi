return function(ContentManager)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local game = ContentManager.Game
local lib = ContentManager.Lib

local CharData = {}
ContentManager.VanillaMarkCharData = CharData

----------------------------------------------------------------------------------------------------
---- Constants / Tables
----------------------------------------------------------------------------------------------------

-- The number value used to represent each mark is the same as the layer ID used for the post-it anm2.
local VanillaMarks = {
	DELIRIUM = 0,
	MOMS_HEART = 1,
	ISAAC = 2,
	SATAN = 3,
	BOSS_RUSH = 4,
	BLUE_BABY = 5,
	LAMB = 6,
	MEGA_SATAN = 7,
	GREED = 8,
	HUSH = 9,
	MOTHER = 10,
	BEAST = 11,
}
ContentManager.VanillaMarks = VanillaMarks

local VanillaMarkSets = {
	POLAROID_NEGATIVE_PATH = {
		VanillaMarks.ISAAC,
		VanillaMarks.SATAN,
		VanillaMarks.BLUE_BABY,
		VanillaMarks.LAMB,
	},
	SOUL_STONE_PATH = {
		VanillaMarks.BOSS_RUSH,
		VanillaMarks.HUSH,
	},
	ALL = {}
}
for _, id in pairs(VanillaMarks) do
	table.insert(VanillaMarkSets.ALL, id)
end
ContentManager.VanillaMarkSets = VanillaMarkSets

-- Boss ID from Game():GetRoom():GetBossID() to corresponding completion mark.
local BossIdToVanillaMark = {
	[8] = VanillaMarks.MOMS_HEART,
	[25] = VanillaMarks.MOMS_HEART, -- It Lives
	[90] = VanillaMarks.MOMS_HEART, -- Mausoleum Heart
	[24] = VanillaMarks.SATAN,
	[39] = VanillaMarks.ISAAC,
	[40] = VanillaMarks.BLUE_BABY,
	[54] = VanillaMarks.LAMB,
	[55] = VanillaMarks.MEGA_SATAN,
	[63] = VanillaMarks.HUSH,
	[70] = VanillaMarks.DELIRIUM,
	[88] = VanillaMarks.MOTHER,
	[100] = VanillaMarks.BEAST,
	[62] = VanillaMarks.GREED,
	[71] = VanillaMarks.GREED, -- Ultra Greedier
}

local VanillaMarkToDisplayName = {
	[VanillaMarks.DELIRIUM] = "Delirium",
	[VanillaMarks.MOMS_HEART] = "Mom's Heart",
	[VanillaMarks.ISAAC] = "Isaac",
	[VanillaMarks.SATAN] = "Satan",
	[VanillaMarks.BOSS_RUSH] = "the Boss Rush",
	[VanillaMarks.BLUE_BABY] = "???",
	[VanillaMarks.LAMB] = "The Lamb",
	[VanillaMarks.MEGA_SATAN] = "Mega Satan",
	[VanillaMarks.GREED] = "Greed Mode",
	[VanillaMarks.HUSH] = "Hush",
	[VanillaMarks.MOTHER] = "Mother",
	[VanillaMarks.BEAST] = "The Beast",
}

-- For parsing arbitrary strings as vanilla completion marks.
local VanillaMarkToNormalizedStrings = {
	[VanillaMarks.DELIRIUM] = {"DELIRIUM", "DELERIUM", "DELI", "VOID"},
	[VanillaMarks.MOMS_HEART] = {"MOMSHEART", "HEART", "WOMB", "ITLIVES", "LIVES"},
	[VanillaMarks.ISAAC] = {"ISAAC", "CATHEDRAL"},
	[VanillaMarks.SATAN] = {"SATAN", "STAN", "SHEOL"},
	[VanillaMarks.BOSS_RUSH] = {"BOSSRUSH", "BOSS", "RUSH"},
	[VanillaMarks.BLUE_BABY] = {"BLUEBABY", "CHEST", "???"},
	[VanillaMarks.LAMB] = {"LAMB", "DARKROOM"},
	[VanillaMarks.MEGA_SATAN] = {"MEGASATAN", "MEGASTAN"},
	[VanillaMarks.GREED] = {"GREED", "GREEDMODE", "ULTRAGREED", "GREEDIER", "GREEDIERMODE", "ULTRAGREEDIER"},
	[VanillaMarks.HUSH] = {"HUSH", "BLUEWOMB"},
	[VanillaMarks.MOTHER] = {"MOTHER", "WITNESS", "CORPSE"},
	[VanillaMarks.BEAST] = {"BEAST", "HOME", "ASCENT"},
}
local NormalizedStringToVanillaMark = {
	POLNEG = VanillaMarkSets.POLAROID_NEGATIVE_PATH,
	POLNEGPATH = VanillaMarkSets.POLAROID_NEGATIVE_PATH,
	POLAROIDNEGATIVE = VanillaMarkSets.POLAROID_NEGATIVE_PATH,
	POLAROIDNEGATIVEPATH = VanillaMarkSets.POLAROID_NEGATIVE_PATH,
	
	SOUL = VanillaMarkSets.SOUL_STONE_PATH,
	SOULSTONE = VanillaMarkSets.SOUL_STONE_PATH,
	SOULPATH = VanillaMarkSets.SOUL_STONE_PATH,
	SOULSTONEPATH = VanillaMarkSets.SOUL_STONE_PATH,
	
	ALL = VanillaMarkSets.ALL,
	EVERYTHING = VanillaMarkSets.ALL,
}
for markID, strings in pairs(VanillaMarkToNormalizedStrings) do
	for _, str in pairs(strings) do
		NormalizedStringToVanillaMark[str] = markID
	end
end
for _, markID in pairs(VanillaMarks) do
	NormalizedStringToVanillaMark[""..markID] = markID
end

----------------------------------------------------------------------------------------------------
---- Initialization / Parsing
----------------------------------------------------------------------------------------------------

local function InitMarks(allUnlocked)
	local tab = {}
	
	local defaultState = allUnlocked or false
	for _, markID in pairs(VanillaMarks) do
		tab[markID] = {Unlock = defaultState, Hard = defaultState}
	end
	
	return tab
end

function ContentManager:ResetVanillaMarkDataForFileLoad()
	for _, data in pairs(CharData) do
		data.Marks = InitMarks()
	end
end

local function ParseMarkIfString(input)
	if type(input) ~= "string" then
		return input
	end
	-- Converts strings to uppercase, removes whitespace, punctuation and any leading "THE".
	local normalizedString = string.upper(input):gsub("[%c%p%s]", ""):gsub("^THE", "")
	return NormalizedStringToVanillaMark[normalizedString]
end

local function ParseMarks(vanillaMarks, outputMarks)
	for _, mark in pairs(lib.ToTable(vanillaMarks)) do
		local parsed = ParseMarkIfString(mark)
		if type(parsed) == "table" then
			ParseMarks(parsed, outputMarks)
		elseif type(parsed) == "number" and parsed >= 0 and parsed <= 11 then
			outputMarks[parsed] = true
		else
			lib.LogErr("Unrecognized vanilla completion mark: " .. mark)
		end
	end
end

function ContentManager:ValidateVanillaMarks(playerType, vanillaMarks)
	for _, pType in pairs(playerType) do
		if not CharData[pType] then
			lib.LogErr("Character #" .. pType .. " is not registered for vanilla completion mark tracking.")
			return
		end
	end
	
	local dedupedMarks = {}
	ParseMarks(vanillaMarks, dedupedMarks)
	local outputMarks = {}
	for mark, _ in pairs(dedupedMarks) do
		table.insert(outputMarks, mark)
	end
	table.sort(outputMarks)
	return outputMarks
end

function ContentManager:GenerateVanillaCompletionDesc(playerTypes, vanillaMarks, hardMode)
	local desc = "Beat "
	if #vanillaMarks == 12 then
		desc = desc .. "everything"
	else
		for i=1, #vanillaMarks do
			desc = desc .. VanillaMarkToDisplayName[vanillaMarks[i]]
			if i == #vanillaMarks - 1 then
				desc = desc .. " and "
			elseif i ~= #vanillaMarks then
				desc = desc .. ", "
			end
		end
	end
	if hardMode then
		desc = desc .. "on hard "
	end
	desc = desc .. " as "
	for i=1, #playerTypes do
		local name = CharData[playerTypes[i]].Name
		if CharData[playerTypes[i]].IsTainted then
			name = "Tainted " .. name
		end
		desc = desc .. name
		if i == #playerTypes - 1 then
			desc = desc .. " and "
		elseif i ~= #playerTypes then
			desc = desc .. ", "
		end
	end
	return desc
end

----------------------------------------------------------------------------------------------------
---- Character Management
----------------------------------------------------------------------------------------------------

-- Registers a character for Vanilla Completion Mark tracking.
function ContentManager:RegisterCharacterForVanillaMarkTracking(characterName, isTainted, existingData)
	local logName = characterName
	if isTainted then
		logName = logName .. " (Tainted)"
	end
	
	local playerType = Isaac.GetPlayerTypeByName(characterName, isTainted)
	
	if playerType < 0 then
		lib.LogErr("Can't register character: No character found named `" .. logName .. "`")
		return
	elseif playerType < 41 then
		lib.LogErr("Can't register a vanilla character: " .. logName)
		return
	end
	
	CharData[playerType] = {
		Name = characterName,
		IsTainted = isTainted,
		Marks = InitMarks(),
	}
	
	if not REPENTOGON then
		PauseScreenCompletionMarksAPI:AddModCharacterCallback(playerType, function()
			return ContentManager:GetVanillaMarks(playerType)
		end)
	end
end

-- Loads existing Vanilla Completion Mark data for a tracked character.
-- For loading SaveData.
function ContentManager:LoadCharacterData(playerType, existingData)
	if not CharData[playerType] then
		lib.LogErr("Tried to load Vanilla Mark data for unmanaged PlayerType: #" .. playerType)
		return
	end
	
	local logName = CharData[playerType].Name
	if CharData[playerType].IsTainted then
		logName = logName .. " (Tainted)"
	end
	
	local existingDataIsEmpty = true
	
	if existingData then
		local convertedData = {}
		for k, v in pairs(existingData) do
			existingDataIsEmpty = false
			
			local markID = ParseMarkIfString(k)
			if type(markID) == "number" then
				convertedData[markID] = v
			end
		end
		existingData = convertedData
	end
	
	if existingData and not existingDataIsEmpty and existingData[0] then
		CharData[playerType].Marks = existingData
		lib.Log("Loaded existing data for character: " .. logName)
	else
		if not existingDataIsEmpty then
			lib.LogErr("Tried to load malformed data for character: " .. logName)
		end
		CharData[playerType].Marks = InitMarks()
	end
	
	ContentManager:CheckForNewUnlocks(true)
end

-- Returns the data that needs to be saved for a managed character.
function ContentManager:GetCharacterSaveData(playerType)
	if CharData[playerType] then
		return CharData[playerType].Marks
	else
		lib.LogErr("Vanilla mark data requested for unmanaged char: #" .. playerType)
	end
end

function ContentManager:ClearVanillaMarks(playerType)
	if CharData[playerType] then
		CharData[playerType].Marks = InitMarks()
	else
		lib.LogErr("Tried to LOCK all vanilla marks for unmanaged char: #" .. playerType)
	end
end

function ContentManager:GrantAllVanillaMarks(playerType)
	if CharData[playerType] then
		CharData[playerType].Marks = InitMarks(true)
		ContentManager:CheckForNewUnlocks()
	else
		lib.LogErr("Tried to UNLOCK all vanilla marks for unmanaged char: #" .. playerType)
	end
end

-- Returns true if all the given characters have all the given completion marks.
function ContentManager:CheckVanillaMarks(playerTypes, marksToCheck, hardMode)
	local difficultyKey = "Unlock"
	if hardMode then
		difficultyKey = "Hard"
	end
	
	for _, pType in pairs(lib.ToTable(playerTypes)) do
		if not CharData[pType] then
			lib.LogErr("Tried to check vanilla marks for an unmanaged char: #" .. pType)
			return false
		end
		for _, mark in pairs(lib.ToTable(marksToCheck)) do
			if not CharData[pType].Marks[mark][difficultyKey] then
				return false
			end
		end
	end
	
	return true
end

function ContentManager:GetVanillaMarks(pType)
	if CharData[pType] then
		return CharData[pType].Marks
	else
		lib.LogErr("Tried to get vanilla marks for unmanaged char: #" .. pType)
	end
end

----------------------------------------------------------------------------------------------------
---- Vanilla Completion Detection
----------------------------------------------------------------------------------------------------

local function GrantCompletionMark(mark, isHardMode)
	local newMark = false
	local markName = VanillaMarkToDisplayName[mark]
	
	for p = 0, game:GetNumPlayers() - 1 do
		local player = Isaac.GetPlayer(p)
		local pType = player:GetPlayerType()
		
		if CharData[pType] then
			local data = CharData[pType].Marks[mark]
			
			local logName = player:GetName()
			if lib.IsTaintedChar(player) then
				logName = logName .. " (Tainted)"
			end
			
			if isHardMode and not data.Hard then
				lib.Log("Adding vanilla mark `" .. markName .. "` (HARD MODE) to `" .. logName .. "`")
				data.Hard = true
				data.Unlock = true
				newMark = true
			elseif not data.Unlock then
				lib.Log("Adding vanilla mark `" .. markName .. "` to `" .. logName .. "`")
				data.Unlock = true
				newMark = true
			end
		end
	end
	
	if newMark then
		ContentManager:CheckForNewUnlocks()
	end
end

local function MegaSatanKilled()
	local megaSatan
	for _, npc in ipairs(Isaac.FindByType(EntityType.ENTITY_MEGA_SATAN_2, 0)) do
		megaSatan = npc
		break
	end
	if not megaSatan then return end
	local sprite = megaSatan:GetSprite()
	return sprite:IsPlaying("Death") and sprite:GetFrame() >= 110
end

local function BeastKilled()
	local beast
	for _, npc in ipairs(Isaac.FindByType(EntityType.ENTITY_BEAST, 0)) do
		beast = npc
		break
	end
	if not beast then return end
	local sprite = beast:GetSprite()
	return sprite:IsPlaying("Death") and sprite:GetFrame() >= 90
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if not ContentManager:CanRunUnlockAchievements() then return end
	
	local levelStage = game:GetLevel():GetStage()
	local room = game:GetRoom()
	local roomType = room:GetType()
	local isHardMode = (game.Difficulty == Difficulty.DIFFICULTY_HARD or game.Difficulty == Difficulty.DIFFICULTY_GREEDIER)
	
	if roomType == RoomType.ROOM_BOSS then
		local isVoid = levelStage == LevelStage.STAGE7
		local boss = room:GetBossID()
		local mark = BossIdToVanillaMark[boss]
		
		if mark and (not isVoid or mark == VanillaMarks.DELIRIUM)
				and (room:IsClear() or (mark == VanillaMarks.MEGA_SATAN and MegaSatanKilled())) then
			GrantCompletionMark(mark, isHardMode)
		end
	elseif levelStage == LevelStage.STAGE8 and roomType == RoomType.ROOM_DUNGEON then
		if BeastKilled() then
			GrantCompletionMark(VanillaMarks.BEAST, isHardMode)
		end
	elseif roomType == RoomType.ROOM_BOSSRUSH and room:IsAmbushDone() then
		GrantCompletionMark(VanillaMarks.BOSS_RUSH, isHardMode)
	end
end)

----------------------------------------------------------------------------------------------------
end
