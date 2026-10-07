local menu=playdate.getSystemMenu()
local events={}
local function event(text)
    events[#events+1]=text
    local f=assert(playdate.file.open('events.txt',playdate.file.kFileWrite))
    f:write(table.concat(events,'\n')); f:close()
end
local action=assert(menu:addMenuItem('重置',function()
    event('action')
    menu:removeAllMenuItems()
    assert(menu:addMenuItem('Replacement',function() event('replacement') end))
end))
local check=assert(menu:addCheckmarkMenuItem('Sound',function(v) event('check:'..tostring(v)) end))
local options=assert(menu:addOptionsMenuItem('Mode',{'A','B','C'},'B',function(v) event('option:'..v) end))
assert(#menu:getMenuItems()==3)
local fourth,err=menu:addMenuItem('Overflow',function() end)
assert(fourth==nil and type(err)=='string')
assert(check.value==false and options.value=='B')
check.value=1; assert(check:getValue()==true); check:setValue(false)
options.value=3; assert(options:getValue()=='C'); options:setValue('B')
action.title='开始'; assert(action:getTitle()=='开始')
action:setTitle('重置')
local image=playdate.graphics.image.new(400,240,playdate.graphics.kColorWhite)
playdate.graphics.pushContext(image)
playdate.graphics.fillRect(0,0,100,240)
playdate.graphics.popContext()
playdate.setMenuImage(image,25)
local frames=0
function playdate.update()
    frames=frames+1
    playdate.graphics.clear()
    playdate.graphics.fillRect(frames%390,30,10,10)
end
function playdate.gameWillPause() event('pause') end
function playdate.gameWillResume() event('resume') end
function playdate.deviceWillLock() event('lock') end
function playdate.deviceDidUnlock() event('unlock') end
function playdate.gameWillTerminate() event('terminate') end
event('boot')
