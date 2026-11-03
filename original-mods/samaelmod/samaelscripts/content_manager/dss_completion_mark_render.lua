return function(ContentManager)
----------------------------------------------------------------------------------------------------

local vanillaMarkSprite = Sprite()
vanillaMarkSprite:Load("gfx/ui/completion_widget.anm2")
vanillaMarkSprite:SetFrame("Idle", 0)
--[[for i=0, 9 do
	vanillaMarkSprite:ReplaceSpritesheet(i,"gfx/ui/completion_widget_pause.png")
end
vanillaMarkSprite:LoadGraphics()]]
vanillaMarkSprite:Stop()

local customMarkSprite
customMarkSprite = Sprite()
customMarkSprite:Load("gfx/ui/pause screen completion marks/custom_marks_sheet.anm2", true)
customMarkSprite:ReplaceSpritesheet(0,"gfx/ui/pause screen completion marks/custom_marks_sheet_dss.png")
customMarkSprite:LoadGraphics()
customMarkSprite:Stop()

local function RenderCustomSheetPiece(pos, anim, frame, row, col, mark)
	customMarkSprite:SetFrame(anim, frame)
	local rot = customMarkSprite.Rotation
	local scale = customMarkSprite.Scale
	local offset = Vector(col * 18 * scale.X, row * 18 * scale.Y):Rotated(rot)
	customMarkSprite.Offset = offset
	customMarkSprite:Render(pos, Vector.Zero, Vector.Zero)
	if mark then
		local sprite = mark.Sprite
		if mark.Animation then
			sprite:Play(mark.Animation, true)
		end
		if mark.Frame then
			sprite:SetFrame(mark.Frame)
		end
		sprite.Rotation = rot
		sprite.Scale = scale
		sprite.Offset = offset + Vector(1,1)
		sprite:Render(pos, Vector.Zero, Vector.Zero)
	end
end

local kMaxCustomMarkPageWidth = 2

local function RenderCustomMarks(marks, pos)
	local numMarks = 0
	for k, v in pairs(marks) do
		numMarks = numMarks + 1
	end
	
	if numMarks == 0 then return end
	
	local numRows = math.ceil(numMarks / kMaxCustomMarkPageWidth)
	local numColumns = math.min(numMarks, kMaxCustomMarkPageWidth)
	
	local markIterator = next(marks)
	
	local finished = false
	
	for row=0, numRows+1 do
		if not markIterator then
			finished = true
		end
		
		-- Render pieces of the mod marks sheet, and the marks themselves.
		for col=0, numColumns+1 do
			local anim
			local frame = 0
			local mark
			if row == 0 then
				if col == 0 then
					anim = "TopLeft"
				elseif col == numColumns+1 then
					anim = "TopRight"
				else
					anim = "Top"
					frame = (col-1) % 3
				end
			elseif row == numRows+1 or finished then
				if col == 0 then
					anim = "BottomLeft"
				elseif col == numColumns+1 then
					anim = "BottomRight"
				else
					anim = "Bottom"
					frame = (col-1) % 3
				end
			elseif col == 0 then
				anim = "Left"
				frame = (row-1) % 3
			elseif col == numColumns+1 then
				anim = "Right"
				frame = (row-1) % 3
			else
				anim = "Middle"
				if not needToWriteModName and markIterator and row > 0 then
					mark = marks[markIterator]
					markIterator = next(marks, markIterator)
				end
			end
			RenderCustomSheetPiece(pos, anim, frame, row, col, mark)
		end
		
		if finished then
			break
		end
		
		row = row + 1
	end
	
	-- Pin
	customMarkSprite:SetFrame("Pin", 0)
	customMarkSprite.Offset = Vector(18 * (numColumns+2) * 0.5 - 9, 0):Rotated(customMarkSprite.Rotation)
	customMarkSprite:Render(pos, Vector.Zero, Vector.Zero)
end

function ContentManager.RenderCompletionNotes(pos, markData, isTainted)
	local vanillaPos = pos + Vector(-30, -20)
	local customPos = pos + Vector(0, 15)
	
	if type(isTainted) == "string" then
		-- This is encyclopedia.
		isTainted = isTainted == 'b'
		vanillaPos = vanillaPos + Vector(3, -7)
		customPos = customPos + Vector(12, -21)
	end
	
	for layer, data in pairs(markData.Vanilla) do
		local frame = 0
		if data.Hard then
			frame = 2
		elseif data.Unlock then
			frame = 1
		end
		if layer == 0 and isTainted and frame < 3 then
			frame = frame + 3
		end
		vanillaMarkSprite:SetLayerFrame(layer, frame)
	end
	
	vanillaMarkSprite:Render(vanillaPos, Vector.Zero, Vector.Zero)
	RenderCustomMarks(markData.Custom, customPos)
end

----------------------------------------------------------------------------------------------------
end