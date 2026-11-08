local game = Game()
local lib

local ContentManager = {
	MiscSaveData = {},
	ACHIEVEMENT_GROUPS = {},
	Game = game,
}

ContentManager.CAN_APPEAR = {
	DISABLED = 1,
	ALLOWED = 2,
	FORCED = 3,
	CUSTOM = 4,
}

ContentManager.CLASS = {
	ITEM = 1,
	TRINKET = 2,
	CARD = 3,
	PILL = 4,
	ENTITY = 5,
	CHARACTER = 6,
}
ContentManager.CLASSNAME = {}
for k, v in pairs(ContentManager.CLASS) do
	ContentManager.CLASSNAME[v] = k
end

ContentManager.CATALOG = {
	[ContentManager.CLASS.ITEM] = {},
	[ContentManager.CLASS.TRINKET] = {},
	[ContentManager.CLASS.CARD] = {},
	[ContentManager.CLASS.PILL] = {},
	[ContentManager.CLASS.ENTITY] = {},
	[ContentManager.CLASS.CHARACTER] = {},
}

local catalogSize = 0
local numCharacters = 0

local spriteCache = {}

local cardFrontsPath
local achievementGfxRoot

local initialized = false
function ContentManager:Init(mod, tab)
	if initialized then
		lib.LogErr("ContentManager already initialized!")
		return
	end
	
	ContentManager.Mod = mod
	ContentManager.ItemConfig = Isaac.GetItemConfig()
	ContentManager.Root = tab.Root
	ContentManager.Shader = tab.Shader
	
	lib = include(tab.Root .. ".lib"):Init(mod)
	ContentManager.Lib = lib
	
	ContentManager.ModPath = lib.GetCurrentModPath()
	ContentManager.CharPortraits = ContentManager.ModPath .. "/content/gfx/characterportraits.anm2"
	ContentManager.CharPortraitsAlt = ContentManager.ModPath .. "/content/gfx/characterportraitsalt.anm2"
	
	cardFrontsPath = tab.EidCardFronts
	achievementGfxRoot = tab.AchievementsGfxRoot
	
	include(tab.Root .. ".pause_screen_completion_marks_api")
	PauseScreenCompletionMarksAPI:SetShader(tab.Shader)
	
	include(tab.Root .. ".unlock_management")(ContentManager)
	include(tab.Root .. ".vanilla_completion_marks")(ContentManager)
	include(tab.Root .. ".custom_completion_marks")(ContentManager)
	include(tab.Root .. ".custom_achievements")(ContentManager)
	include(tab.Root .. ".unlock_popup_renderer")(ContentManager)
	include(tab.Root .. ".dss_completion_mark_render")(ContentManager)
	
	include(tab.Root .. ".spawn_control")(ContentManager)
	include(tab.Root .. ".dss")(ContentManager)
	include(tab.Root .. ".tainted_character")(ContentManager)
	
	if EID then
		EID:setModIndicatorName(mod.Name)
	end
	
	initialized = true
end

----------------------------------------------------------------------------------------------------
---- Saving/Loading
----------------------------------------------------------------------------------------------------

function ContentManager:LoadCatalogSaveData(saveData)
	if not saveData then return end
	for class, subCatalog in pairs(ContentManager.CATALOG) do
		local classSaveData = saveData[class] or {}
		for key, itemData in pairs(subCatalog) do
			local saved = classSaveData[itemData.Name]
			if not saved then
				itemData.New = true
			elseif saved.AppearSetting then
				itemData.AppearSetting = saved.AppearSetting
			end
		end
	end
end

-- Loads saved data. Calling this function also wipes all save file-specific data.
function ContentManager:LoadData(existingData)
	ContentManager.MiscSaveData = {}
	ContentManager:ResetAchievementDataForFileLoad()
	ContentManager:ResetVanillaMarkDataForFileLoad()
	ContentManager:ResetCustomMarkDataForFileLoad()
	
	if existingData then
		ContentManager:LoadCustomMarkData(existingData.CompletionMarks)
		ContentManager:LoadCustomAchievementData(existingData.Achievements)
		ContentManager.MiscSaveData = existingData.Misc or {}
		ContentManager:LoadCatalogSaveData(existingData.Catalog)
	end
	
	ContentManager:CheckForNewUnlocks(true)
end

function ContentManager:GetCatalogSaveData()
	local saveData = {}
	for class, subCatalog in pairs(ContentManager.CATALOG) do
		saveData[class] = {}
		for key, itemData in pairs(subCatalog) do
			saveData[class][itemData.Name] = {
				AppearSetting = itemData.AppearSetting
			}
		end
	end
	return saveData
