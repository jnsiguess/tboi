local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local vuc = {}
vuc.DummyTrinkets = {}
mod.VanillaUnlockChecker = vuc

local function RegisterDummyTrinket(name)
	local dummyTrinket = Isaac.GetTrinketIdByName("(Samael Vanilla Unlock Checker) " .. name)
	if dummyTrinket > 0 then
		vuc.DummyTrinkets[dummyTrinket] = name
	else
		lib.LogErr("Failed to init \"Vanilla Unlock Checker\" trinket: " .. name)
	end
end

RegisterDummyTrinket("RedKey")
RegisterDummyTrinket("IsaacsTomb")
RegisterDummyTrinket("MegaChest")
RegisterDummyTrinket("WoodenChest")
RegisterDummyTrinket("CraneGame")
RegisterDummyTrinket("HellGame")
RegisterDummyTrinket("RottenBeggar")
RegisterDummyTrinket("Confessional")

function mod:CheckVanillaUnlocks()
	local pool = game:GetItemPool()
	local data = mod:VanillaUnlocks()
	
	for dummyTrinket, name in pairs(vuc.DummyTrinkets) do
		data[name] = pool:RemoveTrinket(dummyTrinket)
	end
end

function vuc:GetTrinket(trinketType)
	if vuc.DummyTrinkets[trinketType] then
		local pool = game:GetItemPool()
		pool:RemoveTrinket(trinketType)
		return pool:GetTrinket()
	end
end
mod:AddCallback(ModCallbacks.MC_GET_TRINKET, vuc.GetTrinket)

function vuc:TrinketInit(pickup)
	if vuc.DummyTrinkets[lib.GetBaseTrinketId(pickup.SubType)] then
		local pool = game:GetItemPool()
		pool:RemoveTrinket(pickup.SubType)
		pickup:Morph(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_TRINKET, pool:GetTrinket())
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, vuc.TrinketInit, PickupVariant.PICKUP_TRINKET)

function vuc:PlayerUpdate(player)
	for i=0, 1 do
		local trinket = player:GetTrinket(i)
		if vuc.DummyTrinkets[trinket] then
			player:TryRemoveTrinket(trinket)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, vuc.PlayerUpdate)

function vuc:PostNewRoom()
	for _, player in pairs(lib.GetPlayers()) do
		for dummyTrinket, _ in pairs(vuc.DummyTrinkets) do
			if player:HasTrinket(dummyTrinket) then
				player:TryRemoveTrinket(dummyTrinket)
			end
			if player:HasTrinket(dummyTrinket | TrinketType.TRINKET_GOLDEN_FLAG) then
				player:TryRemoveTrinket(dummyTrinket | TrinketType.TRINKET_GOLDEN_FLAG)
			end
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, vuc.PostNewRoom)
