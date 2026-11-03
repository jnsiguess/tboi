--------------------------------------------------
---- ITEMS
--------------------------------------------------

SamaelMod.ITEMS = {}

-- Collectibles
SamaelMod.ITEMS.REAPER_BUM = Isaac.GetItemIdByName("Reaper Bum")
SamaelMod.ITEMS.PUNISHMENT_OF_THE_GRAVE = Isaac.GetItemIdByName("Punishment of the Grave")
SamaelMod.ITEMS.THANATOPHOBIA = Isaac.GetItemIdByName("Thanatophobia")
SamaelMod.ITEMS.THANATOPHILIA = Isaac.GetItemIdByName("Thanatophilia")
SamaelMod.ITEMS.DENIAL = Isaac.GetItemIdByName("Spirit of Denial")
SamaelMod.ITEMS.ANGER = Isaac.GetItemIdByName("Spirit of Anger")
SamaelMod.ITEMS.BARGAINING = Isaac.GetItemIdByName("Spirit of Bargaining")
SamaelMod.ITEMS.DEPRESSION = Isaac.GetItemIdByName("Spirit of Depression")
SamaelMod.ITEMS.ACCEPTANCE = Isaac.GetItemIdByName("Spirit of Acceptance")
SamaelMod.ITEMS.REMEMBRANCE_OF_THE_FORGOTTEN = Isaac.GetItemIdByName("Remembrance of the Forgotten")
SamaelMod.ITEMS.REMEMBRANCE_OF_DEATH = Isaac.GetItemIdByName("Remembrance of Death")
SamaelMod.ITEMS.MALAKH_MOT = Isaac.GetItemIdByName("Malakh Mot")
SamaelMod.ITEMS.MEMENTO_MORI = Isaac.GetItemIdByName("Memento Mori")
SamaelMod.ITEMS.THANATOS = Isaac.GetItemIdByName("Mask of Thanatos")
SamaelMod.ITEMS.JAR_OF_SCYTHES = Isaac.GetItemIdByName("Jar of Scythes")
SamaelMod.ITEMS.TRUMPET_OF_WOE = Isaac.GetItemIdByName("Trumpet of Woe")

if FiendFolio then
	if FiendFolio.ReferenceItems and FiendFolio.ReferenceItems.Passives then
		table.insert(FiendFolio.ReferenceItems.Passives, {
			ID = SamaelMod.ITEMS.THANATOS,
			Reference = "Shin Megami Tensei: Persona 3",
		})
	end
	if FiendFolio.AddStackableItems then
		FiendFolio:AddStackableItems({
			SamaelMod.ITEMS.REAPER_BUM,
			SamaelMod.ITEMS.PUNISHMENT_OF_THE_GRAVE,
			SamaelMod.ITEMS.THANATOPHILIA,
			SamaelMod.ITEMS.DENIAL,
			SamaelMod.ITEMS.ANGER,
			SamaelMod.ITEMS.BARGAINING,
			SamaelMod.ITEMS.DEPRESSION,
			SamaelMod.ITEMS.ACCEPTANCE,
			SamaelMod.ITEMS.REMEMBRANCE_OF_THE_FORGOTTEN,
			SamaelMod.ITEMS.THANATOS,
		})
	end
end

-- Trinkets
SamaelMod.ITEMS.SIGIL_OF_SAMAEL = Isaac.GetTrinketIdByName("Sigil of Samael")
SamaelMod.ITEMS.SIGIL_OF_LILITH = Isaac.GetTrinketIdByName("Sigil of Lilith")
SamaelMod.ITEMS.SAMAELS_FEATHER = Isaac.GetTrinketIdByName("Samael's Feather")
SamaelMod.ITEMS.CHARON_CLUB_CARD = Isaac.GetTrinketIdByName("Charon Club Card")
SamaelMod.ITEMS.WRAITH_SKULL = Isaac.GetTrinketIdByName("Wraith Skull")

-- Golem Rocks
SamaelMod.ITEMS.EFFIGY_OF_DENIAL = Isaac.GetTrinketIdByName("Effigy of Denial")
SamaelMod.ITEMS.ANGER_FOSSIL = Isaac.GetTrinketIdByName("Anger Fossil")
SamaelMod.ITEMS.BARGAINING_FOSSIL = Isaac.GetTrinketIdByName("Bargaining Fossil")
SamaelMod.ITEMS.DEPRESSION_FOSSIL = Isaac.GetTrinketIdByName("Depression Fossil")
SamaelMod.ITEMS.EFFIGY_OF_ACCEPTANCE = Isaac.GetTrinketIdByName("Effigy of Acceptance")
SamaelMod.ITEMS.SCYTHE_FOSSIL = Isaac.GetTrinketIdByName("Scythe Fossil")
SamaelMod.ITEMS.FRAGMENT_FRAGMENT = Isaac.GetTrinketIdByName("Fragment Fragment")

-- Consumables
SamaelMod.ITEMS.DENIAL_DICE = Isaac.GetCardIdByName("Denial's D9")
SamaelMod.ITEMS.BARGAINING_CHIP = Isaac.GetCardIdByName("Bargaining's Chip")
SamaelMod.ITEMS.FERRYMANS_OBOLS = Isaac.GetCardIdByName("Ferryman's Obols")
SamaelMod.ITEMS.XIII = Isaac.GetCardIdByName("XIII")
SamaelMod.ITEMS.XIII_REVERSED = Isaac.GetCardIdByName("XIII?")
SamaelMod.ITEMS.SOUL_OF_SAMAEL = Isaac.GetCardIdByName("Soul of Samael")
SamaelMod.ITEMS.THANATOSIS = Isaac.GetPillEffectByName("Thanatosis")

