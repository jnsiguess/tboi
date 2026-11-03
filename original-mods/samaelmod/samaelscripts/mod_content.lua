local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local ROOT = "samaelscripts.content_manager"

local ContentManager = include(ROOT .. ".content_manager_main")
ContentManager:Init(mod, {
	Root = ROOT,
	EidCardFronts = "gfx/ui/samael_eid.anm2",
	AchievementsGfxRoot = "gfx/ui/samael achievements/",
	Shader = "PauseScreenCompletionMarks",
})
mod.ContentManager = ContentManager

----------------------------------------------------------------------------------------------------
---- Quick Birthcake Stuff
----------------------------------------------------------------------------------------------------

if Birthcake then
	Birthcake.BirthcakeDescs[lib.SamaelId] = "Soul collector"
	Birthcake.BirthcakeDescs[lib.OtherSamaelId] = Birthcake.BirthcakeDescs[lib.SamaelId]
	Birthcake.BirthcakeDescs[lib.TaintedSamaelId] = "You are what they fear most"
	
	Birthcake.TrinketDesc[lib.SamaelId] = {Normal="Enemies have a chance to spawn souls on death.#Every 4th soul collected spawns a pickup."}
	Birthcake.TrinketDesc[lib.OtherSamaelId] = Birthcake.TrinketDesc[lib.SamaelId]
	Birthcake.TrinketDesc[lib.TaintedSamaelId] = {Normal="Enemies are petrified while a Memento Mori attack is active (bosses are unaffected).#Striking an enemy with the final hit of a 5-sigil combo inflicts fear on all enemies and may petrify bosses."}
end

----------------------------------------------------------------------------------------------------
---- Save/Load
----------------------------------------------------------------------------------------------------

function mod:SaveUnlockData()
	mod.PERSISTENT_DATA.CatalogSaveData = ContentManager:GetSaveData()
	mod.PERSISTENT_DATA.SamaelUnlockData = ContentManager:GetCharacterSaveData(lib.SamaelId)
	mod.PERSISTENT_DATA.TaintedSamaelUnlockData = ContentManager:GetCharacterSaveData(lib.TaintedSamaelId)
end
table.insert(mod.PRE_SAVE, mod.SaveUnlockData)

function mod:LoadUnlockData()
	ContentManager:LoadData(mod.PERSISTENT_DATA.CatalogSaveData or mod.PERSISTENT_DATA.UnlockData)
	ContentManager:LoadCharacterData(lib.SamaelId, mod.PERSISTENT_DATA.SamaelUnlockData)
	ContentManager:LoadCharacterData(lib.TaintedSamaelId, mod.PERSISTENT_DATA.TaintedSamaelUnlockData)
end

----------------------------------------------------------------------------------------------------
---- Custom Achievements
----------------------------------------------------------------------------------------------------

-- Misc
mod.ACHIEVEMENTS = {}
mod.ACHIEVEMENTS.KILL_BARGAINING = "KILL_BARGAINING"
mod.ACHIEVEMENTS.SAVED_ONE_SOUL = "SAVED_ONE_SOUL"
mod.ACHIEVEMENTS.SAVED_ALL_SOULS = "SAVED_ALL_SOULS"
mod.ACHIEVEMENTS.WHOA_IS_THAT_A_PERSONA_3_REFERENCE = "WHOA_IS_THAT_A_PERSONA_3_REFERENCE"

ContentManager:RegisterAchievement(mod.ACHIEVEMENTS.KILL_BARGAINING)
ContentManager:RegisterAchievement(mod.ACHIEVEMENTS.SAVED_ONE_SOUL)
ContentManager:RegisterAchievement(mod.ACHIEVEMENTS.SAVED_ALL_SOULS)
ContentManager:RegisterAchievement(mod.ACHIEVEMENTS.WHOA_IS_THAT_A_PERSONA_3_REFERENCE)

-- Ending
mod.ACHIEVEMENTS.SAMAEL_ENDING = "SAMAEL_ENDING"

