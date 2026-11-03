local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

-- lol
local function GetType(tab)
	return tab.ID or tab.id or tab.Type or tab.type or tab[1]
end
local function GetVariant(tab)
	return tab.VAR or tab.Var or tab.VARIANT or tab.Variant or tab.variant or tab[2] or 0
end
local function GetSubType(tab)
	return tab.SUB or tab.Sub or tab.sub or tab.SUBTYPE or tab.SubType or tab.Subtype or tab.subtype or tab[3] or 0
end

local function DeclareMinion(eType, eVariant, eSubType)
	return {
		Type = eType,
		Variant = eVariant or 0,
		SubType = eSubType or 0,
		Spawn = function(self, player, source, extraData)
			return mod.SpawnThanatophiliaMinion(self, player, source, extraData)
		end,
	}
end

local function DeclareMinionByName(name, eSubType)
	local eType = Isaac.GetEntityTypeByName(name)
	local eVariant = Isaac.GetEntityVariantByName(name)
	if eType <= 0 or eVariant < 0 then return end
	return DeclareMinion(eType, eVariant, eSubType or 0)
end

local function DeclareMinionFromTable(tab)
	return DeclareMinion(GetType(tab), GetVariant(tab), GetSubType(tab))
end

local MINIONS = {
	Bony = DeclareMinion(EntityType.ENTITY_BONY),
	BlackBony = DeclareMinion(EntityType.ENTITY_BLACK_BONY),
	HeadlessBony = DeclareMinionByName("(Thanatophilia) Headless Bony"),
	BonyHead = DeclareMinionByName("(Thanatophilia) Bony Head"),
	BlackBonyHead = DeclareMinionByName("(Thanatophilia) Black Bony Head"),
	HostHead = DeclareMinionByName("(Thanatophilia) Host Head"),
	MobileHostHead = DeclareMinionByName("(Thanatophilia) Mobile Host Head"),
	SpiderBonyHead = DeclareMinionByName("(Thanatophilia) Ultimate Upside Down Spider Bony Skull Head"),
	BigBonyHead = DeclareMinionByName("(Thanatophilia) Big Bony Head"),
	BurningBony = DeclareMinionByName("(Thanatophilia) Burning Bony"),
	DapperBurningBony = DeclareMinionByName("(Thanatophilia) Dapper Burning Bony"),
	BabyBony = DeclareMinion(EntityType.ENTITY_BABY, 3, 617),
	BigBony = DeclareMinion(EntityType.ENTITY_BIG_BONY),
	HeadlessBigBony = DeclareMinionByName("(Thanatophilia) Headless Big Bony"),
	HardHostHead = DeclareMinionByName("(Thanatophilia) Hard Host Head"),
	Necro = DeclareMinion(EntityType.ENTITY_NECRO),
	PsyNecro = DeclareMinionByName("(Thanatophilia) Psy Necro"),
	RagNecro = REVEL and DeclareMinionByName("(Thanatophilia) Rag Necro") or nil,
	BoneMaggot = DeclareMinion(EntityType.ENTITY_CHARGER, 3),
	DeadHand = DeclareMinion(EntityType.ENTITY_MOMS_DEAD_HAND),
	BoneOrbital = DeclareMinion(EntityType.ENTITY_FAMILIAR, FamiliarVariant.BONE_ORBITAL),
	HolyBony = DeclareMinion(EntityType.ENTITY_BONY, 1),
	BoneFly = DeclareMinion(EntityType.ENTITY_BOOMFLY, 4),
	Revenant = DeclareMinion(EntityType.ENTITY_REVENANT),
	QuadRevenant = DeclareMinion(EntityType.ENTITY_REVENANT, 1),
	Pasty = DeclareMinion(EntityType.ENTITY_NEEDLE, 1),
	ClicketyClack = DeclareMinion(EntityType.ENTITY_CLICKETY_CLACK),
	SirenBony = DeclareMinionByName("(Thanatophilia) Siren Bony", 618),
	Forgotten = DeclareMinion(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC, 99),
	MiniBony = DeclareMinion(EntityType.ENTITY_FAMILIAR, FamiliarVariant.MINISAAC, 0),
	ShellHead = DeclareMinionByName("(Thanatophilia) Shell Head"),
	Death = DeclareMinion(EntityType.ENTITY_DEATH),
	DeathBony = DeclareMinionByName("(Thanatophilia) Death Bony"),
	Pile = DeclareMinion(EntityType.ENTITY_POLYCEPHALUS, 1),
	UltraDeath = DeclareMinion(EntityType.ENTITY_BEAST, 40),
	ForgottenBony = DeclareMinionByName("(Thanatophilia) Forgotten Bony"),
	SamaelBony = DeclareMinionByName("(Thanatophilia) Samael Bony"),
	TaintedSamaelBony = DeclareMinionByName("(Thanatophilia) Tainted Samael Bony"),
}
mod.THANATOPHILIA_MINIONS = MINIONS
mod.THANATOPHILIA_MINION_ASSIGNMENTS = {}

