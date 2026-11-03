local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local ANGER = mod.ITEMS.ANGER
local ANGER_FOSSIL = mod.ITEMS.ANGER_FOSSIL
local SPIRIT_OF_ANGER = mod.ENTITIES.SPIRIT_OF_ANGER.Var

local kAngerBaseDamage = 0.5
local kAngerFossilBaseDamage = 2.0

local kSecretRoomBonus = 0.2
local kSlotMachineBonus = 0.01
local kCraneGameBonus = 0.05
local kShellGameBonus = 0.02
local kBeggarBonus = 0.1
local kBloatBonus = 1.0
local kBadTreasureRoomBonus = 2.0
local kBadItemBonus = 0.2
local kActiveItemBonus = 0.1
local kCurseBonus = 0.1
local kMimicChestBonus = 0.1
local kBoosterPackBonus = 0.1
local kGachaBonus = 0.1
local kBadPillBonus = 0.1
local kGreedBonus = 0.25
local kDevilChanceLossBonus = 0.2
local kCopperBombDudBonus = 0.1
local kPerfectionBonus = 0.25

local spiritOfAnger

local function AngerTrackingActive(player)
	if player then
		return lib.HasItem(player, ANGER) or player:HasTrinket(ANGER_FOSSIL)
	end
	
	for _, player in pairs(lib.GetPlayers()) do
		if lib.HasItem(player, ANGER) or player:HasTrinket(ANGER_FOSSIL) then
			return true
		end
	end
end

local function AngerFloorData()
	return mod:GetFloorData("Anger")
end

local function AngerRunData()
	return mod:GetRunData("Anger")
end

local function GetAngerBonus()
	return AngerRunData().Bonus or 0
end

local function GetAngerFossilPenalty()
	return AngerRunData().FossilPenalty or 0
end

local doCacheEvaluation

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if doCacheEvaluation then
		for _, player in pairs(lib.GetPlayers()) do
			if AngerTrackingActive(player) then
				player:AddCacheFlags(CacheFlag.CACHE_DAMAGE)
				player:EvaluateItems()
			end
		end
		doCacheEvaluation = false
	end
end)

local function UpdateAngerFossilPenalty(amount)
	if amount <= 0 or GetAngerFossilPenalty() >= kAngerFossilBaseDamage then return end
	
	local playersWithFossil = {}
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(ANGER_FOSSIL) then
			table.insert(playersWithFossil, player)
		end
	end
	
	if #playersWithFossil == 0 then return end
	
	local data = AngerRunData()
	data.FossilPenalty = (data.FossilPenalty or 0) + amount
	doCacheEvaluation = true
	
	for _, player in ipairs(playersWithFossil) do
		local pos = player.Position + RandomVector() * 10
		
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, 49, 3, pos, Vector.Zero, nil):ToEffect()
		eff.PositionOffset = Vector(0, -40)
		local spr = eff:GetSprite()
		spr:Load("gfx/samael_item_notify.anm2", true)
		spr:Play("AngerFossil", true)
		spr:PlayOverlay("Down", true)
		
		sfxManager:Play(SoundEffect.SOUND_THUMBS_DOWN)
		player:SetColor(Color(0, 0.5, 1, 1), 30, 1, true, true)
	end
end

local ANGER_FOSSIL_POOL = {
	CollectibleType.COLLECTIBLE_POOP,
	CollectibleType.COLLECTIBLE_KAMIKAZE,
	CollectibleType.COLLECTIBLE_MOMS_PAD,
	CollectibleType.COLLECTIBLE_LEMON_MISHAP,
	CollectibleType.COLLECTIBLE_BEAN,
	CollectibleType.COLLECTIBLE_DEAD_BIRD,
	CollectibleType.COLLECTIBLE_INFESTATION,
	CollectibleType.COLLECTIBLE_PORTABLE_SLOT,
	CollectibleType.COLLECTIBLE_BLACK_BEAN,
	CollectibleType.COLLECTIBLE_ABEL,
	CollectibleType.COLLECTIBLE_SPIDERBABY,
	CollectibleType.COLLECTIBLE_TINY_PLANET,
	CollectibleType.COLLECTIBLE_BEST_BUD,
	CollectibleType.COLLECTIBLE_ISAACS_HEART,
	CollectibleType.COLLECTIBLE_CURSED_EYE,
	CollectibleType.COLLECTIBLE_THE_WIZ,
	CollectibleType.COLLECTIBLE_CURSE_OF_THE_TOWER,
	CollectibleType.COLLECTIBLE_OBSESSED_FAN,
	CollectibleType.COLLECTIBLE_LINGER_BEAN,
	CollectibleType.COLLECTIBLE_SHADE,
	CollectibleType.COLLECTIBLE_BROWN_NUGGET,
	CollectibleType.COLLECTIBLE_DARK_PRINCES_CROWN,
	CollectibleType.COLLECTIBLE_LOST_FLY,
	CollectibleType.COLLECTIBLE_FRIEND_ZONE,
	CollectibleType.COLLECTIBLE_FOREVER_ALONE,
	CollectibleType.COLLECTIBLE_DISTANT_ADMIRATION,
}

if FiendFolio then
	table.insert(ANGER_FOSSIL_POOL, FiendFolio.ITEM.COLLECTIBLE.X10BADUMP)
	table.insert(ANGER_FOSSIL_POOL, FiendFolio.ITEM.COLLECTIBLE.X10BZZT)
end

function mod.AngerFossilCrushEffect(player, spawner)
	if not FiendFolio then return end
	
	local itemPool = game:GetItemPool()
	
	lib.Shuffle(ANGER_FOSSIL_POOL, player:GetTrinketRNG(ANGER_FOSSIL))
	
	local itemToSpawn = CollectibleType.COLLECTIBLE_LINGER_BEAN
	
	for _, id in ipairs(ANGER_FOSSIL_POOL) do
		if itemPool:RemoveCollectible(id) then
			itemToSpawn = id
			break
		end
	end
	
	local pos = (spawner or player).Position
	local itemPos = game:GetRoom():FindFreePickupSpawnPosition(pos + RandomVector() * 10, 0, true, false)
	Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, itemToSpawn, itemPos, Vector.Zero, nil)
	Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_BOMB, FiendFolio.PICKUP.BOMB.DOUBLE_COPPER, pos, RandomVector()*3, nil)
	Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_BOMB, FiendFolio.PICKUP.BOMB.COPPER, pos, RandomVector()*3, nil)
