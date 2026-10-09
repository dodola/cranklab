-- Download/run adapter for the bundled PDGames build 18. No translation controls.
local host = assert(_crankLauncher)
local manager = assert(screenManager)
local detail = assert(manager.getScreen('gameDetail'))
local gfx = playdate.graphics
local old={}
for _,name in ipairs({'enter','leave','draw','AButtonDown','leftButtonDown','rightButtonDown'}) do
    old[name]=assert(detail[name])
end
local game, focus, previous, state=nil,1,nil,{}
local function up(fn,key)
    for i=1,80 do
        local name,value=debug.getupvalue(fn,i)
        if not name then break end
        if name==key then return value end
    end
end
local function fullscreen() return up(old.AButtonDown,'fullscreenMode') end
local function active()
    return game and tonumber(game.id) and manager.getCurrentName()=='gameDetail'
       and not fullscreen() and not manager.isTransitioning()
end
local function refresh()
    local value=host.status()
    if value~=previous then
        previous=value
        local ok,result=pcall(json.decode,value)
        state=ok and type(result)=='table' and result or {}
    end
end
function detail:enter(data)
    game=data and data.game;focus=1;previous=nil;state={}
    old.enter(self,data)
    if game and tonumber(game.id) then host.send('inspect',game.id) end
end
function detail:leave(...)
    host.send('leave',game and tonumber(game.id) or 0);game=nil
    return old.leave(self,...)
end
function cranklabLauncherDetails()
    if active() then focus=0;return old.AButtonDown(detail) end
end
function cranklabLauncherActivate()
    if not active() then return end
    refresh();focus=1
    if state.id~=game.id or state.state=='checking' then return end
    local action=state.busy and 'cancel' or state.state=='unavailable' and 'publisher' or 'play'
    if host.send(action,game.id) and action=='play' then
        local public={}
        for _,key in ipairs({'id','title','author','price','availability','images','source','categories','staff_pick'}) do public[key]=game[key] end
        playdate.datastore.write({game=public},'cranklab_launcher_return')
        state.busy=true;state.label='Preparing...'
    end
end
function detail:AButtonDown(...)
    if fullscreen() or focus==0 then return old.AButtonDown(self,...) end
    return cranklabLauncherActivate()
end
function detail:leftButtonDown(...)
    if fullscreen() then return old.leftButtonDown(self,...) end
    focus=0
end
function detail:rightButtonDown(...)
    if fullscreen() then return old.rightButtonDown(self,...) end
    focus=1
end
function detail:draw(...)
    refresh();old.draw(self,...)
    if fullscreen() then return end
    gfx.pushContext();gfx.setDrawOffset(0,0);gfx.clearClipRect()
    gfx.setColor(gfx.kColorWhite);gfx.fillRect(0,188,400,52)
    gfx.setColor(gfx.kColorBlack);gfx.drawLine(0,188,400,188)
    gfx.setClipRect(4,190,392,18)
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    local message=state.id==game.id and state.message or 'Checking game source...'
    if state.busy and state.received and state.received>0 then
        local progress=state.total and state.total>0 and string.format(' %d%%',math.min(99,100*state.received//state.total)) or string.format(' %d KB',state.received//1024)
        message=(message or 'Downloading...')..progress
    end
    gfx.drawText(message or '',4,190);gfx.clearClipRect()
    local function button(index,x,width,label)
        if focus==index then gfx.fillRoundRect(x,212,width,25,4);gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
        else gfx.drawRoundRect(x,212,width,25,4);gfx.setImageDrawMode(gfx.kDrawModeCopy) end
        gfx.setClipRect(x+4,214,width-8,22)
        gfx.drawText(label,x+math.max(4,(width-gfx.getTextWidth(label))//2),216)
        gfx.clearClipRect()
    end
    button(0,4,124,'Details')
    local label=game and state.id==game.id and state.label or 'Checking...'
    button(1,136,260,state.busy and 'Cancel download' or label or 'Download & Play')
    gfx.popContext()
end
local restore=playdate.datastore.read('cranklab_launcher_return')
local update=playdate.update
playdate.update=function(...)
    local result=update(...)
    if restore and restore.game and manager.getCurrentName()=='gameList' and not manager.isTransitioning() then
        local data=restore;restore=nil;manager.switch('gameDetail',data,'left')
    end
    return result
end
