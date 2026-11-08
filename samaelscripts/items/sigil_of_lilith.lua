local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local SIGIL_OF_LILITH = mod.ITEMS.SIGIL_OF_LILITH

local kBabyStageHP = 3

local FAKE_IMP_HORN = "gfx/characters/fiends_horn_tainted.anm2"

local function GetSigilOfLilithData(player)
	local data = lib.GetOrInit(mod:GetPersistentPlayerData(player), "SIGIL_OF_LILITH")
	if not data.Babies then
		data.Babies = {}
	end
	return data
end

local function GetPlayerParent(entity)
	local player = entity.Parent or entity.SpawnerEntity
	if player and player:ToPlayer() then
		return player:ToPlayer()
	end
end

local function FakePlayer(params, exData)
	local headSprite = "gfx/characters/costumes/character_001_isaac_black.png"
	local bodySprite = "gfx/characters/costumes/character_001_isaac_black.png"
	
	if params.Spritesheet then
		headSprite = params.Spritesheet
		bodySprite = params.Spritesheet
	else
		if params.HeadSprite then
			headSprite = params.HeadSprite
		end
		if params.BodySprite then
			bodySprite = params.BodySprite
		end
	end
	
	local headCostumes = {}
	if params.HeadCostumes then
		for _, costume in ipairs(params.HeadCostumes) do
			local sprite = Sprite()
			sprite:Load(costume, true)
			if params.FakeImpHornColor and costume == FAKE_IMP_HORN then
				local x = params.FakeImpHornColor
				local c = Color(1,1,1,1)
				c:SetColorize(x[1], x[2], x[3], x[4])
				sprite.Color = c
			end
			table.insert(headCostumes, sprite)
		end
	end
	
	local bodyCostumes = {}
	if params.BodyCostumes then
		for _, costume in ipairs(params.BodyCostumes) do
			local sprite = Sprite()
			sprite:Load(costume, true)
			table.insert(bodyCostumes, sprite)
		end
	end
	
	local tab = {
		Type = EntityType.ENTITY_FAMILIAR,
		Variant = FamiliarVariant.MINISAAC,
		SubType = params.SubType or 0,
		Anm2 = "gfx/samael_fake_player.anm2",
		Sprites = {
			[1] = bodySprite,
			[4] = headSprite,
		},
		HeadCostumes = headCostumes,
		BodyCostumes = bodyCostumes,
		Scale = 0.666,
		TearScale = 1.5,
		HP = 30,
		RequiredXp = 3,
	}
	
	if exData then
		for k, v in pairs(exData) do
			tab[k] = v
		end
	end
	
	return tab
end