local endingMark = Sprite()
endingMark:Load("gfx/ui/samael achievements/ending_mark.anm2", true)
ContentManager:RegisterCustomMark(mod.ACHIEVEMENTS.SAMAEL_ENDING, endingMark, "Ending", {lib.SamaelId, lib.TaintedSamaelId}, true)

-- Challenges
mod.ACHIEVEMENTS.THE_REAPER = "THE_REAPER"
ContentManager:RegisterAchievement(mod.ACHIEVEMENTS.THE_REAPER)

function mod:TrackThanatosUnlock(_, player, useFlags)
	if useFlags & UseFlag.USE_OWNED ~= 0 and lib.IsSamael(player) then
		local saveData = mod.PERSISTENT_DATA
		saveData.DEATH_CARDS = math.min((saveData.DEATH_CARDS or 0) + 1, 3)
		if saveData.DEATH_CARDS >= 3 then
			ContentManager:GrantCustomAchievement(mod.ACHIEVEMENTS.WHOA_IS_THAT_A_PERSONA_3_REFERENCE)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.TrackThanatosUnlock, Card.CARD_DEATH)
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.TrackThanatosUnlock, Card.CARD_REVERSE_DEATH)
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.TrackThanatosUnlock, mod.ITEMS.XIII)
mod:AddCallback(ModCallbacks.MC_USE_CARD, mod.TrackThanatosUnlock, mod.ITEMS.XIII_REVERSED)

----------------------------------------------------------------------------------------------------
---- Initialize Descriptions/Unlocks/ETC
----------------------------------------------------------------------------------------------------

