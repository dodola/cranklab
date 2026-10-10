-- An ordinary Playdate Lua game: compile with the official SDK's pdc.
local gfx = playdate.graphics
local tone = playdate.sound.synth.new(playdate.sound.kWaveSquare)
local x, y, dx, dy = 200, 80, 2, 2
local paddle, score, paused = 200, 0, false
local save = playdate.datastore.read("score")
local best = save and save.best or 0

function playdate.AButtonDown()
    paused = not paused
    tone:playMIDINote(72, 0.3, 0.06)
end
function playdate.BButtonDown()
    x, y, dx, dy, score = 200, 80, 2, 2, 0
end
function playdate.cranked(change)
    paddle = math.max(28, math.min(372, paddle + change))
end
function playdate.gameWillTerminate()
    playdate.datastore.write({best=best}, "score")
end
function playdate.update()
    if playdate.buttonIsPressed(playdate.kButtonLeft) then paddle = math.max(28, paddle-4) end
    if playdate.buttonIsPressed(playdate.kButtonRight) then paddle = math.min(372, paddle+4) end
    if not paused then
        x, y = x+dx, y+dy
        if x < 7 or x > 393 then dx=-dx end
        if y < 28 then dy=math.abs(dy) end
        if y >= 206 and y <= 215 and math.abs(x-paddle) < 33 and dy > 0 then
            dy = -math.abs(dy); score=score+1; best=math.max(best,score)
            tone:playMIDINote(60+(score%12), 0.2, 0.04)
        end
        if y > 240 then x,y,score=200,80,0 end
    end
    gfx.clear(gfx.kColorWhite)
    gfx.setColor(gfx.kColorBlack)
    gfx.drawText("CRANK PADDLE  Score " .. score .. "  Best " .. best, 10, 6)
    gfx.fillCircleAtPoint(x, y, 6)
    gfx.fillRoundRect(paddle-28, 214, 56, 8, 4)
    if paused then gfx.drawText("A: resume   B: restart", 100, 100) end
end