local LilithBabies = {
	-- Leech Family
	SMALL_LEECH = {
		Type = EntityType.ENTITY_SMALL_LEECH,
		Evolutions = {"LEECH", "FLYING_LEECH"},
		HP = 5,
	},
	LEECH = {
		Type = EntityType.ENTITY_CHARGER,
		RequiredXp = 1,
		Evolutions = {"ELLEECH"},
		Sprites = { [0] = "gfx/familiar/familiar_270_leech.png" },
		HP = 20,
	},
	ELLEECH = {
		Type = EntityType.ENTITY_CHARGER_L2,
		RequiredXp = 4,
		Variant = 1,
		Scale = 0.75,
		HP = 60,
	},
	FLYING_LEECH = {
		Type = EntityType.ENTITY_LEECH,
		RequiredXp = 1,
		Evolutions = {"ADULT_FLYING_LEECH"},
		HP = 25,
	},
	ADULT_FLYING_LEECH = {
		Type = EntityType.ENTITY_ADULT_LEECH,
		RequiredXp = 4,
		HP = 50,
		Scale = 0.75,
	},
	
	-- Misc demon babies
	IMP = {
		Type = EntityType.ENTITY_IMP,
		RequiredXp = 2,
		Evolutions = {"LITTLE_HORN", "LOKI"},
		Scale = 0.75,
		HP = 18,
	},
	BABY_BEGOTTEN = {
		Type = EntityType.ENTITY_BABY_BEGOTTEN,
		RequiredXp = 3,
		Evolutions = {"DARK_ONE", "AZAZEL"},
		Scale = 0.9,
		HP = 18,
	},
	
	-- Cult Family
	CULTIST = {
		Type = EntityType.ENTITY_CULTIST,
		Variant = 1,
		Evolutions = {},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 24,
	},
	WHIPPER = {
		Type = EntityType.ENTITY_WHIPPER,
		Variant = 0,
		Sprites = {
			[0] = "gfx/monsters/repentance/834.000_whipper_body.png",
			[1] = "gfx/monsters/repentance/834.000_whipper_head.png",
			[2] = "gfx/monsters/repentance/834.000_whipper_body.png",
		},
		Evolutions = {"SNAPPER", "LUNATIC"},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 24,
	},
	WHIPPER_G = {
		Type = EntityType.ENTITY_WHIPPER,
		Variant = 0,
		Sprites = {
			[0] = "gfx/monsters/repentance/834.000_whipper_body_gehenna.png",
			[1] = "gfx/monsters/repentance/834.000_whipper_head_gehenna.png",
			[2] = "gfx/monsters/repentance/834.000_whipper_body_gehenna.png",
		},
		Evolutions = {"SNAPPER_G"},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 24,
	},
	SNAPPER = {
		Type = EntityType.ENTITY_WHIPPER,
		Variant = 1,
		Sprites = {
			[0] = "gfx/monsters/repentance/834.001_snapper_body.png",
			[1] = "gfx/monsters/repentance/834.001_snapper_head.png",
			[2] = "gfx/monsters/repentance/834.001_snapper_body.png",
		},
		Evolutions = {},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 35,
	},
	SNAPPER_G = {
		Type = EntityType.ENTITY_WHIPPER,
		Variant = 1,
		Sprites = {
			[0] = "gfx/monsters/repentance/834.001_snapper_body_gehenna.png",
			[1] = "gfx/monsters/repentance/834.001_snapper_head_gehenna.png",
			[2] = "gfx/monsters/repentance/834.001_snapper_body_gehenna.png",
		},
		Evolutions = {},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 35,
	},
	LUNATIC = {
		Type = EntityType.ENTITY_WHIPPER,
		Variant = 2,
		RequiredXp = 3,
		Scale = 0.75,
		HP = 42,
	},
	
	-- Goat Family
	GOAT = {
		Type = EntityType.ENTITY_GOAT,
		RequiredXp = 2,
		Evolutions = {"BLACK_GOAT"},
		Scale = 0.75,
		HP = 18,
	},
	BLACK_GOAT = {
		Type = EntityType.ENTITY_GOAT,
		Variant = 1,
		RequiredXp = 4,
		Evolutions = {"FALLEN"},
		Scale = 0.75,
		HP = 24,
	},
	FALLEN = {
		Type = EntityType.ENTITY_FALLEN,
		RequiredXp = 6,
		Scale = 0.75,
		HP = 80,
		-- Note: Dies at half HP
	},
	
	-- Dark One
	DARK_ONE = {
		Type = EntityType.ENTITY_DARK_ONE,
		RequiredXp = 4,
		Evolutions = {"ADVERSARY"},
		Scale = 0.666,
		HP = 40,
	},
	ADVERSARY = {
		Type = EntityType.ENTITY_ADVERSARY,
		RequiredXp = 3,
		Scale = 0.75,
		HP = 50,
	},
	
	-- Lil Horn
	LITTLE_HORN = {
		Type = EntityType.ENTITY_LITTLE_HORN,
		RequiredXp = 3,
		Evolutions = {"BIG_HORN"},
		Scale = 0.75,
		HP = 30,
	},
	BIG_HORN = {
		Type = EntityType.ENTITY_BIG_HORN,
		RequiredXp = 6,
		Scale = 0.5,
		HP = 66,
	},
	
	-- Loki
	LOKI = {
		Type = EntityType.ENTITY_LOKI,
		RequiredXp = 3,
		DeathEvolution = "LOKII",
		DeathEvolutionVariant = 0,
		DeathEvolutionSubtype = 0,
		Evolutions = {"HORNY_BOYS"},
		Scale = 0.75,
		HP = 30,
	},
	LOKII = {
		Type = EntityType.ENTITY_LOKI,
		Variant = 1,
		Scale = 0.75,
		NumToSpawn = 2,
		HP = 25,
	},
	HORNY_BOYS = {
		Type = EntityType.ENTITY_HORNY_BOYS,
		RequiredXp = 6,
		Scale = 0.75,
		HP = 45,
	},
	
	-- Revenant
	REVENANT = {
		Type = EntityType.ENTITY_REVENANT,
		Evolutions = {"QUAD_REVENANT"},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 20,
	},
	QUAD_REVENANT = {
		Type = EntityType.ENTITY_REVENANT,
		Variant = 1,
		RequiredXp = 3,
		Scale = 0.75,
		HP = 35,
	},
	
	-- Fake players
	MINISAAC = {
		Type = EntityType.ENTITY_FAMILIAR,
		Variant = FamiliarVariant.MINISAAC,
		SubType = 2,
		Evolutions = {"PACT", "BABY_BEGOTTEN", "IMP", "GOAT", "HOODED"},
		Sprites = { [0] = "gfx/samael_entities/sigil_of_lilith_minisaac.png", [1] = "gfx/samael_entities/sigil_of_lilith_minisaac.png" },
		TearVariant = TearVariant.BLOOD,
		HP = 15,
		Scale = 1.0,
	},
	PACT = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_001_isaac_black.png",
			HeadCostumes = {"gfx/characters/080_the pact.anm2"},
		}, {
			Evolutions = {"EVE", "REVENANT", "APOLLYON"},
			TearVariant = TearVariant.BLOOD,
			FireDelay = 8,
			Damage = 4.43,
			Scale = 0.555,
		}),
	EVE = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_001_isaac_black.png",
			HeadCostumes = {"gfx/characters/122_whore of babylon.anm2", "gfx/characters/character_005_evehead.anm2"},
		}, {
			Evolutions = {"BEVE"},
			TearVariant = TearVariant.BLOOD,
			FireDelay = 10,
			--Damage = 5.86,
			Damage = 7.21,
		}),
	BEVE = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_001_isaac_black.png",
			HeadCostumes = {"gfx/characters/n_bloody_babylon.anm2", "gfx/characters/character_b06_eve.anm2"},
			BodyCostumes = {"gfx/characters/n_bloody_babylon.anm2"},
		}, {
			TearVariant = TearVariant.BLOOD,
			FireDelay = 10,
			Damage = 7.21,
		}),
	APOLLYON = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_016_apollyon.png",
			HeadCostumes = {"gfx/characters/character_015_apollyonbody.anm2"},
			BodyCostumes = {"gfx/characters/character_015_apollyonbody.anm2"},
		}, {
			Evolutions = {"BAPOLLYON"},
			FireDelay = 15,
			Damage = 3.5,
		}),
	BAPOLLYON = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_016_apollyon.png",
			HeadCostumes = {"gfx/characters/character_b14_apollyon.anm2"},
			BodyCostumes = {"gfx/characters/character_015_apollyonbody.anm2"},
		}, {
			FireDelay = 15,
			Damage = 3.5,
		}),
	HOODED = FakePlayer({
			SubType = 99,
			Spritesheet = "gfx/characters/costumes/character_001_isaac_black.png",
			HeadCostumes = {"gfx/characters/311_judasshadow.anm2", "gfx/characters/216_ceremonialrobes.anm2"},
			BodyCostumes = {"gfx/characters/311_judasshadow.anm2", "gfx/characters/216_ceremonialrobes.anm2"},
		}, {
			Evolutions = {"CULTIST", "SAMAEL", "WHIPPER", "WHIPPER_G"},
			--TearVariant = TearVariant.BLOOD,
			FireDelay = 10,
			Damage = 5.19,
			Scale = 0.555,
		}),
	SAMAEL = FakePlayer({
			SubType = 99,
			Spritesheet = "gfx/samael_null.png",
			HeadCostumes = {"gfx/characters/samael_realangel.anm2", "gfx/characters/samael_bhood.anm2", "gfx/characters/samael_horns.anm2"},
			BodyCostumes = {"gfx/characters/samael_b/samael_b_costume.anm2"},
		}, {
			Evolutions = {},
			RequiredXp = 4,
			FireDelay = 10,
			Damage = 8,
			MeleeSprite = "gfx/samael_minisaac_scythe.anm2",
		}),
	AZAZEL = {
		Type = mod.ENTITIES.EVIL_AZAZEL.ID,
		Variant = mod.ENTITIES.EVIL_AZAZEL.Var,
		RequiredXp = 3,
		Anm2 = "gfx/samael_azazel.anm2",
		HP = 35,
		Scale = 0.666,
	},
}

local LevelOneBabies = {
	{Baby="SMALL_LEECH", Weight=0.125},
	{Baby="MINISAAC", Weight=1},
}