ContentManager:RegisterCharacter({
	Name = "Samael",
	Title = "The Reaper",
	TagLine = "2017 gang",
	TrackVanillaAchievements = true,
	AchievementGroupIcon = "gfx/ui/samael achievements/group_samael.png",
	EidBirthright = "#Speed up + flight#Malakh Mot charges slightly faster and lasts 25% longer#Malakh Mot spawns 3 mini-reaper familiars to attack with you",
	Wiki = {
		{
			"Start Data",
			"Pocket Active: Malakh Mot",
			"HP: 1 Red Heart, 1 Black Heart, 1 Soul Heart",
			"Speed: 0.85",
			"Tears: 2.73",
			"Damage: 4.00",
			"Range: 6.50",
			"Shotspeed: 1.00",
			"Luck: 0",
		},
		{
			"Traits",
			"Samael is a combat-focused melee character. Your scythe is your primary attack, but you can also hold down an attack direction to charge up a projectile.",
			"Dealing damage charges your pocket active, \"Malakh Mot\", which when used will put you in \"Wraith Mode\" for a few seconds, granting invincibility, increased attack speed, and auto-swing with the scythe (no need to mash anymore).",
			"Against strong enemies that are difficult to approach for melee attacks, use your projectile to charge up \"Malakh Mot\", then use it to get in close for a burst of melee damage!",
			"Samael's scythe synergizes with many items and effects.",
		},
		{
			"Trivia",
			"Samael is the (sometimes fallen) angel of death (\"Malakh Mot\") in Talmudic/Judaistic lore, and the husband of Lilith.",
			"Samael's role in Talmudic/Judaistic lore was commonly that of an \"accuser\" or \"adversary\", essentially an angel of God that tests the loyalty of His followers. This is essentially the role that would later be occupied by Satan (in fact, the title used for this role was \"Ha-Satan\", likely the origin for Satan's name), but an important distinction is that Samael was traditionally still considered a servant of God.",
			"Another title commonly associated with Samael is that of a destroyer angel (Mashhit), bringing death and destruction as willed by God.",
			"Unfortunately, as with many entities from Abrahamic literature, the interpretations and appearances of Samael vary wildly and are frequently contradictory.",
			"Still, certain sources identified Samael as being involved in the temptation of Adam and Eve (at times even claimed to have fathered Cain with Eve), being Esau's guardian angel (the one that Jacob wrestled with), or being the angel that delivered God's message to Abraham to sacrifice Isaac, or tried to stop him from carrying out the act.",
			"Not to be confused with the angel Azrael of Islamic lore (and some Judaist traditions), who despite being the respective angel of death is depicted very differently, as Azrael is not depicted in the same dark and adversarial fashion as Samael.",
			"Samael's name (via Hebrew origins) means \"Venom/Poison of God\". True to this name, Samael is at times described as bringing death with a drop of poison from the tip of his sword.",
			"Another possible meaning of Samael's name is \"Blindness of God\".",
		}
	},
})
ContentManager:RegisterCharacter({
	Name = "Samael",
	IsTainted = true,
	Title = "The Inevitable",
	--TagLine = "",
	TrackVanillaAchievements = true,
	AchievementGroupIcon = "gfx/ui/samael achievements/group_samaelb.png",
	TaintedUnlock = {
		Gfx = "gfx/ui/samael achievements/tainted_samael.png",
		ClosetAnm2 = "gfx/samael_closet.anm2",
		DoorSprite = "gfx/tainted_samael_door.anm2",
	},
	EidBirthright = "Collect souls from enemies you kill#Attacking with Memento Mori will consume souls to spawn homing, explosive ghosts from each sigil",
	Wiki = {
		{
			"Start Data",
			"Pocket Active: Memento Mori",
			"HP: 1 Soul Heart, 1 Black Heart, 1 empty Bone Heart",
			"Speed: 1.14",
			"Tears: 1.40",
			"Damage: 4.00",
			"Range: 6.50",
			"Shotspeed: 1.00",
			"Luck: 0",
		},
		{
			"Traits",
			"Compared to Samael, Tainted Samael has a slower attack rate, and lacks Samael's usual charge-attack projectile. However, Tainted Samael will swing automatically while holding an attack direction, rather than needing to tap continuously.",
			"His pocket active, Memento Mori, allows you to place a sigil on the ground. You can place up to five sigils, and they will draw out a path along them in the order they were placed.",
			"Double-tapping the button for Memento Mori (or using it again after placing the 5th sigil) will trigger an attack where Tainted Samael quickly slashes back along the path you created using the sigils, damaging enemies in the path.",
			"Tainted Samael is invincible for the duration of this attack and for a brief moment afterwards.",
			"Each slash performed during an activation is stronger than the last. For example, if all 5 sigils are placed, the final slash will deal the highest damage.",
			"For the best results, try to hit strong enemies with multiple slashes in a single activation!",
			"Additionally, Samael will fire a projectile at the end of the attack, in the direction of the final slash.",
			"In a pinch, you can activate the Memento Mori attack with only one sigil to aim and fire a projectile in a \"slingshot\" fashion.",
			"Smart use of Memento Mori will allow you to make up for your lack of a consistent ranged attack by catching enemies between your sigils without having to get too close. Taking the time to set up a big combo with all 5 Memento Mori sigils can be worthwhile, if you can catch a boss in the right position!",
			"Memento Mori has various synergies with different items.",
		},
		{
			"Trivia",
			"For trivia on Samael as a figure in Judaism, see regular Samael's Trivia entry. Here I'm instead going to dump some more meta and sappy trivia about Samael (the mod)'s history.",
			"Samael was originally released way back in April 2017. Back then, in the early modding days before The Forgotten existed, a melee character was a pretty novel thing!",
			"Unfortunately, for personal reasons I did not remain active in Isaac modding and its community for very long afterwards.",
			"Over the next few years, I always regretted leaving it behind so soon. Fast forward to 2021, with the release of Repentance, I finally got the push to come back to Isaac modding.",
			"But things were different than they were in 2017. I had a full-time job, a wife, other obligations... I simply didn't have as much time to invest into it.",
			"Still, I updated and reworked Samael until he was in a state I felt proud of again. Samael 2.0 (the rep update) was released in July 2021.",
			"Of course, after that, it was time to tackle Tainted Samael. But not just that. Everything else Samael needed to be \"complete\". A full set of unlocks. More visual and mechanical upgrades. Everything.",
			"Finding time to work on the update was still difficult, for all the same reasons. And there were multiple points in development where I got \"stuck\", or lost focus.",
			"Samael 3.0 finally released in January 2023. Over a year of on-and-off development. In full honesty, Tainted Samael himself was mostly complete about halfway into that period! His unlocks and all the systems around that took a long time.",
			"I'm honestly so happy that I decided to come back and do this. For any of the stress or guilt I may have felt taking so long making this update, it's felt so fulfulling coming back to Isaac modding.",
			"Reconnecting with old friends in the community, and meeting new ones, has also been an incredibly positive experience for me. If any of my friends or aquaintences happen to read this, thank you so much for making me feel welcome.",
			"", "Here's to the ending of one chapter and the beginning of a new one!",
		}
	},
})

