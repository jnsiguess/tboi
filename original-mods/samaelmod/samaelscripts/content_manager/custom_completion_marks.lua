return function(ContentManager)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local game = ContentManager.Game
local lib = ContentManager.Lib

local CustomMarks = {}
local CharData = {}
local UninstalledCharacterData = { Normal = {}, Tainted = {} }

function ContentManager:ResetCustomMarkDataForFileLoad()
	CharData = {}
	UninstalledCharacterData = { Normal = {}, Tainted = {} }
end

----------------------------------------------------------------------------------------------------
---- Add a completion mark
----------------------------------------------------------------------------------------------------

-- Registers a custom completion mark.
function ContentManager:RegisterCustomMark(mark, sprite, anim, allowedChars, hideIfLocked)
	local data = {
		Sprite = sprite,
		Animation = anim,
		HideIfLocked = hideIfLocked,
	}
	
	if allowedChars then
		data.AllowedChars = {}
		for _, playerType in pairs(lib.ToTable(allowedChars)) do
			data.AllowedChars[playerType] = true
		end
	end
	
	CustomMarks[mark] = data
end

function ContentManager:ValidateCustomMarks(playerTypes, customMarks)
	customMarks = lib.ToTable(customMarks)
	
	for _, mark in pairs(customMarks) do
		if not CustomMarks[mark] then
			lib.LogErr("Custom Mark '" .. mark .. "' isn't registered!")
			return
		end
		if playerTypes then
			for _, pType in pairs(playerTypes) do
				if not ContentManager:CanGetCustomMark(pType, mark) then
					lib.LogErr("Character #" .. pType .." cannot obtain custom mark `" .. mark .. "`")
					return
				end
			end
		end
	end
	
	return customMarks
end
-- SamaelMod.ContentManager:GrantCustomMarkToAllPlayers(SamaelMod.ACHIEVEMENTS.SAMAEL_ENDING)
function ContentManager.GetCustomMarks(playerType, showAll)
	local tab = {}
	
	for mark, data in pairs(CustomMarks) do
		local frame = ContentManager:GetCustomMarkFrame(playerType, mark)
		if frame and (frame > 0 or not data.HideIfLocked or showAll) then
			table.insert(tab, {
				Sprite = data.Sprite,
				Animation = data.Animation,
				Frame = frame,
			})
		end
	end
	
	return tab
end

PauseScreenCompletionMarksAPI:AddModMarksCallback(mod.Name, ContentManager.GetCustomMarks)

----------------------------------------------------------------------------------------------------
---- Unlocking
----------------------------------------------------------------------------------------------------

function ContentManager:CanGetCustomMark(pType, mark)
	return CustomMarks[mark] and (not CustomMarks[mark].AllowedChars or CustomMarks[mark].AllowedChars[pType])
end

-- Grants one player the given custom completion mark.
-- Returns true if a mark was newly granted.
local function GrantMark(player, mark, forceHardMode, force)
	if not ContentManager:CanRunUnlockAchievements() and not force then return end
	
	if not CustomMarks[mark] then
		lib.LogErr("Tried to grant unrecognized custom mark: " .. mark)
		return false
	end
	
	local pType = player:GetPlayerType()
	local pName = player:GetName()
	local isTainted = lib.IsTaintedChar(player)
	local logName = pName
	if isTainted then
		logName = logName .. " (Tainted)"
	end
	
	if not ContentManager:CanGetCustomMark(pType, mark) then
		lib.Log(logName .. " is not allowed to gain custom mark: " .. mark)
		return false
	end
	
	local isHardMode = forceHardMode or game.Difficulty == Difficulty.DIFFICULTY_HARD or game.Difficulty == Difficulty.DIFFICULTY_GREEDIER
	
	if not CharData[pType] then
		CharData[pType] = {
			Name = pName,
			IsTainted = isTainted,
			Marks = {},
		}
	end
	
	if not CharData[pType].Marks[mark] then
		CharData[pType].Marks[mark] = { Unlock = false, Hard = false }
	end
	
	local markStatus = CharData[pType].Marks[mark]
	
	if isHardMode and not markStatus.Hard then
		lib.Log("Adding custom mark `" .. mark .. "` (HARD MODE) to character `" .. logName .. "`")
		markStatus.Hard = true
		markStatus.Unlock = true
		return true
	elseif not markStatus.Unlock then
		lib.Log("Adding custom mark `" .. mark .. "` to character `" .. logName .. "`")
		markStatus.Unlock = true
		return true
	end
	
	return false