if MASTEMA then
	LilithBabies.MASTEMA = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_mastema.png",
			HeadCostumes = {"gfx/characters/character_mastema_horns.anm2"},
			BodyCostumes = {"gfx/characters/character_mastema_body.anm2"},
		}, {
			Evolutions = {"BASTEMA"},
			TearVariant = TearVariant.BLOOD,
			FireDelay = 8,
			Damage = 3.3,
		})
	LilithBabies.BASTEMA = FakePlayer({
			Spritesheet = "gfx/characters/costumes/character_mastema_b.png",
			HeadCostumes = {"gfx/characters/character_mastema_b_horns.anm2"},
			BodyCostumes = {"gfx/characters/character_mastema_body.anm2"},
		}, {
			TearColor = Color(1, 0, 1, 1),
			FireDelay = 20,
			Damage = 3.5,
		})
	table.insert(LilithBabies.PACT.Evolutions, "MASTEMA")
end

if FiendFolio then
	LilithBabies.FF_IMP = FakePlayer({
			Spritesheet = "gfx/samael_null.png",
			HeadCostumes = {"gfx/familiar/biend/malice_minion.anm2", FAKE_IMP_HORN},
			BodyCostumes = {"gfx/familiar/biend/malice_minion.anm2"},
			FakeImpHornColor = {0.4, 0.4, 0.4, 1},
		}, {
			Evolutions = {FIEND=4, CHINA=2--[[, FEND=1, FIENT=1]]},
			TearVariant = TearVariant.BLOOD,
			TearColor = FiendFolio.ColorDankBlackReal,
			FireDelay = 9,
			Damage = 2.8,
			Scale = 0.5,
			RequiredXp = 2,
		})
	LilithBabies.MINISAAC.Evolutions.FF_IMP=0.5
	LilithBabies.FIEND = FakePlayer({
			Spritesheet = "gfx/characters/costumes/player_fiend.png",
			HeadCostumes = {FAKE_IMP_HORN},
		}, {
			Evolutions = {"TAINTED_FIEND", "WINGED_FIEND"},
			TearVariant = TearVariant.BLOOD,
			FireDelay = 13,
			Damage = 4.2,
		})
	LilithBabies.WINGED_FIEND = FakePlayer({
			Spritesheet = "gfx/characters/costumes/player_fiend.png",
			HeadCostumes = {FAKE_IMP_HORN, "gfx/characters/037x_mercurius.anm2"},
		}, {
			TearVariant = TearVariant.BLOOD,
			FireDelay = 11,
			Damage = 4.6,
		})
	LilithBabies.TAINTED_FIEND = FakePlayer({
			Spritesheet = "gfx/characters/costumes/player_tainted_fiend.png",
			HeadCostumes = {FAKE_IMP_HORN},
		}, {
			TearVariant = TearVariant.BLOOD,
			FireDelay = 9,
			Damage = 2.8,
		})
	LilithBabies.CHINA = FakePlayer({
			Spritesheet = "gfx/characters/costumes/player_china.png",
			HeadCostumes = {"gfx/characters/china_horns.anm2", FAKE_IMP_HORN},
			FakeImpHornColor = {2, 0.1, 0, 1},
		}, {
			TearVariant = TearVariant.BLOOD,
			TearColor = FiendFolio.ColorChinaYellow,
			FireDelay = 7,
			Damage = 3.5,
		})
	--[[LilithBabies.FEND = FakePlayer({
			SubType = 99,
			Spritesheet = "gfx/characters/costumes/player_fend.png",
			HeadCostumes = {FAKE_IMP_HORN},
			FakeImpHornColor = {2, 1.5, 0.75, 1},
		}, {
			FireDelay = 10,
			Damage = 5.6,
		})
	LilithBabies.FIENT = FakePlayer({
			Spritesheet = "gfx/characters/costumes/player_fient.png",
			HeadCostumes = {FAKE_IMP_HORN},
			FakeImpHornColor = {2.5, 0.8, 0.6, 0.6},
		}, {
			FireDelay = 30,
			Damage = 4.2,
			TearScale = 2,
		})]]
	
	LilithBabies.KUKODEMON = {
		Type = FiendFolio.FF.Kukodemon.ID,
		Variant = FiendFolio.FF.Kukodemon.Var,
		RequiredXp = 4,
		Scale = 0.5,
		HP = 40,
	}
	table.insert(LilithBabies.IMP.Evolutions, "KUKODEMON")
	
	LilithBabies.PITCHFORK_HITCHER = {
		Type = FiendFolio.FF.PitchforkHitcher.ID,
		Variant = FiendFolio.FF.PitchforkHitcher.Var,
		RequiredXp = 4,
		Scale = 0.75,
		HP = 30,
	}
	table.insert(LilithBabies.IMP.Evolutions, "PITCHFORK_HITCHER")
	
	LilithBabies.PSI_HUNTER = {
		Type = FiendFolio.FF.Psihunter.ID,
		Variant = FiendFolio.FF.Psihunter.Var,
		RequiredXp = 4,
		Scale = 0.75,
		HP = 30,
	}
	table.insert(LilithBabies.BABY_BEGOTTEN.Evolutions, "PSI_HUNTER")
	
	LilithBabies.BULL = {
		Type = FiendFolio.FF.Bull.ID,
		Variant = FiendFolio.FF.Bull.Var,
		RequiredXp = 3,
		Scale = 0.75,
		HP = 25,
	}
	table.insert(LilithBabies.GOAT.Evolutions, "BULL")
	
	LilithBabies.GABBER = {
		Type = FiendFolio.FF.Gabber.ID,
		Variant = FiendFolio.FF.Gabber.Var,
		RequiredXp = 4,
		Scale = 0.75,
		HP = 40,
	}
	table.insert(LilithBabies.WHIPPER_G.Evolutions, "GABBER")
	
	LilithBabies.SCYTHE_RIDER = {
		Type = FiendFolio.FF.ScytheRider.ID,
		Variant = FiendFolio.FF.ScytheRider.Var,
		Evolutions = {"REAPER"},
		RequiredXp = 3,
		Scale = 0.75,
		HP = 24,
	}
	table.insert(LilithBabies.HOODED.Evolutions, "SCYTHE_RIDER")
	LilithBabies.REAPER = {
		Type = FiendFolio.FF.Reaper.ID,
		Variant = FiendFolio.FF.Reaper.Var,
		RequiredXp = 4,
		Scale = 0.75,
		HP = 34,
	}
	
	LilithBabies.GRIDDLE_HORN = {
		Type = FiendFolio.FF.GriddleHorn.ID,
		Variant = FiendFolio.FF.GriddleHorn.Var,
		RequiredXp = 4,
		Scale = 0.75,
		HP = 40,
	}
	table.insert(LilithBabies.LITTLE_HORN.Evolutions, "GRIDDLE_HORN")
	
	LilithBabies.LURCH = {
		Type = FiendFolio.FF.Lurch.ID,
		Variant = FiendFolio.FF.Lurch.Var,
		RequiredXp = 3,
		Scale = 0.75,
		HP = 40,
	}
	table.insert(LilithBabies.CULTIST.Evolutions, "LURCH")
	
	LilithBabies.PSILING = {
		Type = FiendFolio.FF.Psiling.ID,
		Variant = FiendFolio.FF.Psiling.Var,
		Evolutions = {"PSLEECH"},
		Scale = 0.75,
		HP = 10,
	}
	table.insert(LevelOneBabies, {Baby="PSILING", Weight=0.1})
	LilithBabies.PSLEECH = {
		Type = FiendFolio.FF.PsionLeech.ID,
		Variant = FiendFolio.FF.PsionLeech.Var,
		Evolutions = {"CROSSEYES", "FORESEER"},
		Scale = 0.75,
		HP = 20,
	}
	LilithBabies.CROSSEYES = {
		Type = FiendFolio.FF.Crosseyes.ID,
		Variant = FiendFolio.FF.Crosseyes.Var,
		Scale = 0.75,
		HP = 30,
	}
	LilithBabies.FORESEER = {
		Type = FiendFolio.FF.Foreseer.ID,
		Variant = FiendFolio.FF.Foreseer.Var,
		Scale = 0.75,
		HP = 30,
	}
