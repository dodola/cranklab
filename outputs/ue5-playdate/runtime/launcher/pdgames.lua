-- Host-owned adapter for PDGames 1.0.0 build 18. Original PDX stays intact.
local host = assert(_crankLauncher)
local manager = assert(screenManager)
local detail = assert(manager.getScreen('gameDetail'))
local gfx = playdate.graphics
local original = {}
for _,name in ipairs({'enter','leave','draw','AButtonDown','upButtonDown','downButtonDown','leftButtonDown','rightButtonDown'}) do
    original[name]=assert(detail[name], 'PDGames adapter: missing '..name)
end
local chineseFont=assert(gfx.font.new('fonts/cranklab-zh'), 'PDGames adapter: missing Chinese font')
local game, focus, lastStatus, baseline = nil,2,nil,nil
local state, wantChinese, applied = {},false,nil
local function up(fn,key,value,set)
    for i=1,80 do
        local name,current=debug.getupvalue(fn,i)
        if not name then break end
        if name==key then
            if set then debug.setupvalue(fn,i,value) end
            return current
        end
    end
end
local function viewingImage() return up(original.AButtonDown,'fullscreenMode') end
local function active()
    return game and tonumber(game.id) and manager.getCurrentName()=='gameDetail' and not viewingImage() and not manager.isTransitioning()
