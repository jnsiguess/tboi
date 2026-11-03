local mod = SamaelMod
local game = mod.Game
local sfxManager = mod.SfxManager

local lib = {}

--------------------------------------------------
---- LOGGING
--------------------------------------------------

function lib.Log(input, toConsole)
	local str = "[Samael] "
	if (type(input) == "userdata" or type(input) == "table") and input.X and input.Y then
		str = str .. "Vector: " .. input.X .. ", " .. input.Y
	else
		str = str .. input
	end
	Isaac.DebugString(str)
	if toConsole then
		print(str)
	end
end

function lib.LogErr(input)
	lib.Log("" .. input, true)
end

--------------------------------------------------
---- PLAYER TYPES
--------------------------------------------------

lib.SamaelId = Isaac.GetPlayerTypeByName("Samael", false)
lib.TaintedSamaelId = Isaac.GetPlayerTypeByName("Samael", true)
lib.OtherSamaelId = Isaac.GetPlayerTypeByName("Samael ", false)

function lib.IsChallengeSamael(player)
	return player and player:ToPlayer() and player:ToPlayer():GetPlayerType() == lib.OtherSamaelId
end

function lib.IsTaintedSamael(player)
	return player and player:ToPlayer() and (lib.IsChallengeSamael(player) or player:ToPlayer():GetPlayerType() == lib.TaintedSamaelId)
end

-- Important: Returns TRUE for Tainted Samael, as well.
-- This is because Samael and Tainted Samael share a lot of base functionality.
function lib.IsSamael(player)
	return player and player:ToPlayer() and (lib.IsTaintedSamael(player) or player:ToPlayer():GetPlayerType() == lib.SamaelId)
end

function lib.IsTaintedChar(player)
	return player:GetPlayerType() == Isaac.GetPlayerTypeByName(player:GetName(), true)
end

--------------------------------------------------
---- VECTORS
--------------------------------------------------

lib.NormalVector = Vector(0,1)
lib.ZeroVector = Vector(0,0)

--Stolen from PROAPI
function lib.Lerp(first, second, percent)
	return (first + (second - first)*percent)
end

-- Slightly different lerp function for getting a greater velocity when further away.
-- In retrospect this might not be good but it's already in use so I don't wan to mess with it.
function lib.LerpVelocity(vel, pos, targetPos, percent)
	return (vel * (1.0 - percent)):__add(targetPos:__sub(pos) * percent)
end

local kUp = Vector(0, -1)
local kDown = Vector(0, 1)
local kLeft = Vector(-1, 0)
local kRight = Vector(1, 0)
-- Returns a vector in the given Direction.
function lib.DirectionalVector(direction, magnitude)
	magnitude = magnitude or 1
	if direction == Direction.UP then
		return kUp*magnitude
	elseif direction == Direction.DOWN then
		return kDown*magnitude
	elseif direction == Direction.LEFT then
		return kLeft*magnitude
	elseif direction == Direction.RIGHT then
		return kRight*magnitude
	else
		return kZeroVector
	end
end

function lib.GetDirectionFromVector(vector)
	local angle = lib.Round(vector:GetAngleDegrees(), 2)
	
	if angle >= 135 or angle <= -135 then
		return Direction.LEFT
	elseif angle < -45 and angle > -135 then
		return Direction.UP
	elseif angle >= -45 and angle <= 45 then
		return Direction.RIGHT
	elseif angle > 45 and angle < 135 then
		return Direction.DOWN
	end
	
	return Direction.NO_DIRECTION
end

-- Clamps a vector to the nearest 90 degree angle.
function lib.ClampVector(vector)
	local dir = lib.GetDirectionFromVector(vector)
	return lib.DirectionalVector(dir)
end

function lib.RotateToward(current, target, rotSpeed)
	current = current % 360
	target = target % 360
	if math.abs((current - 360) - target) < math.abs(current - target) then
		current = current - 360
	elseif math.abs((current + 360) - target) < math.abs(current - target) then
		current = current + 360
	end
	return lib.Lerp(current, target, rotSpeed)
end

----------------------------------------------------------------
---- COLORS
----------------------------------------------------------------

-- AB+ compatible color constructor
-- Thanks, oatmealine
function lib.NewColor(r, g, b, a, ro, go, bo)
	if not REPENTANCE then
		a = a or 1
		ro = ro or 0
		go = go or 0
		bo = bo or 0

		ro = math.floor(ro * 255)
		go = math.floor(go * 255)
		bo = math.floor(bo * 255)
	end
	return Color(r, g, b, a, ro, go, bo)
end

-- Color Constants
lib.SamaelTearColor = lib.NewColor(0.917, 0.417, 0.917) -- Samael's default TearColor
lib.BloodColor = lib.NewColor(0.9, 0, 0)
lib.InvisibleColor = lib.NewColor(1, 1, 1, 0)
lib.NullColor = lib.NewColor(1, 1, 1, 1)

-- Color equivalence function
function lib.SameColor(c1, c2)
	return (
		c1.R == c2.R and c1.G == c2.G and c1.B == c2.B and c1.A == c2.A
		and c1.RO == c2.RO and c1.GO == c2.GO and c1.BO == c2.BO
	)
end

----------------------------------------------------------------
---- AB+ Compatabiltiy Helpers (Unfortunately)
----------------------------------------------------------------

-- AB+ compatible (and mod-safe) item checks
function lib.HasItem(player, collectibleType, ignoreModifiers)
	if not collectibleType or collectibleType <= 0 then return false end
	return player:HasCollectible(collectibleType, ignoreModifiers) or false
end
function lib.HasItemEffect(player, collectibleType)
	if not collectibleType or collectibleType <= 0 then return false end
	return player:GetEffects():HasCollectibleEffect(collectibleType)
end

-- AB+ compatible animation check
function lib.CurrentAnimIs(sprite, animName)
	if REPENTANCE then
		return sprite:GetAnimation() == animName
	else
		return sprite:IsPlaying(animName) or sprite:IsFinished(animName)
	end
end

-- AB+ compatible TearFlag operations
function lib.HasTearFlag(entity, tearflag)
	if not tearflag then return false end
	if REPENTANCE then
		return entity:HasTearFlags(tearflag)
	else
		return entity.TearFlags & tearflag ~= 0
	end
end
function lib.AddTearFlag(entity, tearflag)
	if not tearflag then return false end
	if REPENTANCE then
		entity:AddTearFlags(tearflag)
	elseif not lib.HasTearFlag(entity, tearflag) then
		entity.TearFlags = entity.TearFlags | tearflag
	end
end
function lib.RemoveTearFlag(entity, tearflag)
	if not tearflag then return false end
	if REPENTANCE then
		entity:ClearTearFlags(tearflag)
	elseif lib.HasTearFlag(entity, tearflag) then
		entity.TearFlags = entity.TearFlags - tearflag
	end
end

-- Finds the current animation that a Scythe is playing.
-- For lack of GetAnimation() in AB+
function lib.FindCurrentScytheAnim(sprite)
	local anims = {
			"Swing", "Charge", "Idle", "Spin", "Fire"}
	for i,anim in ipairs(anims) do
		if lib.CurrentAnimIs(sprite, anim) then
			return anim
		elseif lib.CurrentAnimIs(sprite, "Small" .. anim) then
			return "Small" .. anim
		end
	end
end

