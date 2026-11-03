local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local ID = mod.ITEMS.FRAGMENT_FRAGMENT

local BASE_CHANCE = 1 / 3

function mod:FragmentFragmentReward(pos)
	mod.SfxManager:Play(SoundEffect.SOUND_SLOTSPAWN)
	Isaac.Spawn(EntityType.ENTITY_PICKUP, 0, 3, pos, RandomVector() * 2, nil):ToPickup()
end

function mod:FragmentFragment()
	local room = game:GetRoom()
	
	if room:IsClear() then return end
	
	local power = 0
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(ID) then
			power = power + (FiendFolio and FiendFolio.GetGolemTrinketPower(player, ID) or player:GetTrinketMultiplier(ID))
		end
	end
	
	if power == 0 then return end
	
	local chance = power * BASE_CHANCE
	
	local rng = RNG()
	rng:SetSeed(room:GetSpawnSeed(), 44)
	
	if rng:RandomFloat() <= chance then
		Isaac.Spawn(mod.ENTITIES.LOST_SOUL.ID, mod.ENTITIES.LOST_SOUL.Var, 1, Isaac.GetPlayer().Position, lib.ZeroVector, nil)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.FragmentFragment)