end

local flareAnim = {
	{ ScaleX=0.2, ScaleY=0.5, Alpha=0.2, Duration=3 },
	{ ScaleX=1.5, ScaleY=0.8, Alpha=0.75, Duration=3 },
	{ ScaleX=0.8, ScaleY=2.0, Alpha=0.85, Duration=6 },
	{ ScaleX=0.8, ScaleY=1.2, Alpha=0.75, Duration=10 },
	{ ScaleX=1.0, ScaleY=1.0, Alpha=0, Duration=1 },
}

local function IncreaseAngerBonus(amount)
	UpdateAngerFossilPenalty(amount)
	
	local playersWithAnger = {}
	
	for _, player in pairs(lib.GetPlayers()) do
		if lib.HasItem(player, ANGER) then
			table.insert(playersWithAnger, player)
		end
	end
	
	if #playersWithAnger == 0 then return end
	
	local data = AngerRunData()
	data.Bonus = (data.Bonus or 0) + amount
	doCacheEvaluation = true
	
	if spiritOfAnger and spiritOfAnger:Exists() then
		spiritOfAnger:GetSprite():SetFrame(10)
	else
		local pos = playersWithAnger[1].Position + RandomVector() * 30
		spiritOfAnger = Isaac.Spawn(EntityType.ENTITY_EFFECT, SPIRIT_OF_ANGER, 0, pos, lib.ZeroVector, nil):ToEffect()
		spiritOfAnger.Parent = playersWithAnger[1]
	end
	
	sfxManager:Play(SoundEffect.SOUND_FLAMETHROWER_END)
	sfxManager:Play(SoundEffect.SOUND_BEEP, 0.9, 0, false, 0.75)
	
	for _, player in ipairs(playersWithAnger) do
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.LASER_IMPACT, 1, player.Position, lib.ZeroVector, player):ToEffect()
		eff.SpriteRotation = 180
		eff.SpriteOffset = Vector(0, -10)
		eff:FollowParent(player)
		eff:GetData().spiritOfAngerFlare = {}
		eff.Color = Color(1, 1, 1, 1, 0, 0.3, 0)
		eff.DepthOffset = -2
		eff:Update()
		
		local eff = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.LASER_IMPACT, 1, player.Position, lib.ZeroVector, player):ToEffect()
		eff.SpriteRotation = 180
		eff.SpriteOffset = Vector(0, -10)
		eff:FollowParent(player)
		eff:GetData().spiritOfAngerFlare = { small = true }
		eff.Color = Color(1, 1, 1, 1, 0, 0.6, 0)
		eff.DepthOffset = -1
		eff:Update()
		
		player:SetColor(Color(1, 0.5, 0, 1), 30, 1, true, true)
		
		player:AddCostume(Isaac.GetItemConfig():GetCollectible(CollectibleType.COLLECTIBLE_KNOCKOUT_DROPS), true)
	end
end
mod.IncreaseAngerBonus = IncreaseAngerBonus

function mod:SpiritOfAngerFlare(eff)
	local data = eff:GetData().spiritOfAngerFlare
	
	if not data then return end
	
	if not data.anim then
		data.anim = 1
		data.frame = 0
	end
	
	if data.frame >= flareAnim[data.anim].Duration then
		data.anim = data.anim + 1
		data.frame = 0
	end
	local currKf = flareAnim[data.anim]
	local nextKf = flareAnim[data.anim + 1]
	
	if nextKf then
		local n = data.frame / currKf.Duration
		
		local scaleX = lib.Lerp(currKf.ScaleX, nextKf.ScaleX, n)
		local scaleY = lib.Lerp(currKf.ScaleY, nextKf.ScaleY, n)
		eff.SpriteScale = Vector(scaleX, scaleY)
		if data.small then
			eff.SpriteScale = eff.SpriteScale * 0.6
		end
		
		local alpha = lib.Lerp(currKf.Alpha, nextKf.Alpha, n)
		local c = eff.Color
		c:SetTint(c.R, c.G, c.B, alpha)
		eff.Color = c
		
		data.frame = data.frame + 1
	else
		eff:Remove()
		return
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SpiritOfAngerFlare, EffectVariant.LASER_IMPACT)

function mod:SpiritOfAngerUpdate(entity)
	local sprite = entity:GetSprite()
	
	if entity.FrameCount == 0 and entity.Parent and entity.Position:Distance(entity.Parent.Position) > 60 then
		entity.Position = entity.Parent.Position + RandomVector() * 30
	end
	
	if sprite:GetAnimation() ~= "Anger" then
		sprite:Play("Anger", true)
	end
	if sprite:IsFinished("Anger") then
		entity:Remove()
		spiritOfAnger = nil
		return
	end
	
	if sprite:IsEventTriggered("Yell") then
		sfxManager:Play(SoundEffect.SOUND_SPEWER, 1, 0, false, 0.9)
	end
	
	local alpha = 1.0
	
	if sprite:GetFrame() >= 30 then
		alpha = lib.Lerp(alpha, 0.0, (sprite:GetFrame()-30) / 14)
	end
	
	entity.Color = Color(1,1,1, alpha)
	
	local targetOffset = -1 * sprite:GetFrame()
	entity.SpriteOffset = Vector(0, lib.Lerp(entity.SpriteOffset.Y, targetOffset, 0.5))
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.SpiritOfAngerUpdate, SPIRIT_OF_ANGER)

local angerDetectedCardSpawn = 0

function mod:AngerPlayerUpdate(player)
	local data = player:GetData()
	
	local numBoosterPacks = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BOOSTER_PACK)
	local numGacha = player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BUDDY_IN_A_BOX)
	
	if AngerTrackingActive(player) and player.FrameCount > 1 then
		if numBoosterPacks > (data.samaelAngerBoosterPacks or 0) and angerDetectedCardSpawn > 0 then
			IncreaseAngerBonus(kBoosterPackBonus)
		end
		
		if numGacha > (data.samaelAngerGacha or 0) then
			if player:HasTrinket(ANGER_FOSSIL) and not AngerRunData().FossilGachaTriggered then
				AngerRunData().FossilGachaTriggered = true
				UpdateAngerFossilPenalty(kGachaBonus)
			end
			IncreaseAngerBonus(0) -- Just trigger the animation + cache evaluation
		end
	end
	
	data.samaelAngerBoosterPacks = numBoosterPacks
	data.samaelAngerGacha = numGacha
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.AngerPlayerUpdate)

