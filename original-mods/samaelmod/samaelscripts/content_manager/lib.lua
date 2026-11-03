local mod

local lib = {}

lib.NullColor = Color(1,1,1,1)
lib.Black = Color(0,0,0,1)

function lib:Init(modRef)
	mod = modRef
	return lib
end

function lib.Log(str)
	Isaac.DebugString("[" .. mod.Name .. ".ContentManager] " .. str)
end

function lib.LogErr(str)
	local printStr = "[" .. mod.Name .. ".ContentManager] ERROR: " .. str
	Isaac.DebugString(printStr)
	print(printStr)
end

function lib.IsTaintedChar(player)
	return player:GetPlayerType() == Isaac.GetPlayerTypeByName(player:GetName(), true)
end

--Stolen from PROAPI
function lib.Lerp(first, second, percent)
	return (first + (second - first)*percent)
end

-- Returns true if the provided data is a table.
function lib.IsTable(data)
	return type(data) == 'table'
end

-- If the data is nil, return nil.
-- If the data is a table, return it as-is.
-- If the data is NOT a table, put it inside one.
function lib.ToTable(data)
	if not data then
		return nil
	end
	
	if lib.IsTable(data) then
		return data
	end
	
	return { data }
end

-- Returns true if the table contains the given value.
function lib.Contains(tab, valueToFind)
	if not tab then return false end
	
	for _, value in pairs(ToTable(tab)) do
		if value == valueToFind then
			return true
		end
	end
	
	return false
end

-- Returns a subtable within a table, initializing it if necessary.
function lib.GetSubTable(tab, key)
	if not tab[key] then
		tab[key] = {}
	end
	return tab[key]
end

function lib.BoolStr(bool)
	if bool then
		return "TRUE"
	end
	return "FALSE"
end

local function deepcopy(orig, copies)
	copies = copies or {}
	local orig_type = type(orig)
	local copy
	if orig_type == 'table' then
		if copies[orig] then
			copy = copies[orig]
		else
			copy = {}
			copies[orig] = copy
			for orig_key, orig_value in next, orig, nil do
				copy[deepcopy(orig_key, copies)] = deepcopy(orig_value, copies)
			end
			setmetatable(copy, deepcopy(getmetatable(orig), copies))
		end
	else -- number, string, boolean, etc
		copy = orig
	end
	return copy
end

function lib.DeepCopy(tab)
	return deepcopy(tab)
end

function lib.StrSplit(str, delim)
	local tokens = {}
	
	local delim = delim or " "
	local pattern = string.format("([^%s]+)", delim)
	string.gsub(str, pattern, function(s) table.insert(tokens, s) end)
	
	return tokens
end

function lib.SplitDssString(inputStr, charLimit)
	local tokens = lib.StrSplit(string.lower(inputStr))
	local strset = {}
	local line
	for _, str in pairs(tokens) do
		if not line then
			line = str
		elseif string.len(line .. " " .. str) > charLimit then
			table.insert(strset, line)
			line = str
		else
			line = line .. " " .. str
		end
	end
	if line then
		table.insert(strset, line)
	end
	return strset
end

function lib.SplitStringInHalf(inputStr)
	local words = lib.StrSplit(inputStr)
	
	local part1 = ""
	local part2 = ""
	
	local left = 1
	local right = #words
	
	while left <= right do
		if string.len(part1) <= string.len(part2) then
			part1 = part1 .. " " .. words[left]
			left = left + 1
		else
			part2 = words[right] .. " " .. part2
			right = right - 1
		end
	end
	
	return part1, part2
end

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

return lib