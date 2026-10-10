-- A regular PDX exercising persistence and protected error recovery on the MCU.
local gfx = playdate.graphics
local saved = playdate.datastore.read("check") or {launches=0}
saved.launches = saved.launches + 1
playdate.datastore.write(saved, "check")
assert(playdate.datastore.read("check").launches == saved.launches)
local file = assert(playdate.file.open("check.txt", playdate.file.kFileWrite))
assert(file:write("Playdate file roundtrip") == 23)
file:flush()
file:close()
file = assert(playdate.file.open("check.txt", playdate.file.kFileRead))
assert(file:read(23) == "Playdate file roundtrip")
assert(not pcall(function() file:read(-1) end))
file:close()
assert(not pcall(function() file:read(1) end))
print("CHECK SAVE " .. saved.launches)
function playdate.BButtonDown()
    error("Intentional runtime-check input failure")
end
function playdate.update()
    gfx.clear(gfx.kColorWhite)
    gfx.drawText("SAVE " .. saved.launches .. " / FILE IO OK", 35, 90)
    gfx.drawText("B: test game error recovery", 35, 115)
end