end
local function wrapChinese(text)
    local lines,line={},''
    local closing='，。！？；：、）》】」』％…,.!?;:)]}'
    local opening='（《【「『(['
    for paragraph in (text..'\n'):gmatch('(.-)\n') do
        line='';local units,word={},''
        local function flush() if #word>0 then units[#units+1]=word;word='' end end
        for _,cp in utf8.codes(paragraph) do
            local ch=utf8.char(cp)
            if cp<128 and ch:match('[%w_%-]') then word=word..ch
            else flush();if cp~=0xfe0f then units[#units+1]=chineseFont:getTextWidth(ch)>0 and ch or '�' end end
        end
        flush()
        for _,unit in ipairs(units) do
            if chineseFont:getTextWidth(unit)>384 then
                -- Break oversized URLs/words without cutting a UTF-8 character.
                for _,cp in utf8.codes(unit) do
                    local ch=utf8.char(cp)
                    if chineseFont:getTextWidth(line..ch)>384 then lines[#lines+1]=line;line='' end
                    line=line..ch
                end
            else
                if #line>0 and chineseFont:getTextWidth(line..unit)>384 then
                    local at=utf8.offset(line,-1);local last=line:sub(at)
                    if closing:find(unit,1,true) or opening:find(last,1,true) then
                        lines[#lines+1]=line:sub(1,at-1);line=last
                    else lines[#lines+1]=line;line='' end
                end
                if line~='' or unit~=' ' then line=line..unit end
            end
        end
        lines[#lines+1]=line
    end
    return table.concat(lines,'\n'),#lines*(chineseFont:getHeight()+2)
end
local function applyDescription(text)
    local full=up(original.enter,'fullDetail')
    if not full or not game or full.id~=game.id then return end
    local current=up(original.enter,'cachedDescText')
    if not baseline or (current~=baseline.text and (not applied or current~=applied.text)) then
        baseline={text=current,height=up(original.enter,'cachedDescHeight')};applied=nil
    end
    if not baseline or not baseline.text then return end
    if text and applied and applied.source==text and current==applied.text then return end
    if not text and not applied then return end
    local wrapped,height=baseline.text,baseline.height
    if text then wrapped,height=wrapChinese(text) end
    local oldHeight=up(original.enter,'contentImageHeight') or 0
    up(original.enter,'cachedDescText',wrapped,true)
    up(original.enter,'cachedDescHeight',height,true)
    local render=assert(up(original.enter,'renderContentImage'))
    local drawText,drawInRect=gfx.drawText,gfx.drawTextInRect
    local function translatedDraw(value,x,y,...)
        local font=gfx.getFont();gfx.setFont(chineseFont)
        drawText(value,x,y);gfx.setFont(font)
        return false,384,height
    end
    gfx.drawText=function(value,...)
        if value==wrapped and text then return translatedDraw(value,...) end
        return drawText(value,...)
    end
    gfx.drawTextInRect=function(value,...)
        if value==wrapped and text then return translatedDraw(value,...) end
        return drawInRect(value,...)
    end
    local ok,err=pcall(render)
    gfx.drawText=drawText;gfx.drawTextInRect=drawInRect
    if not ok then error(err) end
    local newHeight=up(original.enter,'contentImageHeight') or oldHeight
    local max=math.max(0,(up(original.enter,'maxScrollY') or 0)+newHeight-oldHeight)
    up(original.enter,'maxScrollY',max,true)
    for _,name in ipairs({'scrollY','targetScrollY'}) do up(original.enter,name,math.min(max,up(original.enter,name) or 0),true) end
    applied=text and {text=wrapped,source=text} or nil
    host.send(text and 'lang_zh' or 'lang_en',game.id)
end
local function refresh()
    local value=host.status()
    if value~=lastStatus then
        lastStatus=value
        local ok,parsed=pcall(json.decode,value)
        state=ok and type(parsed)=='table' and parsed or {}
    end
    local t=state.translation
    if wantChinese and game and t and t.id==game.id and t.text and #t.text>0 then applyDescription(t.text)
    elseif not wantChinese or (applied and t and game and t.id==game.id and (not t.text or #t.text==0)) then applyDescription(nil) end
end
function detail:enter(data)
    game=data and data.game;focus=2;state={};lastStatus=nil;baseline=nil;applied=nil;wantChinese=false
    original.enter(self,data)
    if game and tonumber(game.id) then host.send('inspect',game.id) end
end
function detail:leave(...)
    host.send('leave',game and tonumber(game.id) or 0)
    game=nil;baseline=nil;applied=nil;wantChinese=false
    return original.leave(self,...)
end
function cranklabLauncherTranslate()
    if not active() then return end
    refresh();focus=1
    local t=state.translation or {}
    if wantChinese and applied then wantChinese=false;host.send('original',game.id);applyDescription(nil)
    elseif t.id==game.id and t.modelBusy then host.send('model_cancel',game.id);wantChinese=false
    elseif wantChinese and t.id==game.id and (t.state=='loading' or t.state=='translating') then wantChinese=false;host.send('trans_cancel',game.id)
    elseif wantChinese and t.id==game.id and not t.modelReady then host.send('model',game.id)
    else wantChinese=true;host.send('translate',game.id) end
end
function cranklabLauncherDetails()
    if not active() then return end
    focus=0;return original.AButtonDown(detail)
end
function cranklabLauncherActivate()
    if not active() then return end
    refresh();focus=2
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
    if viewingImage() then return original.AButtonDown(self,...) end
    if focus==1 then return cranklabLauncherTranslate() end
    if focus==2 then return cranklabLauncherActivate() end
    return original.AButtonDown(self,...)
end
function detail:leftButtonDown(...)
    if not viewingImage() then focus=math.max(0,focus-1);return end
    return original.leftButtonDown(self,...)
end
function detail:rightButtonDown(...)
    if not viewingImage() then focus=math.min(2,focus+1);return end
    return original.rightButtonDown(self,...)
end
function detail:upButtonDown(...) return original.upButtonDown(self,...) end
function detail:downButtonDown(...) return original.downButtonDown(self,...) end
local function languageLabel()
    local t=state.translation or {}
    if t.id~= (game and game.id) then return '中文' end
    if t.modelBusy then return t.modelProgress and ('下载%d%%'):format(t.modelProgress*100) or '校验中' end
    if wantChinese and applied then return '原文' end
    if wantChinese then
        if t.state=='loading' then return '读取中' end
        if t.state=='translating' then return '翻译中' end
        if t.modelError then return '重试下载' end
        if t.state=='error' then return '重试翻译' end
        if t.state=='empty' then return '暂无介绍' end
        if not t.modelReady then return t.modelError and '重试下载' or (t.modelDownloadLabel or '下载440M') end
    end
    return '中文'
end
function detail:draw(...)
    refresh();original.draw(self,...)
    if viewingImage() then return end
    gfx.pushContext();gfx.setDrawOffset(0,0);gfx.clearClipRect()
    gfx.setColor(gfx.kColorWhite);gfx.fillRect(0,208,400,32)
    gfx.setColor(gfx.kColorBlack);gfx.drawLine(0,208,400,208)
    local function button(index,x,width,label,font)
        if focus==index then gfx.fillRoundRect(x,212,width,25,4);gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
        else gfx.drawRoundRect(x,212,width,25,4);gfx.setImageDrawMode(gfx.kDrawModeCopy) end
        local old=gfx.getFont();if font then gfx.setFont(font) end
        gfx.setClipRect(x+4,214,width-8,22)
        gfx.drawText(label,x+math.max(4,(width-gfx.getTextWidth(label))//2),font and 215 or 216)
        gfx.clearClipRect();gfx.setFont(old)
    end
    button(0,4,92,'Details')
    button(1,100,112,languageLabel(),chineseFont)
    local label=game and state.id==game.id and state.label or 'Checking...'
    if state.busy then label='Cancel download' end
    button(2,220,176,label or 'Download & Play')
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
