local mod = SamaelMod

function mod.GetDssSaveData()
	return mod:GetPersistentData("DSS_DATA")
end

--
-- MenuProvider
--

-- Change this variable to match your mod. The standard is "Dead Sea Scrolls (Mod Name)"
local DSSModName = "Dead Sea Scrolls (Samael)"

-- DSSCoreVersion determines which menu controls the mod selection menu that allows you to enter other mod menus.
-- Don't change it unless you really need to and make sure if you do that you can handle mod selection and global mod options properly.
local DSSCoreVersion = 7

-- Every MenuProvider function below must have its own implementation in your mod, in order to handle menu save data.
local MenuProvider = {}

function MenuProvider.SaveSaveData()
	mod:SaveModData()
end

function MenuProvider.GetPaletteSetting()
	return mod.GetDssSaveData().MenuPalette
end

function MenuProvider.SavePaletteSetting(var)
	mod.GetDssSaveData().MenuPalette = var
end

function MenuProvider.GetHudOffsetSetting()
	if not REPENTANCE then
		return mod.GetDssSaveData().HudOffset
	else
		return Options.HUDOffset * 10
	end
end

function MenuProvider.SaveHudOffsetSetting(var)
	if not REPENTANCE then
		mod.GetDssSaveData().HudOffset = var
	end
end

function MenuProvider.GetGamepadToggleSetting()
	return mod.GetDssSaveData().GamepadToggle
end

function MenuProvider.SaveGamepadToggleSetting(var)
	mod.GetDssSaveData().GamepadToggle = var
end

function MenuProvider.GetMenuKeybindSetting()
	return mod.GetDssSaveData().MenuKeybind
end

function MenuProvider.SaveMenuKeybindSetting(var)
	mod.GetDssSaveData().MenuKeybind = var
end

function MenuProvider.GetMenuHintSetting()
	return mod.GetDssSaveData().MenuHint
end

function MenuProvider.SaveMenuHintSetting(var)
	mod.GetDssSaveData().MenuHint = var
end

function MenuProvider.GetMenuBuzzerSetting()
	return mod.GetDssSaveData().MenuBuzzer
end

function MenuProvider.SaveMenuBuzzerSetting(var)
	mod.GetDssSaveData().MenuBuzzer = var
end

function MenuProvider.GetMenusNotified()
	return mod.GetDssSaveData().MenusNotified
end

function MenuProvider.SaveMenusNotified(var)
	mod.GetDssSaveData().MenusNotified = var
end

function MenuProvider.GetMenusPoppedUp()
	return mod.GetDssSaveData().MenusPoppedUp
end

function MenuProvider.SaveMenusPoppedUp(var)
	mod.GetDssSaveData().MenusPoppedUp = var
end

local DSSInitializerFunction = include("samaelscripts/dss/dssmenucore")

local dssmod = DSSInitializerFunction(DSSModName, DSSCoreVersion, MenuProvider)