end

-- Returns info on completion marks and achievements for saving.
function ContentManager:GetSaveData()
	return {
		Catalog = ContentManager:GetCatalogSaveData(),
		CompletionMarks = ContentManager:GetSaveDataForCustomMarks(),
		Achievements = ContentManager:GetSaveDataForCustomAchievements(),
		Misc = ContentManager.MiscSaveData,
	}
end

----------------------------------------------------------------------------------------------------
-- General Utils
----------------------------------------------------------------------------------------------------

function ContentManager:ResetAppearSettings(excludeCharacters)
	for class, subCatalog in pairs(ContentManager.CATALOG) do
		if not excludeCharacters or class ~= ContentManager.CLASS.CHARACTER then
			for key, itemData in pairs(subCatalog) do
				itemData.AppearSetting = ContentManager.CAN_APPEAR.ALLOWED
			end
		end
	end
end

function ContentManager.EntityKey(eType, eVariant, eSubType)
	return "" .. eType .. "." .. (eVariant or 0) .. "." .. (eSubType or 0)
end

local function GetEntityIds(entityName, subType)
	return {
		Type = Isaac.GetEntityTypeByName(entityName),
		Variant = Isaac.GetEntityVariantByName(entityName),
		SubType = subType or 0,
	}
end

local ClassFuncs = {
	[ContentManager.CLASS.ITEM] = {
		EID = "addCollectible",
		Ency = "AddItem",
		GetID = Isaac.GetItemIdByName,
		Config = function(id)
			return ContentManager.ItemConfig:GetCollectible(id)
		end,
	},
	[ContentManager.CLASS.TRINKET] = {
		EID = "addTrinket",
		Ency = "AddTrinket",
		GetID = Isaac.GetTrinketIdByName,
		Config = function(id)
			return ContentManager.ItemConfig:GetTrinket(id)
		end,
	},
	[ContentManager.CLASS.CARD] = {
		EID = "addCard",
		Ency = "AddCard",
		GetID = Isaac.GetCardIdByName,
		Config = function(id)
			return ContentManager.ItemConfig:GetCard(id)
		end,
	},
	[ContentManager.CLASS.PILL] = {
		EID = "addPill",
		Ency = "AddPill",
		GetID = Isaac.GetPillEffectByName,
		Config = function(id)
			return ContentManager.ItemConfig:GetPillEffect(id)
		end,
	},
	[ContentManager.CLASS.ENTITY] = {
		GetID = GetEntityIds,
		--EID = "addEntity",
	},
}

----------------------------------------------------------------------------------------------------
-- Information Accessors
----------------------------------------------------------------------------------------------------

function ContentManager:GetCharacter(pType)
	return ContentManager.CATALOG[ContentManager.CLASS.CHARACTER][pType]
end

function ContentManager:CharacterLocked(pType)
	local charData = ContentManager:GetCharacter(pType)
	if charData and charData.Unlockable then
		return charData.AppearSetting ~= ContentManager.CAN_APPEAR.FORCED
				and not ContentManager:CheckUnlockConditions(charData.Unlockable.UnlockConditions)
	end
end

local function IsLockedOrDisabledInternal(itemData)
	if game:IsGreedMode() and itemData.NoGreed then
		return true
	end
	
	local globalState = ContentManager.MiscSaveData.GlobalAppearSetting or ContentManager.CAN_APPEAR.ALLOWED
	local itemState = itemData.AppearSetting or ContentManager.CAN_APPEAR.ALLOWED
	
	if globalState == ContentManager.CAN_APPEAR.DISABLED then
		return true
	elseif globalState == ContentManager.CAN_APPEAR.FORCED then
		return false
	elseif globalState == ContentManager.CAN_APPEAR.CUSTOM then
		if itemState == ContentManager.CAN_APPEAR.DISABLED then
			return true
		elseif itemState == ContentManager.CAN_APPEAR.FORCED then
			return false
		end
	end
	
	return itemData.Unlockable and not ContentManager:CheckUnlockConditions(itemData.Unlockable.UnlockConditions)
end

function ContentManager:IsLockedOrDisabled(class, key)
	local itemData = ContentManager.CATALOG[class][key]
	return itemData and IsLockedOrDisabledInternal(itemData)
end

