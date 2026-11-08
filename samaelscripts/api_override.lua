local META, META0

local function BeginClass(T)
	META = {}
	if type(T) == "function" then
		META0 = getmetatable(T())
	else
		META0 = getmetatable(T).__class
	end
end

local function EndClass()
	local oldIndex = META0.__index
	local newMeta = META
	
	rawset(META0, "__index", function(self, k)
		return newMeta[k] or oldIndex(self, k)
	end)
end

BeginClass(EntityKnife)

local originalIsFlying = META0.IsFlying

function META.IsFlying(self)
	if self:GetData().isSamaelKnifeScythe then
		return true
	end
	return originalIsFlying(self)
end

EndClass()
