return function(ContentManager, root, shaderName)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local game = ContentManager.Game
local lib = ContentManager.Lib

----------------------------------------------------------------------------------------------------
---- Unlock Condition Checking
----------------------------------------------------------------------------------------------------

-- Credit to Xalum and Thicco Catto
local CurrentRunCanGrantUnlocks = nil
function ContentManager:CanRunUnlockAchievements()
	if CurrentRunCanGrantUnlocks ~= nil then return CurrentRunCanGrantUnlocks end

	local machine = Isaac.Spawn(6, 11, 0, Vector.Zero, Vector.Zero, nil)
	CurrentRunCanGrantUnlocks = machine:Exists()
	machine:Remove()

	return CurrentRunCanGrantUnlocks
end
mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, function() CurrentRunCanGrantUnlocks = nil end)
mod:AddCallback(ModCallbacks.MC_POST_GAME_END, function() CurrentRunCanGrantUnlocks = nil end)
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function() CurrentRunCanGrantUnlocks = nil end)

-- Returns true if all of the given unlockConditions are true.
function ContentManager:CheckUnlockConditions(unlockConditions)
	local unlocked = true
	
	if unlockConditions.PlayerTypes then
		if unlockConditions.RequiredVanillaMarks then
			unlocked = unlocked and ContentManager:CheckVanillaMarks(unlockConditions.PlayerTypes, unlockConditions.RequiredVanillaMarks, unlockConditions.HardMode)
		end
		if unlockConditions.RequiredCustomMarks then
			unlocked = unlocked and ContentManager:CheckCustomMarks(unlockConditions.PlayerTypes, unlockConditions.RequiredCustomMarks, unlockConditions.HardMode)
		end
	end
	if unlockConditions.RequiredCustomAchievements then
		unlocked = unlocked and ContentManager:CheckCustomAchievements(unlockConditions.RequiredCustomAchievements)
	end
	
	return unlocked
end

----------------------------------------------------------------------------------------------------
---- Unlock Condition Builders
----------------------------------------------------------------------------------------------------

function ContentManager:MakeUnlockConditions(playerTypes, vanillaMarks, customMarks, hardMode, customAchievements, desc, subDesc)
	playerTypes = lib.ToTable(playerTypes)
	
	if vanillaMarks then
		vanillaMarks = ContentManager:ValidateVanillaMarks(playerTypes, vanillaMarks)
		if not vanillaMarks then return end
	end
	if customMarks then
		customMarks = ContentManager:ValidateCustomMarks(playerTypes, customMarks)
		if not customMarks then return end
	end
	if customAchievements then
		customAchievements = ContentManager:ValidateCustomAchievements(customAchievements)
		if not customAchievements then return end
	end
	
	if not desc and vanillaMarks and not customMarks and not customAchievements then
		desc = ContentManager:GenerateVanillaCompletionDesc(playerTypes, vanillaMarks, hardMode)
	end
	
	return {
		PlayerTypes = playerTypes,
		RequiredVanillaMarks = vanillaMarks,
		RequiredCustomMarks = customMarks,
		HardMode = hardMode or false,
		RequiredCustomAchievements = customAchievements,
		Desc = desc,
		SubDesc = subDesc,
	}
end

-- Unlock condition where the given character(s) must have the given vanilla completion marks.
function ContentManager:RequireVanillaMarks(playerTypes, vanillaMarks, hardMode, desc, subDesc)
	return ContentManager:MakeUnlockConditions(playerTypes, vanillaMarks, nil, hardMode, nil, desc, subDesc)
end

-- Unlock condition where the given character(s) must have the given custom completion marks.
function ContentManager:RequireCustomMarks(playerTypes, marks, hardMode, desc, subDesc)
	return ContentManager:MakeUnlockConditions(playerTypes, nil, marks, hardMode, nil, desc, subDesc)
end

-- Unlock condition where the given custom achievement(s) must be acquired.
function ContentManager:RequireCustomAchievements(achievements, desc, subDesc)
	return ContentManager:MakeUnlockConditions(nil, nil, nil, nil, achievements, desc, subDesc)
end

----------------------------------------------------------------------------------------------------
---- Unlockable Management
----------------------------------------------------------------------------------------------------