local bargainingChipDesc = "The Spirit of Bargaining offers 1 of 3 deals."
		.."#Deals can offer items, pickups, or services such as rerolling."
		.."#Deals can cost coins, keys, or bombs, or even an item."
		.."#Bargaining will try to offer deals you might want, and can afford."

ContentManager:RegisterAll({
--------------------------------------------------
-- Samael's Unlocks
{
	ID = mod.ITEMS.THANATOSIS,
	Class = ContentManager.CLASS.PILL,
	PillColor = 11,
	EID = {
		en_us = {
			"Triggers a fake death animation and confuses all enemies in the room."
			.."#You cannot take damage in this state."
			.."#Activates effects that trigger when you take damage."
			.."#Effect ends when you move or attack."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "MomsHeart",
		Gfx = "thanatosis",
	},
},
{
	Name = "Bag o' Bones",
	EntityName = "(Samael) Bag o' Bones",
	Class = ContentManager.CLASS.ENTITY,
	EID = {
		en_us = {
			"Drops bone orbitals, bone hearts, runes, or friendly skeletons."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Isaac",
		Gfx = "bag_o_bones",
		ReplacementEntity = "Grab Bag",
	},
},
{
	ID = mod.ITEMS.REAPER_BUM,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", "POOL_DEVIL", "POOL_GREED_DEVIL", "POOL_BABY_SHOP", mod.DeathPoolName},
	EID = {
		en_us = {
			"Collects souls as you kill enemies."
			.."#Will occasionally give you bone orbitals, wisps, tarot cards, runes, or bone hearts."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Satan",
		Gfx = "reaper_bum",
	},
},
{
	ID = mod.ITEMS.PUNISHMENT_OF_THE_GRAVE,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", "POOL_ANGEL", "POOL_GREED_ANGEL", mod.DeathPoolName},
	EID = {
		en_us = {
			"↑ +1 life"
			.."#Revives you with 4 hearts."
			.."#On revival, if you've taken any devil deals, spawns a hostile angel boss, which will drop two items from the Devil Room pool if killed."
			.."#If you've never taken any devil deals, instead spawns an item from the Angel Room pool on revival."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "BossRush",
		Gfx = "punishment_of_the_grave",
	},
},
{
	ID = mod.ITEMS.DENIAL,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_DEVIL", "POOL_GREED_DEVIL", mod.DeathPoolName},
	EID = {
		en_us = {
			"↓ -2 Luck"
			.."#↑ Grants immunity to curse of the blind."
			.."#↑ Reveals the blind item pedestals in alt-path treasure rooms."
			.."#↑ Once per room, spawns a \"Denial Dice\" consumable, that can be used to reroll one"
			.." item, pickup or slot machine in the room, but only in that room."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "BlueBaby",
		Gfx = "denial",
	},
},
{
	ID = mod.ITEMS.ANGER,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_GREED_TREASURE", "POOL_CURSE", "POOL_GREED_CURSE", "POOL_ULTRA_SECRET", mod.DeathPoolName},
	EID = {
		en_us = {
			"↑ +0.5 damage up."
			.."Gives small damage ups each time certain events occur, including but not limited to: "
			.."#{{Blank}} - A quality 0 item spawns."
			.."#{{Blank}} - An active item spawns that is of equal or lower quality than your current one."
			.."#{{Blank}} - Slot Machines, Crane Games or Shell Games don't pay out."
			.."#{{Blank}} - A beggar takes everything you have without paying out."
			.."#{{Blank}} - You bomb a valid secret room location, but there isn't one."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Lamb",
		Gfx = "anger",
	},
},
{
	ID = mod.ITEMS.BARGAINING,
	Pools = {"POOL_DEVIL", "POOL_GREED_DEVIL", "POOL_SECRET", "POOL_GREED_SECRET", mod.DeathPoolName},
	Class = ContentManager.CLASS.ITEM,
	EID = {
		en_us = {
			"Spawns \"Bargaining's Chip\" in starting rooms and certain special rooms."
			.."#{{Card" .. Isaac.GetCardIdByName("Bargaining's Chip") .. "}} ".. bargainingChipDesc
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "MegaSatan",
		Gfx = "bargaining",
	},
},
{
	ID = mod.ITEMS.DEPRESSION,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", "POOL_CURSE", "POOL_GREED_CURSE", mod.DeathPoolName},
	EID = {
		en_us = {
			"↑ +0.7 Tears up"
			.."#Whenever you take damage, triggers an animation where you lay on the floor"
			.." crying for a few seconds, shooting tears in all directions."
			.."#(You cannot take damage during the animation.)"
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Hush",
		Gfx = "depression",
	},
},
{
	ID = mod.ITEMS.ACCEPTANCE,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_ANGEL", "POOL_GREED_ANGEL", mod.DeathPoolName},
	EID = {
		en_us = {
			"↑ +1 Luck"
			.."#↓ Applies a permanent Curse of the Blind effect - all item pedestals are hidden."
			.."#↑ Each time an item pedestal spawns, or an item is purchased, there is a 50% chance for an extra item to spawn."
			.."#↑ Spawns a random item from the treasure room pool when first picked up."
			.."#Removes and rerolls \"Options\" items if you have them."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Beast",
		Gfx = "acceptance",
	},
},
{
	ID = mod.ITEMS.THANATOPHOBIA,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", mod.DeathPoolName},
	EID = {
		en_us = {
			"When you take damage, emit a shout that hurts and knocks back enemies, and reflects projectiles."
			.."#Grants an increasing damage boost for having lower total health."
			.."#Requires having fewer than 6 hearts total for either effect to trigger."
			.."#The damage boost is not active while a Holy Mantle is active, but a Holy Mantle breaking will trigger the shout."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Mother",
		Gfx = "thanatophobia",
	},
},
{
	ID = mod.ITEMS.MALAKH_MOT,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_DEVIL", "POOL_GREED_DEVIL", mod.DeathPoolName},
	EID = {
		en_us = {
			"Grants a temporary invincible state where you can attack with a scythe."
			.."#Charges based on damage dealt."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "Delirium",
		Gfx = "malakh_mot",
	},
},
{
	ID = mod.ITEMS.XIII,
	Class = ContentManager.CLASS.CARD,
	CardWeight = 0.5,
	CardReplacement = mod.ITEMS.XIII_REVERSED,
	CardReplacementChance = 0.25,
	EID = {
		en_us = {
			"Takes you to a special \"Death Deal\" room where you must choose 1 out of 2-4 deals."
			.."#Each deal requires you to give up one of your items for a new one."
			.."#Deals may offer items from the Angel or Devil pools, or a custom Death pool."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "GreedMode",
		Gfx = "xiii",
	},
},
{
	ID = mod.ITEMS.REMEMBRANCE_OF_THE_FORGOTTEN,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", "POOL_SECRET", "POOL_GREED_SECRET", "POOL_OLD_CHEST", mod.DeathPoolName},
	EID = {
		en_us = {
			"A small skull will appear in each room."
			.."#Picking up the skull will grant a temporary familiar that copies your tears and stats."
			.."#When the familiar expires, the skull respawns and can be picked up again."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		CustomMark = mod.ACHIEVEMENTS.SAMAEL_ENDING,
		Gfx = "remembrance_of_the_forgotten",
		Desc = "Get Samael's special ending",
	},
},
{
	ID = mod.ITEMS.WRAITH_SKULL,
	Class = ContentManager.CLASS.TRINKET,
	EID = {
		en_us = {
			"Upon first taking damage in a room, or whenever at critical health, spawns a mini-reaper familiar to attack enemies."
		}
	},
	Unlock = {
		Char = lib.SamaelId,
		VanillaMark = "ALL",
		Gfx = "wraith_skull",
	},
},
--------------------------------------------------
-- Tainted Samael's Unlocks
{
	ID = mod.ITEMS.SIGIL_OF_SAMAEL,
	Class = ContentManager.CLASS.TRINKET,
	EID = {
		en_us = {
			"Killed enemies count as being killed twice."
			.."#Causes on-kill effects to trigger twice, among other things."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "PolNegPath",
		Gfx = "sigil_of_samael",
	},
},
{
	ID = mod.ITEMS.SOUL_OF_SAMAEL,
	Class = ContentManager.CLASS.CARD,
	IsSoulStone = true,
	EID = {
		en_us = {
			"Enemies in the current room will drop souls on death."
			.."#Spawns pickup rewards for collecting a lot of souls."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "SoulStonePath",
		Gfx = "soul_of_samael",
	},
},
{
	Name = "Ferryman",
	EntityName = "(Samael) Ferryman Beggar",
	Class = ContentManager.CLASS.ENTITY,
	EID = {
		en_us = {
			"Takes you to the Fragment for 10 cents."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "MegaSatan",
		Gfx = "ferryman",
		ReplacementEntity = "Beggar",
	},
},
{
	ID = mod.ITEMS.XIII_REVERSED,
	Class = ContentManager.CLASS.CARD,
	CardWeight = 0,
	NoGreed = true,
	EID = {
		en_us = {
			"Enemies in the room will endlessly respawn until you take damage."
			.."#Afterwards, spawns rewards based on the total damage dealt to enemies."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "GreedMode",
		Gfx = "xiii_reversed",
	},
},
{
	ID = mod.ITEMS.MEMENTO_MORI,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_ANGEL", "POOL_GREED_ANGEL", mod.DeathPoolName},
	EID = {
		en_us = {
			"Use to place a sigil at your location - up to 5 can be placed."
			.."#Double-tap to perform an invincible slashing attack along the path of your placed sigils."
			.."#Damage increases the more sigils are placed before activation."
			.."#Has synergies with many other items."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "Delirium",
		Gfx = "memento_mori",
	},
},
{
	ID = mod.ITEMS.THANATOPHILIA,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", mod.DeathPoolName},
	EID = {
		en_us = {
			"Chance to spawn friendly skeletal enemies from killed enemies."
			.."#Will also periodically spawn a couple when you have none."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "Mother",
		Gfx = "thanatophilia",
	},
},
{
	ID = mod.ITEMS.SIGIL_OF_LILITH,
	Class = ContentManager.CLASS.TRINKET,
	EID = {
		en_us = {
			"Spawns a friendly demon baby if you don't have one."
			.."#The baby can evolve into stronger forms by clearing enough rooms with it alive."
			.."#If it dies, you'll get a new one next room."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "Beast",
		Gfx = "sigil_of_lilith",
	},
},
{
	ID = mod.ITEMS.REMEMBRANCE_OF_DEATH,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", "POOL_SECRET", "POOL_GREED_SECRET", "POOL_OLD_CHEST", mod.DeathPoolName},
	EID = {
		en_us = {
			"Holding down an attack direction places a stationary reaper shadow at your current location."
			.."#When you let go, the shadow slashes back to your new location, damaging enemies in its path."
			.."#Scales with your stats."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		CustomMark = mod.ACHIEVEMENTS.SAMAEL_ENDING,
		Gfx = "remembrance_of_death",
		Desc = "Get Tainted Samael's special ending",
	},
},
{
	ID = mod.ITEMS.TRUMPET_OF_WOE,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_DEVIL", "POOL_GREED_DEVIL", "POOL_ANGEL", "POOL_GREED_ANGEL", mod.DeathPoolName},
	EID = {
		en_us = {
			"#Effect changes each use:"
			.."#{{Blank}}"
			.."#{{Blank}} {{ColorSilver}}1st:{{CR}} Summons locusts"
			.."#{{Blank}} {{ColorSilver}}2nd:{{CR}} Summons friendly reapers"
			.."#{{Blank}} {{ColorSilver}}3rd:{{CR}} Summons a friendly Ultra Death"
			.."#{{Blank}}"
			.."#After that, loops back to the first effect."
		}
	},
	Unlock = {
		Char = lib.TaintedSamaelId,
		VanillaMark = "ALL",
		Gfx = "trumpet_of_woe",
	},
},
--------------------------------------------------
-- Misc
{
	ID = mod.ITEMS.FERRYMANS_OBOLS,
	Class = ContentManager.CLASS.CARD,
	EID = {
		en_us = {
			"Spawns a pre-paid Ferryman."
		}
	},
	Unlock = {
		Achievement = mod.ACHIEVEMENTS.SAVED_ONE_SOUL,
		Gfx = "ferrymans_obols",
		Desc = "Free a soul lost within a Fragment",
		SubDesc = "Requires Tainted Samael's Mega Satan unlock",
	},
},
{
	ID = mod.ITEMS.CHARON_CLUB_CARD,
	Class = ContentManager.CLASS.TRINKET,
	EID = {
		en_us = {
			"Guarantees that Ferrymen spawn on every floor, even floors where they wouldn't normally spawn naturally."
			.."#Halves the toll required to pay Ferrymen."
		}
	},
	Unlock = {
		Achievement = mod.ACHIEVEMENTS.SAVED_ALL_SOULS,
		Gfx = "charon_club_card",
		Desc = "Free all four souls lost within a single Fragment",
		SubDesc = "Requires Tainted Samael's Mega Satan unlock",
	},
},
{
	ID = mod.ITEMS.BARGAINING_CHIP,
	Class = ContentManager.CLASS.CARD,
	EID = {
		en_us = {
			bargainingChipDesc
		}
	},
	Unlock = {
		Achievement = mod.ACHIEVEMENTS.KILL_BARGAINING,
		Gfx = "bargaining_chip",
		Desc = "Refuse bargaining's deal",
		SubDesc = "Requires Samael's Mega Satan unlock",
	},
},
{
	ID = mod.ITEMS.DENIAL_DICE,
	Class = ContentManager.CLASS.CARD,
	CardWeight = 0,
	EID = {
		en_us = {
			"Rerolls a single item, pickup, or machine."
			.."#Items will attempt to reroll into the same pool they came from."
			.."#Chests, consumables, trinkets and machines will reroll into another of the same type."
			.."#Only spawned by \"Spirit of Denial\", once per room."
			.."#Cannot be taken out of the room it spawned in."
		}
	},
	Hidden = true,
},
{
	ID = mod.ITEMS.THANATOS,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", mod.DeathPoolName},
	EID = {
		en_us = {
			"Grants a chain of coffin familiars that follow behind you that block projectiles."
			.."#The coffins will break open after blocking enough projectiles, shooting flames and spawning a small dead friend."
			.."#Coffins respawn periodically over time, or upon entering a new room."
		}
	},
	Unlock = {
		Achievement = mod.ACHIEVEMENTS.WHOA_IS_THAT_A_PERSONA_3_REFERENCE,
		Gfx = "thanatos",
		Desc = "Use 3 death cards as Samael",
	},
},
{
	ID = mod.ITEMS.JAR_OF_SCYTHES,
	Class = ContentManager.CLASS.ITEM,
	Pools = {"POOL_TREASURE", "POOL_GREED_TREASURE", mod.DeathPoolName},
	EID = {
		en_us = {
			"Gains stocks when enemies are killed (max: 20)."
			.."#On use, consumes up to 4 stocks and spawns that many spinning scythe projectiles."
			.."#Damage scales with your stats."
		}
	},
	Unlock = {
		Achievement = mod.ACHIEVEMENTS.THE_REAPER,
		Gfx = "jar_of_scythes",
		Desc = "Complete \"The Reaper\" (Challenge)",
	},
},
{
	ID = mod.ITEMS.SAMAELS_FEATHER,
	Class = ContentManager.CLASS.TRINKET,
	EID = {
		en_us = {
			"???"
		}
	},
	Hidden = true,
},
--------------------------------------------------
-- (Fiend Folio) Golem Rocks
{
	ID = mod.ITEMS.EFFIGY_OF_DENIAL,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
	},
	EID = {
		en_us = {
			"Chance to spawn a {{Card"..mod.ITEMS.DENIAL_DICE.."}} on room clear"
			.." (can be used to reroll one thing in the room it spawned in, same ones spawned by {{Collectible"..mod.ITEMS.DENIAL.."}})."
			.."#{{ffGrind}} Spawns 3 {{Card"..mod.ITEMS.DENIAL_DICE.."}} when grinded."
		}
	},
},
{
	ID = mod.ITEMS.ANGER_FOSSIL,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
		FossilCrushEffect = function(player, spawner) mod.AngerFossilCrushEffect(player, spawner) end,
	},
	EID = {
		en_us = {
			"Grants a large damage up that slightly decreases whenever certain \"annoying\" events occur."
			.."#Triggers off of the same events as {{Collectible"..mod.ITEMS.ANGER.."}}."
			.."#{{ffCrush}} Spawns a quality 0 item and copper bombs when crushed."
		}
	},
},
{
	ID = mod.ITEMS.BARGAINING_FOSSIL,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
		FossilCrushEffect = function(player, spawner) mod.BargainingFossilCrushEffect(player, spawner) end,
	},
	EID = {
		en_us = {
			"Spawns a few \"deals\" in the starting room of each floor, offering exchanges of random pickups."
			.."#The deals operate similarly to those from {{Collectible"..mod.ITEMS.BARGAINING.."}}."
			.."#{{ffCrush}} Spawns 2 deals for rock trinkets."
		}
	},
},
{
	ID = mod.ITEMS.DEPRESSION_FOSSIL,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
		FossilCrushEffect = function(player, spawner) mod.DepressionFossilCrushEffect(player, spawner) end,
	},
	EID = {
		en_us = {
			"↑ Tears up!"
			.."#Every 15-60 seconds, triggers the effect of {{Collectible"..mod.ITEMS.DEPRESSION.."}},"
			.. " making you lie on the floor crying for a few seconds, shooting tears in all directions."
			.."#(You are invincible during this animation and a short period afterwards.)"
			.."#{{ffCrush}} Grants a large tears up that diminishes over time."
		}
	},
},
{
	ID = mod.ITEMS.EFFIGY_OF_ACCEPTANCE,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
	},
	EID = {
		en_us = {
			"Grants a choice between two items in treasure rooms and boss rooms. However, the second option is always hidden."
			.."#If a blind item is taken, grants +0.5 luck and a soul heart."
		}
	},
},
{
	ID = mod.ITEMS.SCYTHE_FOSSIL,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
		FossilCrushEffect = function() mod:ScytheFossilCrushEffect() end,
	},
	EID = {
		en_us = {
			"Killed enemies count as being killed twice."
			.."#Causes on-kill effects to trigger twice, among other things."
			.."#(Same effect as {{Trinket"..mod.ITEMS.SIGIL_OF_SAMAEL.."}})"
			.."#{{ffCrush}} Spawns a bunch of harmless ghosts that quickly die, triggering on-kill effects. This also triggers in the next 4 uncleared rooms."
		}
	},
},
{
	ID = mod.ITEMS.FRAGMENT_FRAGMENT,
	Class = ContentManager.CLASS.TRINKET,
	GolemRock = {
		Rarity = 1,
	},
	EID = {
		en_us = {
			"Chance to spawn a friendly lost soul in uncleared rooms."
			.."#If it is still alive on room clear, it spawns a pickup reward."
		}
	},
},
--------------------------------------------------
-- Technical/Hidden
{
	ID = Isaac.GetItemIdByName("Tainted Samael Tractor Beam"),
	Class = ContentManager.CLASS.ITEM,
	Hidden = true,
},
})
