local value=abi.object.new()
assert(value.value=='owned value')
value:retain();value=nil
collectgarbage('collect');collectgarbage('collect');assert(abi.collected()==0)
abi.release();collectgarbage('collect');collectgarbage('collect');assert(abi.collected()==1)
for i=1,100 do local object=abi.object.new();assert(object.value=='owned value') end
collectgarbage('collect');collectgarbage('collect');assert(abi.collected()==101)
local nested=0
function abiNestedCallback(number)
    assert(number==2.5 and abi.echo(number)==3.75)
    nested=nested+1
end
assert(abi.nested()==17 and nested==1)
assert(abi.echo(1.25)==1.875)
assert(abi.binary('A\0BCD')=='A\0BCD')
local nativeBitmap=abi.bitmap()
collectgarbage('collect')
assert(nativeBitmap:getSize()==16)
local f=assert(playdate.file.open('mixed-ok',playdate.file.kFileWrite));f:write('done');f:close()
playdate.update=function()
    playdate.graphics.clear(playdate.graphics.kColorWhite)
    playdate.graphics.drawText('C / Lua ABI passed',30,80)
    nativeBitmap:draw(10,10)
end