-- Checks ALL the unlockables and updates their unlock status.
-- If they're newly unlocked, trigger the achievement popup.
function ContentManager:CheckForNewUnlocks(loading)
	lib.Log("Checking unlocks...")
	for class, subCatalog in pairs(ContentManager.CATALOG) do
		for key, itemData in pairs(subCatalog) do
			if itemData.Unlockable then
				local unlockData = itemData.Unlockable
				--lib.Log("Checking unlock conditions for " .. key)
				local unlocked = ContentManager:CheckUnlockConditions(unlockData.UnlockConditions)
				
				if unlocked ~= unlockData.Unlocked then
					local current = lib.BoolStr(unlockData.Unlocked)
					local new = lib.BoolStr(unlocked)
					
					if unlocked and (not loading or itemData.New) then
						lib.Log("Unlock status for `" .. key .. "` has changed from `" .. current .. "` to `" .. new .. "`.")
						if unlockData.AchievementGraphic and ContentManager:AllowUnlockPopup(itemData) then
							lib.Log("New unlock! Triggering popup: " .. unlockData.AchievementGraphic)
							ContentManager:QueueUnlockPopup(unlockData.AchievementGraphic)
						else
							lib.Log("No popup gfx for unlock: " .. key)
						end
					end
					
					itemData.New = false
					unlockData.Unlocked = unlocked
				end
			end
		end
	end
	lib.Log("Done checking unlocks.")
end

----------------------------------------------------------------------------------------------------
---- Unlockable Definition
----------------------------------------------------------------------------------------------------

ContentManager.MiscUnlockGroup = "misc unlocks"
ContentManager.NonUnlockGroup = "non-unlocks"

local numGroups = 0
ContentManager.UnlockGroupId = {
	[ContentManager.MiscUnlockGroup] = -1,
	[ContentManager.NonUnlockGroup] = -2,
}

ContentManager.UnlockGroupIcons = {}

-- Defines a custom unlockable.
function ContentManager:DefineUnlockable(group, name, id, unlockConditions, achievementGraphic, replacementInfo)
	local pType
	
	if not group and unlockConditions.PlayerTypes and #unlockConditions.PlayerTypes == 1
			and (unlockConditions.RequiredVanillaMarks or unlockConditions.RequiredCustomMarks)
			and not unlockConditions.RequireCustomAchievements then
		pType = unlockConditions.PlayerTypes[1]
		local data = ContentManager.VanillaMarkCharData[pType]
		if data then
			group = data.Name
			if data.IsTainted then
				group = "Tainted " .. group
			end
		end
	end
	
	if not group then
		group = ContentManager.MiscUnlockGroup
	end
	
	group = string.lower(group)
	
	if not ContentManager.UnlockGroupId[group] then
		ContentManager.UnlockGroupId[group] = numGroups + 1
		numGroups = numGroups + 1
	end
	
	if not ContentManager.UnlockGroupIcons[group] and pType then
		ContentManager.UnlockGroupIcons[group] = ContentManager.CATALOG[ContentManager.CLASS.CHARACTER][pType].AchievementGroupIcon
	end
	
	return {
		Group = group,
		UnlockConditions = unlockConditions,
		AchievementGraphic = achievementGraphic,
		ReplacementInfo = replacementInfo,
		Unlocked = ContentManager:CheckUnlockConditions(unlockConditions),
	}
end

----------------------------------------------------------------------------------------------------
---- Encyclopedia
----------------------------------------------------------------------------------------------------

local EncyTypeToClass = {
	items = ContentManager.CLASS.ITEM,
	trinkets = ContentManager.CLASS.TRINKET,
	cards = ContentManager.CLASS.CARD,
	runes = ContentManager.CLASS.CARD,
	souls = ContentManager.CLASS.CARD,
	pills = ContentManager.CLASS.PILL,
}

function ContentManager.EncyclopediaUnlockFunc(tab)
	local class = EncyTypeToClass[tab.typeString]
	local key = tab.ItemId
	
	if class and key then
		local data = ContentManager.CATALOG[class][key]
		if data and ContentManager:ShowAsLockedInEncyclopedia(data) then
			local desc = data.Unlockable.UnlockConditions.Desc
			if desc then
				tab.Desc = desc
				local subDesc = data.Unlockable.UnlockConditions.SubDesc
				if subDesc then
					desc = desc .. " (" .. subDesc .. ")"
				end
			end
			tab.TargetColor = Encyclopedia.LockColor
			return tab
		end
	end
end

----------------------------------------------------------------------------------------------------
end
