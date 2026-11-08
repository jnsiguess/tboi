return function(ContentManager)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local game = ContentManager.Game
local lib = ContentManager.Lib

local Achievements = {}
ContentManager.CustomAchievements = Achievements

function ContentManager:ResetAchievementDataForFileLoad(achievement)
	for achievement, status in pairs(Achievements) do
		Achievements[achievement] = false
	end
end

function ContentManager:ValidateCustomAchievements(achievements)
	achievements = lib.ToTable(achievements)
	
	for _, ach in pairs(achievements) do
		if Achievements[ach] == nil then
			lib.LogErr("Unregistered custom achievement: " .. ach)
			return
		end
	end
	
	return achievements
end

----------------------------------------------------------------------------------------------------
---- Adding achievements
----------------------------------------------------------------------------------------------------

function ContentManager:RegisterAchievement(achievement)
	if not achievement then
		lib.LogErr("Tried to register a NULL custom achievement string!")
		return
	end
	if not Achievements[achievement] then
		Achievements[achievement] = false
	else
		lib.LogErr("Tried to re-initialize custom achievement: " .. achievement)
	end
end

----------------------------------------------------------------------------------------------------
---- Locking/Unlocking
----------------------------------------------------------------------------------------------------

-- Returns true if the given custom achievement(s) are owned.
function ContentManager:CheckCustomAchievements(achievementsToCheck)
	for _, achievement in pairs(lib.ToTable(achievementsToCheck)) do
		if not Achievements[achievement] then
			return false
		end
	end
	
	return true
end

-- Grants the given custom achievement.
function ContentManager:GrantCustomAchievement(achievement, force)
	if not ContentManager:CanRunUnlockAchievements() and not force then return end
	
	if Achievements[achievement] == nil then
		lib.LogErr("Tried to grant unrecognized custom achievement: " .. achievement)
	elseif not Achievements[achievement] then
		lib.Log("Unlocked custom achievement: " .. achievement)
		Achievements[achievement] = true
		ContentManager:CheckForNewUnlocks()
	end
end

function ContentManager:GrantAllCustomAchievements()
	local grantedNew = false
	
	for achievement, unlocked in pairs(Achievements) do
		if not Achievements[achievement] then
			grantedNew = true
		end
		Achievements[achievement] = true
	end
	
	if grantedNew then
		ContentManager:CheckForNewUnlocks()
	end
end

function ContentManager:ClearCustomAchievements()
	for achievement, unlocked in pairs(Achievements) do
		Achievements[achievement] = false
	end
end

----------------------------------------------------------------------------------------------------
---- Saving/Loading
----------------------------------------------------------------------------------------------------

function ContentManager:GetSaveDataForCustomAchievements()
	return Achievements
end

function ContentManager:LoadCustomAchievementData(saveData)
	if not saveData then return end
	
	for achievement, state in pairs(saveData) do
		Achievements[achievement] = state
	end
end

----------------------------------------------------------------------------------------------------
end