if not REPENTANCE then
-- AB+ True Co-op (better late than never I suppose).
function onTrueCoopInit()
	InfinityTrueCoopInterface.AddCharacter({
			Name = "Samael",
			Type = PlayerType.PLAYER_ISAAC,
			SelectionGfx = "gfx/characters/truecoop_samael.png",
			SoulHearts = 4,
				BlackHearts = 2,
				OnStart = function(player)
					player:GetSprite():Load("gfx/samael.anm2", true)
					mod:PostPlayerInit(player)
						mod:SpawnScythe(player)
						mod:UpdateScytheType(player)
						mod:UpdateNumScythes(player)
						mod:SpawnChargeBar(player)
			end,
			AllowHijacking = true,
			ActualType = kSamaelId,
			BossPortrait = "gfx/ui/boss/playerportrait_samael.png",
			BossName = "gfx/ui/boss/playername_samael.png",
			GhostName = "gfx/ui/boss/playername_samael.png",
	})
	InfinityTrueCoopInterface.AddCharacterToWheel("Samael")
	InfinityTrueCoopInterface.AssociatePlayerTypeName(kSamaelId, "Samael")
end

if InfinityTrueCoopInterface then
	onTrueCoopInit()
else
	if not __infinityTrueCoop then
		__infinityTrueCoop = {}
	end

	__infinityTrueCoop[#__infinityTrueCoop + 1] = onTrueCoopInit
end
-- True co-op end.
end

--------------------------------------------------
---- RNG
--------------------------------------------------

-- Helper for calculating luck-based activation chances
function lib.GetActivationChance(baseChance, luck, maxLuck, noLowerThanBase)
	local chance = baseChance + (1-baseChance)*(luck / maxLuck)
	if noLowerThanBase then
		return math.max(chance, baseChance)
	else
		return chance
	end
end

-- Same as above but with a hard upper limit below 100%.
function lib.GetCappedActivationChance(baseChance, maxChance, luck, luckForMaxChance, noLowerThanBase)
	local chance = baseChance + (maxChance-baseChance) * math.min(luck / luckForMaxChance, 1)
	if noLowerThanBase then
		return math.max(chance, baseChance)
	else
		return chance
	end
end

local function GetWeight(var)
	if type(var) == "table" and var.Weight then
		if type(var.Weight) == "number" then
			return var.Weight
		end
		lib.LogErr("lib.PickRandom received a table with non-numeric Weight.")
	end
	return 1
end

function lib.PickRandom(tab, rngOrSeed)
	if not tab then return end
	if type(tab) ~= "table" then return tab end
	
	local totalWeight = 0
	for _, value in pairs(tab) do
		local weight = GetWeight(value)
		totalWeight = totalWeight + weight
	end
	
	if totalWeight == 0 then return end
	
	local rand
	if type(rngOrSeed) == "number" then
		local rng = RNG()
		rng:SetSeed(rngOrSeed, 35)
		rand = rng:RandomFloat() * totalWeight
	elseif rngOrSeed then
		rand = rngOrSeed:RandomFloat() * totalWeight
	else
		rand = Random() % totalWeight
	end
	local origRand = rand
	local last
	
	for _, value in pairs(tab) do
		local weight = GetWeight(value)
		if rand < weight then
			return value
		elseif weight > 0 then
			last = value
		end
		rand = rand - weight
	end
	
	if totalWeight > 0 then
		lib.LogErr("Weighted random chose " .. origRand .. " out of " .. totalWeight)
	end
	
	return last
end

--------------------------------------------------
---- STATS
--------------------------------------------------

function lib.FireDelayToTears(fireDelay)
	return 30 / (fireDelay + 1)
end

function lib.TearsToFireDelay(tears)
	return math.max((30 / tears) - 1, -0.99)
end

-- Returns the players' "Tears" stat (# of tears shot per second).
function lib.CalcTears(player)
	return lib.FireDelayToTears(player.MaxFireDelay)
end

-- Returns the player's "damage per second", based on stats alone.
function lib.CalcDps(player)
	return player.Damage * lib.CalcTears(player)
end

-- Applies a "Tears Up" to the player's FireDelay stat, using a fixed Tears modifier (like 0.7 for sad onion).
function lib.TearsUp(firedelay, val)
	local currentTears = lib.FireDelayToTears(firedelay)
	local newTears = currentTears + val
	return lib.TearsToFireDelay(newTears)
end

-- Applied a multiplier to the player's Tears stat.
function lib.TearsMult(firedelay, mult)
	local currentTears = lib.FireDelayToTears(firedelay)
	local newTears = currentTears * mult
	return lib.TearsToFireDelay(newTears)
end

function lib.FireDelayIgnoringWeaponType(player, fireDelay)
	fireDelay = fireDelay or player.MaxFireDelay
	if player:HasCollectible(CollectibleType.COLLECTIBLE_TECHNOLOGY_2) then
		fireDelay = lib.TearsMult(fireDelay, 1.5)
	end
	if player:HasWeaponType(WeaponType.WEAPON_BRIMSTONE) then
		return lib.TearsMult(fireDelay, 3)
	elseif player:HasWeaponType(WeaponType.WEAPON_BOMBS) then
		return lib.TearsMult(fireDelay, 2.5)
	elseif player:HasWeaponType(WeaponType.WEAPON_MONSTROS_LUNGS) then
		return lib.TearsMult(fireDelay, 4.3)
	end
	return fireDelay
end

-- Returns the player's MaxFireDelay, ignoring the negative multipliers of certain WeaponTypes.
function lib.GetUnmodifiedFireDelay(player)
	local fireDelay = player.MaxFireDelay 
	if lib.IsTaintedSamael(player) and fireDelay > 0 then
		fireDelay = fireDelay / 2.05
	end
	-- Samael already ignores WeaponType when it comes to fire delay.
	if lib.IsSamael(player) then
		return fireDelay
	end
	return lib.FireDelayIgnoringWeaponType(player, fireDelay)
end

-- Returns Tears, ignoring WeaponTypes.
function lib.CalcUnmodifiedTears(player)
	return lib.FireDelayToTears(lib.GetUnmodifiedFireDelay(player))
end

-- Returns DPS, ignoring WeaponTypes.
function lib.CalcUnmodifiedDps(player)
	return player.Damage * lib.CalcUnmodifiedTears(player)
end

-- Accurate count of how many tears the player would fire at once, for the default "TEARS" WeaponType only.
-- Modified version of a function made by Aevilok.
function lib.GetNumProjectiles(player, noRng)
	local mutantSpider = CollectibleType.COLLECTIBLE_MUTANT_SPIDER
	local innerEye = CollectibleType.COLLECTIBLE_INNER_EYE
	local _2020 = CollectibleType.COLLECTIBLE_20_20
	local wiz = CollectibleType.COLLECTIBLE_THE_WIZ
	
	local baseProjectiles
	if player:HasCollectible(mutantSpider) and player:HasCollectible(innerEye) then
		baseProjectiles = 5
	elseif player:HasCollectible(mutantSpider) then
		baseProjectiles = 4
	elseif player:HasCollectible(innerEye) then
		baseProjectiles = 3
	elseif player:HasCollectible(_2020) then
		baseProjectiles = 2
	else
		baseProjectiles = 1
	end
	
	local stackingItemProjectiles =
			2 * (math.max(0, player:GetCollectibleNum(mutantSpider) - 1))
			+ math.max(0, (player:GetCollectibleNum(innerEye) - 1))
			+ math.max(0, (player:GetCollectibleNum(_2020) - 1))
	
	local numProjectiles = baseProjectiles + stackingItemProjectiles
	
	if not noRng and player:HasPlayerForm(PlayerForm.PLAYERFORM_BOOK_WORM) and Random() % 4 == 0 then
		numProjectiles = numProjectiles + 1
	end
	
	numProjectiles = numProjectiles + (numProjectiles % (player:GetCollectibleNum(wiz)+1))
	
	return numProjectiles
end

--------------------------------------------------
---- TABLES
--------------------------------------------------

function lib.GetOrInit(tab, key, key2)
	if not key then
		lib.LogErr("Tried to access nil key!")
		return
	end
	if not tab then
		lib.LogErr("Tried to access key " .. key .. " in nil table.")
		return
	end
	if type(key) == "number" and key ~= key then
		lib.LogErr("Tried to access NaN key...")
		return
	end
	if not tab[key] then
		tab[key] = {}
	end
	if key2 then
		return lib.GetOrInit(tab[key], key2)
	end
	return tab[key]
end

function lib.ShallowCopy(tab)
	local newTab = {}
	for k,v in pairs(tab) do
		newTab[k] = v
	end
	return newTab
end

function lib.Shuffle(tbl, rngOrSeed)
	local rng
	
	if type(rngOrSeed) == "number" then
		rng = RNG()
		rng:SetSeed(rngOrSeed, 1)
	else
		rng = rngOrSeed
	end
	
	for i = #tbl, 2, -1 do
		local j
		if rng then
			j = rng:RandomInt(i)+1
		else
			j = (Random() % i)+1
		end
		tbl[i], tbl[j] = tbl[j], tbl[i]
	end
	return tbl
end

function lib.IsEmpty(tab)
	return (next(tab) == nil)
end

function lib.Access(tab, ...)
	if not tab then return end
	
	for _, k in ipairs({...}) do
		tab = tab[k]
		if type(tab) ~= "table" then
			return tab
		end
	end
	
	return tab
end

function lib.MakeLookupTable(tab)
	local newTab = {}
	for _, key in pairs(tab) do
		newTab[key] = true
	end
	return newTab
end

--------------------------------------------------
---- SCHEDULER (Stolen from Fiend Folio)
--------------------------------------------------

DelayedFuncs = {}

local function RunUpdates(tab)
	for i = #tab, 1, -1 do
		local f = tab[i]
		f.Delay = f.Delay - 1
		if f.Delay <= 0 then
			f.Func()
			table.remove(tab, i)
		end
	end
end

function lib.ScheduleForUpdate(foo, delay, callback, noCancelOnNewRoom)
	callback = callback or ModCallbacks.MC_POST_UPDATE
	if not DelayedFuncs[callback] then
		DelayedFuncs[callback] = {}
		mod:AddCallback(callback, function()
			RunUpdates(DelayedFuncs[callback])
		end)
	end

	table.insert(DelayedFuncs[callback], { Func = foo, Delay = delay or 0, NoCancel = noCancelOnNewRoom })
end

mod:AddPriorityCallback(ModCallbacks.MC_POST_NEW_ROOM, CallbackPriority.IMPORTANT, function()
	for callback, tab in pairs(DelayedFuncs) do
		for i = #tab, 1, -1 do
			local f = tab[i]
			if not f.NoCancel then
				table.remove(tab, i)
			end
		end
	end
end)

--------------------------------------------------
---- POINTERS
--------------------------------------------------

function lib.Pointee(ptr)
	if ptr and ptr.Ref and ptr.Ref:Exists() then
		return ptr.Ref
	end
end

--------------------------------------------------
---- MISC
--------------------------------------------------

lib.VanillaSlotVariants = lib.MakeLookupTable({
	1, -- Slot Machine
	2, -- Blood Donation Machine
	3, -- Fortune Telling Machine
	4, -- Beggar
	5, -- Devil Beggar
	6, -- Shell Game
	7, -- Key Beggar
	8, -- Donation Machine
	9, -- Bomb Beggar
	10, -- Restock Machine
	11, -- Greed Machine
	12, -- Mom's Dresser
	13, -- Battery Beggar
	15, -- Hell Game
	16, -- Crane Game
	17, -- Confessional
	18, -- Rotten Beggar
})

lib.VanillaChestVariants = lib.MakeLookupTable({
	PickupVariant.PICKUP_CHEST,
	PickupVariant.PICKUP_BOMBCHEST,
	PickupVariant.PICKUP_SPIKEDCHEST,
	PickupVariant.PICKUP_MIMICCHEST,
	PickupVariant.PICKUP_ETERNALCHEST,
	PickupVariant.PICKUP_OLDCHEST,
	PickupVariant.PICKUP_WOODENCHEST,
	PickupVariant.PICKUP_MEGACHEST,
	PickupVariant.PICKUP_HAUNTEDCHEST,
	PickupVariant.PICKUP_LOCKEDCHEST,
	PickupVariant.PICKUP_REDCHEST,
	PickupVariant.PICKUP_MOMSCHEST,
})

function lib.IsVanillaChest(entity)
	return entity.Type == EntityType.ENTITY_PICKUP and lib.VanillaChestVariants[entity.Variant] ~= nil
end

function lib.Spawn(tab, pos, vel, spawner)
	return Isaac.Spawn(tab.ID, tab.Var, tab.Sub, pos or game:GetRoom():GetCenterPos(), vel or lib.ZeroVector, spawner)
end

function lib.GetCurrentRoomItemPool()
	local room = game:GetRoom()
	return math.max(game:GetItemPool():GetPoolForRoom(room:GetType(), room:GetAwardSeed()), ItemPoolType.POOL_TREASURE)
end

local catchSpawns = false
local caughtSpawns = {}

function lib.TestSpawn(eType, eVariant, eSubType, noUpdate, returnEntity, morph)
	catchSpawns = true
	local thing = Isaac.Spawn(eType, eVariant, eSubType, Vector(-100, -100), lib.ZeroVector, nil)
	thing.EntityCollisionClass = 0
	thing:AddEntityFlags(EntityFlag.FLAG_APPEAR | EntityFlag.FLAG_DONT_COUNT_BOSS_HP | EntityFlag.FLAG_HIDE_HP_BAR
			| EntityFlag.FLAG_FRIENDLY | EntityFlag.FLAG_AMBUSH | EntityFlag.FLAG_NO_REWARD | EntityFlag.FLAG_NO_QUERY)
	if not noUpdate then
		thing:Update()
	end
	local newType = thing.Type
	local newVariant = thing.Variant
	local newSubType = thing.SubType
	local changed = eType ~= newType or eVariant ~= newVariant or eSubType ~= newSubType
	if eType == EntityType.ENTITY_PICKUP and morph and thing.Type == eType and (thing.Variant ~= eVariant or thing.SubType ~= eSubType) then
		thing:ToPickup():Morph(eType, eVariant, eSubType, true, true, true)
		if not noUpdate then
			thing:Update()
		end
	end
	for _, ent in pairs(caughtSpawns) do
		if ent and ent:Exists() and GetPtrHash(ent) ~= GetPtrHash(thing) then
			ent:Remove()
		end
	end
	catchSpawns = false
	caughtSpawns = {}
	if returnEntity then
		return thing
	end
	thing:Remove()
	return newType, newVariant, newSubType, changed
end

function mod:CatchSpawn(ent)
	if catchSpawns then
		table.insert(caughtSpawns, ent)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.CatchSpawn)