end

for key, babyInfo in pairs(LilithBabies) do
	babyInfo.Name = key
end

local function GetBabyInfo(baby)
	local data = baby:GetData()
	if data.sigilOfLilithBabyInfo then
		return data.sigilOfLilithBabyInfo
	end
	local babyIdx = baby:GetData().sigilOfLilithBaby
	local player = baby.SpawnerEntity
	local pData = GetSigilOfLilithData(player)
	local babyData = pData.Babies[babyIdx]
	local babyInfo = LilithBabies[babyData.Key]
	data.sigilOfLilithBabyInfo = babyInfo
	return babyInfo
end

local function InitBaby(player, baby, babyInfo, idx)
	baby.SpawnerEntity = player
	baby.Parent = player
	baby:GetData().sigilOfLilithBaby = idx
	baby:GetData().sigilOfLilithBabyInfo = babyInfo
	
	if baby:ToNPC() then
		baby:AddCharmed(EntityRef(player), -1)
		
		if baby:IsBoss() then
			baby:AddEntityFlags(EntityFlag.FLAG_PERSISTENT)
			baby:GetData().samaelCorrectFriendlyPos = true
		end
	end
	
	if babyInfo.Scale and baby:ToNPC() then
		baby.Scale = babyInfo.Scale
	end
	
	if babyInfo.HP then
		baby.MaxHitPoints = babyInfo.HP
	end
	local stage = game:GetLevel():GetStage()
	if game:GetLevel():IsAscent() then
		stage = math.max(stage, 9)
	end
	if stage >= 5 then
		stage = stage * 0.8
	end
	baby.MaxHitPoints = baby.MaxHitPoints + kBabyStageHP * stage
	
	baby.HitPoints = math.min(baby.HitPoints, baby.MaxHitPoints)
	
	if babyInfo.Anm2 then
		baby:GetSprite():Load(babyInfo.Anm2, true)
	end
	
	if babyInfo.Sprites then
		for i, gfx in pairs(babyInfo.Sprites) do
			baby:GetSprite():ReplaceSpritesheet(i, gfx)
		end
		baby:GetSprite():LoadGraphics()
	end
end

local function SpawnNewBaby(player, idx, pos, key)
	if not key then
		local rng = player:GetTrinketRNG(SIGIL_OF_LILITH)
		key = lib.PickRandom(LevelOneBabies, rng).Baby
	end
	
	pos = pos or player.Position
	
	local babyInfo = LilithBabies[key]
	local babyRef
	
	for i=1, babyInfo.NumToSpawn or 1 do
		local baby = Isaac.Spawn(babyInfo.Type, babyInfo.Variant or 0, babyInfo.SubType or 0, pos, lib.ZeroVector, player)
		if i == 1 then
			baby:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		end
		baby = baby:ToNPC() or baby:ToFamiliar()
		InitBaby(player, baby, babyInfo, idx)
		baby.HitPoints = baby.MaxHitPoints
		if not babyRef then
			babyRef = baby
		end
	end
	
	local scale = Vector(0.8, 0.8)
	local poof1 = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 3, babyRef.Position, lib.ZeroVector, babyRef):ToEffect()
	poof1.SpriteScale = scale
	local poof2 = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF02, 4, babyRef.Position, lib.ZeroVector, babyRef):ToEffect()
	poof2.SpriteScale = scale
	
	local heart = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.HEART, 0, babyRef.Position, lib.ZeroVector, babyRef):ToEffect()
	heart:FollowParent(babyRef)
	heart.ParentOffset = Vector(0, -50)
	
	sfxManager:Play(SoundEffect.SOUND_THUMBSUP)
	sfxManager:Play(SoundEffect.SOUND_MUSHROOM_POOF_2)
	
	babyRef:SetColor(Color(1,0,0,1,1), 10, 1, true, true)
	
	return {
		Key = key,
		XP = 0,
		Ref = babyRef,
	}
end

function mod:SpecialSpawnSigilOfLilithBaby(player, name, key, pos)
	local baby = SpawnNewBaby(player, key, pos, name)
	baby.GridCollisionClass = EntityGridCollisionClass.GRIDCOLL_WALLS
	GetSigilOfLilithData(player).Babies[key] = baby
end

function mod:SigilOfLilithPlayerUpdate(player)
	local data = GetSigilOfLilithData(player)
	
	local maxBabies = player:GetTrinketMultiplier(SIGIL_OF_LILITH)
	
	if mod.GameStarted and maxBabies > 0 and game:GetRoom():IsClear() then
		for i=1, maxBabies do
			if not data.Babies[i] or not data.Babies[i].Ref or not data.Babies[i].Ref:Exists() then
				data.Babies[i] = SpawnNewBaby(player, i)
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.SigilOfLilithPlayerUpdate)

function mod:SigilOfLilithDeath(entity)
	if not entity:GetData().sigilOfLilithBaby then return end
	
	local player = GetPlayerParent(entity)
	local babyIdx = entity:GetData().sigilOfLilithBaby
	
	if player and player:HasTrinket(SIGIL_OF_LILITH) and babyIdx then
		local rng = player:GetTrinketRNG(SIGIL_OF_LILITH)
		local pData = GetSigilOfLilithData(player)
		local babyData = pData.Babies[babyIdx]
		local babyInfo = LilithBabies[babyData.Key]
		
		if babyInfo.DeathEvolution then
			local evoKey = babyInfo.DeathEvolution
			if babyInfo.DeathEvolutionVariant and entity.Variant ~= babyInfo.DeathEvolutionVariant then return end
			if babyInfo.DeathEvolutionSubtype and entity.SubType ~= babyInfo.DeathEvolutionSubtype then return end
			pData.Babies[babyIdx] = SpawnNewBaby(player, babyIdx, entity.Position, evoKey)
			entity:BloodExplode()
			entity:Remove()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, mod.SigilOfLilithDeath)