end

-- Grants one player the given custom completion mark.
function ContentManager:GrantCustomMarkToOnePlayer(player, mark, forceHardMode, force)
	if GrantMark(player, mark, forceHardMode, force) then
		ContentManager:CheckForNewUnlocks()
	end
end

-- Grants all players the given custom completion mark.
function ContentManager:GrantCustomMarkToAllPlayers(mark, forceHardMode, force)
	local newMark = false
	
	for p = 0, game:GetNumPlayers() - 1 do
		local player = Isaac.GetPlayer(p)
		if player and player:Exists() then
			newMark = newMark or GrantMark(player, mark, forceHardMode, force)
		end
	end
	
	if newMark then
		ContentManager:CheckForNewUnlocks()
	end
end

function ContentManager:ResetCustomMarks(pType)
	CharData[pType] = nil
end

function ContentManager:ResetCustomMarksForEveryone()
	CharData = {}
	UninstalledCharacterData = { Normal = {}, Tainted = {} }
end

----------------------------------------------------------------------------------------------------
---- Checking
----------------------------------------------------------------------------------------------------

-- Returns true if all the given characters have all the given completion marks.
function ContentManager:CheckCustomMarks(playerTypes, marksToCheck, hardMode)
	local difficultyKey = "Unlock"
	if hardMode then
		difficultyKey = "Hard"
	end
	
	for _, pType in pairs(lib.ToTable(playerTypes)) do
		if not CharData[pType] then
			return false
		end
		for _, mark in pairs(lib.ToTable(marksToCheck)) do
			if not CharData[pType].Marks[mark] or not CharData[pType].Marks[mark][difficultyKey] then
				return false
			end
		end
	end
	
	return true
end

-- 0 = Locked
-- 1 = Unlocked
-- 2 = Hard Mode
function ContentManager:GetCustomMarkFrame(pType, mark)
	if not CustomMarks[mark] then
		lib.LogErr("Custom mark not found: " .. mark)
		return
	end
	
	if not ContentManager:CanGetCustomMark(pType, mark) then
		-- Character cannot obtain this mark.
		return
	end
	
	if not CharData[pType] or not CharData[pType].Marks[mark] then
		return 0
	end
	
	local status = CharData[pType].Marks[mark]
	
	if status.Hard then
		return 2
	elseif status.Unlock then
		return 1
	end
	return 0
end

----------------------------------------------------------------------------------------------------
---- Saving/Loading
----------------------------------------------------------------------------------------------------

local function LoadDataInternal(saveData, tainted)
	if not saveData then return end
	
	for name, marks in pairs(saveData) do
		local playerType = Isaac.GetPlayerTypeByName(name, tainted)
		if playerType < 0 then
			-- Character is not installed.
			local logName = name
			if tainted then
				logName = logName .. " (Tainted)"
			end
			lib.Log("Failed to load custom mark data for " .. logName .. " - character is not installed (data will be kept, don't worry).")
			-- Store this data so we don't lose it.
			if tainted then
				UninstalledCharacterData.Tainted[name] = marks
			else
				UninstalledCharacterData.Normal[name] = marks
			end
		else
			CharData[playerType] = {
				Name = name,
				IsTainted = tainted,
				Marks = marks,
			}
		end
	end
end

function ContentManager:LoadCustomMarkData(saveData)
	if not saveData then return end
	LoadDataInternal(saveData.Normal, false)
	LoadDataInternal(saveData.Tainted, true)
end

function ContentManager:GetSaveDataForCustomMarks()
	local saveData = lib.DeepCopy(UninstalledCharacterData)
	
	for pType, data in pairs(CharData) do
		if data.Marks then
			if data.IsTainted then
				saveData.Tainted[data.Name] = data.Marks
			else
				saveData.Normal[data.Name] = data.Marks
			end
		end
	end
	
	return saveData
end

----------------------------------------------------------------------------------------------------
end