function mod:AngerDetectCard(pickup)
	angerDetectedCardSpawn = 2
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.AngerDetectCard, PickupVariant.PICKUP_TAROTCARD)

function mod:AngerDetectCard2()
	if angerDetectedCardSpawn > 0 then
		angerDetectedCardSpawn = angerDetectedCardSpawn - 1
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AngerDetectCard2)

function mod:AngerDamageCache(player)
	if lib.HasItem(player, ANGER) then
		local baseDamageUp = kAngerBaseDamage * player:GetCollectibleNum(ANGER)
		local damageBonus = GetAngerBonus()
		local gachaBonus = kGachaBonus * player:GetCollectibleNum(CollectibleType.COLLECTIBLE_BUDDY_IN_A_BOX)
		player.Damage = player.Damage + baseDamageUp + damageBonus + gachaBonus
	end
	
	if player:HasTrinket(ANGER_FOSSIL) then
		local baseDamageUp = kAngerFossilBaseDamage
		local penalty = GetAngerFossilPenalty()
		local mult = FiendFolio and FiendFolio.GetGolemTrinketPower(player, ANGER_FOSSIL) or player:GetTrinketMultiplier(ANGER_FOSSIL)
		local finalChange = math.max(baseDamageUp - penalty, 0) * mult
		player.Damage = player.Damage + finalChange
	end
end
mod:AddCallback(ModCallbacks.MC_EVALUATE_CACHE, mod.AngerDamageCache, CacheFlag.CACHE_DAMAGE)

-------------------------------------------------------
---- TRACKING FAILED ATTEMPTS AT FINDING SECRET ROOMS
-------------------------------------------------------

local DoorSlotGridIndexOffset = {
	[RoomShape.ROOMSHAPE_1x1] = {
		[DoorSlot.LEFT0] = 0,
		[DoorSlot.UP0] = 0,
		[DoorSlot.RIGHT0] = 0,
		[DoorSlot.DOWN0] = 0,
	},
	[RoomShape.ROOMSHAPE_IH] = {
		[DoorSlot.LEFT0] = 0,
		[DoorSlot.RIGHT0] = 0,
	},
	[RoomShape.ROOMSHAPE_IV] = {
		[DoorSlot.UP0] = 0,
		[DoorSlot.DOWN0] = 0,
	},
	[RoomShape.ROOMSHAPE_1x2] = {
		[DoorSlot.LEFT0] = 0,
		[DoorSlot.LEFT1] = 13,
		[DoorSlot.UP0] = 0,
		[DoorSlot.RIGHT0] = 0,
		[DoorSlot.RIGHT1] = 13,
		[DoorSlot.DOWN0] = 13,
	},
	[RoomShape.ROOMSHAPE_IIV] = {
		[DoorSlot.UP0] = 0,
		[DoorSlot.DOWN0] = 13,
	},
	[RoomShape.ROOMSHAPE_2x1] = {
		[DoorSlot.LEFT0] = 0,
		[DoorSlot.UP0] = 0,
		[DoorSlot.UP1] = 1,
		[DoorSlot.RIGHT0] = 1,
		[DoorSlot.DOWN0] = 0,
		[DoorSlot.DOWN1] = 1,
	},
	[RoomShape.ROOMSHAPE_IIH] = {
		[DoorSlot.LEFT0] = 0,
		[DoorSlot.RIGHT0] = 1,
	},
	[RoomShape.ROOMSHAPE_2x2] = {
		[DoorSlot.LEFT0] = 0,
		[DoorSlot.LEFT1] = 13,
		[DoorSlot.UP0] = 0,
		[DoorSlot.UP1] = 1,
		[DoorSlot.RIGHT0] = 1,
		[DoorSlot.RIGHT1] = 14,
		[DoorSlot.DOWN0] = 13,
		[DoorSlot.DOWN1] = 14,
	},
}

local function AddLRoom(shape, overrides)
	DoorSlotGridIndexOffset[shape] = lib.ShallowCopy(DoorSlotGridIndexOffset[RoomShape.ROOMSHAPE_2x2])
	for slot, offset in pairs(overrides) do
		DoorSlotGridIndexOffset[shape][slot] = offset
	end
end
AddLRoom(RoomShape.ROOMSHAPE_LTL, {
	[DoorSlot.LEFT0] = 1,
	[DoorSlot.UP0] = 13,
})
AddLRoom(RoomShape.ROOMSHAPE_LTR, {
	[DoorSlot.RIGHT0] = 0,
	[DoorSlot.UP1] = 14,
})
AddLRoom(RoomShape.ROOMSHAPE_LBL, {
	[DoorSlot.DOWN0] = 0,
	[DoorSlot.LEFT1] = 14,
})
AddLRoom(RoomShape.ROOMSHAPE_LBR, {
	[DoorSlot.RIGHT1] = 13,
	[DoorSlot.DOWN1] = 1,
})

-- SaveData table for keeping track of locations where Secret Rooms could have been, but aren't.
local function PossibleSecretRooms()
	return mod:GetFloorData("AngerPossibleSecretRooms")
end

-- Table for tracking references the actual secret rooms on the floor.
local SecretRooms = nil

-- Gets the GridIndex that would be reached by traveling through the given door in the given room.
-- Doesn't know if the doorSlot is valid or not. Can return nil if impossible.
function mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot)
	local doorSlotIdxOffset = DoorSlotGridIndexOffset[roomDesc.Data.Shape][doorSlot]
	
	if not doorSlotIdxOffset then return end
	
	local idx = roomDesc.GridIndex + doorSlotIdxOffset
	
	if idx % 13 > 0 and (doorSlot == DoorSlot.LEFT0 or doorSlot == DoorSlot.LEFT1) then
		return idx - 1
	elseif idx >= 13 and (doorSlot == DoorSlot.UP0 or doorSlot == DoorSlot.UP1) then
		return idx - 13
	elseif idx % 13 < 12 and (doorSlot == DoorSlot.RIGHT0 or doorSlot == DoorSlot.RIGHT1) then
		return idx + 1
	elseif idx < 156 and (doorSlot == DoorSlot.DOWN0 or doorSlot == DoorSlot.DOWN1) then
		return idx + 13
	end