function mod:SigilOfLilithClear()
	for _, entity in pairs(Isaac.GetRoomEntities()) do
		local babyIdx = entity:GetData().sigilOfLilithBaby
		if babyIdx then
			local player = GetPlayerParent(entity)
			if player then
				local rng = player:GetTrinketRNG(SIGIL_OF_LILITH)
				local pData = GetSigilOfLilithData(player)
				local babyData = pData.Babies[babyIdx]
				local babyInfo = LilithBabies[babyData.Key]
				
				if not babyData.TargetEvolution or not LilithBabies[babyData.TargetEvolution] then
					babyData.TargetEvolution = nil
					if babyInfo.Evolutions then
						local weightedEvolutions = {}
						for k, v in pairs(babyInfo.Evolutions) do
							local evo, weight
							if type(k) == "string" and type(v) == "number" then
								evo = k
								weight = v
							else
								evo = v
								weight = 1
							end
							table.insert(weightedEvolutions, {Evo=evo, Weight=weight})
						end
						local result = lib.PickRandom(weightedEvolutions, rng)
						if result then
							babyData.TargetEvolution = result.Evo
						end
					end
				end
				
				if babyData.TargetEvolution then
					babyData.XP = babyData.XP + 1
					if player:HasTrinket(SIGIL_OF_LILITH) then
						local xpToEvolve = LilithBabies[babyData.TargetEvolution].RequiredXp
						if babyData.XP >= xpToEvolve then
							pData.Babies[babyIdx] = SpawnNewBaby(player, babyIdx, entity.Position, babyData.TargetEvolution)
							entity:Remove()
						else
							sfxManager:Play(SoundEffect.SOUND_BEEP)
							entity:SetColor(Color(1,0,0,1,1), 10, 1, true, true)
						end
					end
				end
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_SPAWN_CLEAN_AWARD, mod.SigilOfLilithClear)

function mod:SigilOfLilithBabyUpdate(baby)
	if not mod.GameStarted and not baby:GetData().sigilOfLilithBaby and (baby:ToFamiliar() or baby:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)) then
		for _, player in pairs(lib.GetPlayers()) do
			local pData = GetSigilOfLilithData(player)
			for idx, babyData in pairs(pData.Babies) do
				local babyInfo = LilithBabies[babyData.Key]
				if (not babyData.Ref or not babyData.Ref:Exists()) and babyInfo.Type == baby.Type then
					InitBaby(player, baby, babyInfo, idx)
					babyData.Ref = baby
				end
			end
		end
	end
	
	if game:GetRoom():GetFrameCount() <= 1 and baby:GetData().samaelCorrectFriendlyPos then
		baby.Position = (baby.Parent or Isaac.GetPlayer(0)).Position
	end
	
	if not baby:GetData().sigilOfLilithBaby then return end
	
	if FiendFolio then
		if baby.Type == FiendFolio.FF.GriddleHorn.ID and baby.Variant == FiendFolio.FF.GriddleHorn.Var and baby:GetData().state == "shootmote" then
			baby:GetData().state = "idle"
			baby.StateFrame = 0
			if baby:GetData().ad then
				baby:GetData().ad[5][2] = baby:GetData().ad[5][2] + 10
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithBabyUpdate)
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.SigilOfLilithBabyUpdate)

function mod:SigilOfLilithMissingBabies()
	for _, player in pairs(lib.GetPlayers()) do
		local pData = GetSigilOfLilithData(player)
		for idx, babyData in pairs(pData.Babies) do
			local babyInfo = LilithBabies[babyData.Key]
			if not babyData.Ref or not babyData.Ref:Exists() then
				local baby = Isaac.Spawn(babyInfo.Type, babyInfo.Variant or 0, babyInfo.SubType or 0, player.Position, lib.ZeroVector, player)
				baby = baby:ToNPC() or baby:ToFamiliar()
				InitBaby(player, baby, babyInfo, idx)
				babyData.Ref = baby
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.SigilOfLilithMissingBabies)

function mod:SigilOfLilithProj(proj)
	if proj.SpawnerEntity and (proj.SpawnerEntity:GetData().sigilOfLilithBaby or (proj.SpawnerEntity.SpawnerEntity and proj.SpawnerEntity.SpawnerEntity:GetData().sigilOfLilithBaby)) then
		proj:AddProjectileFlags(ProjectileFlags.HIT_ENEMIES | ProjectileFlags.CANT_HIT_PLAYER)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PROJECTILE_INIT, mod.SigilOfLilithProj)

function mod:SigilOfLilithProjCol(proj, collider)
	if proj.SpawnerEntity and proj.SpawnerEntity:GetData().sigilOfLilithBaby and collider and (collider:ToPlayer() or collider:HasEntityFlags(EntityFlag.FLAG_FRIENDLY)) then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_PROJECTILE_COLLISION, mod.SigilOfLilithProjCol)

-------------------- MISC AI TWEAKS --------------------

local function MaintainDistance(npc, target, distance, orbit)
	if npc and target then
		local currentDist = npc.Position:Distance(target.Position)
		local targetDist = target.Size + (distance or 50)
		local targetOffset = (npc.Position - target.Position):Resized(targetDist)
		if currentDist < targetDist then
			local targetPos = target.Position + targetOffset
			npc.Velocity = lib.Lerp(npc.Velocity, (targetPos - npc.Position), 0.1)
		end
		if currentDist < targetDist * 1.5 and orbit then
			local targetPos = target.Position + targetOffset:Rotated(10)
			npc.Velocity = lib.Lerp(npc.Velocity, (targetPos - npc.Position), 0.1)
		end
	end
end

function mod:SigilOfLilithImpUpdate(npc)
	if npc:GetData().sigilOfLilithBaby then
		local target = npc.Target or npc:GetPlayerTarget()
		
		local targetDist = 75
		
		if target:ToPlayer() then
			targetDist = 30
		end
		
		if npc.State == 4 or npc.State == 8 then
			MaintainDistance(npc, target, targetDist)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithImpUpdate, EntityType.ENTITY_IMP)

