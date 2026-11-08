local mod = SamaelMod
local lib = mod.Lib
local game = mod.Game
local sfxManager = mod.SfxManager

function mod:TriggerDarkRoomTeleport()
	if game:GetLevel():GetStage() ~= LevelStage.STAGE6
			or game:GetRoom():GetBossID() ~= 40 then
		return
	end

	local samael
	local fates = 0
	local hasIsaacsHead = false

	for _, player in pairs(lib.GetPlayers()) do
		if not samael and lib.IsSamael(player) then
			samael = player
		end
		fates = fates + player:GetCollectibleNum(CollectibleType.COLLECTIBLE_FATES_REWARD)
		if player:HasTrinket(TrinketType.TRINKET_ISAACS_HEAD) then
			hasIsaacsHead = true
		end
	end
	
	if samael and fates > (mod:GetAllRunData().numFates or 0) and hasIsaacsHead then
		game:GetLevel():SetStage(LevelStage.STAGE6, 0)
		samael:UseActiveItem(CollectibleType.COLLECTIBLE_FORGET_ME_NOW)
	end
	
	mod:GetAllRunData().numFates = fates
	mod:GetAllRunData().samaelEndingInitiated = true
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.TriggerDarkRoomTeleport)

function mod:CanTriggerFinalSequence()
	if not game:GetLevel():GetStage() == LevelStage.STAGE6
			or not mod:GetAllRunData().samaelEndingInitiated then
		return
	end

	local hasIsaacsHead = false
	local hasFatesReward = false
	local samael
	
	for _, player in pairs(lib.GetPlayers()) do
		if player:HasTrinket(TrinketType.TRINKET_ISAACS_HEAD) then
			hasIsaacsHead = true
		end
		if lib.HasItem(player, CollectibleType.COLLECTIBLE_FATES_REWARD) then
			hasFatesReward = true
		end
		if not samael and lib.IsSamael(Isaac.GetPlayer(i)) then
			samael = Isaac.GetPlayer(i)
		end
	end
	
	return samael and hasIsaacsHead and hasFatesReward
end

local isaacWisp = nil
local blueWisp = nil
local lambWisp = nil
local satan = nil
local uriel = nil
local gabriel = nil

local kEndingTime = 200
if not REPENTANCE then
	kEndingTime = 65
end
local endingCountdown = kEndingTime
local endingSprite = nil
local whiteOut = nil

local altEndingEnt

local dirt = nil

function mod:FindGraveRoom()
	for i=0,168 do
		local roomDesc = game:GetLevel():GetRoomByIdx(i)
		local data = roomDesc.Data
		if data and data.Name:find("^Grave Room") ~= nil then
			--game:StartRoomTransition(i, Direction.UP, RoomTransitionAnim.PIXELATION)
			game:ChangeRoom(i)
			return roomDesc
		end
	end
end

function mod:InitFinalRoom()
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	local room = game:GetRoom()
	
	local currentRoomIsGraveRoom = roomDesc.Data.Name:find("^Grave Room") ~= nil
	
	if mod:GetAllRunData().startFinalSequence and currentRoomIsGraveRoom then
		for i = 1, room:GetGridSize() do
			local gridEntity = room:GetGridEntity(i)
			if gridEntity then
				if gridEntity:GetType() == GridEntityType.GRID_DOOR then
					gridEntity:ToDoor():Close(true)
					gridEntity:ToDoor():Bar()
					if REPENTANCE then
						gridEntity:GetSprite().Color = lib.InvisibleColor
					end
				elseif gridEntity:GetType() ~= GridEntityType.GRID_WALL then
					gridEntity:Destroy(true)
					room:RemoveGridEntity(i, 0, false)
					gridEntity:Update()
				end
			end
		end
		room:KeepDoorsClosed()
		
		if isaacWisp ~= nil then isaacWisp:Remove() end
		if blueWisp ~= nil then blueWisp:Remove() end
		if lambWisp ~= nil then lambWisp:Remove() end
		
		isaacWisp = nil
		blueWisp = nil
		lambWisp = nil
		satan = nil
		uriel = nil
		gabriel = nil
		endingSprite = nil
		endingCountdown = kEndingTime
		whiteOut = nil
		
		dirt = nil
		
		for i, entity in pairs(Isaac.GetRoomEntities()) do
			if REPENTANCE and entity.Type == EntityType.ENTITY_FAMILIAR then
				if entity.Variant == FamiliarVariant.ISAACS_HEAD then
					if not isaacWisp then
						isaacWisp = entity:ToFamiliar()
					else
						entity:Remove()
					end
				elseif entity.Variant == FamiliarVariant.FATES_REWARD then
					if not blueWisp then
						blueWisp = entity:ToFamiliar()
					else
						entity:Remove()
					end
				end
			elseif entity.Type == EntityType.ENTITY_EFFECT and entity.Variant == EffectVariant.DIRT_PATCH then
				if not dirt then
					dirt = entity:ToEffect()
					dirt.Position = room:GetCenterPos()
					dirt.State = 2
				else
					entity:Remove()
				end
			elseif entity.Type == EntityType.ENTITY_PICKUP or entity.Type == EntityType.ENTITY_FIREPLACE
					or entity:IsEnemy() then
				entity:Remove()
			end
		end
		
		if not dirt then
			dirt = Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.DIRT_PATCH, 0, room:GetCenterPos(), lib.ZeroVector, nil):ToEffect()
			dirt.State = 2
		end
		
		if not mod:GetAllRunData().finalSequenceState then
			mod:GetAllRunData().finalSequenceState = "START"
		end
	elseif mod:GetAllRunData().startFinalSequence and not currentRoomIsGraveRoom then
		mod:FindGraveRoom()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.InitFinalRoom)

