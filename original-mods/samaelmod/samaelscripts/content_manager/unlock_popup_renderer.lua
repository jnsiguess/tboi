return function(ContentManager)
----------------------------------------------------------------------------------------------------

-- Based on Xalum's GiantBook/Achievement renderer, which was made for FiendFolio (Thanks!)

local mod = ContentManager.Mod
local game = ContentManager.Game
local lib = ContentManager.Lib
local sfx = SFXManager()

local function GetScreenBottomRight()
	return game:GetRoom():GetRenderSurfaceTopLeft() * 2 + Vector(442,286)
end

local function GetScreenCenterPosition()
	return GetScreenBottomRight() / 2
end

----------------------------------------------------------------------------------------------------
-- Pausing

local paused
local pausedAt
local pauseDuration = 0
local forceUnpause

local function PauseGame(frames, force)
	if game:GetRoom():GetBossID() ~= 54 or force then -- Intentionally fail achievement note pauses on Lamb, since it breaks the Victory Lap menu super hard
		for _, projectile in pairs(Isaac.FindByType(9)) do
			projectile:Remove()
			
			local poof = Isaac.Spawn(1000, 15, 0, projectile.Position, Vector.Zero, nil)
			poof.SpriteScale = Vector.One * 0.75
		end
		
		pausedAt = pausedAt or game:GetFrameCount()
		pauseDuration = pauseDuration + frames
		paused = true
		
		Isaac.GetPlayer():UseActiveItem(CollectibleType.COLLECTIBLE_PAUSE, UseFlag.USE_NOANIM)
	end
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if pausedAt and pausedAt + pauseDuration < game:GetFrameCount() then
		paused = false
		pausedAt = nil
		pauseDuration = 0
		
		forceUnpause = true
	end
end)

for hook = InputHook.IS_ACTION_PRESSED, InputHook.IS_ACTION_TRIGGERED do
	mod:AddPriorityCallback(ModCallbacks.MC_INPUT_ACTION, CallbackPriority.EARLY, function(_, entity, hook, action)
		if paused and action ~= ButtonAction.ACTION_CONSOLE then
			return false
		end
	end, hook)
end

mod:AddPriorityCallback(ModCallbacks.MC_INPUT_ACTION, CallbackPriority.EARLY, function(_, entity, hook, action)
	if paused and action ~= ButtonAction.ACTION_CONSOLE then
		return 0
	elseif forceUnpause and action == ButtonAction.ACTION_SHOOTDOWN then
		if entity:ToPlayer() and not entity:ToPlayer().ControlsEnabled then
			-- If the player has controls disabled we can't get the game to unpause.
			-- But we can't do it this MC_INPUT_ACTION anyway, so enable controls and wait until the next run.
			entity:ToPlayer().ControlsEnabled = true
			return
		end
		forceUnpause = false
		return 0.1
	end
end, InputHook.GET_ACTION_VALUE)

----------------------------------------------------------------------------------------------------
-- Popup Rendering

local popupSprite = Sprite()
popupSprite:Load("gfx/ui/contentmanager/achievement.anm2", true)

local updateSprite = false
local renderingPopup = false
local popupIdleTime = 0
local waitFrames = 0

local unlockQueue = {}
function ContentManager:QueueUnlockPopup(gfx)
	table.insert(unlockQueue, gfx)
end

local function PlayUnlockPopup(gfx)
	if Options.DisplayPopups then
		PauseGame(41)
		
		popupSprite:ReplaceSpritesheet(2, gfx)
		popupSprite:LoadGraphics()
		popupSprite:Play("Appear", true)
		
		updateSprite = false
		renderingPopup = true
		
		sfx:Play(SoundEffect.SOUND_CHOIR_UNLOCK)
	end
end

function ContentManager:HasQueuedUnlockPopup()
	return #unlockQueue > 0
end

function ContentManager:IsPlayingUnlockPopup()
	return renderingPopup
end

function ContentManager:UnlockPopupPlayingOrQueued()
	return ContentManager:IsPlayingUnlockPopup() or ContentManager:HasQueuedUnlockPopup()
end

local function ShouldWaitForFf()
	return FiendFolio and FiendFolio.IsPlayingAchievementNote()
end

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if not renderingPopup and #unlockQueue > 0 and (not DeadSeaScrollsMenu or not DeadSeaScrollsMenu.IsOpen()) and waitFrames <= 0 and not ShouldWaitForFf() then
		PlayUnlockPopup(unlockQueue[1])
		table.remove(unlockQueue, 1)
	end
	
	if renderingPopup then
		game:ShakeScreen(0)
	end
end)

local function RenderPopup()
	if renderingPopup then
		if waitFrames > 0 or ShouldWaitForFf() then
			popupSprite:SetFrame(0)
		else
			if updateSprite then
				popupSprite:Update()
			end
			updateSprite = not updateSprite
		end
		
		local position = GetScreenCenterPosition()
		popupSprite:Render(position, Vector.Zero, Vector.Zero)
		
		if popupSprite:IsFinished("Appear") then
			renderingPopup = false
		end
	end
end

local lastRender = 0

mod:AddCallback(ModCallbacks.MC_GET_SHADER_PARAMS, function(_, shaderName)
	local currentFrame = Isaac.GetFrameCount()
	
	if currentFrame > lastRender and shaderName == ContentManager.Shader then
		if waitFrames > 0 and not ShouldWaitForFf() then
			waitFrames = waitFrames - 1
		end
		RenderPopup()
		lastRender = currentFrame
	end
end)

-- Just in case
local function Reset()
	paused = nil
	pausedAt = nil
	pauseDuration = 0
	forceUnpause = nil
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_END, Reset)
mod:AddCallback(ModCallbacks.MC_PRE_GAME_EXIT, Reset)

-- Don't look
local sorryCucco = nil
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
	if not sorryCucco and CCO and CCO.AchievementDisplayAPI and CCO.AchievementDisplayAPI.PlayAchievement then
		sorryCucco = CCO.AchievementDisplayAPI.PlayAchievement
		CCO.AchievementDisplayAPI.PlayAchievement = function(gfxroot, duration)
			waitFrames = waitFrames + (duration or 90) + 50
			sorryCucco(gfxroot, duration)
		end
	end
end)

function mod:PopupTest()
	mod.ContentManager:QueueUnlockPopup("gfx/ui/samael achievements/tainted_samael.png")
	--[[if CCO and CCO.AchievementDisplayAPI then
		CCO.AchievementDisplayAPI.PlayAchievement("gfx/ui/job achievements/achievement.soul_of_job.png")
	end
	if FiendFolio then
		FiendFolio.QueueAchievementNote("gfx/ui/achievement/achievement_fiend.png")
	end]]
end

----------------------------------------------------------------------------------------------------
end
