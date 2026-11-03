return function(ContentManager)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local game = Game()
local lib = ContentManager.Lib
local Catalog = ContentManager.CATALOG

local function getScreenBottomRight()
	return game:GetRoom():GetRenderSurfaceTopLeft() * 2 + Vector(442,286)
end

local function getScreenCenterPosition()
	return getScreenBottomRight() / 2
end

local function AddButtonsForUnlocks(menu, unlocks, includeToggles)
	if not menu.buttons then
		menu.buttons = {}
	end
	
	for _, itemData in ipairs(unlocks) do
		local unlockData = itemData.Unlockable
		local name = string.lower(itemData.Name)
		local button = {
			fsize = 2,
		}
		if includeToggles then
			button.setting = ContentManager.CAN_APPEAR.ALLOWED
			button.variable = name
			button.load = function()
				return itemData.AppearSetting or ContentManager.CAN_APPEAR.ALLOWED
			end
			button.store = function(var)
				itemData.AppearSetting = var
			end
			if unlockData then
				button.choices = {'disabled', 'appear if unlocked', 'always unlocked'}
			else
				button.choices = {'disabled', 'enabled'}
			end
		end
		if string.len(name) > 22 then
			button.strset = lib.SplitDssString(name, 22)
		else
			button.str = name
		end
		if unlockData then
			if unlockData.UnlockConditions.Desc then
				local desc = unlockData.UnlockConditions.Desc
				local descSet
				if type(desc) == "table" then
					descSet = {}
					for _, str in pairs(desc) do
						table.insert(descSet, string.lower(str))
					end
				else
					descSet = lib.SplitDssString(""..desc, 13)
				end
				button.tooltip = {
					buttons = {
						{strset = descSet},
					}
				}
				local subDesc = unlockData.UnlockConditions.SubDesc
				if subDesc then
					local subDescSet
					if type(subDesc) == "table" then
						subDescSet = {}
						for _, str in pairs(subDesc) do
							table.insert(subDescSet, string.lower(str))
						end
					else
						subDescSet = lib.SplitDssString("("..subDesc..")", 20)
					end
					table.insert(button.tooltip.buttons, {strset = subDescSet, fsize=1})
				end
			end
		end
		table.insert(menu.buttons, button)
		table.insert(menu.buttons, {str = "", nosel = true, fsize=(includeToggles and 2 or 1)})
	end
end

local function ItemSettingsEnabled(menu)
	if menu and menu.buttons then
		for _, btn in ipairs(menu.buttons) do
			if btn.str == 'allow mod items?' then
				return btn.setting == ContentManager.CAN_APPEAR.CUSTOM
			end
		end
	end
end

local portraitSprite = Sprite()
portraitSprite:Load(ContentManager.CharPortraits, true)

local portraitsAltSprite = Sprite()
portraitsAltSprite:Load(ContentManager.CharPortraitsAlt, true)

