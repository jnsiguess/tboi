local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

local WRAITH_SKULL = mod.ITEMS.WRAITH_SKULL

function mod:WraithSkullEffect(player)
	local data = player:GetData()
	
	if player:HasTrinket(WRAITH_SKULL) and not data.spawnedWraithSkullMiniReaper then
		data.spawnedWraithSkullMiniReaper = true
		
		local poof = Isaac.Spawn(EntityType.ENTITY_EFFECT, 16, 1, player.Position, lib.ZeroVector, player):ToEffect()
		poof.Color = lib.NewColor(0,0,0,0.4)
		poof.SpriteScale = Vector(0.7, 0.7)
		local poof2 = Isaac.Spawn(EntityType.ENTITY_EFFECT, 15, 2, player.Position, lib.ZeroVector, player):ToEffect()
		poof2.Color = lib.NewColor(0,0,0,0.5)
		
		for i=1, player:GetTrinketMultiplier(WRAITH_SKULL) do
			local miniReaper = Isaac.Spawn(mod.ENTITIES.MINI_REAPER.ID, mod.ENTITIES.MINI_REAPER.Var, 1, player.Position, RandomVector()*8, player)
			miniReaper:ClearEntityFlags(EntityFlag.FLAG_APPEAR)
			miniReaper:Update()
		end
		
		sfxManager:Play(SoundEffect.SOUND_DEATH_CARD, 1, 0, false, 1.5)
	end
end

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	for _, player in pairs(lib.GetPlayers()) do
		player:GetData().spawnedWraithSkullMiniReaper = nil
	end
end)

function mod:WraithSkullDamageTrigger(player)
	if player and player:ToPlayer() and player:ToPlayer():HasTrinket(WRAITH_SKULL) then
		mod:WraithSkullEffect(player:ToPlayer())
	end
end
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, mod.WraithSkullDamageTrigger, EntityType.ENTITY_PLAYER)

function mod:WraithSkullUpdate(player)
	if game:GetRoom():GetFrameCount() == 0 or game:GetRoom():IsClear() or not player:HasTrinket(WRAITH_SKULL) then return end
	
	local totalHealth = player:GetHearts() + player:GetSoulHearts() + player:GetBoneHearts()
	
	if player:GetData().wraithActive or (totalHealth <= 2 and not player:GetEffects():HasCollectibleEffect(CollectibleType.COLLECTIBLE_HOLY_MANTLE)) then
		mod:WraithSkullEffect(player)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_PEFFECT_UPDATE, mod.WraithSkullUpdate)