end

-- Checks a room index that is adjacent to a possible secret room location.
-- Returns 0 if there is no room here.
-- Returns 1 if it could connect to the secret room location.
-- Returns -99 if it could not connect to the secret room location, which means it'd be impossible
-- for the secret room to be there.
local function CheckAdjacentRoom(possibleSecretRoomIndex, adjacentRoomIndex, superSecret)
	local level = game:GetLevel()
	
	local roomDesc = level:GetRoomByIdx(adjacentRoomIndex, 0)
	
	-- Ignore nil rooms, red rooms, and hidden secret rooms.
	if not roomDesc or roomDesc.GridIndex < 0 or (roomDesc.Flags & RoomDescriptor.FLAG_RED_ROOM ~= 0)
			or (roomDesc.DisplayFlags == 0 and (roomDesc.Data.Type == RoomType.ROOM_SECRET
			or roomDesc.Data.Type == RoomType.ROOM_SUPERSECRET or roomDesc.Data.Type == RoomType.ROOM_ULTRASECRET)) then
		return 0
	end
	
	-- Check if there are any adjacent rooms that cannot be adjacent to a secret room.
	if (superSecret and roomDesc.Data.Type ~= RoomType.ROOM_DEFAULT)
			or roomDesc.Data.Type == RoomType.ROOM_SECRET or roomDesc.Data.Type == RoomType.ROOM_SUPERSECRET
			or roomDesc.Data.Type == RoomType.ROOM_ULTRASECRET or roomDesc.Data.Type == RoomType.ROOM_BOSS then
		return -99
	end
	
	-- Check if any of the doors in this room shape could connect to the secret room location.
	for doorSlot=0, 7 do
		local validDoorSlot = (roomDesc.Data.Doors & (1 << doorSlot) ~= 0)
		if validDoorSlot and possibleSecretRoomIndex == mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot) then
			return 1
		end
	end
	
	return -99
end

-- Returns true if the given GridIndex is a valid secret room location, but there isn't actually a
-- secret room here.
local function IsPossibleSecretRoom(idx, superSecret)
	local roomDesc = game:GetLevel():GetRoomByIdx(idx, 0)
	
	if roomDesc and roomDesc.GridIndex >= 0 then
		return false
	end
	
	local valid = 0
	
	-- Left
	if idx % 13 > 0 then
		valid = valid + CheckAdjacentRoom(idx, idx - 1, superSecret)
	end
	-- Up
	if idx >= 13 then
		valid = valid + CheckAdjacentRoom(idx, idx - 13, superSecret)
	end
	-- Right
	if idx % 13 < 12 then
		valid = valid + CheckAdjacentRoom(idx, idx + 1, superSecret)
	end
	-- Down
	if idx < 156 then
		valid = valid + CheckAdjacentRoom(idx, idx + 13, superSecret)
	end
	
	if superSecret then
		return valid == 1
	end
	return valid >= 2
end

-- Stores references to the actual secret rooms on this floor.
function mod:AngerTrackActualSecretRooms()
	if SecretRooms then return end
	
	SecretRooms = {}
	
	local rooms = game:GetLevel():GetRooms()
	for i=0, rooms.Size-1 do
		local roomDesc = rooms:Get(i)
		if roomDesc.Data.Type == RoomType.ROOM_SECRET or roomDesc.Data.Type == RoomType.ROOM_SUPERSECRET then
			SecretRooms[i] = roomDesc
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AngerTrackActualSecretRooms)

function mod:AngerResetSecretRoomTracking()
	SecretRooms = nil
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, mod.AngerResetSecretRoomTracking)
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.AngerResetSecretRoomTracking)
mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, mod.AngerResetSecretRoomTracking)

-- Check the secret rooms on the current floor to see if they've yet to be discovered.
-- Return counts of the remaining undiscovered secret rooms. (Can be >1 with Luna.)
local function AngerCheckSecretRooms()
	if not SecretRooms then
		mod:AngerTrackActualSecretRooms()
	end
	
	local numSecretRoomsToFind = 0
	local numSuperSecretRoomsToFind = 0
	
	for _, secretRoomDesc in pairs(SecretRooms) do
		if secretRoomDesc.Data.Type == RoomType.ROOM_SECRET and secretRoomDesc.DisplayFlags == 0 then
			numSecretRoomsToFind = numSecretRoomsToFind + 1
		end
		if secretRoomDesc.Data.Type == RoomType.ROOM_SUPERSECRET and secretRoomDesc.DisplayFlags == 0 then
			numSuperSecretRoomsToFind = numSuperSecretRoomsToFind + 1
		end
	end
	
	return numSecretRoomsToFind > 0, numSuperSecretRoomsToFind > 0
end

-- Check empty spaces adjacent to this room via the given DoorSlot and flag them if it's not
-- possible for a secret room to be there.
function mod:AngerUpdatePossibleSecretRoom(adjIdx, currentRoomDoorSlot, allowSecretRooms, allowSuperSecretRooms)
	local room = game:GetRoom()
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	
	local isValidDoorSlot = room:IsDoorSlotAllowed(currentRoomDoorSlot)
	local door = room:GetDoor(currentRoomDoorSlot)
	
	if adjIdx and PossibleSecretRooms()[""..adjIdx] ~= false then
		PossibleSecretRooms()[""..adjIdx] = isValidDoorSlot and not door and (
				(allowSecretRooms and IsPossibleSecretRoom(adjIdx))
				or (allowSuperSecretRooms and IsPossibleSecretRoom(adjIdx, true))
		)
	end
end

-- Check all empty DoorSlots in the room to check if they COULD have led to a secret room.
function mod:AngerUpdatePossibleSecretRoomsOnNewRoom()
	local room = game:GetRoom()
	
	if room:GetFrameCount() ~= 1 or not lib.IsInMainDimension() or not AngerTrackingActive() then return end
	
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	
	local secretRoomsToFind, superSecretRoomsToFind = AngerCheckSecretRooms()
	
	for doorSlot=0, 7 do
		local adjIdx = mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot)
		if adjIdx then
			mod:AngerUpdatePossibleSecretRoom(adjIdx, doorSlot, secretRoomsToFind, superSecretRoomsToFind)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AngerUpdatePossibleSecretRoomsOnNewRoom)