function ContentManager:ItemLockedOrDisabled(item)
	return ContentManager:IsLockedOrDisabled(ContentManager.CLASS.ITEM, item)
end

function ContentManager:TrinketLockedOrDisabled(trinket)
	local itemData = ContentManager.CATALOG[ContentManager.CLASS.TRINKET][trinket]
	if itemData and itemData.GolemRock and not FiendFolio then
		return true
	end
	return ContentManager:IsLockedOrDisabled(ContentManager.CLASS.TRINKET, trinket)
end

function ContentManager:CardLockedOrDisabled(card)
	return ContentManager:IsLockedOrDisabled(ContentManager.CLASS.CARD, card)
end

function ContentManager:PillLockedOrDisabled(pillEffect)
	return ContentManager:IsLockedOrDisabled(ContentManager.CLASS.PILL, pillEffect)
end

function ContentManager:EntityLockedOrDisabled(eType, eVariant, eSubType)
	local key = ContentManager.EntityKey(eType, eVariant, eSubType)
	return ContentManager:IsLockedOrDisabled(ContentManager.CLASS.ENTITY, key)
end

function ContentManager:ShowAsLockedInEncyclopedia(itemData)
	local globalState = ContentManager.MiscSaveData.GlobalAppearSetting or ContentManager.CAN_APPEAR.ALLOWED
	return IsLockedOrDisabledInternal(itemData)
			and globalState ~= ContentManager.CAN_APPEAR.DISABLED
			and not (globalState == ContentManager.CAN_APPEAR.CUSTOM and itemData.AppearSetting ~= ContentManager.CAN_APPEAR.ALLOWED)
end

function ContentManager:AllowUnlockPopup(itemData)
	local globalState = ContentManager.MiscSaveData.GlobalAppearSetting or ContentManager.CAN_APPEAR.ALLOWED
	return globalState == ContentManager.CAN_APPEAR.ALLOWED
			or (globalState == ContentManager.CAN_APPEAR.CUSTOM and itemData.AppearSetting == ContentManager.CAN_APPEAR.ALLOWED)
end

function ContentManager:GetItem(item)
	return ContentManager.CATALOG[ContentManager.CLASS.ITEM][item]
end

function ContentManager:GetTrinket(trinket)
	return ContentManager.CATALOG[ContentManager.CLASS.TRINKET][trinket]
end

function ContentManager:GetCard(card)
	return ContentManager.CATALOG[ContentManager.CLASS.CARD][card]
end

function ContentManager:GetPill(pillEffect)
	return ContentManager.CATALOG[ContentManager.CLASS.PILL][pillEffect]
end

----------------------------------------------------------------------------------------------------
-- Item Registration (Items/Trinkets/Consumables)
----------------------------------------------------------------------------------------------------