mod:AddCallback(ModCallbacks.MC_FAMILIAR_INIT, mod.CatchSpawn)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_INIT, mod.CatchSpawn)
mod:AddCallback(ModCallbacks.MC_POST_PROJECTILE_INIT, mod.CatchSpawn)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT, mod.CatchSpawn)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_INIT, mod.CatchSpawn)
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.CatchSpawn)

function lib.RefreshGrids()
	local room = game:GetRoom()
	for i=0, room:GetGridSize() do
		local gridEntity = room:GetGridEntity(i)
		if gridEntity and (gridEntity:ToPit() or gridEntity:ToRock()) and gridEntity:GetType() ~= GridEntity.GRID_WALL then
			gridEntity:Init(gridEntity.Desc.SpawnSeed)
			gridEntity:PostInit()
		end
	end
end

-- Width of a grid square.
local GRID_WIDTH = 40
-- Diagonal width of a grid square.
local GRID_DIAGONAL = GRID_WIDTH * math.sqrt(2)

function lib.FindGridEntitiesInRadius(pos, radius)
	local foundGrids = {}
	
	local room = game:GetRoom()
	local roomWidth = room:GetGridWidth()
	
	local startGrid = room:GetClampedGridIndex(pos + Vector(-radius, -radius))
	local endGrid = room:GetClampedGridIndex(pos + Vector(radius, radius))
	
	local w = (endGrid % roomWidth) - (startGrid % roomWidth)
	local h = math.floor(endGrid / roomWidth) - math.floor(startGrid / roomWidth)
	
	for x=0, w do
		for y=0, h do
			local gridIndex = startGrid + x + roomWidth * y
			local gridEntity = room:GetGridEntity(gridIndex)
			if gridEntity then
				local gridPos = room:GetGridPosition(gridIndex)
				local dist = gridPos:Distance(pos)
				if dist <= radius or (dist <= radius + GRID_DIAGONAL*0.5 and gridIndex == room:GetGridIndex(pos + (gridPos - pos):Resized(radius))) then
					table.insert(foundGrids, gridEntity)
				end
			end
		end
	end
	
	return foundGrids