local function AnyPlayerHasInfiniteExplosions()
	for _, player in pairs(lib.GetPlayers()) do
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_DR_FETUS)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_EPIC_FETUS)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_IPECAC)
				or lib.HasItem(player, CollectibleType.COLLECTIBLE_TERRA) then
			return true
		end
	end
end

-- When a player's placed bomb explosion goes off, check if it could have opened a secret room in
-- a valid location where there is not currently a secret room.
function mod:AngerExplosion(eff)
	if eff.FrameCount ~= 1 or not lib.IsInMainDimension() then return end
	
	local fromPlayerBomb = eff.SpawnerType == EntityType.ENTITY_BOMBDROP and eff.SpawnerEntity and eff.SpawnerEntity:ToBomb() and eff.SpawnerEntity.SpawnerType == EntityType.ENTITY_PLAYER
	if not fromPlayerBomb then return end
	local bomb = eff.SpawnerEntity:ToBomb()
	
	if not AngerTrackingActive() or AnyPlayerHasInfiniteExplosions() then return end
	
	local level = game:GetLevel()
	local room = game:GetRoom()
	local roomDesc = level:GetCurrentRoomDesc()
	
	local secretRoomsToFind, superSecretRoomsToFind = AngerCheckSecretRooms()
	
	--local bombRadius = 74 * eff.SpriteScale.X
	local bombRadius = lib.GetBombRadiusFromDamage(bomb.ExplosionDamage) * bomb.RadiusMultiplier
	
	for doorSlot=0, 7 do
		local isValidSlot = room:IsDoorSlotAllowed(doorSlot)
		local door = room:GetDoor(doorSlot)
		local pos = room:GetDoorSlotPosition(doorSlot)
		local dist = pos:Distance(eff.Position)
		
		if isValidSlot and not door and dist <= bombRadius + 20 then
			local adjIdx = mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot)
			if adjIdx then
				local adjRoom = level:GetRoomByIdx(adjIdx, 0)
				
				-- Make sure things are up to date with any map effects the player could have obtained.
				mod:AngerUpdatePossibleSecretRoom(adjIdx, doorSlot, secretRoomsToFind, superSecretRoomsToFind)
				
				if PossibleSecretRooms()[""..adjIdx] then
					-- The bombed location was a valid secret room location.
					IncreaseAngerBonus(kSecretRoomBonus)
					PossibleSecretRooms()[""..adjIdx] = false
				end
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE, mod.AngerExplosion, EffectVariant.BOMB_EXPLOSION)

--------------------------------------------------
---- TESTING CODE
--------------------------------------------------

local font = Font()
font:Load("font/pftempestasevencondensed.fnt")

-- Debug rendering.
function mod:AngerRoomRender()
	local level = game:GetLevel()
	local room = game:GetRoom()
	local roomDesc = level:GetCurrentRoomDesc()
	
	for doorSlot=0, 7 do
		local isValidSlot = room:IsDoorSlotAllowed(doorSlot)
		local door = room:GetDoor(doorSlot)
		local adjIdx = mod:GetAdjacentRoomGridIndex(roomDesc, doorSlot)
		if adjIdx and isValidSlot and not door then
			local str = "X"
			if PossibleSecretRooms()[""..adjIdx] then
				str = "?"
			end
			local screenPos = Isaac.WorldToScreen(room:GetDoorSlotPosition(doorSlot))
			font:DrawString(str, screenPos.X, screenPos.Y, KColor(1,1,1,1,0,0,0),0,true)
		end
	end
end
--mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.AngerRoomRender)

--[[
local myRock
local kStartingDist = 150
local currentDist = kStartingDist
local lastBombPos
local kStartingStep = 10
local step = kStartingStep
local minStep = 0.0001
local done = false
local currentAngle = 0

local tab = {}

function mod:NewAngerTesting()
	local room = game:GetRoom()
	
	if room:GetFrameCount() == 0 then return end
	
	if room:GetFrameCount() == 1 then
		local idx = room:GetGridIndex(room:GetCenterPos())
		room:SpawnGridEntity(idx, GridEntityType.GRID_ROCK, 1, 1, 0)
		myRock = room:GetGridEntity(idx):ToRock()
	end
	
	if not myRock or done then return end
	if room:GetFrameCount() % 2 ~= 0 then return end
	
	if myRock.State ~= 1 then
		--lib.Log("Dist: " .. currentDist, true)
		if currentDist == kStartingDist - step then
			lib.LogErr("starting too close")
			done = true
		elseif step > minStep then
			step = step * 0.1
			lib.Log(step, true)
			currentDist = currentDist + step
		else
			tab[currentAngle] = {}
			
			step = kStartingStep
			local size = currentDist
			while step >= minStep do
				while room:GetGridIndex(lastBombPos + (myRock.Position - lastBombPos):Resized(size)) == myRock:GetGridIndex() do
					size = size - step
				end
				size = size + step
				step = step * 0.1
			end
			
			tab[currentAngle].Size = size
			tab[currentAngle].Dist = lastBombPos:Distance(myRock.Position)
			tab[currentAngle].Pos = myRock.Position + (lastBombPos - myRock.Position):Resized(150)
			
			currentAngle = currentAngle + 45
			if currentAngle >= 360 then
				done = true
				return
			end
			currentDist = kStartingDist
			step = kStartingStep
		end
		myRock.State = 1
	end
	
	currentDist = currentDist - step
	
	local bombPos = myRock.Position + Vector(1,0):Resized(currentDist):Rotated(currentAngle)
	lastBombPos = bombPos
	game:BombExplosionEffects(bombPos, 10, TearFlags.TEAR_NORMAL, lib.NullColor, nil, 1)
end
--mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.NewAngerTesting)

function mod:AngerRender()
	for angle, t in pairs(tab) do
		local screenPos = Isaac.WorldToScreen(t.Pos)
		font:DrawString("Dist="..t.Dist, screenPos.X, screenPos.Y, KColor(1,1,1,1,0,0,0),0,true)
		font:DrawString("Size="..t.Size, screenPos.X, screenPos.Y + 15, KColor(1,1,1,1,0,0,0),0,true)
	end
end
--mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.AngerRender)]]

