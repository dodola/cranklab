-- Development fixture: proves callback ordering, input reset and save persistence.
local tone = assert(playdate.sound.sampleplayer.new("tone"))
tone:play(0)
local frames, paused, locked = 0, false, false
local previous = 0
if playdate.file.exists("checkpoint.txt") then
    local f = assert(playdate.file.open("checkpoint.txt", playdate.file.kFileRead))
    previous = tonumber(f:read(100)) or 0
    f:close()
end
local function snapshot(event)
    local text = string.format("%s,%d,%d,%.4f,%d,%.6f,%.6f,%d\n", event, frames,
        playdate.getButtonState(), playdate.getCrankPosition(),
        playdate.getCurrentTimeMilliseconds(), playdate.getElapsedTime(),
        tone:getOffset(), previous)
    local f = assert(playdate.file.open("lifecycle.txt", playdate.file.kFileWrite))
    assert(f:write(text) == #text)
    f:close()
    if event ~= "frame" then
        f = assert(playdate.file.open("events.txt", playdate.file.kFileAppend))
        assert(f:write(text) == #text)
        f:close()
    end
end
local function checkpoint()
    local f = assert(playdate.file.open("checkpoint.txt", playdate.file.kFileWrite))
    local text = tostring(previous + frames)
    assert(f:write(text) == #text)
    f:close()
end
function playdate.AButtonDown() snapshot("a-down") end
function playdate.AButtonUp() snapshot("a-up") end
function playdate.gameWillPause()
    assert(not paused, "duplicate pause")
    assert(playdate.getButtonState() == 0, "held button at pause")
    paused = true
    snapshot("pause")
end
function playdate.deviceWillLock()
    assert(paused and not locked, "invalid lock transition")
    locked = true
    checkpoint()
    snapshot("lock")
end
function playdate.deviceDidUnlock()
    assert(paused and locked, "invalid unlock transition")
    locked = false
    snapshot("unlock")
end
function playdate.gameWillResume()
    assert(paused and not locked, "invalid resume transition")
    assert(playdate.getButtonState() == 0, "held button after resume")
    paused = false
    snapshot("resume")
end
function playdate.gameWillTerminate()
    checkpoint()
    snapshot("terminate")
end
snapshot("boot")
function playdate.update()
    assert(not paused and not locked, "update while suspended")
    frames = frames + 1
    playdate.graphics.clear(playdate.graphics.kColorWhite)
    playdate.graphics.fillRect(15, 15, 40 + playdate.getButtonState(), 25)
    local rad = playdate.getCrankPosition() * math.pi / 180
    playdate.graphics.drawLine(200,120,200+70*math.cos(rad),120+70*math.sin(rad))
    snapshot("frame")
end