end

function lib.PreLoadGfx(input)
	if type(input) ~= "table" then
		input = {input}
	end
	for _, gfx in pairs(input) do
		local sprite = Sprite()
		sprite:Load("gfx/" .. gfx .. ".anm2", true)
	end
end

function lib.HideItemSprite(pedestal)
	pedestal:GetSprite():ReplaceSpritesheet(1, "gfx/items/collectibles/questionmark.png")
	pedestal:GetSprite():LoadGraphics()
end

function lib.ReloadItemSprite(pedestal)
	local item = Isaac.GetItemConfig():GetCollectible(pedestal.SubType)
	if not item then return end
	local gfx = item.GfxFileName
	if not gfx or gfx == "" then return end
	pedestal:GetSprite():ReplaceSpritesheet(1, gfx)
	pedestal:GetSprite():LoadGraphics()
end

function lib.FindWall(pos, dir)
	if not dir or dir:Length() == 0 then
		return pos
	end
	
	local room = game:GetRoom()
	
	local x = 128
	
	while x > 1 do
		local step = dir:Resized(x)
		while room:IsPositionInRoom(pos, 0) do
			pos = pos + step
		end
		pos = pos - step
		x = x / 2
	end
	
	return pos
end

-- Gets the current size of the screen (I think Kilburn made this).
function lib.GetScreenSize()
	local room = game:GetRoom()
	local pos = room:WorldToScreenPosition(Vector(0,0)) - room:GetRenderScrollOffset() - game.ScreenShakeOffset
	
	local rx = pos.X + 60 * 26 / 40
	local ry = pos.Y + 140 * (26 / 40)
	
	return Vector(rx*2 + 13*26, ry*2 + 7*26)
end

function lib.WorldToScreen(pos)
	local screenPos = Isaac.WorldToScreen(pos)
	if lib.IsInMirror() then
		screenPos = Vector(Isaac.GetScreenWidth() - screenPos.X, screenPos.Y)
	end
	return screenPos
end

function lib.GetDistFromRoomEdge(pos)
	local clampedPos = game:GetRoom():GetClampedPosition(pos, 0)
	return pos:Distance(clampedPos)
end

function lib.Round(n, numDecimalPlaces)
	local mult = 10^(numDecimalPlaces or 0)
	local x = (n*mult) % 1 >= 0.5 and math.ceil(n*mult) or math.floor(n*mult)
	return x / mult
end

-- Extra-safe player iteration. Might not be necessary but shouldn't have much of an impact on performance.
function lib.GetPlayers()
	local players = {}
	for i=0, game:GetNumPlayers()-1 do
		local player = game:GetPlayer(i)
		if player and player:Exists() then
			players[i] = player
		end
	end
	return players
end

function lib.IsInMainDimension()
	local level = game:GetLevel()
	local roomDesc = level:GetCurrentRoomDesc()
	return level:GetRoomByIdx(roomDesc.GridIndex, 0).ListIndex == roomDesc.ListIndex
end

function lib.IsInSecondaryDimension()
	local level = game:GetLevel()
	local roomDesc = level:GetCurrentRoomDesc()
	return level:GetRoomByIdx(roomDesc.GridIndex, 1).ListIndex == roomDesc.ListIndex
end

function lib.GetDimension()
	local level = game:GetLevel()
	local roomIdx = level:GetCurrentRoomIndex()
	for i=0, 2 do
		if GetPtrHash(level:GetRoomByIdx(roomIdx, i)) == GetPtrHash(level:GetRoomByIdx(roomIdx, -1)) then
			return i
		end
	end
end

function lib.IsInMirror()
	local level = game:GetLevel()
	return lib.IsInSecondaryDimension() and level:GetStage() == LevelStage.STAGE1_2 and game:GetRoom():GetType() ~= RoomType.ROOM_SECRET_EXIT
			and (level:GetStageType() == StageType.STAGETYPE_REPENTANCE or level:GetStageType() == StageType.STAGETYPE_REPENTANCE_B)
end

function lib.IsInMineshaft()
	local level = game:GetLevel()
	return lib.IsInSecondaryDimension() and level:GetStage() == LevelStage.STAGE2_2 and game:GetRoom():GetType() ~= RoomType.ROOM_SECRET_EXIT
			and (level:GetStageType() == StageType.STAGETYPE_REPENTANCE or level:GetStageType() == StageType.STAGETYPE_REPENTANCE_B)
end

local kGibsHelper = Isaac.GetEntityVariantByName("(Samael) Bone Gibs Helper")

local function MakeGibs(pos, subType)
	if kGibsHelper < 1 then return end
	
	local helper = Isaac.Spawn(EntityType.ENTITY_EFFECT, kGibsHelper, subType, pos, lib.ZeroVector, nil)
	helper:AddEntityFlags(EntityFlag.FLAG_NO_TARGET | EntityFlag.FLAG_FRIENDLY | EntityFlag.FLAG_CHARM | EntityFlag.FLAG_NO_REWARD | EntityFlag.FLAG_NO_QUERY)
	helper.Visible = false
	local c = Color(1,1,1,0.75)
	local x = 1.5
	c:SetColorize(x,x,x, 1)
	helper.SplatColor = c
	helper:Kill()
	helper:Remove()
end

function lib.BoneGibsBurst(pos)
	MakeGibs(pos, 0)
end

function lib.DustGibsBurst(pos)
	MakeGibs(pos, 1)
end

function lib.PlayMusic(music)
	mod.MusicManager:Play(music, Options.MusicVolume)
	mod.MusicManager:UpdateVolume()
end

local SoundQueue = {}

function lib.PlayDelayedSound(sound, frameDelay, volume, pitch)
	table.insert(SoundQueue, {
		Sound = sound,
		FrameDelay = frameDelay,
		Volume = volume or 1.0,
		Pitch = pitch or 1.0,
	})
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	local toRemove = {}
	
	for key, data in pairs(SoundQueue) do
		data.FrameDelay = data.FrameDelay - 1
		if data.FrameDelay <= 0 then
			sfxManager:Play(data.Sound, data.Volume, 0, false, data.Pitch)
			table.insert(toRemove, key)
		end
	end
	
	for _, key in pairs(toRemove) do
		SoundQueue[key] = nil
	end
end)

local SoundsToSuppress = {}

function lib.SuppressSound(sound, frames)
	if not sound or sound <= 0 then return end
	sfxManager:Stop(sound)
	SoundsToSuppress[sound] = frames or 1
end

