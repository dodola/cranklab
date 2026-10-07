auto spriteFunction=[&](const char *method){function((std::string("playdate.graphics.sprite.")+method).c_str());};
if(name=="sprite.newSprite") {spriteFunction("new");call(0,1);result(hold(-1,"sprite"));return;}
if(name=="sprite.copy") {
    auto p=a.word();auto original=resource(p,"sprite");spriteFunction("copy");push(p,"sprite");call(1,1);auto copy=hold(-1,"sprite");
    resource(copy).userdata=original.userdata;resource(copy).callbacks=original.callbacks;
    const char *methods[]={"setUpdateFunction","setDrawFunction","setCollisionResponseFunction"};
    for(int i=0;i<3;i++)if(original.callbacks[i]){spriteFunction(methods[i]);push(copy);lua_pushlightuserdata(L,this);lua_pushinteger(L,copy);lua_pushinteger(L,original.callbacks[i]);lua_pushinteger(L,i+1);lua_pushcclosure(L,spriteCallback,4);call(2,0);}
    result(copy);return;
}
if(name=="sprite.removeAllSprites" || name=="sprite.getSpriteCount" || name=="sprite.updateAndDrawSprites" || name=="sprite.drawSprites") {
    const char *method=name=="sprite.removeAllSprites"?"removeAll":name=="sprite.getSpriteCount"?"getSpriteCount":name=="sprite.drawSprites"?"drawSprites":"updateAndDrawSprites";
    spriteFunction(method);call(0,name=="sprite.getSpriteCount"?1:0);if(name=="sprite.getSpriteCount")result((uint32_t)lua_tointeger(L,-1));return;
}
if(name=="sprite.freeSprite" || name=="sprite.addSprite" || name=="sprite.removeSprite" || name=="sprite.markDirty" || name=="sprite.clearCollideRect" || name=="sprite.clearClipRect" || name=="sprite.clearStencil") {
    auto p=a.word();std::string method=name=="sprite.freeSprite"?"free":name=="sprite.addSprite"?"add":name=="sprite.removeSprite"?"remove":name=="sprite.markDirty"?"markDirty":name.substr(7);
    if(name=="sprite.freeSprite"){spriteFunction("remove");push(p,"sprite");call(1,0);push(p,"sprite");pd_sprite_native_release(L,-1);drop(p);return;}
    spriteFunction(method.c_str());push(p,"sprite");if(name=="sprite.markDirty")lua_pushboolean(L,1);call(name=="sprite.markDirty"?2:1,0);return;
}
if(name=="sprite.removeSprites") {auto list=a.word();int count=a.integer();if(count<0 || count>65536)throw NativeError("Invalid sprite list length");for(int i=0;i<count;i++){spriteFunction("remove");push(read<uint32_t>(list+i*4),"sprite");call(1,0);}return;}
if(name=="sprite.setUserdata") {auto p=a.word(),ud=a.word();resource(p,"sprite").userdata=ud;return;}
if(name=="sprite.getUserdata") {result(resource(a.word(),"sprite").userdata);return;}
if(name=="sprite.setBounds" || name=="sprite.setCollideRect") {auto p=a.word();spriteFunction(name.substr(7).c_str());push(p,"sprite");for(int i=0;i<4;i++)pushNum(a.number());call(5,0);return;}
if(name=="sprite.getBounds" || name=="sprite.getCollideRect") {
    auto p=a.word();spriteFunction(name.substr(7).c_str());push(p,"sprite");call(1,name=="sprite.getBounds"?4:1);
    float rect[4];if(name=="sprite.getBounds")for(int i=0;i<4;i++)rect[i]=(float)lua_tonumber(L,i-4);
    else {const char *fields[]={"x","y","width","height"};for(int i=0;i<4;i++){lua_getfield(L,-1,fields[i]);rect[i]=(float)lua_tonumber(L,-1);lua_pop(L,1);}}
    for(int i=0;i<4;i++)reg(UC_ARM_REG_S0+i,floatBits(rect[i]));return;
}
if(name=="sprite.moveTo" || name=="sprite.moveBy" || name=="sprite.setSize" || name=="sprite.setCenter") {auto p=a.word();spriteFunction(name.substr(7).c_str());push(p,"sprite");pushNum(a.number());pushNum(a.number());call(3,0);return;}
if(name=="sprite.getPosition" || name=="sprite.getCenter") {auto p=a.word(),x=a.word(),y=a.word();spriteFunction(name.substr(7).c_str());push(p,"sprite");call(1,2);write<float>(x,(float)lua_tonumber(L,-2));write<float>(y,(float)lua_tonumber(L,-1));return;}
if(name=="sprite.setImage") {auto p=a.word(),image=a.word();int flip=a.integer();spriteFunction("setImage");push(p,"sprite");push(image,"bitmap");pushInt(flip);call(3,0);return;}
if(name=="sprite.getImage") {auto p=a.word();spriteFunction("getImage");push(p,"sprite");call(1,1);result(hold(-1,"bitmap"));return;}
if(name=="sprite.setUpdateFunction" || name=="sprite.setDrawFunction" || name=="sprite.setCollisionResponseFunction") {
    auto p=a.word(),callback=a.word();int kind=name=="sprite.setUpdateFunction"?1:name=="sprite.setDrawFunction"?2:3;
    resource(p,"sprite").callbacks[kind-1]=callback;
    spriteFunction(name.substr(7).c_str());push(p,"sprite");
    if(callback){lua_pushlightuserdata(L,this);lua_pushinteger(L,p);lua_pushinteger(L,callback);lua_pushinteger(L,kind);lua_pushcclosure(L,spriteCallback,4);}else lua_pushnil(L);
    call(2,0);return;
}
if(name=="sprite.setZIndex" || name=="sprite.setImageFlip" || name=="sprite.setDrawMode" || name=="sprite.setTag") {
    auto p=a.word();int v=a.integer();if(name=="sprite.setZIndex")v=(int16_t)v;if(name=="sprite.setTag")v=(uint8_t)v;
    spriteFunction(name=="sprite.setDrawMode"?"setImageDrawMode":name.substr(7).c_str());push(p,"sprite");pushInt(v);call(2,0);return;
}
if(name=="sprite.getZIndex" || name=="sprite.getImageFlip" || name=="sprite.getTag" || name=="sprite.isVisible" || name=="sprite.updatesEnabled") {
    auto p=a.word();spriteFunction(name.substr(7).c_str());push(p,"sprite");call(1,1);result(lua_isboolean(L,-1)?lua_toboolean(L,-1):(uint32_t)lua_tointeger(L,-1));return;
}
if(name=="sprite.setUpdatesEnabled" || name=="sprite.setCollisionsEnabled" || name=="sprite.setVisible" || name=="sprite.setOpaque" || name=="sprite.setIgnoresDrawOffset") {auto p=a.word();int flag=a.integer();spriteFunction(name.substr(7).c_str());push(p,"sprite");lua_pushboolean(L,flag!=0);call(2,0);return;}
if(name=="sprite.setClipRect") {auto p=a.word();int left=a.integer(),top=a.integer(),right=a.integer(),bottom=a.integer();spriteFunction("setClipRect");push(p,"sprite");pushInt(left);pushInt(top);pushInt(right-left);pushInt(bottom-top);call(5,0);return;}
if(name=="sprite.setStencil" || name=="sprite.setStencilImage") {auto p=a.word(),image=a.word();int tile=name=="sprite.setStencilImage"?a.integer():0;spriteFunction(image?"setStencilImage":"clearStencil");push(p,"sprite");if(image){push(image,"bitmap");lua_pushboolean(L,tile!=0);}call(image?3:1,0);return;}
if(name=="sprite.moveWithCollisions" || name=="sprite.checkCollisions") {
    auto p=a.word();float x=a.number(),y=a.number();auto outX=a.word(),outY=a.word(),length=a.word();
    spriteFunction(name.substr(7).c_str());push(p,"sprite");pushNum(x);pushNum(y);call(3,4);
    write<float>(outX,(float)lua_tonumber(L,-4));write<float>(outY,(float)lua_tonumber(L,-3));int count=(int)lua_tointeger(L,-1);int collisions=lua_absindex(L,-2);
    // ARM EABI uses the smallest fitting enum: responseType is one byte.
    constexpr uint32_t collisionSize=72;
    if(count<0 || count>65536)throw NativeError("Invalid collision list length");write<int>(length,count);uint32_t data=count?allocate(count*collisionSize):0;
    auto field=[&](int item,const char *key){lua_getfield(L,item,key);};
    auto numField=[&](int item,const char *key){field(item,key);float v=(float)lua_tonumber(L,-1);lua_pop(L,1);return v;};
    for(int i=0;i<count;i++) {
        lua_rawgeti(L,collisions,i+1);int item=lua_gettop(L);uint32_t dest=data+i*collisionSize;
        field(item,"sprite");write<uint32_t>(dest,hold(-1,"sprite"));lua_pop(L,1);
        field(item,"other");write<uint32_t>(dest+4,hold(-1,"sprite"));lua_pop(L,1);
        field(item,"type");write<uint8_t>(dest+8,(uint8_t)lua_tointeger(L,-1));lua_pop(L,1);
        field(item,"overlaps");write<uint8_t>(dest+9,(uint8_t)lua_toboolean(L,-1));lua_pop(L,1);
        write<float>(dest+12,numField(item,"ti"));
        const char *keys[]={"move","normal","touch","spriteRect","otherRect"};int offsets[]={16,24,32,40,56};
        for(int k=0;k<5;k++){field(item,keys[k]);int nested=lua_gettop(L);const char *coordinates[]={"x","y","width","height"};for(int j=0;j<(k<3?2:4);j++){float v=numField(nested,coordinates[j]);if(k==1)write<int>(dest+offsets[k]+j*4,(int)v);else write<float>(dest+offsets[k]+j*4,v);}lua_pop(L,1);}
        lua_pop(L,1);
    }
    result(data);return;
}
if(name=="sprite.querySpritesAtPoint" || name=="sprite.querySpritesInRect" || name=="sprite.overlappingSprites") {
    int args=0;spriteFunction(name.substr(7).c_str());
    if(name=="sprite.overlappingSprites"){push(a.word(),"sprite");args=1;}else {args=name=="sprite.querySpritesAtPoint"?2:4;for(int i=0;i<args;i++)pushNum(a.number());}
    auto length=a.word();call(args,1);int count=(int)lua_rawlen(L,-1);uint32_t data=count?allocate(count*4):0;write<int>(length,count);
    for(int i=0;i<count;i++){lua_rawgeti(L,-1,i+1);write<uint32_t>(data+i*4,hold(-1,"sprite"));lua_pop(L,1);}result(data);return;
}
// Redraw is already conservative in this backend; every sprite pass covers
// current/vacated bounds. These hints require no extra native state.
if(name=="sprite.setAlwaysRedraw" || name=="sprite.addDirtyRect" || name=="sprite.resetCollisionWorld")return;
throw NativeError("Unimplemented C API: "+name);