local function StopAttackingPlayer(idleState, sound, eType)
	mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
		if npc:GetData().sigilOfLilithBaby and (npc.Target or npc:GetPlayerTarget()).Type == EntityType.ENTITY_PLAYER then
			if idleState then
				npc.State = idleState
				MaintainDistance(npc, npc:GetPlayerTarget())
			end
			lib.SuppressSound(sound)
		end
	end, eType)
end
StopAttackingPlayer(3, SoundEffect.SOUND_BIG_LEECH, EntityType.ENTITY_ADULT_LEECH)
StopAttackingPlayer(nil, SoundEffect.SOUND_LEECH, EntityType.ENTITY_CHARGER_L2)

function mod:SigilOfLilithGoatUpdate(npc)
	if not npc:GetData().sigilOfLilithBaby then return end
	
	local target = npc.Target or npc:GetPlayerTarget()
	
	if target:ToPlayer() then
		MaintainDistance(npc, target, 30)
	elseif npc.State == 4 then
		MaintainDistance(npc, target, 66)
	end
	
	if npc.FrameCount % 300 == 0 and npc.State == 4 then
		npc.State = 8
		npc.StateFrame = 0
		npc:GetSprite():Play("AttackHori", true)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithGoatUpdate, EntityType.ENTITY_GOAT)

local function ImmuneState(state, eType)
	mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, function(_, npc)
		if npc:GetData().sigilOfLilithBaby and npc:ToNPC() and npc:ToNPC().State == state then
			return false
		end
	end)
end
ImmuneState(8, EntityType.ENTITY_CHARGER)
ImmuneState(8, EntityType.ENTITY_CHARGER_L2)
ImmuneState(8, EntityType.ENTITY_LEECH)
ImmuneState(8, EntityType.ENTITY_ADULT_LEECH)
ImmuneState(6, EntityType.ENTITY_GOAT)

mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.EARLY, function(_, npc)
	if npc:GetData().sigilOfLilithBaby and FiendFolio then
		if npc.Type == FiendFolio.FF.Bull.ID and npc.Variant == FiendFolio.FF.Bull.Var and npc:GetData().state == "charge" then
			return false
		elseif npc.Type == FiendFolio.FF.PsionLeech.ID and npc.Variant == FiendFolio.FF.PsionLeech.Var and npc:GetData().state == "attack" then
			return false
		end
	end
end)

function mod:SigilOfLilithFallenUpdate(npc)
	if not npc:GetData().sigilOfLilithBaby then return end
	
	local sprite = npc:GetSprite()
	
	-- Don't split - just die
	if sprite:IsPlaying("Split") and sprite:GetFrame() > 10 then
		npc:Kill()
		return
	end
	
	local target = npc.Target or npc:GetPlayerTarget()
	
	if target:ToPlayer() then
		npc.State = 4
		if sprite:GetAnimation() == "Walk" and npc.Velocity:Length() < 1 then
			sprite:SetFrame("Walk", 24)
		end
	elseif npc.State == 4 then
		MaintainDistance(npc, target, 75)
	end
	
	if npc.State == 9 then
		npc.State = 10
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithFallenUpdate, EntityType.ENTITY_FALLEN)

function mod:SigilOfLilithBoneTrap(npc)
	if npc.Variant == 10 and npc.SpawnerEntity and (npc.SpawnerEntity:GetData().sigilOfLilithBaby or npc.SpawnerEntity:GetData().isThanatophiliaMinion) then
		npc:GetSprite():Load("gfx/samael_bone_trap.anm2", true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.SigilOfLilithBoneTrap, EntityType.ENTITY_CULTIST)

-------------------- "FAKE PLAYER" --------------------

local function SpawnLocust(player, pos, target)
	local locustType = (Random() % 5) + 1
	local n = 1
	if locustType == 5 then
		n = n + Random() % 4
	end
	for i=1, n do
		local fly = Isaac.Spawn(EntityType.ENTITY_FAMILIAR, FamiliarVariant.BLUE_FLY, locustType, pos, lib.ZeroVector, player)
		fly.Parent = player
		fly.Target = target
		fly:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
		fly.Velocity = RandomVector() * 5
	end
end

function mod:SigilOfLilithFakePlayerUpdate(fam)
	if not fam:GetData().sigilOfLilithBaby then return end
	
	local player = fam.Player or Isaac.GetPlayer(0)
	local data = fam:GetData()
	local sprite = fam:GetSprite()
	local babyInfo = GetBabyInfo(fam)
	
	if babyInfo.Scale then
		fam.SpriteScale = Vector(babyInfo.Scale, babyInfo.Scale)
	end
	
	if fam.FireCooldown > 0 and data.samaelLastFireCooldown == 0 then
		if babyInfo.FireDelay then
			fam.FireCooldown = babyInfo.FireDelay
		end
		data.HeadFrameDelay = math.ceil(fam.FireCooldown * 0.3)
		
		if babyInfo.Name == "APOLLYON" and fam:GetDropRNG():RandomInt(6) == 0 then
			local fly = player:AddBlueFlies(1, fam.Position, fam.Target)
		elseif babyInfo.Name == "BAPOLLYON" and fam:GetDropRNG():RandomInt(6) == 0 then
			SpawnLocust(player, fam.Position, fam.Target)
		end
		
		if babyInfo.Name == "BASTEMA" then
			if (data.bastemaCounter or 0) < 2 then
				fam.FireCooldown = 0
				data.bastemaCounter = (data.bastemaCounter or 0) + 1
			else
				data.bastemaCounter = 0
			end
		end
	end
	data.samaelLastFireCooldown = fam.FireCooldown
	
	if (data.HeadFrameDelay or 0) > 0 then
		sprite:SetOverlayFrame(sprite:GetOverlayAnimation(), 2)
		data.HeadFrameDelay = data.HeadFrameDelay - 1
	else
		sprite:SetOverlayFrame(sprite:GetOverlayAnimation(), 0)
	end
	
	-- Offset the natural health degredation of minisaacs.
	if fam.FrameCount % 15 == 0 and fam.HitPoints < fam.MaxHitPoints then
		fam.HitPoints = math.min(fam.HitPoints + 0.2, fam.MaxHitPoints)
	end
end
mod:AddCallback(ModCallbacks.MC_FAMILIAR_UPDATE, mod.SigilOfLilithFakePlayerUpdate, FamiliarVariant.MINISAAC)

