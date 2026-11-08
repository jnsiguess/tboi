return function(ContentManager)
----------------------------------------------------------------------------------------------------

local mod = ContentManager.Mod
local lib = ContentManager.Lib
local game = Game()

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
	local room = game:GetRoom()
	local centre = room:GetCenterPos()

	local player = Isaac.GetPlayer()
	local pType = player:GetPlayerType()

	local nontainted = Isaac.GetPlayerTypeByName(player:GetName(), false)
	local tainted = Isaac.GetPlayerTypeByName(player:GetName(), true)

	local charData = ContentManager:GetCharacter(nontainted)
	local tCharData = ContentManager:GetCharacter(tainted)

	if charData or tCharData then
		local level = game:GetLevel()
		local desc = level:GetCurrentRoomDesc()
		
		if level:GetStage() == LevelStage.STAGE8 and desc.SafeGridIndex == 94 then
			if room:IsFirstVisit() then
				for _, shopkeeper in pairs(Isaac.FindByType(EntityType.ENTITY_SHOPKEEPER)) do
					shopkeeper:Remove()
				end
				for _, item in pairs(Isaac.FindByType(EntityType.ENTITY_PICKUP)) do
					item:Remove()
				end
				for _, guy in pairs(Isaac.FindByType(6, 14)) do
					guy:Remove()
				end
			end
			
			if tCharData and ContentManager:CharacterLocked(tainted) and ((pType == nontainted and ContentManager:CanRunUnlockAchievements()) or pType == tainted) then
				local closetDude = Isaac.FindByType(6, 14)[1] or Isaac.Spawn(6, 14, 0, centre, Vector.Zero, nil)
				local sprite = closetDude:GetSprite()
				if tCharData.TaintedUnlock then
					if tCharData.TaintedUnlock.ClosetAnm2 then
						sprite:Load(tCharData.TaintedUnlock.ClosetAnm2, false)
						sprite:Play(sprite:GetDefaultAnimation(), true)
					end
					if tCharData.TaintedUnlock.ClosetSprite then
						sprite:ReplaceSpritesheet(0, tCharData.TaintedUnlock.ClosetSprite)
					end
					sprite:LoadGraphics()
				end
				
				if pType == tainted then
					for i=0, 7 do
						local door = room:GetDoor(i)
						if door then
							room:RemoveDoor(i)
						end
					end
					for i = 1, 3 do
						Isaac.Spawn(EntityType.ENTITY_EFFECT, 21, 0, centre, Vector.Zero, nil)
					end
					Isaac.Spawn(EntityType.ENTITY_EFFECT, 64, 0, centre, Vector.Zero, nil)
				end
			elseif room:IsFirstVisit() then
				if pType == tainted then
					Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE, CollectibleType.COLLECTIBLE_INNER_CHILD, room:GetCenterPos(), Vector.Zero, nil)
				else
					Isaac.Spawn(EntityType.ENTITY_SHOPKEEPER, 0, 0, room:GetCenterPos(), Vector.Zero, nil)
				end
			end
		elseif tCharData and ContentManager:CharacterLocked(tainted) and pType == tainted then
			ContentManager.LockTaintedCharInHome(player)
		end
	end
end)

function ContentManager:UnlockTaintedCharacter(pType)
	local charData = ContentManager:GetCharacter(pType)
	
	if ContentManager:CharacterLocked(pType) and charData
			and charData.TaintedUnlock and charData.TaintedUnlock.Achievement then
		ContentManager:GrantCustomAchievement(charData.TaintedUnlock.Achievement, true)
	end
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if game:GetLevel():GetStage() == LevelStage.STAGE8 and game:GetLevel():GetCurrentRoomDesc().SafeGridIndex == 94 then
		for _, guy in pairs(Isaac.FindByType(6, 14)) do
			if guy:GetSprite():IsFinished("PayPrize") then
				local player = Isaac.GetPlayer()
				local pType = player:GetPlayerType()
				local nontainted = Isaac.GetPlayerTypeByName(player:GetName(), false)
				local tainted = Isaac.GetPlayerTypeByName(player:GetName(), true)
				
				if pType == nontainted then
					ContentManager:UnlockTaintedCharacter(tainted)
					return
				end
			end
		end
	end
end)

mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, function(_, player)
	local pType = player:GetPlayerType()
	if player.InitSeed == Isaac.GetPlayer().InitSeed then
		ContentManager.LockTaintedCharInHome(player)
	elseif ContentManager:CharacterLocked(pType) then
		-- Non-main characters playing a locked characters get forcibly replaced.
		local nontainted = Isaac.GetPlayerTypeByName(player:GetName(), false)
		local tainted = Isaac.GetPlayerTypeByName(player:GetName(), true)
		player:ClearCostumes()
		if pType == tainted then
			player:ChangePlayerType(nontainted)
		else
			player:ChangePlayerType(PlayerType.PLAYER_ISAAC)
		end
		for f=CacheFlag.CACHE_DAMAGE, CacheFlag.CACHE_COLOR do
			player:AddCacheFlags(f)
		end
		player:EvaluateItems()
	end
end)

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function(_, player)
	ContentManager.LockTaintedCharInHome()
end)

function ContentManager.LockTaintedCharInHome(player)
	player = player or Isaac.GetPlayer()
	local pType = player:GetPlayerType()

	if ContentManager:CharacterLocked(pType) then
		player.ControlsEnabled = false
		player.Visible = false
		player.Color = Color(1,1,1,0)
		player:GetData().taintedCharacterLockedInCloset = true

		local hud = game:GetHUD()
		hud:SetVisible(false)

		if game.Difficulty < Difficulty.DIFFICULTY_GREED then
			local level = game:GetLevel()
			if level:GetStage() ~= LevelStage.STAGE8 then
				Isaac.ExecuteCommand("stage 13")
			end
			if level:GetCurrentRoomIndex() ~= 94 then
				if not level:GetRoomByIdx(94).Data then
					level:MakeRedRoomDoor(95, DoorSlot.LEFT0)
				end
				level:ChangeRoom(94)
			end
		end
		player.Position = Vector.Zero
	elseif player:GetData().taintedCharacterLockedInCloset then
		player:GetData().taintedCharacterLockedInCloset = nil
		Isaac.ExecuteCommand('restart')
	end
end

----------------------------------------------------------------------------------------------------
end