--------------------------------------------------
---- SLOT MACHINE HANDLING
--------------------------------------------------

local SlotMachineLoseSounds = {SoundEffect.SOUND_SCAMPER, SoundEffect.SOUND_THUMBS_DOWN}
local SlotMachineWinSounds = {SoundEffect.SOUND_SLOTSPAWN, SoundEffect.SOUND_THUMBSUP}

local function CheckSlotMachine(slot)
	local data = slot:GetData()
	local soundData = lib.GetOrInit(data, "SamaelSlotSoundData")
	local anim = slot:GetSprite():GetAnimation()
	
	if data.SamaelSlotCheckResult or (anim == "Prize" and anim ~= data.SamaelSlotLastAnim) then
		local probablyWon = false
		for _, sound in pairs(SlotMachineWinSounds) do
			if sfxManager:IsPlaying(sound) and not soundData[sound] then
				probablyWon = true
			end
		end
		if not probablyWon then
			for _, sound in pairs(SlotMachineLoseSounds) do
				if sfxManager:IsPlaying(sound) and not soundData[sound] then
					IncreaseAngerBonus(kSlotMachineBonus)
				end
			end
		end
		data.SamaelSlotCheckResult = false
	end
	
	if slot:GetSprite():IsFinished("WiggleEnd") then
		data.SamaelSlotCheckResult = true
	end
	
	for _, sound in pairs(SlotMachineLoseSounds) do
		soundData[sound] = sfxManager:IsPlaying(sound)
	end
	for _, sound in pairs(SlotMachineWinSounds) do
		soundData[sound] = sfxManager:IsPlaying(sound)
	end
	data.SamaelSlotLastAnim = anim
end

local function CheckCraneGame(slot)
	local sprite = slot:GetSprite()
	if sprite:IsPlaying("NoPrize") and sprite:GetFrame() == 3 then
		IncreaseAngerBonus(kCraneGameBonus)
	end
end

local function CheckShellGame(slot)
	local data = slot:GetData()
	local sprite = slot:GetSprite()
	if sprite:WasEventTriggered("Prize") then
		if not data.SamaelSlotAppliedAngerBonus and not sfxManager:IsPlaying(SoundEffect.SOUND_SLOTSPAWN) and sfxManager:IsPlaying(SoundEffect.SOUND_BOSS2INTRO_ERRORBUZZ) then
			IncreaseAngerBonus(kShellGameBonus)
			data.SamaelSlotAppliedAngerBonus = true
		end
	else
		data.SamaelSlotAppliedAngerBonus = false
	end
end

local function ShouldTriggerBeggarBonus(slot, player)
	if slot.Variant == 5 then
		return player:GetHearts() == 0 and player:GetEffectiveMaxHearts() > 0
	elseif slot.Variant == 7 then
		return player:GetNumKeys() == 0
	elseif slot.Variant == 9 then
		return player:GetNumBombs() == 0
	else
		return player:GetNumCoins() == 0
	end
end

function CheckBeggar(slot)
	local data = slot:GetData()
	local sprite = slot:GetSprite()
	
	local beggarBonus = mod:GetFloorData("AngerBeggarGaveBonus")
	
	if not beggarBonus[""..slot.InitSeed] and (sprite:IsFinished("PayNothing") or (data.samaelAngerLastOverlay == "PayNothing" and sprite:GetOverlayAnimation() ~= "PayNothing")) then
		local bonusTriggered = false
		for _, player in pairs(lib.GetPlayers()) do
			if not bonusTriggered and ShouldTriggerBeggarBonus(slot, player) then
				bonusTriggered = true
			end
		end
		if bonusTriggered then
			IncreaseAngerBonus(kBeggarBonus)
			beggarBonus[""..slot.InitSeed] = true
		end
	end
	
	data.samaelAngerLastOverlay = sprite:GetOverlayAnimation()
end

function mod:AngerSlotMachines()
	if not AngerTrackingActive() then return end
	
	for _, slot in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT, -1, -1, true, false)) do
		if slot.Variant == 1 then
			CheckSlotMachine(slot)
		elseif slot.Variant == 16 then
			CheckCraneGame(slot)
		elseif slot.Variant == 6 or slot.Variant == 15 then
			CheckShellGame(slot)
		elseif slot.Variant == 4 or slot.Variant == 5 or slot.Variant == 7 or slot.Variant == 9 or slot.Variant == 13 or slot.Variant == 18 then
			CheckBeggar(slot)
		end
		
		mod:AngerFiendFolioSlots(slot)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AngerSlotMachines)

--------------------------------------------------
---- PORTABLE SLOT
--------------------------------------------------

function mod:AngerPortableSlot(_, rng, player, useFlags)
	if AngerTrackingActive() and useFlags & UseFlag.USE_OWNED ~= 0 and sfxManager:IsPlaying(SoundEffect.SOUND_THUMBS_DOWN) then
		IncreaseAngerBonus(kSlotMachineBonus)
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_USE_ITEM, -1, mod.AngerPortableSlot, CollectibleType.COLLECTIBLE_PORTABLE_SLOT)

--------------------------------------------------
---- CURSES
--------------------------------------------------

local AnnoyingCurses = {LevelCurse.CURSE_OF_BLIND, LevelCurse.CURSE_OF_THE_LOST, LevelCurse.CURSE_OF_THE_UNKNOWN}

function mod:AngerCheckCurses()
	local currentCurses = game:GetLevel():GetCurses()
	for _, curse in pairs(AnnoyingCurses) do
		if currentCurses & curse ~= 0 then
			IncreaseAngerBonus(kCurseBonus)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL, mod.AngerCheckCurses)

--------------------------------------------------
---- MIMIC CHEST
--------------------------------------------------

local numMimicChests = 0

function mod:AngerMimicChestDamage(player, damage, damageFlags, damageSourceRef)
	if damageSourceRef.Type == EntityType.ENTITY_PICKUP and numMimicChests > 0
			and (damageSourceRef.Variant == PickupVariant.PICKUP_MIMICCHEST or damageSourceRef.Variant == PickupVariant.PICKUP_SPIKEDCHEST) then
		numMimicChests = numMimicChests - 1
		IncreaseAngerBonus(kMimicChestBonus)
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, mod.AngerMimicChestDamage, EntityType.ENTITY_PLAYER)