function ContentManager:Register(tab)
	if not tab.Class or not ContentManager.CLASSNAME[tab.Class] then
		lib.LogErr("Invalid Class: " .. (tab.Class or "NULL"))
		return
	end
	
	if tab.Class == ContentManager.CLASS.CHARACTER then
		return ContentManager:RegisterCharacter(tab)
	end
	
	local classFuncs = ClassFuncs[tab.Class]
	local id = tab.ID or classFuncs.GetID(tab.EntityName or tab.Name)
	local config = classFuncs.Config and classFuncs.Config(id) 
	local name = tab.Name or (config and config.Name)
	
	local catalogEntry = {
		Name = name,
		ID = id,
		Class = tab.Class,
		CatalogID = catalogSize + 1,
		AppearSetting = ContentManager.CAN_APPEAR.ALLOWED,
		Hidden = tab.Hidden,
		NoGreed = tab.NoGreed,
		GolemRock = tab.GolemRock,
		HideInDss = tab.GolemRock ~= nil,
	}
	
	if tab.Class == ContentManager.CLASS.CARD then
		tab.Sprite = tab.Sprite or cardFrontsPath
		catalogEntry.CardWeight = tab.CardWeight
		if tab.CardReplacement then
			catalogEntry.CardReplacement = tab.CardReplacement
			catalogEntry.CardReplacementChance = tab.CardReplacementChance or 0.5
		end
		catalogEntry.CardType = (config and config.CardType) or ItemConfig.CARDTYPE_TAROT
		catalogEntry.MimicCharge = (config and config.MimicCharge) or 6
	end
	
	if Encyclopedia and tab.Class ~= ContentManager.CLASS.ENTITY then
		local encyTab = {
			Class = ContentManager.Mod.Name,
			ID = id,
			Hide = tab.Hidden,
			ModName = ContentManager.Mod.Name,
		}
		local encyFunc = classFuncs.Ency
		if tab.Class == ContentManager.CLASS.CARD and catalogEntry.CardType == ItemConfig.CARDTYPE_RUNE then
			encyFunc = catalogEntry.IsSoulStone and "AddSoul" or "AddRune"
		end
		if tab.Sprite then
			encyTab.Sprite = Encyclopedia.RegisterSprite(tab.Sprite, name)
		end
		if tab.Class == ContentManager.CLASS.PILL and tab.PillColor then
			encyTab.Color = tab.PillColor
		end
		if tab.Pools then
			encyTab.Pools = {}
			for _, poolName in pairs(tab.Pools) do
				table.insert(encyTab.Pools, Encyclopedia.GetItemPoolIdByName(poolName))
			end
		end
		if tab.EID then
			encyTab.WikiDesc = Encyclopedia.EIDtoWiki(tab.EID.en_us[2] or tab.EID.en_us[1])
		end
		encyTab.UnlockFunc = ContentManager.EncyclopediaUnlockFunc
		if tab.GolemRock then
			encyTab.Class = "Golem's Rocks"
			tab.GolemRock.DelayedEncyFunc = function()
				Encyclopedia[encyFunc](encyTab)
			end
		else
			Encyclopedia[encyFunc](encyTab)
		end
	end
	
	if EID and tab.EID then
		if tab.Class == ContentManager.CLASS.CARD then
			if not spriteCache[tab.Sprite] then
				spriteCache[tab.Sprite] = Sprite()
				spriteCache[tab.Sprite]:Load(tab.Sprite, true)
			end
			EID:addIcon("Card"..id, name, 0, 9, 9, -3, 0, spriteCache[tab.Sprite])
		end
		local eidFunc = classFuncs.EID
		for locale, data in pairs(tab.EID) do
			local localName, desc
			if data[2] then
				localName = data[1]
				desc = data[2]
			else
				localName = name
				desc = data[1]
			end
			if tab.Class == ContentManager.CLASS.TRINKET and tab.GolemRock then
				desc = desc .. "#!!! Rock Trinket"
				if tab.GolemRock.FossilCrushEffect then
					desc = desc .. " {{ColorRed}}(Fossil){{CR}}"
				elseif string.find(name, " Geode$") then
					desc = desc .. " {{ColorTeal}}(Geode){{CR}}"
				end
			end
			if tab.Class == ContentManager.CLASS.ENTITY then
				EID:addEntity(id.Type, id.Variant, id.SubType, localName, desc, locale)
			else
				EID[eidFunc](EID, id, desc, localName, locale)
			end
		end
	end
	
	if tab.Unlock then
		local ulTab = tab.Unlock
		local conditions = ContentManager:MakeUnlockConditions(
			ulTab.Char or ulTab.Chars,
			ulTab.VanillaMark or ulTab.VanillaMarks,
			ulTab.CustomMark or ulTab.CustomMarks,
			ulTab.Hard or ulTab.HardMode,
			ulTab.Achievement or ulTab.Achievements,
			ulTab.Desc,
			ulTab.SubDesc
		)
		local replacement = ulTab.ReplacementEntity
		if type(replacement) == "string" then
			replacement = GetEntityIds(replacement)
		end
		catalogEntry.Unlockable = ContentManager:DefineUnlockable(
			ulTab.Group,
			name,
			id,
			conditions,
			achievementGfxRoot .. ulTab.Gfx .. ".png",
			replacement
		)
	elseif tab.TransitiveUnlock then
		local ulTab = tab.TransitiveUnlock
		if not ulTab.Class or not ulTab.ID then
			lib.LogErr("Invalid TransitiveUnlock definition (missing data) for: " .. name)
		else
			local parentItemData = ContentManager.CATALOG[ulTab.Class][ulTab.ID]
			if parentItemData and parentItemData.Unlockable then
				catalogEntry.Unlockable = ContentManager:DefineUnlockable(ulTab.Group or parentItemData.Group, name, id, parentItemData.UnlockConditions)
			else
				lib.LogErr("Invalid TransitiveUnlock definition (no parent) for: " .. name)
			end
		end
	end
	
	local key = id
	
	if tab.Class == ContentManager.CLASS.ENTITY then
		key = ContentManager.EntityKey(id.Type, id.Variant, id.SubType)
	end
	
	ContentManager.CATALOG[tab.Class][key] = catalogEntry
	catalogSize = catalogSize + 1