local FF = FiendFolio and FiendFolio.FF or {}

if FiendFolio then
	MINIONS.Sternum = DeclareMinionFromTable(FF.Sternum)
	MINIONS.Splodum = DeclareMinionFromTable(FF.Splodum)
	MINIONS.Crepitus = DeclareMinionFromTable(FF.Crepitus)
	MINIONS.DoomFly = DeclareMinionFromTable(FF.DoomFly)
	MINIONS.Possessed = DeclareMinionFromTable(FF.Posssessed or FF.Possessed)
	MINIONS.Jawbone = DeclareMinionFromTable(FF.Jawbone)
	MINIONS.BoneWorm = DeclareMinionFromTable(FF.BoneWorm)
	MINIONS.Ribbone = DeclareMinionFromTable(FF.Ribbone)
	MINIONS.ClicketyClash = DeclareMinionFromTable(FF.ClicketyClash)
	MINIONS.Cracker = DeclareMinionFromTable(FF.Cracker)
	MINIONS.Rotskull = DeclareMinionFromTable(FF.Rotskull)
	MINIONS.MolarSystem = DeclareMinionFromTable(FF.MolarSystem)
	MINIONS.Marlin = DeclareMinionFromTable(FF.Marlin)
	MINIONS.DryWheeze = DeclareMinionFromTable(FF.DryWheeze)
	MINIONS.DrShambles = DeclareMinionFromTable(FF.DrShambles)
	MINIONS.Pyroclasm = DeclareMinionFromTable(FF.Pyroclasm)
	--MINIONS.Meltdown = DeclareMinionFromTable(FF.Meltdown2)
	MINIONS.ScytheRider = DeclareMinionFromTable(FF.ScytheRider)
	MINIONS.Reaper = DeclareMinionFromTable(FF.Reaper)
end

local REV = REVEL and REVEL.ENT or {}

if REVEL then
	MINIONS.Draugr = DeclareMinionFromTable(REV.DRAUGR)
	MINIONS.Haugr = DeclareMinionFromTable(REV.HAUGR)
	MINIONS.Jaugr = DeclareMinionFromTable(REV.JAUGR)
	MINIONS.RagBony = DeclareMinionFromTable(REV.RAG_BONY)
end

if CiiruleanItems then
	MINIONS.BoneAngel = DeclareMinionFromTable(CiiruleanItems.SAMAEL_BABY)
end

-- Assigns a given Thanatophilia minion to be spawned from this specific entity.
-- Nulls / -1 are interpreted as wildcard values.
local function AssignMinion(minion, eType, eVariant, eSubType, extraData)
	if not eType then return end
	local typeLevel = lib.GetOrInit(mod.THANATOPHILIA_MINION_ASSIGNMENTS, eType)
	local varLevel = lib.GetOrInit(typeLevel, eVariant or -1)
	local subTypeLevel = lib.GetOrInit(varLevel, eSubType or -1)
	subTypeLevel.Minion = minion
	subTypeLevel.ExtraData = extraData
end

local function AssignMinionFromTable(minion, tab, extraData)
	AssignMinion(minion, GetType(tab), GetVariant(tab), GetSubType(tab), extraData)
end