function mod:AngerMimicChestInit(pickup)
	numMimicChests = numMimicChests + 1
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.AngerMimicChestInit, PickupVariant.PICKUP_MIMICCHEST)

function mod:AngerMimicChestReset()
	numMimicChests = 0
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.AngerMimicChestReset)

--------------------------------------------------
---- DEVIL/ANGEL CHANCE LOSS
--------------------------------------------------

function mod:AngelDevilChanceLossDamage(player, damage, damageFlags, damageSourceRef)
	player = player:ToPlayer()
	local exemptDamageFlags = DamageFlag.DAMAGE_RED_HEARTS | DamageFlag.DAMAGE_DEVIL | DamageFlag.DAMAGE_FAKE | DamageFlag.DAMAGE_NO_PENALTIES
	if player and game:GetLevel():CanSpawnDevilRoom() and AngerTrackingActive(player) and player:GetSoulHearts() == 0 and player:GetBoneHearts() == 0 and damageFlags & exemptDamageFlags == 0 then
		player:GetData().angerPreDamageDevilChance = game:GetRoom():GetDevilRoomChance()
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.DEFAULT, mod.AngelDevilChanceLossDamage, EntityType.ENTITY_PLAYER)

function mod:AngerDevilChanceLossDetection(player)
	local prevDevilChance = player:GetData().angerPreDamageDevilChance
	if prevDevilChance then
		if AngerTrackingActive(player) and prevDevilChance > game:GetRoom():GetDevilRoomChance() then
			IncreaseAngerBonus(kDevilChanceLossBonus)
		end
		player:GetData().angerPreDamageDevilChance = nil
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.AngerDevilChanceLossDetection)

--------------------------------------------------
---- PERFECTION
--------------------------------------------------

function mod:AngerPerfection(player, damage, damageFlags, damageSourceRef)
	player = player:ToPlayer()
	if AngerTrackingActive(player) and player:HasTrinket(TrinketType.TRINKET_PERFECTION) and not player:GetData().spiritOfAngerLostPerfection then
		IncreaseAngerBonus(kPerfectionBonus)
		player:GetData().spiritOfAngerLostPerfection = true
	end
end
mod:AddPriorityCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, CallbackPriority.LATE, mod.AngerPerfection, EntityType.ENTITY_PLAYER)

--------------------------------------------------
---- THE BLOAT
--------------------------------------------------

function mod:AngerBloatDetection(bloat)
	if bloat.Variant == 1 and not AngerFloorData().seenBloat and AngerTrackingActive() then
		AngerFloorData().seenBloat = true
		IncreaseAngerBonus(kBloatBonus)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, mod.AngerBloatDetection, EntityType.ENTITY_PEEP)

--------------------------------------------------
---- BAD OR UNWANTED ITEMS
--------------------------------------------------

-- I disagree with these qualities for the purposes of Anger.
local ItemQualityOverride = {}
ItemQualityOverride[CollectibleType.COLLECTIBLE_CURSE_OF_THE_TOWER] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_ANKH] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_LAZARUS_RAGS] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_THE_WIZ] = 0
if REPENTANCE then
	ItemQualityOverride[CollectibleType.COLLECTIBLE_DARK_PRINCES_CROWN] = 0
else
	ItemQualityOverride[CollectibleType.COLLECTIBLE_DARK_PRINCESS_CROWN] = 0
end
ItemQualityOverride[CollectibleType.COLLECTIBLE_LOST_FLY] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_FRIEND_ZONE] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_FOREVER_ALONE] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_DISTANT_ADMIRATION] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_VENTRICLE_RAZOR] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_DELIRIOUS] = 0
ItemQualityOverride[CollectibleType.COLLECTIBLE_JUDAS_SHADOW] = 0

ItemQualityOverride[CollectibleType.COLLECTIBLE_SKATOLE] = 1
ItemQualityOverride[CollectibleType.COLLECTIBLE_HUSHY] = 4
ItemQualityOverride[CollectibleType.COLLECTIBLE_LIL_DUMPY] = 4

local function GetSeenBadItems()
	return lib.GetOrInit(AngerRunData(), "SeenBadItem")
end

local function CheckItem(player, id, maxActiveQuality)
	local seenBadItems = GetSeenBadItems()
	
	if id == 0 or seenBadItems[""..id] then return end
	
	local item = Isaac.GetItemConfig():GetCollectible(id)
	if not item or item.Hidden or item:HasTags(ItemConfig.TAG_QUEST) then return end
	
	local quality = ItemQualityOverride[id] or item.Quality
	
	if quality == 0 then
		seenBadItems[""..id] = true
		IncreaseAngerBonus(kBadItemBonus)
	elseif item.Type == ItemType.ITEM_ACTIVE and quality <= maxActiveQuality then
		seenBadItems[""..id] = true
		IncreaseAngerBonus(kActiveItemBonus)
	end
end

function mod:AngerBadItemDetection(pickup)
	if pickup.SubType == 0 or GetSeenBadItems()[""..pickup.SubType] or pickup.Price ~= 0
			or game:GetLevel():GetCurses() & LevelCurse.CURSE_OF_BLIND ~= 0 then
		return
	end
	
	if not AngerTrackingActive() then return end
	
	local currentActiveQuality = 10
	
	for _, player in pairs(lib.GetPlayers()) do
		local slotsToCheck = {ActiveSlot.SLOT_PRIMARY}
		
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_SCHOOLBAG) then
			table.insert(slotsToCheck, ActiveSlot.SLOT_SECONDARY)
		end
		
		for _, slot in pairs(slotsToCheck) do
			local activeItemId = player:GetActiveItem(slot)
			
			if activeItemId == 0 then
				currentActiveQuality = -1
			else
				local activeItem = Isaac.GetItemConfig():GetCollectible(activeItemId)
				if activeItem then
					currentActiveQuality = math.min(currentActiveQuality, activeItem.Quality)
				end
			end
		end
	end
	
	CheckItem(player, pickup.SubType, currentActiveQuality)
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, mod.AngerBadItemDetection, PickupVariant.PICKUP_COLLECTIBLE)