local function GenerateCompletionMarkPage()
	local button = {
		str = "",
		fsize = 3,
		choices = {},
		setting = 1,
		variable = "COMPLETION_NOTE_CHAR",
		tooltip = {strset = {''}}
	}
	
	local nameMap = {}
	
	local menu = {
		title = "completion marks",
		nocursor = true,
		buttons = {
			{str = "", fsize = 3, nosel = true},
			{str = "", fsize = 3, nosel = true},
			{str = "", fsize = 3, nosel = true},
			{str = "", fsize = 3, nosel = true},
			{str = "", fsize = 3, nosel = true},
			{str = "", fsize = 3, nosel = true},
		},
		nameMap = nameMap,
		postrender = function(item, tbl)
			local pos = getScreenCenterPosition() - Vector(10, 0)
			local pType = nameMap[button.choices[button.setting]]
			local charData = ContentManager.VanillaMarkCharData[pType]
			
			local markData = {
				Vanilla = charData.Marks,
				Custom = ContentManager.GetCustomMarks(pType, true),
			}
			
			ContentManager.RenderCompletionNotes(pos, markData, charData.IsTainted)
			
			if charData.IsTainted then
				local taintedSprite = portraitsAltSprite
				taintedSprite:Play(charData.Name)
				if ContentManager:CharacterLocked(pType) then
					local door = ContentManager:GetCharacter(pType).TaintedUnlock.DoorSprite
					if door then
						taintedSprite = door
					else
						taintedSprite.Color = lib.Black
					end
				end
				taintedSprite:Render(pos + Vector(-80, 15), Vector.Zero, Vector.Zero)
				taintedSprite.Color = lib.NullColor
			else
				portraitSprite:Play(charData.Name)
				portraitSprite:Render(pos + Vector(-80, 15), Vector.Zero, Vector.Zero)
			end
			
			button.tooltip = {
				buttons = {
					{str = string.lower(ContentManager.CATALOG[ContentManager.CLASS.CHARACTER][pType].Title or '')},
					{str = string.lower(ContentManager.CATALOG[ContentManager.CLASS.CHARACTER][pType].TagLine or ''), fsize = 1},
				}
			}
		end,
	}
	
	local chars = {}
	
	for pType, data in pairs(ContentManager.VanillaMarkCharData) do
		local name = string.lower(data.Name)
		if data.IsTainted then
			name = "tainted " .. name
		end
		
		table.insert(chars, {
			Name = name,
			ID = pType,
		})
	end
	
	table.sort(chars, function(a,b) return a.ID < b.ID end)
	
	for _, data in ipairs(chars) do
		table.insert(button.choices, data.Name)
		nameMap[data.Name] = data.ID
	end
	
	table.insert(menu.buttons, button)
	
	return menu
end

local function GenerateCustomAppearSettingsPage(directory, groups)
	local menu = {
		title = "customize items",
		buttons = {},
		tooltip = {strset = {'customize if', 'each item', 'or unlock', 'can appear'}}
	}
	
	table.insert(menu.buttons, {
		str = "reset to defaults",
		fsize = 2,
		tooltip = {strset = {'reset all', 'items to', 'appear only', 'if unlocked'}},
		func = function(button, item, root)
			--button.tooltip = {str = 'done!'}
			ContentManager:ResetAppearSettings(true)
			for _, button in pairs(item.buttons) do
				if button.dest then
					root.DSSMOD.reloadButtons(root, root.Directory[button.dest])
				end
			end
		end,
		displayif = perItemSettingsAreEnabledFunc,
	})
	table.insert(menu.buttons, {str = "", fsize = 1, nosel = true})
	
	local subMenuLinks = {}
	
	for groupName, groupUnlocks in pairs(groups) do
		table.sort(groupUnlocks, function(a,b) return a.CatalogID < b.CatalogID end)
		
		local isNonUnlockGroup = (groupName == ContentManager.NonUnlockGroup)
		local subMenuName = "unlocks:" .. groupName
		table.insert(subMenuLinks, {
			str = groupName,
			dest = subMenuName,
			UnlockGroupId = ContentManager.UnlockGroupId[groupName],
			--tooltip = {strset = {'enable', 'or disable', 'specific ' .. (isNonUnlockGroup and 'items' or 'unlocks')}},
			displayif = perItemSettingsAreEnabledFunc,
		})
		local subMenu = {
			title = groupName,
			buttons = {},
		}
		AddButtonsForUnlocks(subMenu, groupUnlocks, true)
		directory[subMenuName] = subMenu
	end
	
	table.sort(subMenuLinks, function(a,b)
		if a.UnlockGroupId < 1 or b.UnlockGroupId < 1 then
			return a.UnlockGroupId > b.UnlockGroupId
		end
		return a.UnlockGroupId < b.UnlockGroupId
	end)
	
	for _, link in pairs(subMenuLinks) do
		link.UnlockGroupId = nil
		table.insert(menu.buttons, link)
	end
	
	return menu
