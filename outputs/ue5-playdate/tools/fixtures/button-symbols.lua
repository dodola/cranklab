local g=playdate.graphics
local symbols=utf8.char(0x24b6)..utf8.char(0x24b7)
assert(g.getTextWidth(symbols)==28, 'Button symbols need Unicode advances')
function playdate.update()
    g.clear(g.kColorWhite)
    g.setImageDrawMode(g.kDrawModeCopy)
    g.drawText(symbols,10,10)
    g.setColor(g.kColorBlack)
    g.fillRect(50,10,28,14)
    g.setImageDrawMode(g.kDrawModeFillWhite)
    g.drawText(symbols,50,10)
    g.setImageDrawMode(g.kDrawModeCopy)
end