end

function ContentManager:RegisterAll(itemList)
	for _, itemData in pairs(itemList) do
		ContentManager:Register(itemData)
	end
end

----------------------------------------------------------------------------------------------------
-- Character Registration
----------------------------------------------------------------------------------------------------

function ContentManager:RegisterCharacter(tab)
	local id = Isaac.GetPlayerTypeByName(tab.Name, tab.IsTainted or false)
	
	local charNum = numCharacters + 1
	
	if tab.TrackVanillaAchievements then
		ContentManager:RegisterCharacterForVanillaMarkTracking(tab.Name, tab.IsTainted)
	end
	
	local catalogEntry = {
		Name = tab.Name,
		Title = tab.Title,
		TagLine = tab.TagLine,
		ID = id,
		CatalogID = 1000 + charNum,
		AchievementGroupIcon = tab.AchievementGroupIcon,
		TaintedUnlock = tab.TaintedUnlock,
	}
	if tab.IsTainted then
		catalogEntry.Name = "Tainted " .. catalogEntry.Name
	end
	
	if tab.IsTainted and tab.TaintedUnlock then
		local achievement = "TAINTED_UNLOCK:" .. tab.Name
		ContentManager:RegisterAchievement(achievement)
		local conditions = ContentManager:RequireCustomAchievements(achievement, "Open the hidden door in Home with " .. tab.Name)
		catalogEntry.Unlockable = ContentManager:DefineUnlockable(tab.Name, catalogEntry.Name, id, conditions, tab.TaintedUnlock.Gfx)
		catalogEntry.TaintedUnlock.Achievement = achievement
		if catalogEntry.TaintedUnlock.DoorSprite and type(catalogEntry.TaintedUnlock.DoorSprite) == "string" then
			local sprite = Sprite()
			sprite:Load(catalogEntry.TaintedUnlock.DoorSprite, true)
			sprite:Play(sprite:GetDefaultAnimation(), true)
			catalogEntry.TaintedUnlock.DoorSprite = sprite
		end
	end
	
	ContentManager.CATALOG[ContentManager.CLASS.CHARACTER][id] = catalogEntry
	numCharacters = numCharacters + 1
	
	if EID and tab.EidBirthright then
		EID:addBirthright(id, tab.EidBirthright)
	end
	
	if Encyclopedia then
		local hasWikiBirthright = false
		local wikiDesc
		if tab.Wiki then
			wikiDesc = {}
			for _, descLines in ipairs(tab.Wiki) do
				local tab = {}
				if string.lower(descLines[1] or "") == "birthright" then
					hasWikiBirthright = true
				end
				for lineNum, line in ipairs(descLines) do
					if type(line) == "table" then
						table.insert(tab, line)
					elseif lineNum == 1 then
						table.insert(tab, {str = line, fsize = 2, clr = 3, halign = 0})
					else
						table.insert(tab, {str = line})
					end
				end
				table.insert(wikiDesc, tab)
			end
		end
		
		if not hasWikiBirthright and tab.EidBirthright then
			wikiDesc = wikiDesc or {}
			local wiki = Encyclopedia.EIDtoWiki(tab.EidBirthright, "Birthright")
			if wiki and wiki[1] then
				table.insert(wikiDesc, wiki[1])
			end
		end
		
		local encyTab = {
			ModName = ContentManager.Mod.Name,
			Name = tab.Name,
			ID = id,
			WikiDesc = wikiDesc,
			CompletionTrackerFuncs = {function()
				return {
					Vanilla = ContentManager:GetVanillaMarks(id),
					Custom = ContentManager.GetCustomMarks(id, true),
				}
			end},
			CompletionRenderFuncs = {ContentManager.RenderCompletionNotes},
		}
		
		local func = Encyclopedia.AddCharacter
		if tab.IsTainted then
			func = Encyclopedia.AddCharacterTainted
			encyTab.Title = tab.Title
			if catalogEntry.TaintedUnlock then
				encyTab.UnlockFunc = function(self)
					if ContentManager:CharacterLocked(id) then
						if catalogEntry.TaintedUnlock.DoorSprite then
							self.Spr = catalogEntry.TaintedUnlock.DoorSprite
						end
						self.Desc = "Use Red Key on the hidden door in the Final Chapter as " .. tab.Name .. "."
						self.TargetColor = Encyclopedia.VanillaColor
						return self
					end
				end
			end
		end
		
		func(encyTab)
	end
end

return ContentManager