end

function ContentManager:GenerateDssMenu(directory)
	local groups = {}
	
	for class, subCatalog in pairs(Catalog) do
		if class ~= ContentManager.CLASS.CHARACTER then
			for key, itemData in pairs(subCatalog) do
				if not itemData.Hidden and not itemData.HideInDss then
					local unlockData = itemData.Unlockable
					local group
					if unlockData then
						if unlockData.Group and unlockData.Group ~= "" then
							group = string.lower(unlockData.Group)
						else
							group = ContentManager.MiscUnlockGroup
						end
					else
						group = ContentManager.NonUnlockGroup
					end
					
					if not groups[group] then
						groups[group] = {}
					end
					table.insert(groups[group], itemData)
				end
			end
		end
	end
	
	local globalItemAppearChoiceButton = {
		str = "mod items",
		fsize = 3,
		choices = {'disabled', 'unlockable', 'all unlocked', 'customize'},
		setting = ContentManager.CAN_APPEAR.ALLOWED,
		variable = "ALLOW_MY_MOD_ITEMS",
		load = function()
			local var = ContentManager.MiscSaveData.GlobalAppearSetting
			return var or ContentManager.CAN_APPEAR.ALLOWED
		end,
		store = function(var)
			ContentManager.MiscSaveData.GlobalAppearSetting = var
		end,
		tooltip = {strset = {'choose if', 'items from', 'this mod', 'can appear'}}
	}
	
	local perItemSettingsAreEnabledFunc = function(button, menuItem, menuObj)
		return globalItemAppearChoiceButton.setting == ContentManager.CAN_APPEAR.CUSTOM
	end
	
	local menu = {
		--title = string.lower(mod.Name),
		title = "items / unlocks",
		noscroll = true,
		buttons = {
			{
				str = "achievements",
				dest = "achievementviewer",
				fsize = 3,
				tooltip = {strset = {'view', 'achievements'}}
			},
			{
				str = "completion marks",
				dest = "char_completion_marks",
				fsize = 3,
				tooltip = {strset = {'view', 'character', 'completion', 'marks'}}
			},
			{str = "", fsize = 2, nosel = true},
		},
	}
	
	for key, charData in pairs(Catalog[ContentManager.CLASS.CHARACTER]) do
		if charData.Unlockable then
			table.insert(menu.buttons, {
				str = string.lower(charData.Name),
				fsize = 3,
				choices = {'unlockable', 'always unlocked'},
				setting = 1,
				variable = "CHAR:"..charData.Name,
				load = function()
					local var = 1
					if charData.AppearSetting == ContentManager.CAN_APPEAR.FORCED then
						var = 2
					end
					return var
				end,
				store = function(var)
					local setting = ContentManager.CAN_APPEAR.ALLOWED
					if var == 2 then
						setting = ContentManager.CAN_APPEAR.FORCED
					end
					charData.AppearSetting = setting
				end,
				tooltip = {strset = {'choose if', 'this character', 'is unlocked', 'by default'}}
			})
			table.insert(menu.buttons, {str = "", fsize = 1, nosel = true})
		end
	end
	
	table.insert(menu.buttons, globalItemAppearChoiceButton)
	table.insert(menu.buttons, {str = "", fsize = 1, nosel = true})
	
	directory["char_completion_marks"] = GenerateCompletionMarkPage()
	directory["per_item_settings"] = GenerateCustomAppearSettingsPage(directory, groups)
	
	table.insert(menu.buttons, {
		str = "customize items/unlocks",
		dest = "per_item_settings",
		fsize = 2,
		tooltip = {strset = {'enable or', 'disable items', 'individually'}},
		displayif = perItemSettingsAreEnabledFunc,
	})
	
	include(ContentManager.Root .. ".dss_achievement_viewer")(ContentManager, directory)
	
	return menu
end

----------------------------------------------------------------------------------------------------
end