-- Curse of the blind will make it so we don't identify an item until the player is holding it.
function mod:AngerBadItemHeldByPlayer(player)
	if AngerTrackingActive(player) and player:IsHoldingItem() and player.QueuedItem and player.QueuedItem.Item then
		CheckItem(player, player.QueuedItem.Item.ID, 1)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.AngerBadItemHeldByPlayer)

--------------------------------------------------
---- BAD PILLS
--------------------------------------------------

local IsPillIdentified = {}

local IsBadPillEffect = {}
IsBadPillEffect[PillEffect.PILLEFFECT_BAD_TRIP] = true
IsBadPillEffect[PillEffect.PILLEFFECT_HEALTH_DOWN] = true
IsBadPillEffect[PillEffect.PILLEFFECT_RANGE_DOWN] = true
IsBadPillEffect[PillEffect.PILLEFFECT_SPEED_DOWN] = true
IsBadPillEffect[PillEffect.PILLEFFECT_TEARS_DOWN] = true
IsBadPillEffect[PillEffect.PILLEFFECT_LUCK_DOWN] = true
IsBadPillEffect[PillEffect.PILLEFFECT_AMNESIA] = true
IsBadPillEffect[PillEffect.PILLEFFECT_WIZARD] = true
IsBadPillEffect[PillEffect.PILLEFFECT_ADDICTED] = true
IsBadPillEffect[PillEffect.PILLEFFECT_QUESTIONMARK] = true
IsBadPillEffect[PillEffect.PILLEFFECT_RETRO_VISION] = true
IsBadPillEffect[PillEffect.PILLEFFECT_SHOT_SPEED_DOWN] = true

function mod:AngerUsePill(pillEffect, player, useFlags)
	if not AngerTrackingActive(player) or lib.HasItem(player, CollectibleType.COLLECTIBLE_PHD) or lib.HasItem(player, CollectibleType.COLLECTIBLE_FALSE_PHD) then return end
	
	local itemPool = game:GetItemPool()
	
	local pillColor = player:GetPill(0)
	local isHeldPill = pillColor > 0 and pillEffect == itemPool:GetPillEffect(pillColor, player)
	local isIdentified = IsPillIdentified[pillColor]
	local isBadPill = IsBadPillEffect[pillEffect]
	local effectUnknown = not isHeldPill or (isHeldPill and not isIdentified)
	
	if isBadPill and effectUnknown then
		IncreaseAngerBonus(kBadPillBonus)
	end
end
mod:AddCallback(ModCallbacks.MC_USE_PILL, mod.AngerUsePill)

function mod:AngerTrackPills()
	local itemPool = game:GetItemPool()
	
	for i = PillColor.PILL_BLUE_BLUE, PillColor.PILL_WHITE_YELLOW do
		IsPillIdentified[i] = itemPool:IsPillIdentified(i)
	end
	
	for i = PillColor.PILL_BLUE_BLUE + PillColor.PILL_GIANT_FLAG, PillColor.PILL_WHITE_YELLOW + PillColor.PILL_GIANT_FLAG do
		IsPillIdentified[i] = itemPool:IsPillIdentified(i)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.AngerTrackPills)

-----------------------------------------------------------
---- NEW ROOM (Greed / That one treasure room with no item)
-----------------------------------------------------------

function mod:AngerNewRoom()
	if not game:GetRoom():IsFirstVisit() or not AngerTrackingActive() then return end
	
	local roomData = game:GetLevel():GetCurrentRoomDesc().Data
	
	if not roomData then return end
	
	local runData = AngerRunData()
	local floorData = AngerFloorData()
	
	-- Greed
	if (roomData.Type == RoomType.ROOM_SECRET or roomData.Type == RoomType.ROOM_SHOP) and not (runData.seenGreed and runData.seenSuperGreed) then
		for _, greed in pairs(Isaac.FindByType(EntityType.ENTITY_GREED, -1, -1, false, true)) do
			if greed.Variant == 0 and not runData.seenGreed then
				runData.seenGreed = true
				IncreaseAngerBonus(kGreedBonus)
			elseif greed.Variant == 1 and not runData.seenSuperGreed then
				runData.seenSuperGreed = true
				IncreaseAngerBonus(kGreedBonus)
			end
		end
	end
	
	-- That one "Bad" treasure room
	if not floorData.seenBadTreasureRoom and roomData.Type == RoomType.ROOM_TREASURE --[[and roomData.Variant == 22]] and roomData.Name == "bad" then
		floorData.seenBadTreasureRoom = true
		IncreaseAngerBonus(kBadTreasureRoomBonus)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.AngerNewRoom)

-----------------------------------------------------------
---- FIEND FOLIO COMPATABILITY
-----------------------------------------------------------

local kPokerTableBonus = 0.02
local kEvilBeggarBonus = 0.1

function mod:AngerFiendFolioSlots(entity)
	if not FiendFolio or not AngerTrackingActive() then return end
	
	local sprite = entity:GetSprite()
	local data = entity:GetData()
	
	if entity.Variant == FiendFolio.FF.PokerTable.Var then
		if sprite:IsEventTriggered("Lose") then
			IncreaseAngerBonus(kPokerTableBonus)
		end
	end
	
	if entity.Variant == FiendFolio.FF.EvilBeggar.Var then
		local prevAnim = data.samaelAngerLastAnim
		local currAnim = sprite:GetAnimation()
		if currAnim == "Idle" and prevAnim and prevAnim ~= "Idle" and prevAnim ~= "Prize"
				and prevAnim ~= "Teleport" and prevAnim ~= "Bombed" then
			IncreaseAngerBonus(kEvilBeggarBonus)
		end
		if prevAnim ~= currAnim then
			data.samaelAngerLastAnim = currAnim
		end
	end
	
	if entity.Variant == FiendFolio.FF.CellGame.Var then
		CheckShellGame(entity)
	end
	
	if entity.Variant == FiendFolio.FF.ZodiacBeggar.Var then
		CheckBeggar(entity)
	end
end

function mod:AngerCopperBombs(bomb)
	local data = bomb:GetData()
	
	if not data.samaelGotTheDud and data.FFCopperBombWasADud then
		IncreaseAngerBonus(kCopperBombDudBonus)
	end
	
	data.samaelGotTheDud = data.FFCopperBombWasADud
end
mod:AddCallback(ModCallbacks.MC_POST_BOMB_UPDATE, mod.AngerCopperBombs)