--------------------------------------------------
---- ENTITIES
--------------------------------------------------

local function Entity(name, subType)
	local id = Isaac.GetEntityTypeByName(name)
	local var = Isaac.GetEntityVariantByName(name)
	local subt = subType or 0
	return {
		Name = name,
		Type = id,
		ID = id,
		Variant = var,
		Var = var,
		SubType = subt,
		Sub = subt,
	}
end

SamaelMod.ENTITIES = {}
-- Slots
SamaelMod.ENTITIES.FERRYMAN = Entity("(Samael) Ferryman Beggar")
SamaelMod.ENTITIES.FERRYMAN_EFFECT = Entity("(Samael) Ferryman Effect")
SamaelMod.ENTITIES.LOST_SOUL_BEGGAR = Entity("(Samael) Lost Soul Beggar")
-- Pickups
SamaelMod.ENTITIES.BAG_O_BONES = Entity("(Samael) Bag o' Bones")
SamaelMod.ENTITIES.FORGOTTEN_SKULL = Entity("(Samael) Forgotten Skull")
-- Familiars
SamaelMod.ENTITIES.FORGOTTEN_SOUL = Entity("(Samael) Forgotten Familiar")
SamaelMod.ENTITIES.DEATH_SHADOW = Entity("(Samael) Death Shadow")
SamaelMod.ENTITIES.MEMENTO_MORI_SIGIL = Entity("Memento Mori Sigil")
SamaelMod.ENTITIES.REAPER_BUM = Entity("(Samael) Reaper Bum")
SamaelMod.ENTITIES.MINI_REAPER = Entity("(Samael) Mini Reaper")
SamaelMod.ENTITIES.MINI_REAPER_SCYTHE = Entity("(Samael) Mini Reaper Scythe", 7)
-- Spirits
SamaelMod.ENTITIES.SPIRIT_OF_DENIAL = Entity("(Samael) Spirit of Denial")
SamaelMod.ENTITIES.SPIRIT_OF_ANGER = Entity("(Samael) Spirit of Anger")
SamaelMod.ENTITIES.SPIRIT_OF_BARGAINING = Entity("(Samael) Spirit of Bargaining")
SamaelMod.ENTITIES.SPIRIT_OF_BARGAINING_EFFECT = Entity("(Samael) Spirit of Bargaining Effect")
SamaelMod.ENTITIES.SPIRIT_OF_ACCEPTANCE = Entity("(Samael) Spirit of Acceptance")
-- Misc Effects
SamaelMod.ENTITIES.REAPER_STATUE = Entity("(Samael) Reaper Statue", 617)
SamaelMod.ENTITIES.BARGAINING_DEAL = Entity("(Samael) Bargaining Deal", 0)
SamaelMod.ENTITIES.DEATH_DEAL = Entity("(Samael) Death Deal", 1)
SamaelMod.ENTITIES.STANDALONE_DEAL = Entity("(Samael) Standalone Bargaining Deal", 2)
SamaelMod.ENTITIES.LIGHT_FROM_ABOVE = Entity("(Samael) Acceptance Light")
-- Fragment
SamaelMod.ENTITIES.FRAGMENT_PORTAL = Entity("(Samael) Fragment Portal")
SamaelMod.ENTITIES.ENEMY_PORTAL = Entity("(Samael) Enemy Portal")
SamaelMod.ENTITIES.LOST_SOUL = Entity("(Samael) Lost Soul")
SamaelMod.ENTITIES.LOST_SOUL_EFFECT = Entity("(Samael) Lost Soul Effect", 6)
SamaelMod.ENTITIES.BACKDROP_REPLACER = Entity("(Samael) Fragment Backdrop Replacer")
SamaelMod.ENTITIES.PLAYER_POS = Entity("(Samael) Fragment Player Position")
SamaelMod.ENTITIES.FORCE_BROKEN_WALL = Entity("(Samael) Fragment Force Broken Wall")
SamaelMod.ENTITIES.FORCE_INTACT_WALL = Entity("(Samael) Fragment Force Intact Wall")
SamaelMod.ENTITIES.CHARM_MARK = Entity("(Samael) Charm/Mark")
-- ???
SamaelMod.ENTITIES.EVIL_AZAZEL = Entity("(Samael) Azazel but Fucked Up")
-- Dummy Entities
SamaelMod.ENTITIES.DUMMY = Entity("Samael Dummy Entity", 0)
SamaelMod.ENTITIES.DEAL_PARENT_DUMMY = Entity("Samael Dummy Entity", 1)
SamaelMod.ENTITIES.DUMMY_ENEMY = Entity("(Samael) D/ummy Enemy", 619)
SamaelMod.ENTITIES.SAFE_FF_ENEMY = Entity("(Samael/FF) Safe Scythe Rider")

-- Entity ID used for some custom / dummy entities.
SamaelMod.SHARED_ENTITY_ID = 617

function SamaelMod:DummyEntity(entity)
	if entity.SubType == 0 and not entity.Parent then
		entity:Remove()
	end
end
SamaelMod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, SamaelMod.DummyEntity, SamaelMod.ENTITIES.DUMMY.Var)

--------------------------------------------------
---- MISC
--------------------------------------------------

local function Challenge(name)
	return {
		ID = Isaac.GetChallengeIdByName(name),
		Name = name,
	}
end

SamaelMod.CHALLENGES = {}
SamaelMod.CHALLENGES.THE_REAPER = Challenge("[Samael] The Reaper")