function mod:SigilOfLilithFakePlayerRender(fam)
	if not fam:GetData().sigilOfLilithBaby then return end
	
	local pos = Isaac.WorldToScreen(fam.Position)
	
	local babyInfo = GetBabyInfo(fam)
	
	if babyInfo.BodyCostumes then
		for _, costume in ipairs(babyInfo.BodyCostumes) do
			costume.Scale = fam.SpriteScale
			costume:SetFrame(fam:GetSprite():GetAnimation(), fam:GetSprite():GetFrame())
			costume:Render(pos, lib.ZeroVector, lib.ZeroVector)
			local c1 = Color(1,1,1,1,1,1,1) * costume.Color
			local c2 = fam:GetColor()
			c1:SetTint(c2.R, c2.G, c2.B, c2.A)
			c1:SetOffset(c2.RO, c2.GO, c2.BO)
			costume.Color = c1
		end
		fam:GetSprite():RenderLayer(4, pos, lib.ZeroVector, lib.ZeroVector)
	end
	
	if babyInfo.HeadCostumes then
		for _, costume in ipairs(babyInfo.HeadCostumes) do
			costume.Scale = fam.SpriteScale
			costume:SetFrame(fam:GetSprite():GetOverlayAnimation(), fam:GetSprite():GetOverlayFrame())
			costume:Render(pos, lib.ZeroVector, lib.ZeroVector)
			local c1 = Color(1,1,1,1,1,1,1) * costume.Color
			local c2 = fam:GetColor()
			c1:SetTint(c2.R, c2.G, c2.B, c2.A)
			c1:SetOffset(c2.RO, c2.GO, c2.BO)
			costume.Color = c1
		end
	end
	
	--Isaac.RenderText("" .. fam.HitPoints .. " / " .. fam.MaxHitPoints, pos.X, pos.Y, 1, 1, 1, 255)
end
mod:AddCallback(ModCallbacks.MC_POST_FAMILIAR_RENDER, mod.SigilOfLilithFakePlayerRender, FamiliarVariant.MINISAAC)

function mod:SigilOfLilithFakePlayerTearsInit(tear)
	if tear.SpawnerEntity and tear.SpawnerEntity.Type == EntityType.ENTITY_FAMILIAR and tear.SpawnerEntity:GetData().sigilOfLilithBaby then
		local babyInfo = GetBabyInfo(tear.SpawnerEntity)
		if babyInfo.TearScale then
			tear.Scale = babyInfo.TearScale
		end
		if babyInfo.TearVariant then
			tear:ChangeVariant(babyInfo.TearVariant)
		end
		if babyInfo.TearFlags then
			tear:AddTearFlags(babyInfo.TearFlags)
		end
		if babyInfo.TearColor then
			tear.Color = babyInfo.TearColor
		end
		
		if tear.SpawnerEntity:GetData().bastemaCounter == 1 then
			tear.Velocity = tear.Velocity:Rotated(10)
		end
		if tear.SpawnerEntity:GetData().bastemaCounter == 2 then
			tear.Velocity = tear.Velocity:Rotated(-10)
			tear:Update()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_INIT, mod.SigilOfLilithFakePlayerTearsInit)

function mod:SigilOfLilithFakePlayerTearsUpdate(tear)
	if tear.FrameCount == 0 and tear.SpawnerEntity and tear.SpawnerEntity.Type == EntityType.ENTITY_FAMILIAR and tear.SpawnerEntity:GetData().sigilOfLilithBaby then
		local babyInfo = GetBabyInfo(tear.SpawnerEntity)
		if babyInfo.Damage then
			tear.CollisionDamage = babyInfo.Damage
		end
		--[[if babyInfo.DamageMult then
			tear.CollisionDamage = tear.CollisionDamage * babyInfo.DamageMult
		end]]
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.SigilOfLilithFakePlayerTearsUpdate)

function mod:SigilOfLilithFakePlayerMeleeInit(knife)
	if knife.SpawnerType == EntityType.ENTITY_FAMILIAR and knife.SpawnerEntity
			and knife.SpawnerEntity.Variant == FamiliarVariant.MINISAAC 
			and knife.SpawnerEntity.SubType == 99 and knife.SpawnerEntity:GetData().sigilOfLilithBaby then
		local babyInfo = GetBabyInfo(knife.SpawnerEntity)
		if babyInfo.MeleeSprite then
			knife:GetSprite():Load(babyInfo.MeleeSprite, true)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_INIT, mod.SigilOfLilithFakePlayerMeleeInit, 4)

function mod:SigilOfLilithFakePlayerMelee(knife)
	if knife.SpawnerType == EntityType.ENTITY_FAMILIAR and knife.SpawnerEntity
			and knife.SpawnerEntity.Variant == FamiliarVariant.MINISAAC 
			and knife.SpawnerEntity.SubType == 99 and knife.SpawnerEntity:GetData().sigilOfLilithBaby then
		local babyInfo = GetBabyInfo(knife.SpawnerEntity)
		if babyInfo.Damage then
			knife.CollisionDamage = babyInfo.Damage / 3
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE, mod.SigilOfLilithFakePlayerMelee, 4)

-------------------- FRIENDLY LOKI TWEAKS --------------------

local function CountBoomFlies()
	local numBoomFlies = 0
	for _, fly in pairs(Isaac.FindByType(EntityType.ENTITY_BOOMFLY)) do
		if fly:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
			numBoomFlies = numBoomFlies + 1
		end
	end
	return numBoomFlies
end

function mod:SigilOfLilithLokiUpdate(npc)
	if npc:GetData().sigilOfLilithBaby then
		local target = npc.Target or npc:GetPlayerTarget()
		
		if npc.State == 8 and npc:GetSprite():GetFrame() == 0 then
			if (FiendFolio and npc.SubType == FiendFolio.FF.AlienLokiChampion.Sub) or CountBoomFlies() >= 4 then
				npc.State = 9
				npc:GetSprite():Play("Attack03", true)
				npc.StateFrame = 0
			end
		end
		
		if npc.State == 3 then
			MaintainDistance(npc, target, 85)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithLokiUpdate, EntityType.ENTITY_LOKI)

function mod:SigilOfLilithHornyUpdate(npc)
	if npc:GetData().sigilOfLilithBaby then
		local target = npc.Target or npc:GetPlayerTarget()
		
		if npc.State == 13 and npc:GetSprite():GetFrame() == 0 then
			if CountBoomFlies() > 0 then
				npc.State = 3
				npc:GetSprite():Play("Idle", true)
				npc.StateFrame = 0
			end
		end
		
		if npc.State == 3 then
			MaintainDistance(npc, target, 85)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithHornyUpdate, EntityType.ENTITY_HORNY_BOYS)

function mod:SigilOfLilithFire(npc)
	if npc.Variant == 10 and not npc:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) and npc.SpawnerEntity
			and (npc.SpawnerEntity:GetData().sigilOfLilithBaby or (npc.SpawnerEntity.SpawnerEntity and npc.SpawnerEntity.SpawnerEntity:GetData().sigilOfLilithBaby)) then
		npc:AddEntityFlags(EntityFlag.FLAG_FRIENDLY)
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithFire, EntityType.ENTITY_FIREPLACE)