function mod:SoundSuppressor()
	for sound, frames in pairs(SoundsToSuppress) do
		sfxManager:Stop(sound)
		if frames <= 1 then
			SoundsToSuppress[sound] = nil
		else
			SoundsToSuppress[sound] = frames - 1
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.SoundSuppressor)

function lib.RefundInvalidCardUse(player, card, useFlags, sound, soundSuppressFrames)
	local owned = useFlags & UseFlag.USE_OWNED ~= 0
	local mimicked = useFlags & UseFlag.USE_MIMIC ~= 0
	local duplicated = useFlags & UseFlag.USE_CARBATTERY ~= 0
	if owned and not mimicked and not duplicated then
		player:AddCard(card)
		if sound then
			lib.SuppressSound(sound, soundSuppressFrames)
		end
		sfxManager:Play(SoundEffect.SOUND_THUMBS_DOWN, 0.75, 0, false, 2)
	end
end

-- Thanks kittenchilly
function lib.GetBombRadiusFromDamage(damage)
	if 175.0 <= damage then
		return 105.0
	else
		if damage <= 140.0 then
			return 75.0
		else
			return 90.0
		end
	end
end

-- Thanks kittenchilly x2
function mod:AddSmeltedTrinket(player, trinket, firstTimePickingUp)
	--get the trinkets they're currently holding
	local trinket0 = player:GetTrinket(0)
	local trinket1 = player:GetTrinket(1)
	
	--remove them
	if trinket1 ~= 0 then
		player:TryRemoveTrinket(trinket1)
	end
	if trinket0 ~= 0 then
		player:TryRemoveTrinket(trinket0)
	end
	
	player:AddTrinket(trinket, firstTimePickingUp == nil and true or firstTimePickingUp) --add the trinket
	player:UseActiveItem(CollectibleType.COLLECTIBLE_SMELTER, UseFlag.USE_NOANIM) --smelt it
	
	--give their trinkets back
	if trinket1 ~= 0 then
		player:AddTrinket(trinket1, false)
	end
	if trinket0 ~= 0 then
		player:AddTrinket(trinket0, false)
	end
end

function lib.IsGoldenTrinket(id)
	return id & TrinketType.TRINKET_GOLDEN_FLAG ~= 0
end

function lib.GetBaseTrinketId(id)
	return id & TrinketType.TRINKET_ID_MASK
end

function lib.TeleportEnemy(player, target)
	local teleTear = Isaac.Spawn(EntityType.ENTITY_TEAR, 0, 0, target.Position, lib.ZeroVector, player):ToTear()
	teleTear.CollisionDamage = 0
	teleTear:AddTearFlags(TearFlags.TEAR_TELEPORT)
	teleTear:AddTearFlags(TearFlags.TEAR_PIERCING)
	teleTear.Color = lib.InvisibleColor
	teleTear:GetData().samaelDummyTearTarget = target
	teleTear:GetData().samaelDummyTearFrame = game:GetFrameCount()
end

function mod:DummyTear(tear)
	local data = tear:GetData()
	
	if data.samaelDummyTearFrame and data.samaelDummyTearFrame + 2 < game:GetFrameCount() then
		tear:Remove()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, mod.DummyTear)

function mod:DummyTearCollision(tear, collider)
	local data = tear:GetData()
	
	if data.samaelDummyTearTarget and data.samaelDummyTearTarget.InitSeed ~= collider.InitSeed then
		return true
	end
end
mod:AddCallback(ModCallbacks.MC_PRE_TEAR_COLLISION, mod.DummyTearCollision)

local roomSpawnCache = {}

function lib.GetRoomSpawns(roomData)
	roomData = roomData or game:GetLevel():GetCurrentRoomDesc().Data
	
	if not roomData then return end
	
	local hash = GetPtrHash(roomData)
	
	if roomSpawnCache[hash] then return roomSpawnCache[hash] end
	
	local roomWidth = 15
	if roomData.Shape > RoomShape.ROOMSHAPE_IIV then  -- Every shape after this is wide.
		roomWidth = 28
	end
	
	local roomSpawns = {}
	
	for i = 0, roomData.Spawns.Size - 1 do
		local spawn = roomData.Spawns:Get(i)
		
		if spawn then
			local gridIdx = roomWidth + 1 + (spawn.X + roomWidth * spawn.Y)
			
			roomSpawns[gridIdx] = {}
			
			local sumWeight = spawn.SumWeights
			local weight = 0
			
			for j = 1, spawn.EntryCount do
				local entry = spawn:PickEntry(weight)
				table.insert(roomSpawns[gridIdx], {
					Type = entry.Type,
					Variant = entry.Variant,
					SubType = entry.Subtype,
					Weight = entry.Weight,
				})
				weight = weight + entry.Weight / sumWeight
			end
		end
	end
	
	roomSpawnCache[hash] = roomSpawns
	return roomSpawns
end

mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, function()
	roomSpawnCache = {}
end)

--returns the path to the current mod (by piber)
function lib.GetCurrentModPath()
	--use some very hacky trickery to get the path to this mod
	local _, err = pcall(require, "")
	local _, basePathStart = string.find(err, "no file '", 1)
	local _, modPathStart = string.find(err, "no file '", basePathStart)
	local modPathEnd, _ = string.find(err, ".lua'", modPathStart)
	local modPath = string.sub(err, modPathStart+1, modPathEnd-1)
	modPath = string.gsub(modPath, "\\", "/")
	
	return modPath
end

function lib.InStageApiFloor()
	return StageAPI and StageAPI.Loaded and StageAPI.InOverriddenStage()
end

function lib.PasteRoomData(idx, data)
	local level = game:GetLevel()
	local mutableRoomDesc = level:GetRoomByIdx(idx)
	mutableRoomDesc.Data = data
	mutableRoomDesc.Flags = flags or 0
	
	if lib.InStageApiFloor() then
		local levelRoom = StageAPI.LevelRoom{
			RoomType = roomType or RoomType.ROOM_DEFAULT,
			RoomDescriptor = mutableRoomDesc,
			FromData = mutableRoomDesc.ListIndex,
		}
		StageAPI.SetLevelRoom(levelRoom, mutableRoomDesc.ListIndex)
	end
end

function lib.CopyGotoRoomData(idx, flags, roomType)
	local level = game:GetLevel()
	local mutableRoomDesc = level:GetRoomByIdx(idx)
	mutableRoomDesc.Data = level:GetRoomByIdx(-3).Data
	mutableRoomDesc.Flags = flags or 0
	
	if lib.InStageApiFloor() then
		local levelRoom = StageAPI.LevelRoom{
			RoomType = roomType or RoomType.ROOM_DEFAULT,
			RoomDescriptor = mutableRoomDesc,
			FromData = mutableRoomDesc.ListIndex,
		}
		StageAPI.SetLevelRoom(levelRoom, mutableRoomDesc.ListIndex)
	end
end

local SpritesheetlessChamps = {
	[ChampionColor.FLICKER] = true,
	[ChampionColor.CAMO] = true,
	[ChampionColor.TINY] = true,
	[ChampionColor.GIANT] = true,
	[ChampionColor.SIZE_PULSE] = true,
	[ChampionColor.KING] = true,
}

--- Replace spritesheets while working with champion sheets. Credit to Guwahavel.
function lib.ReplaceEnemySpritesheet(npc, filepath, layer, loadGraphics) --Leave the ".png" OUT!!!
	layer = layer or 0
	loadGraphics = loadGraphics or true
	npc = npc:ToNPC()
	local sprite = npc:GetSprite()
	if npc:IsChampion() and not SpritesheetlessChamps[npc:GetChampionColorIdx()] then
		filepath = filepath .. "_champion"
	end
	filepath = filepath .. ".png"
	sprite:ReplaceSpritesheet(layer, filepath)
	if loadGraphics then
		sprite:LoadGraphics()
	end