local function AssignMinionsInternal(minion, assignments)
	if not minion or not assignments then return end
	
	-- Bulk assignment for entities with no exdata or explicit variant/subtype specifications.
	for _, id in ipairs(assignments.BULK or {}) do
		if type(id) == "table" then
			AssignMinionFromTable(minion, id)
		else
			AssignMinion(minion, id)
		end
	end
	
	for _, entInfo in ipairs(assignments) do
		local exData = {}
		for k, v in pairs(entInfo) do
			if type(k) == "string" then
				exData[k] = v
			end
		end
		
		if type(entInfo[1]) == "table" then
			AssignMinionFromTable(minion, entInfo[1], exData)
		else
			AssignMinion(minion, entInfo[1], entInfo[2], entInfo[3], exData)
		end
	end
end

local function AssignMinions(data)
	for _, tab in ipairs(data) do
		AssignMinionsInternal(tab[1], tab[2])
	end
end

AssignMinions({
	{MINIONS.Bony, {
		BULK = {
			EntityType.ENTITY_GAPER, EntityType.ENTITY_BONY, EntityType.ENTITY_MULLIGAN, EntityType.ENTITY_HIVE,
			EntityType.ENTITY_DOPLE, EntityType.ENTITY_GURGLE, EntityType.ENTITY_HANGER, EntityType.ENTITY_SKINNY,
			EntityType.ENTITY_HOMUNCULUS, EntityType.ENTITY_GURGLING, EntityType.ENTITY_BEGOTTEN, EntityType.ENTITY_NULLS,
			EntityType.ENTITY_BONE_KNIGHT, EntityType.ENTITY_CYCLOPIA, EntityType.ENTITY_HUSH_GAPER, EntityType.ENTITY_GREED_GAPER,
			EntityType.ENTITY_DEEP_GAPER, EntityType.ENTITY_BLURB, EntityType.ENTITY_DANNY, EntityType.ENTITY_MOLE,
			EntityType.ENTITY_EXORCIST, EntityType.ENTITY_WHIPPER, EntityType.ENTITY_TWITCHY, EntityType.ENTITY_MAZE_ROAMER,
			EntityType.ENTITY_GOAT, EntityType.ENTITY_RAG_MAN, EntityType.ENTITY_GURDY, EntityType.ENTITY_GURDY_JR,
			EntityType.ENTITY_SLOTH, EntityType.ENTITY_LUST, EntityType.ENTITY_GREED, EntityType.ENTITY_PRIDE,
			FF.Dangler, FF.Trashbagger, FF.Chorister, FF.Fathead, FF.CancerBoy, FF.Foetus, FF.Hooligan, FF.Necrotic, FF.Flanks,
			FF.MazeRunner, FF.Floodface, FF.Craterface, FF.Dweller, FF.Resident, FF.Haunted, FF.Fishface, FF.FishfaceShiny, FF.Fingore,
			FF.Charlie, FF.Sooty, FF.Fishaac, FF.MrHorf, FF.MrRedHorf, FF.Pester, FF.Enlightened, FF.Unenlightened, FF.Aper, FF.Bull,
			FF.Curdle, FF.CurdleNaked, FF.Marge, FF.Ragurge, FF.Wick, FF.Cathy, FF.Crotchety, FF.ScytheRider, FF.Slobber, FF.Bleedy,
			FF.PaleBleedy, FF.Delinquent, REV.SICKIE,
		},
		{EntityType.ENTITY_KNIGHT, 0},
		{EntityType.ENTITY_KNIGHT, 1},
		{EntityType.ENTITY_PREY, 0},
		{EntityType.ENTITY_MOTHER, 20},
		{EntityType.ENTITY_GEMINI, 0, Scale=1.25},
		{EntityType.ENTITY_GEMINI, 1, Scale=1.25},
		{EntityType.ENTITY_GEMINI, 2, Scale=1.25},
		{EntityType.ENTITY_SINGE, 0},
		{FF.MagGaper, Scale=1.45},
	}},
	{MINIONS.BlackBony, {
		{EntityType.ENTITY_BLACK_BONY},
		{EntityType.ENTITY_MULLIGAN, 1},
		{EntityType.ENTITY_MULLIGAN, 2},
		{EntityType.ENTITY_FARTIGAN},
		{EntityType.ENTITY_WRATH},
		{EntityType.ENTITY_KNIGHT, 4},
		{FF.Powderkeg},
		{FF.Mullikaboom},
	}},
	{MINIONS.HeadlessBony, {
		BULK = {
			EntityType.ENTITY_GUSHER, EntityType.ENTITY_HOPPER, EntityType.ENTITY_LEAPER, EntityType.ENTITY_VIS, EntityType.ENTITY_FLAMINGHOPPER, 
			EntityType.ENTITY_WALKINGBOIL, EntityType.ENTITY_SPLASHER, EntityType.ENTITY_BLACK_GLOBIN_BODY, EntityType.ENTITY_BLASTER, 
			EntityType.ENTITY_BOUNCER, EntityType.ENTITY_VIS_VERSA, EntityType.ENTITY_BOMBGAGGER, EntityType.ENTITY_EVIS, 
			FF.Facade, FF.Cushion, FF.Pipeneck, FF.Strobila, FF.Guflush, FF.Nihilist, FF.GutKnight, FF.Jammed, 
			FF.Gis, FF.MrGob, FF.Sixth, FF.Nobody, FF.Slinger, FF.Nimbus, FF.Molly, FF.Balor, FF.Brooter, FF.Cistern,
			FF.HollowKnight, FF.Squire, FF.Piper, REV.JACKAL, REV.JACKAL_GILDED,
		},
		{EntityType.ENTITY_HOPPER, 3, Num=2},
		{EntityType.ENTITY_MRMAW, 0},
		{EntityType.ENTITY_MRMAW, 2},
		{EntityType.ENTITY_FACELESS, 0},
		{EntityType.ENTITY_EXORCIST, 1},
		{EntityType.ENTITY_GAPER_L2, 2},
		{EntityType.ENTITY_ENVY, 0},
		{EntityType.ENTITY_ENVY, 1},
	}},
	{MINIONS.BonyHead, {
		BULK = {
			EntityType.ENTITY_HORF, EntityType.ENTITY_MAW, EntityType.ENTITY_KEEPER, EntityType.ENTITY_BUTTLICKER, EntityType.ENTITY_TURDLET,
			EntityType.ENTITY_FLOATING_KNIGHT, EntityType.ENTITY_FLESH_DEATHS_HEAD, EntityType.ENTITY_DUKIE, EntityType.ENTITY_BLISTER,
			EntityType.ENTITY_MINISTRO, EntityType.ENTITY_SUB_HORF, EntityType.ENTITY_FLOATING_HOST, EntityType.ENTITY_BUTT_SLICKER, EntityType.ENTITY_PEEP, 
			FF.MrBones, FF.Creepterum, FF.ShockCollar, FF.Bellow, FF.TDweller, FF.Ribeye, FF.Flare, FF.Crisply, FF.RedHorf,
			FF.ShittyHorf, FF.BowlerHead, FF.StrikerHead, FF.BowlerHeadSeptic, FF.MrHorfHead, FF.MrRedHorfHead, FF.Residuum,
			FF.Technician, FF.Spinny, FF.MutantHorf, FF.BlueHorf, FF.Slim, FF.PaleSlim, FF.Gob, REV.FATSNOW, REV.SASQUATCH,
		},
		{EntityType.ENTITY_MRMAW, 1},
		{EntityType.ENTITY_MRMAW, 3},
		{EntityType.ENTITY_SWINGER, 1},
		{EntityType.ENTITY_DUMP, 1},
		{EntityType.ENTITY_POLYCEPHALUS, Num=3, Scale=1.25, RareAlt=MINIONS.Pile},
		{EntityType.ENTITY_STAIN, Num=3, Scale=1.25, RareAlt=MINIONS.Pile},
		{EntityType.ENTITY_HORNFEL, 0},
		{EntityType.ENTITY_LOKI, 0},
		{EntityType.ENTITY_HORNY_BOYS, 0, Num=2},
		{FF.Dizzy, Scale=1.3},
		-- Aquagob?
	}},
	{MINIONS.BlackBonyHead, {
		{EntityType.ENTITY_POOT_MINE},
	}},
	{MINIONS.HostHead, {
		{EntityType.ENTITY_HOST, 0},
		{FF.Cappin},
		{FF.SludgeHost},
		{FF.Hostlet, Scale=0.75},
		{FF.Sentry, Scale=0.9},
		{FF.Mold},
		{FF.BolaHead},
	}},
	{MINIONS.MobileHostHead, {
		{EntityType.ENTITY_MOBILE_HOST},
	}},
	{MINIONS.HardHostHead, {
		{EntityType.ENTITY_HOST, 3},
	}},
	{MINIONS.SpiderBonyHead, {
		{EntityType.ENTITY_HOPPER, 1},
		{FF.Spinneretch},
	}},
	{MINIONS.PsyNecro, {
		{EntityType.ENTITY_MAW, 2},
		{EntityType.ENTITY_RAGLING},
		{EntityType.ENTITY_PSY_HORF},
		{FF.Clergy},
	}},
	{MINIONS.Necro, {
		{EntityType.ENTITY_NECRO},
	}},
	{MINIONS.BoneOrbital, {
		{EntityType.ENTITY_CLOTTY, 0},
		{EntityType.ENTITY_CLOTTY, 3},
		{EntityType.ENTITY_HALF_SACK},
		{EntityType.ENTITY_MEGA_CLOTTY},
		{EntityType.ENTITY_GYRO},
		{EntityType.ENTITY_CLOGGY},
		{EntityType.ENTITY_EMBRYO},
		{EntityType.ENTITY_ONE_TOOTH},
		{EntityType.ENTITY_FAT_BAT},
		{FF.Brisket},
		{FF.Meatwad, Num=2},
		{FF.Haunch, Num=2},
	}},
	{MINIONS.BoneMaggot, {
		{EntityType.ENTITY_MAGGOT},
		{EntityType.ENTITY_CHARGER},
		{EntityType.ENTITY_SPITY},
		{EntityType.ENTITY_SPITY, 1, Scale=1.5},
		{EntityType.ENTITY_CONJOINED_SPITTY, Num=2},
		{EntityType.ENTITY_CHARGER_L2, 0, Scale=1.5},
	}},
	{MINIONS.BabyBony, {
		{EntityType.ENTITY_BABY},
		{EntityType.ENTITY_UNBORN, Scale=0.85},
		{EntityType.ENTITY_BABY_BEGOTTEN},
		{EntityType.ENTITY_VIS_FATTY, 1},
		{EntityType.ENTITY_FRED},
		{EntityType.ENTITY_IMP},
		{EntityType.ENTITY_GEMINI, 10},
		{EntityType.ENTITY_GEMINI, 11},
		FiendFolio and {FF.BubbleBaby.ID, FF.BubbleBaby.Var, 1, Scale=0.8} or nil,
		FiendFolio and {FF.BubbleBaby.ID, FF.BubbleBaby.Var, 2, Scale=0.9} or nil,
		{FF.Thrall},
		{FF.Cherub},
		{FF.Harletwin, Scale=0.95},
		{FF.Effigy, Scale=0.95},
		{FF.Neonate},
	}},
	{MINIONS.HolyBony, {
		{EntityType.ENTITY_BONY, 1},
		{EntityType.ENTITY_HIVE, 2},
		{FF.Warden},
	}},
	{MINIONS.BurningBony, {
		{EntityType.ENTITY_GAPER, 2, SuperRareAlt=MINIONS.DapperBurningBony},
		{EntityType.ENTITY_GURGLE, 1},
		{EntityType.ENTITY_SKINNY, 2},
		{EntityType.ENTITY_DANNY, 1},
		{FF.Woodburner},
		{FF.WoodburnerEasy},
	}},
	{MINIONS.BoneFly, {
		{EntityType.ENTITY_BOOMFLY},
		{EntityType.ENTITY_BABY_PLUM, Scale=2},
		{FF.Warble},
		{FF.Poobottle, Scale=1.25},
		{FF.Drainfly, Scale=1.25},
		{FF.Bumbler, Scale=1.25},
	}},
	{MINIONS.BigBony, {
		{EntityType.ENTITY_FATTY},
		{EntityType.ENTITY_CONJOINED_FATTY, 1},
		{EntityType.ENTITY_BUBBLES},
		{EntityType.ENTITY_QUAKEY},
		{EntityType.ENTITY_BIG_BONY, 0},
		{EntityType.ENTITY_GUTTED_FATTY, 0},
		{EntityType.ENTITY_PEEPER_FATTY, 0},
		{EntityType.ENTITY_BLOATY},
		{EntityType.ENTITY_VIS_FATTY, 0},
		{EntityType.ENTITY_MOLE, 1},
		{EntityType.ENTITY_MEGA_FATTY, Scale=1.25},
		{EntityType.ENTITY_SISTERS_VIS, Scale=1.25},
		{EntityType.ENTITY_CHIMERA, 1},
		{EntityType.ENTITY_GLUTTONY, Scale=0.85},
		{EntityType.ENTITY_FAT_SACK},
		{FF.Peepisser},
		{FF.Accursed},
	}},
	{MINIONS.HeadlessBigBony, {
		{EntityType.ENTITY_FACELESS, 1},
		{EntityType.ENTITY_CAGE, Scale=1.75},
		{EntityType.ENTITY_BLUBBER},
		{FF.Starving},
	}},
	{MINIONS.BigBonyHead, {
		{EntityType.ENTITY_HORSEMAN_HEAD, Scale=1.15},
		{EntityType.ENTITY_FAMINE, Scale=1.15},
		{EntityType.ENTITY_WAR, Scale=1.15},
		{EntityType.ENTITY_BUMBINO, Scale=1.25},
		{EntityType.ENTITY_SUB_HORF, 1, Scale=1.15},
		{EntityType.ENTITY_GAPER_L2, 1, Scale=1.15},
		{FF.Torment},
	}},
	{MINIONS.DeadHand, {
		{EntityType.ENTITY_MOMS_HAND},
		{EntityType.ENTITY_MOMS_DEAD_HAND},
	}},
	{MINIONS.Revenant, {
		{EntityType.ENTITY_REVENANT},
		{EntityType.ENTITY_DOPLE, 1},
		{EntityType.ENTITY_GOAT, 1},
		{EntityType.ENTITY_DARK_ONE, Scale=1.25},
		{EntityType.ENTITY_ADVERSARY, Scale=1.25},
		{FF.MsDominator},
		{FF.Dominated},
		{FF.Psihunter},
	}},
	{MINIONS.QuadRevenant, {
		{EntityType.ENTITY_REVENANT},
	}},
	{MINIONS.Pasty, {
		{EntityType.ENTITY_NEEDLE},
		{EntityType.ENTITY_PIN},
		{FF.Weaver},
		{FF.WeaverSr},
		{FF.DreadWeaver},
		{FF.Thread},
		{FF.Archer},
	}},
	{MINIONS.ClicketyClack, {
		{EntityType.ENTITY_CLICKETY_CLACK, SkipAppear=true, Anim="Regen", State=13},
		{EntityType.ENTITY_FLESH_MAIDEN, SkipAppear=true, Anim="Regen", State=13},
		{FF.Fishfreak, SkipAppear=true, Anim="Regen", State=13},
	}},
	{MINIONS.ShellHead, {
		{EntityType.ENTITY_LARRYJR},
		{FF.Ossularry},
	}},
	{MINIONS.MiniBony, {
		BULK = {
			FF.Morsel,
			FF.Snagger,
			FF.Cancerlet,
			FF.Falafel,
			FF.Slick,
		},
	}},
	-- Special
	{MINIONS.Forgotten, {
		{EntityType.ENTITY_ISAAC, 0, Chance=1.0, SkipAppear=true},
	}},
	{MINIONS.SirenBony, {
		{EntityType.ENTITY_SIREN, 1, Chance=1.0, SkipAppear=true, Anim="Appear"},
	}},
	{MINIONS.DeathBony, {
		{EntityType.ENTITY_DEATH, 0, Chance=1.0},
		{EntityType.ENTITY_DEATH, 30, Chance=1.0},
	}},
	{MINIONS.UltraDeath, {
		{EntityType.ENTITY_BEAST, 40, 0, Chance=1.0},
	}},
	-- FF
	{MINIONS.BoneWorm, {
		{FF.BoneWorm},
		{EntityType.ENTITY_ROUND_WORM},
		{EntityType.ENTITY_NIGHT_CRAWLER},
		{EntityType.ENTITY_ROUNDY},
		{EntityType.ENTITY_PARA_BITE},
		{EntityType.ENTITY_ROUND_WORM, 2, Scale=1.5},
		{EntityType.ENTITY_ROUND_WORM, 3, Scale=1.5},
		{FF.Psystalk},
		{REV.SAND_WORM},
	}},
	{MINIONS.Jawbone, {
		{FF.Jawbone},
		{EntityType.ENTITY_ONE_TOOTH},
		{FF.Foamy},
		{FF.MilkTooth, Scale=0.85},
		{FF.Battie, Scale=1.4},
	}},
	{MINIONS.Ribbone, {
		{FF.Ribbone},
		{EntityType.ENTITY_FAT_BAT},
	}},
	{MINIONS.Sternum, {
		{FF.Sternum},
		{EntityType.ENTITY_EMBRYO},
	}},
	{MINIONS.Splodum, {
		{FF.Splodum},
	}},
	{MINIONS.DoomFly, {
		{FF.DoomFly},
		{FF.DeadFly, Scale=1.25},
		{FF.PhoenixIgnited, Scale=1.25},
	}},
	{MINIONS.Crepitus, {
		{FF.Crepitus},
	}},
	{MINIONS.ClicketyClash, {
		{FF.ClicketyClash},
	}},
	{MINIONS.Possessed, {
		{FF.Posssessed or FF.Possessed},
	}},
	{MINIONS.DryWheeze, {
		{FF.DryWheeze},
		{FF.Shottie},
		{FF.Sniffle},
		{FF.Rook},
		{FF.SuperShottie},
	}},
	{MINIONS.Cracker, {
		{FF.Cracker},
		{FF.Quaker},
		{FF.Shaker},
		{FF.Slammer},
		{FF.PaleSlammer},
		{FF.Smore},
		{FF.SmoreSeptic},
		{FF.Stompy},
		{FF.Doomer},
		{FF.Marzy},
		{FF.Wimpy, Scale=0.6},
	}},
	{MINIONS.Rotskull, {
		{FF.Rotskull},
	}},
	{MINIONS.MolarSystem, {
		{FF.MolarSystem},
	}},
	{MINIONS.Marlin, {
		{FF.Marlin},
	}},
	{MINIONS.DrShambles, {
		{FF.DrShambles},
	}},
	{MINIONS.Pyroclasm, {
		{FF.Pyroclasm},
	}},
	-- Retribution
	{MINIONS.BoneAngel, {
		{EntityType.ENTITY_BABY, 1},
		{EntityType.ENTITY_BABY, 1, 1, Scale=0.85},
		{FF.Cherub},
	}},
	-- Revelations
	{MINIONS.Draugr, {
		{REV.DRAUGR},
	}},
	{MINIONS.Haugr, {
		{REV.HAUGR},
	}},
	{MINIONS.Jaugr, {
		{REV.JAUGR},
	}},
	{MINIONS.RagBony, {
		-- Some RagFolk are handled specially due to their revival mechanics and aren't listed here.
		{REV.RAGTIME},
		{REV.RAG_DANCER},
		{FF.Ragurge},
		{EntityType.ENTITY_RAG_MAN, RagBuffed=true},
		{REV.NECRAGMANCER, RagBuffed=true, Scale=1.25},
	}},
	{MINIONS.RagNecro, {
		{REV.RAG_GAPER_HEAD},
		{EntityType.ENTITY_RAGLING, 1},
	}},
	-- Used for excluding specific variants/subtypes from spawning minions that their Type normally would.
	{MINIONS.NONE, {
		BULK = {
			FF.RolyPoly, FF.PsiKnight, FF.PsychoFly, FF.Gobhopper, FF.Bombmuncher, 
			FF.BubbleBat, FF.Globwad, FF.FleshSistern, FF.SourpatchHead, FF.SourpatchHeadSeptic, 
			FF.Sourpatch, FF.SourpatchSeptic, FF.SourpatchBody, FF.SourpatchBodySeptic,
			FF.Warhead, FF.Drainer, FF.Stomy, FF.Poople,
		},
	}},
})
