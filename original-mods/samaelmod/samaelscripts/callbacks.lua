local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game

local POST_SLOT_UPDATE = {}

local function GetPostSlotUpdateFuncs(variant, subType)
	return lib.GetOrInit(POST_SLOT_UPDATE, variant or -1, subType or -1)
end

function mod:AddPostSlotUpdateFunc(func, variant, subType)
	table.insert(GetPostSlotUpdateFuncs(variant, subType), func)
end

function mod:PostSlotUpdate()
	for _, slot in pairs(Isaac.FindByType(EntityType.ENTITY_SLOT, -1, -1, true, false)) do
		for _, func in pairs(GetPostSlotUpdateFuncs()) do
			func(_, slot)
		end
		for _, func in pairs(GetPostSlotUpdateFuncs(slot.Variant)) do
			func(_, slot)
		end
		for _, func in pairs(GetPostSlotUpdateFuncs(slot.Variant, slot.SubType)) do
			func(_, slot)
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.PostSlotUpdate)