end

local triggeringOnDamageEffects = false

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	triggeringOnDamageEffects = false
end)
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, CallbackPriority.IMPORTANT, function()
	if triggeringOnDamageEffects then
		return {ShowAnim=false}
	end
end)
mod:AddPriorityCallback(ModCallbacks.MC_PRE_USE_ITEM, CallbackPriority.IMPORTANT, function()
	if triggeringOnDamageEffects then
		return false
	end
end)

function lib.TriggerOnDamageEffects(player)
	triggeringOnDamageEffects = true
	player:UseActiveItem(CollectibleType.COLLECTIBLE_DULL_RAZOR, UseFlag.USE_NOANIM)
	triggeringOnDamageEffects = false
end

local BIRTHCAKE_ID

function lib.HasBirthcake(player)
	if not Birthcake then return false end
	
	if not BIRTHCAKE_ID then
		BIRTHCAKE_ID = TrinketType.TRINKET_BIRTHCAKE or Isaac.GetTrinketIdByName("Birthcake") or -1
	end
	
	if BIRTHCAKE_ID > 0 and player:HasTrinket(BIRTHCAKE_ID) then
		return player:GetTrinketMultiplier(BIRTHCAKE_ID)
	end
end

-- Returns the first player that can be found by looking up the SpawnerEntities/Parents of the starting entity.
function lib.FindPlayerSpawnerOrParent(entity, seen)
	seen = seen or {}
	if entity and entity:Exists() and not seen[GetPtrHash(entity)] then
		seen[GetPtrHash(entity)] = true
		if entity:ToPlayer() then
			return entity:ToPlayer()
		elseif entity:ToFamiliar() and entity:ToFamiliar().Player and entity:ToFamiliar().Player:Exists() then
			return entity:ToFamiliar().Player
		end
		return lib.FindPlayerSpawnerOrParent(entity.SpawnerEntity, seen) or lib.FindPlayerSpawnerOrParent(entity.Player, seen)
	end
end

------------------------------------------------------------------------------------------
-- Unique keys / names for each floor.
------------------------------------------------------------------------------------------

local STAGE_TO_CHAPTER = {
	[LevelStage.STAGE1_1] = 1,
	[LevelStage.STAGE1_2] = 1,
	[LevelStage.STAGE2_1] = 2,
	[LevelStage.STAGE2_2] = 2,
	[LevelStage.STAGE3_1] = 3,
	[LevelStage.STAGE3_2] = 3,
	[LevelStage.STAGE4_1] = 4,
	[LevelStage.STAGE4_2] = 4,
	[LevelStage.STAGE4_3] = 4.5,
	[LevelStage.STAGE5] = 5,
	[LevelStage.STAGE6] = 6,
	[LevelStage.STAGE7] = 7,
	[LevelStage.STAGE8] = 8,
}

function lib.GetChapter()
	return STAGE_TO_CHAPTER[game:GetLevel():GetAbsoluteStage()] or 1
end

lib.FloorName = {
	['BASEMENT'] = "Basement",
	['CELLAR'] = "Cellar",
	['BURNING_BASEMENT'] = "Burning Basement",
	['DOWNPOUR'] = "Downpour",
	['DROSS'] = "Dross",
	['CAVES'] = "Caves",
	['CATACOMBS'] = "Catacombs",
	['FLOODED_CAVES'] = "Flooded Caves",
	['MINES'] = "Mines",
	['ASHPIT'] = "Ashpit",
	['DEPTHS'] = "Depths",
	['NECROPOLIS'] = "Necropolis",
	['DANK_DEPTHS'] = "Dank Depths",
	['MAUSOLEUM'] = "Mausoleum",
	['GEHENNA'] = "Gehenna",
	['WOMB'] = "Womb",
	['UTERO'] = "Utero",
	['SCARRED_WOMB'] = "Scarred Womb",
	['CORPSE'] = "Corpse",
	['BLUE_WOMB'] = "???",
	['SHEOL'] = "Sheol",
	['CATHEDRAL'] = "Cathedral",
	['DARK_ROOM'] = "Dark Room",
	['CHEST'] = "Chest",
	['SHOP'] = "The Shop",
	['VOID'] = "The Void",
	['ASCENT'] = "Ascent",
	['HOME'] = "Home",
}

local FLOOR_NAME_MAP = {
	[1] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.BASEMENT,
		[StageType.STAGETYPE_WOTL] = lib.FloorName.CELLAR,
		[StageType.STAGETYPE_AFTERBIRTH] = lib.FloorName.BURNING_BASEMENT,
		[StageType.STAGETYPE_REPENTANCE] = lib.FloorName.DOWNPOUR,
		[StageType.STAGETYPE_REPENTANCE_B] = lib.FloorName.DROSS,
	},
	[2] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.CAVES,
		[StageType.STAGETYPE_WOTL] = lib.FloorName.CATACOMBS,
		[StageType.STAGETYPE_AFTERBIRTH] = lib.FloorName.DROWNED_CAVES,
		[StageType.STAGETYPE_REPENTANCE] = lib.FloorName.MINES,
		[StageType.STAGETYPE_REPENTANCE_B] = lib.FloorName.ASHPIT,
	},
	[3] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.DEPTHS,
		[StageType.STAGETYPE_WOTL] = lib.FloorName.NECROPOLIS,
		[StageType.STAGETYPE_AFTERBIRTH] = lib.FloorName.DANK_DEPTHS,
		[StageType.STAGETYPE_REPENTANCE] = lib.FloorName.MAUSOLEUM,
		[StageType.STAGETYPE_REPENTANCE_B] = lib.FloorName.GEHENNA,
	},
	[4] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.WOMB,
		[StageType.STAGETYPE_WOTL] = lib.FloorName.UTERO,
		[StageType.STAGETYPE_AFTERBIRTH] = lib.FloorName.SCARRED_WOMB,
		[StageType.STAGETYPE_REPENTANCE] = lib.FloorName.CORPSE,
		[StageType.STAGETYPE_REPENTANCE_B] = lib.FloorName.CORPSE,
	},
	[4.5] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.BLUE_WOMB,
	},
	[5] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.SHEOL,
		[StageType.STAGETYPE_WOTL] = lib.FloorName.CATHEDRAL,
	},
	[6] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.DARK_ROOM,
		[StageType.STAGETYPE_WOTL] = lib.FloorName.CHEST,
	},
	[7] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.VOID,
	},
	[8] = {
		[StageType.STAGETYPE_ORIGINAL] = lib.FloorName.HOME,
	},
}

function lib.GetCurrentFloorName(countAscent, allowStageApiFloors)
	if countAscent and game:GetLevel():IsAscent() then
		return lib.FloorName.ASCENT
	end
	
	if allowStageApiFloors and lib.InStageApiFloor() and StageAPI.CurrentStage and StageAPI.CurrentStage.Name and StageAPI.CurrentStage.Name ~= "" then
		return StageAPI.CurrentStage.Name
	end
	
	local result
	
	local level = game:GetLevel()
	local chapter = lib.GetChapter()
	local stageType = level:GetStageType()
	
	if chapter == 6 and game:IsGreedMode() then
		return lib.FloorName.SHOP
	end
	
	local subTable = FLOOR_NAME_MAP[chapter]
	if subTable then
		result = subTable[stageType] or subTable[StageType.STAGETYPE_ORIGINAL]
	end
	
	return result or lib.FloorName.BASEMENT
end

---------------------------------------------------------------------------------------
---- BASEMENT RENEVATOR HELPER (FOR FRAGMENT TESTING)
---------------------------------------------------------------------------------------

