local mod = SamaelMod
local lib = mod.Lib
local game = Game()

local renderActive = include("samaelscripts.renderActive")
mod.RenderActive = renderActive

--[[renderActive:Add(mod.ITEMS.MALAKH_MOT, {
	Sprite = Sprite(),
	Directory = "gfx/samael_counter.anm2",
	UpdatedFrame = function(player)
		return mod:GetSamaelBirthrightGhosts(player)
	end,
	Condition = function(player, activeSlot)
		return lib.IsSamael(player) and not lib.IsTaintedSamael(player)
				and player:HasCollectible(CollectibleType.COLLECTIBLE_BIRTHRIGHT)
	end,
})]]
renderActive:Add({
	ItemID = mod.ITEMS.MEMENTO_MORI,
	Anm2Filename = "gfx/samael_counter.anm2",
	Update = function(player, slot, data)
		local frame = mod:GetMementoMoriBirthrightGhosts(player)
		data.Sprite:SetFrame(frame)
	end,
	Condition = function(player, activeSlot)
		return lib.IsTaintedSamael(player) and player:HasCollectible(CollectibleType.COLLECTIBLE_BIRTHRIGHT)
	end,
})

mod:AddCallback(ModCallbacks.MC_POST_RENDER, renderActive.OnRender)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, renderActive.OnUpdate)
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, renderActive.ResetOnGameStart)

function mod:RenderActives(shaderName)
	if shaderName == "PauseScreenCompletionMarks" and game:GetHUD():IsVisible() then
		renderActive:OnGetShaderParams()
	end
end
mod:AddCallback(ModCallbacks.MC_GET_SHADER_PARAMS, mod.RenderActives)