-------------------- FRIENDLY LITTLE/BIG HORN TWEAKS --------------------

-- Big horn hand subtypes:
-- 0 = fake hole
-- (L/R)
-- 1/2 = slap
-- 3/4 = bomb
-- 5/6 = cluster bombs
-- 7/8 = little horn

local function FromMyBigHorn(ent)
	return ent.SpawnerEntity and ent.SpawnerEntity.Type == EntityType.ENTITY_BIG_HORN
		and ent.SpawnerEntity.Variant == 0 and ent.SpawnerEntity:GetData().sigilOfLilithBaby
end

local function FromMyLittleHorn(ent)
	return ent.SpawnerEntity and ent.SpawnerEntity.Type == EntityType.ENTITY_LITTLE_HORN
		and ent.SpawnerEntity.Variant == 0 and ent.SpawnerEntity:GetData().sigilOfLilithBaby
end

function mod:SigilOfLilithBigHornInit(npc)
	if npc.Variant > 0 and FromMyBigHorn(npc) then
		npc.Scale = npc.SpawnerEntity:ToNPC().Scale
		
		--local target = npc.SpawnerEntity.Target or npc:GetPlayerTarget()
		
		if npc.Variant == 1 then
			if npc.SubType == 0 then
				npc:Remove()
				return
			--elseif (npc.SubType == 3 or npc.SubType == 4) and target and not target:ToPlayer() and not target:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) then
			elseif npc.SubType > 2 then
				npc.SubType = (npc.SubType % 2) + 1
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.SigilOfLilithBigHornInit, EntityType.ENTITY_BIG_HORN)

function mod:SigilOfLilithBigHornUpdate(npc)
	if npc.Variant == 0 and npc:GetData().sigilOfLilithBaby then
		local sprite = npc:GetSprite()
		if (sprite:IsPlaying("Charge") or sprite:IsPlaying("Charge2") or sprite:IsPlaying("Charge3"))
				and sprite:GetFrame() == 0 and #Isaac.FindByType(EntityType.ENTITY_LITTLE_HORN, 1) >= 4 then
			sprite:Play("Grin", true)
			--npc.State = 3
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithBigHornUpdate, EntityType.ENTITY_BIG_HORN)

function mod:SigilOfLilithHornBallsInit(npc)
	if npc.Variant == 1 then
		if FromMyBigHorn(npc) or FromMyLittleHorn(npc) then
			npc:GetSprite():Load("gfx/samael_darkball.anm2", true)
			if FromMyLittleHorn(npc) then
				npc.Scale = npc.SpawnerEntity:ToNPC().Scale
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.SigilOfLilithHornBallsInit, EntityType.ENTITY_LITTLE_HORN)

function mod:SigilOfLilithHornBallsUpdate(npc)
	if npc.Variant == 1 and npc.SubType == 0 and (FromMyBigHorn(npc) or FromMyLittleHorn(npc)) then
		local target = npc.Target or npc:GetPlayerTarget()
		if target and target:ToPlayer() then
			MaintainDistance(npc, target, 30, true)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithHornBallsUpdate, EntityType.ENTITY_LITTLE_HORN)

function mod:SigilOfLilithPoof(eff)
	if eff.FrameCount == 0 and eff.SpawnerEntity and eff.SpawnerEntity:ToNPC() and FromMyBigHorn(eff.SpawnerEntity) then
		eff.SpriteScale = eff.SpriteScale * eff.SpawnerEntity:ToNPC().Scale
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SigilOfLilithPoof, 16)

function mod:SigilOfLilithLittleHornUpdate(npc)
	if npc.Variant == 0 and npc:GetData().sigilOfLilithBaby then
		local target = npc.Target or npc:GetPlayerTarget()
		local isTargetingPlayer = target and target:ToPlayer()
		
		if npc.SubType == 1 and npc.State == 8 and isTargetingPlayer then
			-- (Fire champion) Don't spit bombs at the player.
			npc.State = 6
		end
		if npc.State == 13 or npc.State == 14 then
			-- Don't try to create pitfalls.
			npc.State = 9
		end
		if npc.State == 10 then
			-- Don't throw Mega Troll Bombs
			npc.State = 9
		end
		if npc.State == 9 and isTargetingPlayer then
			-- Don't throw bombs at the player.
			npc.State = 6
		end
		
		if npc.State == 3 then
			MaintainDistance(npc, target, 100)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.SigilOfLilithLittleHornUpdate, EntityType.ENTITY_LITTLE_HORN)

function mod:SigilOfLilithPitfall(npc)
	if FromMyLittleHorn(npc) then
		npc:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.SigilOfLilithPitfall, EntityType.ENTITY_PITFALL)

function mod:SigilOfLilithBomb(bomb)
	if FromMyLittleHorn(bomb) then
		bomb.SpriteScale = bomb.SpriteScale * bomb.SpawnerEntity:ToNPC().Scale
	end
end
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.SigilOfLilithBomb)

-------------------- AZAZZER --------------------

function mod:EvilAzazelUpdate(npc)
	if npc.Variant ~= mod.ENTITIES.EVIL_AZAZEL.Var then return end
	
	npc.SpriteOffset = Vector(0, -4)
	
	local target = npc.Target or npc:GetPlayerTarget()
	MaintainDistance(npc, target, 30)
end
mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, mod.EvilAzazelUpdate, mod.ENTITIES.EVIL_AZAZEL.ID)

function mod:EvilAzazelLaserInit(laser)
	if laser.SpawnerEntity and laser.SpawnerEntity.Type == mod.ENTITIES.EVIL_AZAZEL.ID and laser.SpawnerEntity.Variant == mod.ENTITIES.EVIL_AZAZEL.Var then
		laser.PositionOffset = Vector(0,0)
		laser.ParentOffset = Vector(0,0)
		laser.Color = lib.InvisibleColor
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_INIT, mod.EvilAzazelLaserInit)

function mod:EvilAzazelLaserUpdate(laser)
	if laser.SpawnerEntity and laser.SpawnerEntity.Type == mod.ENTITIES.EVIL_AZAZEL.ID and laser.SpawnerEntity.Variant == mod.ENTITIES.EVIL_AZAZEL.Var then
		if laser.FrameCount == 1 then
			laser.SpriteScale = Vector.One * laser.SpawnerEntity:ToNPC().Scale
			laser.PositionOffset = Vector(0, -23)
			laser:SetMaxDistance(80)
		end
		if laser.FrameCount == 2 then
			laser.Color = lib.NullColor
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, mod.EvilAzazelLaserUpdate)