local BR_TESTING_MAIN_PATH = {
	[1] = {  -- Basement
		-- Base Stats
	},
	[2] = {  -- Caves
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 1,
	},
	[3] = {  -- Depths
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 1,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 1,
	},
	[4] = {  -- Womb
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 1,
	},
	[4.5] = {  -- Blue Womb
		[CollectibleType.COLLECTIBLE_NEGATIVE] = 3,
		[CollectibleType.COLLECTIBLE_MEAT] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
	},
	[5] = {  -- Cathedral/Sheol
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
	},
	[6] = {  -- DarkRoom/Chest
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
		[CollectibleType.COLLECTIBLE_MAGIC_MUSHROOM] = 1,
	},
	[7] = {  -- Void
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
		[CollectibleType.COLLECTIBLE_MAGIC_MUSHROOM] = 1,
	},
	[8] = {  -- Home
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
		[CollectibleType.COLLECTIBLE_MAGIC_MUSHROOM] = 1,
	},
}

local BR_TESTING_ALT_PATH = {
	[1] = {  -- Downpour
		-- Base Stats
	},
	[2] = {  -- Mines
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 1,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 1,
	},
	[3] = {  -- Mausoleum
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 1,
	},
	[4] = {  -- Corpse
		[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
		[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
	},
}

local BR_TESTING_ASCENT = {
	[CollectibleType.COLLECTIBLE_PENTAGRAM] = 2,
	[CollectibleType.COLLECTIBLE_SAD_ONION] = 2,
}

function lib.FragmentBrTestItems()
	local player = Isaac.GetPlayer()
	
	local items
	
	local chapter = lib.GetChapter()
	
	if game:GetLevel():IsAscent() then
		item = BR_TESTING_ASCENT
	elseif stageType == StageType.STAGETYPE_REPENTANCE or stageType == StageType.STAGETYPE_REPENTANCE_B then
		items = BR_TESTING_ALT_PATH[chapter]
	end
	
	items = items or BR_TESTING_MAIN_PATH[chapter] or {}
	
	for itemID, num in pairs(items) do
		for i=1, num do
			player:AddCollectible(itemID)
		end
	end
end

--------------------------------------------------
---- LASER PATH HELPERS
---- Anyone snooping feel free to use these!
--------------------------------------------------

local CachedLaserPaths = {}
local CachedLaserLengths = {}

-- Returns a table representing the path of a laser. Also returns the length of the path.
-- Table entries are tables with two values:
--  - `Position` (The vector position of this point along the laser's path.)
--  - `Distance` (The current distance along the path of the laser, from the start.)
-- Caches data so that multiple calls in the same frame won't redo the work.
-- Note that the laser's PositionOffset may have to be added for the positions to look accurate.
function lib.GetLaserPath(laser)
	local currentFrame = game:GetFrameCount()
	local data = laser:GetData()
	local hash = GetPtrHash(laser)
	
	if CachedLaserPaths[hash] and CachedLaserLengths[hash] then
		return CachedLaserPaths[hash], CachedLaserLengths[hash]
	end
	
	local path = {}
	local pathLength = 0
	
	-- Insert the root/anchor position of the laser first, except for circle lasers.
	if not laser:IsCircleLaser() then
		table.insert(path, {
			Position = laser.Position,
			Distance = 0,
		})
	end
	
	local samplePoints = laser:GetSamples()
	
	-- Iterate over each sample point of the laser.
	for i=0, #samplePoints-1 do
		local pos = Vector(samplePoints:Get(i).X, samplePoints:Get(i).Y)
		
		local previous = path[#path]
		
		local isDuplicate = previous and pos.X == previous.Position.X and pos.Y == previous.Position.Y
		
		if not isDuplicate then
			if previous then
				pathLength = pathLength + pos:Distance(previous.Position)
			end
			table.insert(path, {
				Position = pos,
				Distance = pathLength,
			})
		end
	end
	
	CachedLaserPaths[hash] = path
	CachedLaserLengths[hash] = pathLength
	
	return path, pathLength
end

mod:AddCallback(ModCallbacks.MC_POST_LASER_UPDATE, function(_, laser)
	-- Have to wipe the cache for a laser every time it updates, because the shape may have changed.
	local hash = GetPtrHash(laser)
	CachedLaserPaths[hash] = nil
	CachedLaserLengths[hash] = nil
end)

-- Returns the total length of a laser's path, from tip to end.
function lib.GetLaserLength(laser)
	local path, pathLength = lib.GetLaserPath(laser)
	return pathLength
end

-- Binary search implementation for finding a point at a certain distance along a path.
-- Don't call this function - use GetPosAtDistanceAlongLaserPath below.
function lib.BinarySearch(tab, l, r, targetDist)
	if l == r then
		return tab[l].Position
	end
	
	if l > r then
		local errStr = "GetPosAtDistanceAlongLaserPath Error: Binary search failed."
		print(errStr)
		Isaac.DebugString(errStr)
		return
	end
	
	local mid = math.floor((l + r) / 2)
	
	if not tab[mid] then
		local errStr = "GetPosAtDistanceAlongLaserPath Error: Laser path table is likely malformed."
		print(errStr)
		Isaac.DebugString(errStr)
		return
	end
	
	if tab[mid].Distance <= targetDist then
		if not tab[mid+1] then
			local errStr = "GetPosAtDistanceAlongLaserPath Error (maybe): Reached the end of the table unexpectedly. Table possibly malformed -  normally this doesn't trigger."
			print(errStr)
			Isaac.DebugString(errStr)
			return tab[mid].Position
		elseif tab[mid+1].Distance >= targetDist then
			local n = (targetDist - tab[mid].Distance) / (tab[mid+1].Distance - tab[mid].Distance)
			return lib.Lerp(tab[mid].Position, tab[mid+1].Position, n)
		end
		-- Go right
		return lib.BinarySearch(tab, mid+1, r, targetDist)
	end
	
	-- Go left
	return lib.BinarySearch(tab, l, mid, targetDist)
end

-- Returns a Vector position at a specified distance along a laser's path.
-- Note that the laser's PositionOffset may have to be added for the position to look accurate.
function lib.GetPosAtDistanceAlongLaserPath(laser, targetDist)
	local path
	if type(laser) == "table" then
		path = laser
	else
		path = lib.GetLaserPath(laser)
	end
	return lib.BinarySearch(path, 1, #path, targetDist)
end

--------------------------------------------------------------
---- Segmented Enemy Identification, STOLEN from Fiend Folio
---- Thanks, Taiga!
--------------------------------------------------------------

-- List of basegame segmented enemies
local BasegameSegmentedEnemies = {
	[35 .. " " .. 0] = true, -- Mr. Maw (body)
	[35 .. " " .. 1] = true, -- Mr. Maw (head)
	[35 .. " " .. 2] = true, -- Mr. Red Maw (body)
	[35 .. " " .. 3] = true, -- Mr. Red Maw (head)
	[89] = true, -- Buttlicker
	[216 .. " " .. 0] = true, -- Swinger (body)
	[216 .. " " .. 1] = true, -- Swinger (head)
	[239] = true, -- Grub
	[244 .. " " .. 2] = true, -- Tainted Round Worm

	[19 .. " " .. 0] = true, -- Larry Jr.
	[19 .. " " .. 1] = true, -- The Hollow
	[19 .. " " .. 2] = true, -- Tuff Twins
	[19 .. " " .. 3] = true, -- The Shell
	[28 .. " " .. 0] = true, -- Chub
	[28 .. " " .. 1] = true, -- C.H.A.D.
	[28 .. " " .. 2] = true, -- The Carrion Queen
	[62 .. " " .. 0] = true, -- Pin
	[62 .. " " .. 1] = true, -- Scolex
	[62 .. " " .. 2] = true, -- The Frail
	[62 .. " " .. 3] = true, -- Wormwood
	[79 .. " " .. 0] = true, -- Gemini
	[79 .. " " .. 1] = true, -- Steven
	[79 .. " " .. 10] = true, -- Gemini (baby)
	[79 .. " " .. 11] = true, -- Steven (baby)
	[92 .. " " .. 0] = true, -- Heart
	[92 .. " " .. 1] = true, -- 1/2 Heart
	[93 .. " " .. 0] = true, -- Mask
	[93 .. " " .. 1] = true, -- Mask II
	[97] = true, -- Mask of Infamy
	[98] = true, -- Heart of Infamy
	[266] = true, -- Mama Gurdy
	[912 .. " " .. 0 .. " " .. 0] = true, -- Mother (phase one)
	[912 .. " " .. 0 .. " " .. 2] = true, -- Mother (left arm)
	[912 .. " " .. 0 .. " " .. 3] = true, -- Mother (right arm)
	[918 .. " " .. 0] = true, -- Turdlet
}

-- Main segment of basegame segmented enemies
local BasegameMainSegment = {
	[35 .. " " .. 1] = true, -- Mr. Maw (head)
	[35 .. " " .. 3] = true, -- Mr. Red Maw (head)
	[92 .. " " .. 0] = true, -- Heart
	[92 .. " " .. 1] = true, -- 1/2 Heart
	[216 .. " " .. 0] = true, -- Swinger (body)
	[244 .. " " .. 2 .. " " .. 0] = true, -- Tainted Round Worm (head)

	[79 .. " " .. 0] = true, -- Gemini
	[79 .. " " .. 1] = true, -- Steven
	[97] = true, -- Mask of Infamy
	[266 .. " " .. 0] = true, -- Mama Gurdy (body)
	[912 .. " " .. 0 .. " " .. 0] = true, -- Mother (phase one)

--	[89] = true, -- Buttlicker is weird
--	[239] = true, -- Grub is weird

--	[19 .. " " .. 0] = true, -- Larry Jr. is weird
--	[19 .. " " .. 1] = true, -- The Hollow is weird
--	[19 .. " " .. 2] = true, -- Tuff Twins is weird
--	[19 .. " " .. 3] = true, -- The Shell is weird
--	[28 .. " " .. 0] = true, -- Chub is weird
--	[28 .. " " .. 1] = true, -- C.H.A.D. is weird
--	[28 .. " " .. 2] = true, -- The Carrion Queen is weird
--	[62 .. " " .. 0] = true, -- Pin is weird
--	[62 .. " " .. 1] = true, -- Scolex is weird
--	[62 .. " " .. 2] = true, -- The Frail is weird
--	[62 .. " " .. 3] = true, -- Wormwood is weird
--	[918 .. " " .. 0] = true, -- Turdlet is weird
}

-- Treats segments as separate entities.
-- For basegame segmented enemies/bosses like Larry Jr., Gemini, etc.
local BasegameReducedSyncSegments = {
	[35 .. " " .. 0] = true, -- Mr. Maw (body)
	[35 .. " " .. 1] = true, -- Mr. Maw (head)
	[35 .. " " .. 2] = true, -- Mr. Red Maw (body)
	[35 .. " " .. 3] = true, -- Mr. Red Maw (head)
	[89] = true, -- Buttlicker
	[216 .. " " .. 0] = true, -- Swinger (body)
	[216 .. " " .. 1] = true, -- Swinger (head)

	[19 .. " " .. 0] = true, -- Larry Jr.
	[19 .. " " .. 1] = true, -- The Hollow
	[19 .. " " .. 2] = true, -- Tuff Twins
	[19 .. " " .. 3] = true, -- The Shell
	[79 .. " " .. 0] = true, -- Gemini
	[79 .. " " .. 1] = true, -- Steven
	[79 .. " " .. 10] = true, -- Gemini (baby)
	[79 .. " " .. 11] = true, -- Steven (baby)
	[92 .. " " .. 0] = true, -- Heart
	[92 .. " " .. 1] = true, -- 1/2 Heart
	[93 .. " " .. 0] = true, -- Mask
	[93 .. " " .. 1] = true, -- Mask II
	[97] = true, -- Mask of Infamy
	[98] = true, -- Heart of Infamy
}

function lib.IsBasegameSegmented(entity)
	return BasegameSegmentedEnemies[entity.Type] or
			BasegameSegmentedEnemies[entity.Type .. " " .. entity.Variant] or
			BasegameSegmentedEnemies[entity.Type .. " " .. entity.Variant .. " " .. entity.SubType]
end

function lib.IsBasegameMainSegment(entity)
	if entity.Type == 19 then -- Larry Jr., The Hollow, Tuff Twins, The Shell
		return entity.Parent == nil
	elseif entity.Type == 28 then -- Chub, C.H.A.D., The Carrion Queen
		return entity.Parent == nil
	elseif entity.Type == 62 then -- Pin, Scolex, The Frail, Wormwood
		return entity.Parent == nil
	elseif entity.Type == 89 then -- Buttlicker
		return entity.Parent == nil
	elseif entity.Type == 239 then -- Grub
		return entity.Parent == nil
	elseif entity.Type == 918 then -- Turdlet
		return entity.Parent == nil
	else
		return BasegameMainSegment[entity.Type] or
				BasegameMainSegment[entity.Type .. " " .. entity.Variant] or
				BasegameMainSegment[entity.Type .. " " .. entity.Variant .. " " .. entity.SubType]
	end
end

function lib.IsBasegameReducedSyncSegment(entity)
	return BasegameReducedSyncSegments[entity.Type] or
		   BasegameReducedSyncSegments[entity.Type .. " " .. entity.Variant] or
		   BasegameReducedSyncSegments[entity.Type .. " " .. entity.Variant .. " " .. entity.SubType]
end

-- Returns true if the entity is non-segmented, is the main segment, or is a segment treated as a distinct entity.
-- So returns true for pieces of Larry Jr, but not for Pin's segments.
function lib.IsDistinctEntity(entity)
	local isDistinct = not lib.IsBasegameSegmented(entity) or lib.IsBasegameMainSegment(entity) or lib.IsBasegameReducedSyncSegment(entity)
	if isDistinct and FiendFolio then
		isDistinct = not FiendFolio:isSegmented(entity) or FiendFolio:isMainSegment(entity) or FiendFolio:isReducedSyncSegment(entity)
	end
	return isDistinct
end

--------------------------------------------------------------
---- "True" Enemy Death Tracking
--------------------------------------------------------------

mod:AddCallback(ModCallbacks.MC_POST_ENTITY_KILL, function(_, entity)
	if entity:ToNPC() then
		entity:GetData().samaelEnemyDied = true
		entity:GetData().samaelDeathType = entity.Type
	end
end)

mod:AddCallback(ModCallbacks.MC_NPC_UPDATE, function(_, npc)
	local data = npc:GetData()
	if data.samaelEnemyDied and not npc:IsDead() then
		data.samaelEnemyDied = false
	end
end)

-- Check on MC_POST_ENTITY_REMOVE
function lib.AllowOnDeathEffect(npc, ignoreNoRewardFlag, noFriendly)
	local data = npc:GetData()
	return npc:IsEnemy() and npc:IsDead()
			and data.samaelEnemyDied and data.samaelDeathType == npc.Type
			and (ignoreNoRewardFlag or not npc:HasEntityFlags(EntityFlag.FLAG_NO_REWARD))
			and not (npc:HasEntityFlags(EntityFlag.FLAG_FRIENDLY) and noFriendly)
			and lib.IsDistinctEntity(npc)
end

return lib
