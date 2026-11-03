local mod = SamaelMod
local lib = mod.Lib

local game = mod.Game

-- Tainted Samael Pocket Active (Memento Mori) Item Interactions

local MementoMoriItemDescriptions = {}

local function MementoMoriEidAppendCondition(descObj)
	if not descObj or descObj.ObjType ~= 5 or descObj.ObjVariant ~= 100 or not MementoMoriItemDescriptions[descObj.ObjSubType] then
		return false
	end
	
	for i=0, Game():GetNumPlayers()-1 do
		local player = Isaac.GetPlayer(i)
		if player and player:GetPlayerType() == lib.TaintedSamaelId then
			return true
		end
	end
	
	return false
end

local function MementoMoriEidAppendCallback(descObj)
	if descObj.ObjType == 5 and descObj.ObjVariant == 100 and MementoMoriItemDescriptions[descObj.ObjSubType] then
		for _, str in ipairs(MementoMoriItemDescriptions[descObj.ObjSubType]) do
			EID:appendToDescription(descObj, "#{{Collectible"..Isaac.GetItemIdByName("Memento Mori") .."}} " .. str)
		end
	end
	return descObj
end

EID:addDescriptionModifier("samaelMementoMoriModifier", MementoMoriEidAppendCondition, MementoMoriEidAppendCallback)

MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_INNER_EYE] = {"+2 hits per slash"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_SPOON_BENDER] = {"Homing slashes"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_MY_REFLECTION] = {"Return to starting point after slashes"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_NUMBER_ONE] = {"↑Pee up"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_HALO_OF_FLIES] = {"Sigils get an orbital fly"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_DR_FETUS] = {"Places bombs on sigils that detonate on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TECHNOLOGY] = {"Sigils are replaced by technology eyes that shoot lasers at enemies"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_CHOCOLATE_MILK] = {"↓DMG (First slash)", "↑↑DMG (Third slash onwards)"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_PARASITE] = {"Emit split tears while slashing"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_BRIMSTONE] = {"Replaces slashes with brimstone lasers"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_LUMP_OF_COAL] = {"Slash damage increased on enemies further from sigils"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TOUGH_LOVE] = {"Slashes may knock teeth out of enemies"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TECHNOLOGY_2] = {"Sigils are connected by damaging lasers"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_MUTANT_SPIDER] = {"+3 hits per slash"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_BLACK_BEAN] = {"Sigils fart on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_SACRED_HEART] = {"Homing slashes"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_EPIC_FETUS] = {"Slashing attack replaced with carpet bombing"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_LOST_CONTACT] = {"Sigils can block projectiles"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_ANTI_GRAVITY] = {"Activation no longer moves you along the sigils", "Slashes still occur independently of you"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_CRICKETS_BODY] = {"Sigils fire tears on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_MONSTROS_LUNG] = {"Sigils spew tears on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TINY_PLANET] = {"Slashes orbit the origin point before traveling to the next sigil"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TECH_5] = {"Lasers will randomly fire between sigils"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_20_20] = {"+1 hits per slash"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_BLOOD_CLOT] = {"Alternating +1/+0 damage for each slash"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_FIRE_MIND] = {"Sigils have flames on them", "Slashes leave a trail of fire jets"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_DARK_MATTER] = {"Sigils may inflict fear on enemies"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_PROPTOSIS] = {"Slash damage increased on enemies close to sigils"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_CURSED_EYE] = {"Slashes have a chance to teleport enemies"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_MYSTERIOUS_LIQUID] = {"Sigils and slashes leave green creep"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE] = {"Sigils can be moved"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_GODHEAD] = {"Sigils have a damaging aura"}

MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_CONTINUUM] = {"Slashes go all the way across the screen"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_HOLY_LIGHT] = {"Leave a trail of holy light beams while slashing"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_MARKED] = {"Sigils are placed at the target instead of at your position"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TECH_X] = {"Sigils are surrounded by laser rings"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TRACTOR_BEAM] = {"Tears will periodically travel between sigils"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_BLACK_POWDER] = {"Creates a pentagram if you draw a pentagram shape with your sigils"}

MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_APPLE] = {"Places delicious apples onto sigils (may or may not contain razor blades)"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_COMPOUND_FRACTURE] = {"Sigils emit bones on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_EYE_OF_BELIAL] = {"Eye of Belial tears appear out of enemies hit by slashes"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_JACOBS_LADDER] = {"Emit electricity during slashes"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_GHOST_PEPPER] = {"Chance to emit flame from sigil on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_BIRDS_EYE] = {"Chance to emit flame from sigil on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_LARGE_ZIT] = {"May shoot pus while slashing"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_BACKSTABBER] = {"Slashes may inflict bleeding"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TECHNOLOGY_ZERO] = {"When you attack, electricity connects you and your sigils"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_POP] = {"Samael would play pool"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_HAEMOLACRIA] = {"Sigils spew tears on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_LACHRYPHAGY] = {"No synergy implemented"} -- Sigils may bite or eat enemies?
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TRISAGION] = {"Trisagion lasers will periodically travel between sigils"}

MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_MUCORMYCOSIS] = {"Sigils emit spores on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_PLAYDOUGH_COOKIE] = {"Random trail effects while slashing"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_EYE_OF_THE_OCCULT] = {"(With {{Collectible118}} Brimstone): Slashes pass through your target"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_JUPITER] = {"Leave fart trails while slashing"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_TERRA] = {"Sigils trigger shockwaves on activation"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_OCULAR_RIFT] = {"Rifts will occasionally appear on sigils"}
MementoMoriItemDescriptions[CollectibleType.COLLECTIBLE_STRANGE_ATTRACTOR] = {"Sigils attract enemies and pickups"}

-- (Regular+Tainted) Samael Scythe Item Interactions

local scytheSprite = Sprite()
scytheSprite:Load("gfx/ui/samael_eid.anm2", true)
EID:addIcon("SamaelScythe", "Scythe", 0, 14, 12, -3, 0, scytheSprite)

local SamaelItemDescriptions = {}

local function SamaelScytheEidAppendCondition(descObj)
	if not descObj or descObj.ObjType ~= 5 or descObj.ObjVariant ~= 100 or not SamaelItemDescriptions[descObj.ObjSubType] then
		return false
	end
	
	for i=0, Game():GetNumPlayers()-1 do
		local player = Isaac.GetPlayer(i)
		if player and lib.IsSamael(player) then
			return true
		end
	end
	
	return false
end

local function SamaelScytheEidAppendCallback(descObj)
	if descObj.ObjType == 5 and descObj.ObjVariant == 100 and SamaelItemDescriptions[descObj.ObjSubType] then
		for _, str in ipairs(SamaelItemDescriptions[descObj.ObjSubType]) do
			EID:appendToDescription(descObj, "#{{SamaelScythe}} " .. str)
		end
	end
	return descObj
end

EID:addDescriptionModifier("samaelScytheModifier", SamaelScytheEidAppendCondition, SamaelScytheEidAppendCallback)

SamaelItemDescriptions[CollectibleType.COLLECTIBLE_20_20] = {"Dual wielding!!"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_THE_WIZ] = {"Dual wielding!!"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_INNER_EYE] = {"Triple wielding!!!"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_MUTANT_SPIDER] = {"Quadruple wielding!!!!"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_TECHNOLOGY] = {"+1 laser ring when swinging the scythe"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_CHOCOLATE_MILK] = {"The scythe swing can be charged for extra damage"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_BRIMSTONE] = {"Scythe swings open damaging brimstone holes", "Charged attack fires laser from the hole"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_TECHNOLOGY_2] = {"+1 laser ring when swinging the scythe"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_TECH_5] = {"+1 laser ring when swinging the scythe"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_MOMS_EYE] = {"Chance to swing extra scythe behind"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_LOKIS_HORNS] = {"Chance to swing extra scythes in cardinal directions"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_TECH_X] = {"+1 laser ring when swinging the scythe"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_BACKSTABBER] = {"Chance to inflict bleeding with the scythe"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_SULFURIC_ACID] = {"Scythe swings can break rocks"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_LUDOVICO_TECHNIQUE] = {"Charge attack throws the scythe, which can be controlled", "To recall the scythe, touch it or tap the drop button"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_MONSTROS_LUNG] = {"A small burst of tears is fired on swing"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_SPIN_TO_WIN] = {"Spin attack!!"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_TERRA] = {"Scythe triggers random ground shockwaves on hit"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_BERSERK] = {"(Bug) Scythe kills don't increase the duration of the effect while active"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_C_SECTION] = {"(Bug) Fetus tears do not currently behave as they should"}
SamaelItemDescriptions[CollectibleType.COLLECTIBLE_CURSED_EYE] = {"Scythe hits have a chance to teleport enemies"}

-- Remember to fix mom's wig