mod.IsaacWispColor = Color(1,1,1,1)
mod.IsaacWispColor:SetColorize(1, 1.5, 2, 1.5)
mod.BlueWispColor = Color(0.85, 0.85, 1, 1)
mod.BlueWispColor:SetColorize(0.4, 0.8, 1.75, 1)
mod.LambWispColor = Color(1,1,1,1)
mod.LambWispColor:SetColorize(0.8, 1, 3, 2)

function mod:FinalRoomHandler()
	if not game:GetLevel():GetStage() == LevelStage.STAGE6
			or not mod:GetAllRunData().startFinalSequence
			or not mod:GetAllRunData().finalSequenceState then
		return nil
	end

	local player = Isaac.GetPlayer(0)
	local room = game:GetRoom()
	local roomDesc = game:GetLevel():GetCurrentRoomDesc()
	local roomCenter = room:GetCenterPos()

	room:KeepDoorsClosed()
	
	local wispSpawnPos = player.Position
	
	if isaacWisp ~= nil and isaacWisp.Variant == FamiliarVariant.ISAACS_HEAD then
		isaacWisp.Parent = lambWisp
		if mod:GetAllRunData().finalSequenceState == "START" and room:GetFrameCount() > 50 then
			wispSpawnPos = isaacWisp.Position
			isaacWisp:BloodExplode()
			isaacWisp:Remove()
			isaacWisp = nil
		end
	end

	if blueWisp ~= nil and blueWisp.Variant == FamiliarVariant.FATES_REWARD then
		if isaacWisp then
			blueWisp.Parent = isaacWisp
		else
			blueWisp.Parent = lambWisp
		end
		if mod:GetAllRunData().finalSequenceState == "START" and room:GetFrameCount() > 75 then
			wispSpawnPos = blueWisp.Position
			blueWisp:BloodExplode()
			blueWisp:Remove()
			blueWisp = nil
		end
	end

	if REPENTANCE and (isaacWisp == nil or not isaacWisp:Exists()) then
		isaacWisp = player:AddWisp(CollectibleType.COLLECTIBLE_CANDLE, wispSpawnPos)
		isaacWisp.Color = mod.IsaacWispColor
		isaacWisp:GetData().isSamaelWisp = true
		isaacWisp.Parent = dirt
		isaacWisp.OrbitAngleOffset = 0
		isaacWisp.CollisionDamage = 0
		isaacWisp.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		isaacWisp:ClearEntityFlags(EntityFlag.FLAG_PERSISTENT)
	end
	
	if REPENTANCE and (blueWisp == nil or not blueWisp:Exists()) then
		blueWisp = player:AddWisp(CollectibleType.COLLECTIBLE_CANDLE, wispSpawnPos)
		blueWisp.Color = mod.BlueWispColor
		blueWisp:GetData().isSamaelWisp = true
		blueWisp.Parent = dirt
		blueWisp.OrbitAngleOffset = 4
		blueWisp.CollisionDamage = 0
		blueWisp.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		blueWisp:ClearEntityFlags(EntityFlag.FLAG_PERSISTENT)
	end
	
	if REPENTANCE and (lambWisp == nil or not lambWisp:Exists()) then
		lambWisp = player:AddWisp(CollectibleType.COLLECTIBLE_CANDLE, wispSpawnPos)
		lambWisp.Color = mod.LambWispColor
		lambWisp:GetData().isSamaelWisp = true
		lambWisp.Parent = dirt
		lambWisp.OrbitAngleOffset = 2
		lambWisp.CollisionDamage = 0
		lambWisp.EntityCollisionClass = EntityCollisionClass.ENTCOLL_NONE
		lambWisp:ClearEntityFlags(EntityFlag.FLAG_PERSISTENT)
	end
	
	if REPENTANCE then
		isaacWisp.FireCooldown = 99
		blueWisp.FireCooldown = 99
		lambWisp.FireCooldown = 99
	
		if room:GetFrameCount() == 1 and isaacWisp.Variant ~= FamiliarVariant.ISAACS_HEAD 
				and blueWisp.Variant ~= FamiliarVariant.FATES_REWARD then
			for i, entity in pairs(Isaac.GetRoomEntities()) do
				if entity.Type == EntityType.ENTITY_FAMILIAR and (entity.Variant == FamiliarVariant.ISAACS_HEAD
						or entity.Variant == FamiliarVariant.FATES_REWARD) then
					entity:BloodExplode()
					entity:Remove()
				end
			end
		end
	end
	
	if mod:GetAllRunData().finalSequenceState == "START" and room:GetFrameCount() > 150 then
		mod:GetAllRunData().finalSequenceState = "SATAN"
	end
	
	if mod:GetAllRunData().finalSequenceState == "SATAN" and satan == nil then
		satan = Isaac.Spawn(EntityType.ENTITY_SATAN, 0, 0, roomCenter:__add(Vector(0, -150)), lib.ZeroVector, nil):ToNPC()
		Isaac.Spawn(EntityType.ENTITY_EFFECT, EffectVariant.POOF01, 0, satan.Position:__add(Vector(0,10)), lib.ZeroVector, satan)
		satan:PlaySound(SoundEffect.SOUND_SUMMONSOUND, 1, 0, false, 1)
		satan:Update()
	end
	
	if mod:GetAllRunData().finalSequenceState == "SATAN" and not satan:Exists() and room:GetAliveEnemiesCount() == 0 then
		mod:GetAllRunData().finalSequenceState = "ANGELS"
	end
	
	if mod:GetAllRunData().finalSequenceState == "ANGELS" and uriel == nil and gabriel == nil then
		uriel = Isaac.Spawn(EntityType.ENTITY_URIEL, 0, 0, roomCenter:__add(Vector(-200, 0)), lib.ZeroVector, nil):ToNPC()
		gabriel = Isaac.Spawn(EntityType.ENTITY_GABRIEL, 0, 0, roomCenter:__add(Vector(200, 0)), lib.ZeroVector, nil):ToNPC()
		gabriel:PlaySound(SoundEffect.SOUND_SUPERHOLY, 0.85, 0, false, 1)
		uriel:AddEntityFlags(EntityFlag.FLAG_AMBUSH)
		gabriel:AddEntityFlags(EntityFlag.FLAG_AMBUSH)
		uriel:Update()
		gabriel:Update()
	end
	
	if mod:GetAllRunData().finalSequenceState == "ANGELS" and not uriel:Exists() and not gabriel:Exists() and room:GetAliveEnemiesCount() == 0 then
		mod:GetAllRunData().finalSequenceState = "PREENDING"
		endingCountdown = kEndingTime
		MusicManager():Play(Music.MUSIC_JINGLE_BOSS_OVER, 0.2)
	end
	
	if mod:GetAllRunData().finalSequenceState == "PREENDING" then
		endingCountdown = endingCountdown - 1
		local x = 100 * (endingCountdown/kEndingTime)
		if REPENTANCE then
			local v = Vector(x,x)
			isaacWisp.OrbitDistance = v
			blueWisp.OrbitDistance = v
			lambWisp.OrbitDistance = v
		end
		if x < 20 and not whiteOut then
			whiteOut = Sprite()
			whiteOut:Load("gfx/ui/bossoverlay_whiteout.anm2", true)
			whiteOut.PlaybackSpeed = 0.5
			whiteOut.Scale = Vector(10,10)
			whiteOut:Play("Fade", false)
		elseif whiteOut and whiteOut:GetFrame() >= 15 then
			whiteOut:SetFrame("Fade", 15)
			whiteOut:Stop()
		end
		if x <	0.15 then
			mod:GetAllRunData().finalSequenceState = "ENDING"
		end
	end
	
	if mod:GetAllRunData().finalSequenceState == "ENDING" then
		if not endingSprite then
			for i, entity in pairs(Isaac.GetRoomEntities()) do
				if entity.Type == EntityType.ENTITY_PLAYER then
					entity:ToPlayer().ControlsEnabled = false
				else
					entity:Remove()
				end
			end
			
			if whiteOut then
				whiteOut = nil
			end
			
			endingSprite = Sprite()
			endingSprite:Load("gfx/samael_ending.anm2", true)
			endingSprite.PlaybackSpeed = 0.5
			endingSprite:Play("Ending", false)
			
			if REPENTANCE then
				game:GetHUD():SetVisible(false)
			end
			lib.PlayMusic(Music.MUSIC_GAME_OVER)
		end
		if endingSprite:WasEventTriggered("Swap") and not mod:GetAllRunData().checkedForTaintedSamaelEnding then
			mod:GetAllRunData().checkedForTaintedSamaelEnding = true
			for _, player in pairs(lib.GetPlayers()) do
				if lib.IsTaintedSamael(player) then
					mod:GetAllRunData().finalSequenceState = "ALT_ENDING"
					local npc = Isaac.Spawn(EntityType.ENTITY_DOGMA, 10, 617, game:GetRoom():GetCenterPos(), Vector.Zero, nil)
					npc:Update()
					altEndingEnt = EntityPtr(npc)
					endingSprite = nil
					break
				end
			end
		elseif endingSprite:IsEventTriggered("Transition") then
			sfxManager:Play(18, 0.9, 0, false, 1.0)
		elseif endingSprite:IsFinished("Ending") then
			mod:GetAllRunData().finalSequenceState = "DONE"
		end
	end
	
	if mod:GetAllRunData().finalSequenceState == "ALT_ENDING" then
		if not altEndingEnt or not altEndingEnt.Ref or not altEndingEnt.Ref:Exists() then
			mod:GetAllRunData().finalSequenceState = "DONE"
		end
	end
	
	if mod:GetAllRunData().finalSequenceState == "DONE" then
		mod.ContentManager:GrantCustomMarkToAllPlayers(mod.ACHIEVEMENTS.SAMAEL_ENDING)
		mod:GetAllRunData().finalSequenceState = "FINISH"
	end
	
	if mod:GetAllRunData().finalSequenceState == "FINISH" then
		if not mod.ContentManager:UnlockPopupPlayingOrQueued() then
			game:FinishChallenge()
		end
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.FinalRoomHandler)

mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, function(_, _)
	endingSprite = nil
	whiteOut = nil
end)

function mod:RenderCutscene() 
	if whiteOut then
		whiteOut:Render(lib.ZeroVector,lib.ZeroVector,lib.ZeroVector)
		whiteOut:Update()
	end
	if endingSprite then
		endingSprite:Render(lib.GetScreenSize() * 0.5,lib.ZeroVector,lib.ZeroVector)
		endingSprite:Update()
	end
end
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.RenderCutscene)

local function PlayingSamaelEnding()
	local state = mod:GetAllRunData().finalSequenceState
	return state == "ENDING" or state == "ALT_ENDING" or state == "DONE" or state == "FINISH"
end

function mod:CheckTaintedMusic()
	if PlayingSamaelEnding() and mod.MusicManager:GetCurrentMusicID() == Music.MUSIC_TITLE_REPENTANCE
			and lib.IsTaintedChar(Isaac.GetPlayer()) then
		mod.MusicManager:EnableLayer(0, true)
	end
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.CheckTaintedMusic)

local SAMAEL_ALT_ENDING_VO = Isaac.GetSoundIdByName("SamaelEndingAlt")
local startedEndingVo = false

function mod:StopEndingVo()
	if startedEndingVo then
		sfxManager:Stop(SAMAEL_ALT_ENDING_VO)
		startedEndingVo = false
	end
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, mod.StopEndingVo)
mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, mod.StopEndingVo)

mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, function(_, npc)
	if npc.Variant == 10 and npc.SubType == 617 then
		if not PlayingSamaelEnding() then
			npc:Remove()
			return
		end
		npc:GetSprite():Play("Part1", true)
		npc:GetSprite():LoadGraphics()
		sfxManager:Play(SoundEffect.SOUND_DEATH_CARD, 1, 0, false, 0.5)
		mod.MusicManager:Pause()
		lib.ScheduleForUpdate(function()
			sfxManager:Play(SAMAEL_ALT_ENDING_VO, 1.85, 0, false, 1.0)
			startedEndingVo = true
		end)
		npc.RenderZOffset = 9999
		npc.DepthOffset = 9999
	end
end, EntityType.ENTITY_DOGMA)

mod:AddCallback(ModCallbacks.MC_PRE_NPC_UPDATE, function(_, npc)
	if npc.Variant == 10 and npc.SubType == 617 then
		npc.Position = game:GetRoom():GetCenterPos()
		
		if not PlayingSamaelEnding() then
			npc:Remove()
			return
		end
		local data = npc:GetData()
		data.Frames = (data.Frames or 0) + 1
		data.Wait = (data.Wait or 0)
		if data.Wait > 0 then
			data.Wait = data.Wait - 1
			return
		end
		local spr = npc:GetSprite()
		if data.Next then
			spr:Play(data.Next, true)
			data.Next = nil
			data.Frames = 0
		end
		
		if spr:IsFinished("Part1") then
			spr:Play("Part2", true)
		end
		if spr:IsFinished("Part2") then
			spr:Play("Part3", true)
		end
		if spr:IsFinished("Part3") then
			spr:Play("Part4", true)
		end
		if spr:IsFinished("Part4") then
			data.Wait = 120
			data.Next = "Part5"
			return
		end
		if spr:IsPlaying("Part5") then
			if data.Frames == 100 then
				spr:PlayOverlay("Memory1", true)
				sfxManager:Play(SoundEffect.SOUND_DEATH_CARD, 1, 0, false, 0.5)
			elseif data.Frames == 200 then
				spr:PlayOverlay("Memory2", true)
				sfxManager:Play(SoundEffect.SOUND_DEATH_CARD, 1, 0, false, 0.6)
			end
		end
		if spr:IsFinished("Part5") then
			sfxManager:Stop(SAMAEL_ALT_ENDING_VO)
			lib.PlayMusic(Music.MUSIC_TITLE_REPENTANCE)
			mod:CheckTaintedMusic()
			spr:Play("Part6", true)
			data.Frames = 0
		end
		
		if spr:IsPlaying("Part6") and data.Frames > 90 then
			spr:Play("Part7", true)
			data.Frames = 0
		end
		
		local endPause = 120
		local fadeTime = 60
		local blackTime = 30
		
		if spr:IsPlaying("Part7") and data.Frames > endPause then
			local n = 1 - ((data.Frames - endPause) / fadeTime)
			n = math.max(0, math.min(n, 1))
			spr.Color = Color(n,n,n,1)
		end
		if spr:IsPlaying("Part7") and data.Frames > endPause + fadeTime + blackTime then
			mod:GetAllRunData().finalSequenceState = "DONE"
		end
		
		if spr:IsFinished("Part6") then
			spr:Play("Part6", true)
		end
		if spr:IsFinished("Part7") then
			spr:Play("Part7", true)
		end
		
		if spr:IsEventTriggered("Static") then
			sfxManager:Play(SoundEffect.SOUND_STATIC, 0.5, 0, false, 0.35 + 0.1 * ((Random() % 100) / 99))
		end
		if spr:IsEventTriggered("Open") then
			sfxManager:Play(SoundEffect.SOUND_DOOR_HEAVY_OPEN, 1.5, 0, false, 1)
		end
		if spr:IsEventTriggered("Shut") then
			sfxManager:Play(SoundEffect.SOUND_DOOR_HEAVY_CLOSE, 1.5, 0, false, 1)
		end
		if spr:IsEventTriggered("Push") then
			sfxManager:Play(SoundEffect.SOUND_STATIC, 0.5, 0, false, 0.4)
			sfxManager:Play(Isaac.GetSoundIdByName("SamaelEndingSfx2"), 0.7, 0, false, 0.75)
		end
		if spr:IsEventTriggered("Grab") then
			sfxManager:Play(Isaac.GetSoundIdByName("SamaelEndingSfx1"), 0.7, 0, false, 0.5)
		end
		if spr:IsEventTriggered("Death") then
			sfxManager:Play(SoundEffect.SOUND_DEATH_CARD, 2, 0, false, 1.0)
		end
		if spr:IsEventTriggered("Portal") then
			sfxManager:Play(SoundEffect.SOUND_DOGMA_DEATH, 0.6, 0, false, 0.75)
		end
	end
end, EntityType.ENTITY_DOGMA)

for hook = InputHook.IS_ACTION_PRESSED, InputHook.IS_ACTION_TRIGGERED do
	mod:AddPriorityCallback(ModCallbacks.MC_INPUT_ACTION, CallbackPriority.EARLY, function(_, entity, hook, action)
		if PlayingSamaelEnding() and action ~= ButtonAction.ACTION_CONSOLE then
			return false
		end
	end, hook)
end

mod:AddPriorityCallback(ModCallbacks.MC_INPUT_ACTION, CallbackPriority.EARLY, function(_, entity, hook, action)
	if PlayingSamaelEnding() and action ~= ButtonAction.ACTION_CONSOLE then
		return 0
	end
end, InputHook.GET_ACTION_VALUE)