-- Creating a menu like any other DSS menu is a simple process.
-- You need a "Directory", which defines all of the pages ("items") that can be accessed on your menu, and a "DirectoryKey", which defines the state of the menu.
local exampledirectory = {
	DSSMOD = dssmod,
	-- The keys in this table are used to determine button destinations.
	main = {
		-- "title" is the big line of text that shows up at the top of the page!
		title = 'samael',

		-- "buttons" is a list of objects that will be displayed on this page. The meat of the menu!
		buttons = {
			-- The simplest button has just a "str" tag, which just displays a line of text.
			
			-- The "action" tag can do one of three pre-defined actions:
			--- "resume" closes the menu, like the resume game button on the pause menu. Generally a good idea to have a button for this on your main page!
			--- "back" backs out to the previous menu item, as if you had sent the menu back input
			--- "openmenu" opens a different dss menu, using the "menu" tag of the button as the name
			{str = 'resume game', action = 'resume'},

			-- The "dest" option, if specified, means that pressing the button will send you to that page of your menu.
			-- If using the "openmenu" action, "dest" will pick which item of that menu you are sent to.
			{str = 'settings', dest = 'settings'},
			{str = 'items / unlocks', dest = 'unlocks'},
			{str = 'credits', dest = 'credits'},

			-- A few default buttons are provided in the table returned from DSSInitializerFunction.
			-- They're buttons that handle generic menu features, like changelogs, palette, and the menu opening keybind
			-- They'll only be visible in your menu if your menu is the only mod menu active; otherwise, they'll show up in the outermost Dead Sea Scrolls menu that lets you pick which mod menu to open.
			-- This one leads to the changelogs menu, which contains changelogs defined by all mods.
			dssmod.changelogsButton,
		},

		-- A tooltip can be set either on an item or a button, and will display in the corner of the menu while a button is selected or the item is visible with no tooltip selected from a button.
		-- The object returned from DSSInitializerFunction contains a default tooltip that describes how to open the menu, at "menuOpenToolTip"
		-- It's generally a good idea to use that one as a default!
		tooltip = dssmod.menuOpenToolTip
	},
	settings = {
		title = 'samael - settings',
		buttons = {
			-- These buttons are all generic menu handling buttons, provided in the table returned from DSSInitializerFunction
			-- They'll only show up if your menu is the only mod menu active
			-- You should generally include them somewhere in your menu, so that players can change the palette or menu keybind even if your mod is the only menu mod active.
			-- You can position them however you like, though!
			dssmod.gamepadToggleButton,
			dssmod.menuKeybindButton,
			dssmod.paletteButton,
			dssmod.menuHintButton,
			dssmod.menuBuzzerButton,

			{
				str = 'scythe can grab pickups',
				choices = {'disabled', 'enabled'},
				fsize = 2,
				setting = 2,
				variable = 'ScythePickup',
				
				load = function()
					if mod:GetOptions().scythePickup == false then
						return 1
					end
					return 2
				end,
				
				store = function(var)
					mod:GetOptions().scythePickup = (var == 2)
				end,
				
				tooltip = {strset = {'samael can', 'grab pickups', 'with his', 'scythe'}}
			},
			{str = "", fsize = 1, nosel = true},
			{
				str = 'range = scythe size',
				choices = {'disabled', 'enabled'},
				fsize = 2,
				setting = 2,
				variable = 'ScytheRange',
				
				load = function()
					if mod:GetOptions().scytheRangeSize == false then
						return 1
					end
					return 2
				end,
				
				store = function(var)
					mod:GetOptions().scytheRangeSize = (var == 2)
				end,
				
				tooltip = {strset = {'scythe can', 'get bigger or', 'smaller based',  'on range'}}
			},
		}
	},
	credits = include("samaelscripts/dss/credits.lua"),
	samaelpopup = {
		title = "samael 3.0 update",
		fsize = 1,
		noscroll = true,
		buttons = {
			{str = "welcome back! samael now has", nosel = true},
			{str = "completion mark tracking and unlocks!", nosel = true},
			{str = "", nosel = true},
			{str = "since you've played as samael before...", nosel = true},
			{str = "do you want to be given", fsize = 2, glowcolor = 3, nosel = true},
			{str = "all the completion marks", fsize = 2, glowcolor = 3, nosel = true},
			{str = "for the original samael?", fsize = 2, glowcolor = 3, nosel = true},
			{str = "(tainted samael is unaffected)",  nosel = true},
			{str = "", nosel = true},
			{
				str = "no thanks",
				substr = {str="i'll get them myself", size=1},
				action = "resume",
				fsize = 3,

				func = function(button, item, root)
					mod.PERSISTENT_DATA.ShowLegacySamaelPopup = nil
				end
			},
			{str = "", nosel = true},
			{
				str = "yes please",
				substr = {str="i already did everything with samael", size=1},
				action = "resume",
				fsize = 3,

				func = function()
					mod:RetroactiveSamaelUnlocks()
					mod.PERSISTENT_DATA.ShowLegacySamaelPopup = nil
				end,
			},
			{str = "", nosel = true},
		},

		tooltip = {
			buttons = {
				{strset = {"you can also", "do this later", "with this", "console", "command:"}},
				{str = "", fsize = 1},
				{str = "samael unlockall", fsize = 1},
			}
		},
	},
	taintedsamaelpopup = {
		title = "tainted samael",
		fsize = 1,
		noscroll = true,
		buttons = {
			{str = "since you've played as samael", nosel = true},
			{str = "before the 3.0 update...", nosel = true},
			{str = "", nosel = true},
			{str = "would you like to unlock", fsize = 2, glowcolor = 3, nosel = true},
			{str = "tainted samael now?", fsize = 2, glowcolor = 3, nosel = true},
			{str = "", nosel = true},
			{
				str = "no thanks",
				substr = {str="i'll unlock him properly", size=1},
				action = "resume",
				fsize = 3,

				func = function(button, item, root)
					mod.PERSISTENT_DATA.ShowTaintedSamaelPopup = nil
				end
			},
			{str = "", nosel = true},
			{
				str = "yes please",
				substr = {str="let me play the new character!", size=1},
				action = "resume",
				fsize = 3,

				func = function()
					mod:UnlockTaintedSamael()
					mod.PERSISTENT_DATA.ShowTaintedSamaelPopup = nil
				end,
			},
			{str = "", nosel = true},
		},

		tooltip = {
			buttons = {
				{strset = {"you can also", "do this later", "with this", "console", "command:"}},
				{str = "", fsize = 1},
				{str = "samael unlocktainted", fsize = 1},
			}
		},
	},
}

exampledirectory.unlocks = mod.ContentManager:GenerateDssMenu(exampledirectory)

local exampledirectorykey = {
	Item = exampledirectory.main, -- This is the initial item of the menu, generally you want to set it to your main item
	Main = 'main', -- The main item of the menu is the item that gets opened first when opening your mod's menu.

	-- These are default state variables for the menu; they're important to have in here, but you don't need to change them at all.
	Idle = false,
	MaskAlpha = 1,
	Settings = {},
	SettingsChanged = false,
	Path = {},
}

DeadSeaScrollsMenu.AddMenu("Samael", {
	DSSMOD = dssmod,
	
	-- The Run, Close, and Open functions define the core loop of your menu
	-- Once your menu is opened, all the work is shifted off to your mod running these functions, so each mod can have its own independently functioning menu.
	-- The DSSInitializerFunction returns a table with defaults defined for each function, as "runMenu", "openMenu", and "closeMenu"
	-- Using these defaults will get you the same menu you see in Bertran and most other mods that use DSS
	-- But, if you did want a completely custom menu, this would be the way to do it!
	
	-- This function runs every render frame while your menu is open, it handles everything! Drawing, inputs, etc.
	Run = dssmod.runMenu,
	-- This function runs when the menu is opened, and generally initializes the menu.
	Open = dssmod.openMenu,
	-- This function runs when the menu is closed, and generally handles storing of save data / general shut down.
	Close = dssmod.closeMenu,

	Directory = exampledirectory,
	DirectoryKey = exampledirectorykey
})
