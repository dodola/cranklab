#include "pd_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* All Lua registry objects below are owned exclusively by the game thread. */
#define ITEMS "crank.sdkmenu.items"
#define MENU "crank.sdkmenu.object"
#define IMAGE "crank.sdkmenu.image"
#define OFFSET "crank.sdkmenu.offset"
#define META "crank.sdkmenu.item"
static uint32_t next_id = 0; /* Do not recycle IDs across game sessions. */

static int field_int(lua_State *L, int index, const char *key) {
    lua_getfield(L,index,key); int value=(int)lua_tointeger(L,-1); lua_pop(L,1); return value;
}
static void int_field(lua_State *L, int index, const char *key, int value) {
    index=lua_absindex(L,index); lua_pushinteger(L,value); lua_setfield(L,index,key);
}
static int get_title(lua_State *L) { lua_getfield(L,1,"_title"); return 1; }
static int get_value(lua_State *L) {
    const int kind=field_int(L,1,"_kind"), value=field_int(L,1,"_value");
    if(kind==1) lua_pushboolean(L,value);
    else if(kind==2) { lua_getfield(L,1,"_options"); lua_rawgeti(L,-1,value); }
    else lua_pushinteger(L,value);
    return 1;
}
static int set_title(lua_State *L) {
    luaL_checkstring(L,2); lua_pushvalue(L,2); lua_setfield(L,1,"_title"); pd_sdkmenu_publish(L); return 0;
}
static int set_value(lua_State *L) {
    int kind=field_int(L,1,"_kind"), value=0;
    if(kind==1) value=lua_isboolean(L,2) ? lua_toboolean(L,2) : luaL_checkinteger(L,2)!=0;
    else if(kind==2) {
        lua_getfield(L,1,"_options"); int count=(int)lua_rawlen(L,-1);
        if(lua_type(L,2)==LUA_TSTRING) {
            const char *wanted=lua_tostring(L,2);
            for(int i=1;i<=count;i++) { lua_rawgeti(L,-1,i); int match=strcmp(lua_tostring(L,-1),wanted)==0; lua_pop(L,1); if(match) {value=i; break;} }
        } else value=(int)luaL_checkinteger(L,2);
        if(value<1 || value>count) return luaL_error(L,"Invalid menu option value");
        lua_pop(L,1);
    } else value=(int)luaL_checkinteger(L,2);
    int_field(L,1,"_value",value); pd_sdkmenu_publish(L); return 0;
}
static int set_callback(lua_State *L) {
    if(!lua_isnoneornil(L,2)) luaL_checktype(L,2,LUA_TFUNCTION);
    lua_pushvalue(L,2); lua_setfield(L,1,"_callback"); return 0;
}
static int remove_item(lua_State *L) {
    int item=lua_gettop(L)>1 ? 2 : 1;
    lua_getfield(L,LUA_REGISTRYINDEX,ITEMS); int count=(int)lua_rawlen(L,-1), items=lua_gettop(L);
    for(int i=1;i<=count;i++) {
        lua_rawgeti(L,items,i); int match=lua_rawequal(L,-1,item); lua_pop(L,1);
        if(!match) continue;
        for(int j=i;j<count;j++) {lua_rawgeti(L,items,j+1); lua_rawseti(L,items,j);}
        lua_pushnil(L); lua_rawseti(L,items,count); break;
    }
    pd_sdkmenu_publish(L); return 0;
}
static int remove_all(lua_State *L) {
    lua_newtable(L); lua_setfield(L,LUA_REGISTRYINDEX,ITEMS); pd_sdkmenu_publish(L); return 0;
}
static int list_items(lua_State *L) {
    lua_getfield(L,LUA_REGISTRYINDEX,ITEMS); int source=lua_gettop(L), count=(int)lua_rawlen(L,source);
    lua_newtable(L);
    for(int i=1;i<=count;i++) {lua_rawgeti(L,source,i); lua_rawseti(L,-2,i);}
    return 1;
}
static int item_index(lua_State *L) {
    const char *key=luaL_checkstring(L,2);
    if(strcmp(key,"title")==0) return get_title(L);
    if(strcmp(key,"value")==0) return get_value(L);
    luaL_getmetatable(L,META); lua_getfield(L,-1,"methods"); lua_getfield(L,-1,key); return 1;
}
static int item_newindex(lua_State *L) {
    const char *key=luaL_checkstring(L,2);
    if(strcmp(key,"title")==0 || strcmp(key,"value")==0) {
        int title=strcmp(key,"title")==0; lua_remove(L,2); return title ? set_title(L) : set_value(L);
    }
    lua_rawset(L,1); return 0;
}
static int add_item(lua_State *L) {
    int title=(int)lua_tointeger(L,lua_upvalueindex(1)), kind=(int)lua_tointeger(L,lua_upvalueindex(2));
    luaL_checkstring(L,title);
    int supplied=lua_gettop(L);
    lua_getfield(L,LUA_REGISTRYINDEX,ITEMS); int items=lua_gettop(L), count=(int)lua_rawlen(L,items);
    if(count>=3) {lua_pushnil(L); lua_pushliteral(L,"The system menu allows three custom items"); return 2;}
    int value=0, callback=title+1, options=0;
    if(kind==1) {
        if(title+1<=supplied && !lua_isfunction(L,title+1)) {
            value=lua_isnil(L,title+1) ? 0 : lua_isboolean(L,title+1) ? lua_toboolean(L,title+1) : luaL_checkinteger(L,title+1)!=0;
            callback=title+2;
        }
    } else if(kind==2) {
        options=title+1; luaL_checktype(L,options,LUA_TTABLE);
        int n=(int)lua_rawlen(L,options); if(n<1) {lua_pushnil(L); lua_pushliteral(L,"Menu options cannot be empty"); return 2;}
        for(int i=1;i<=n;i++) {lua_rawgeti(L,options,i); luaL_checktype(L,-1,LUA_TSTRING); lua_pop(L,1);}
        value=1; callback=title+2;
        if(callback<=supplied && !lua_isfunction(L,callback)) {
            if(!lua_isnil(L,callback)) {
                const char *wanted=luaL_checkstring(L,callback); value=0;
                for(int i=1;i<=n;i++) {lua_rawgeti(L,options,i); int match=strcmp(wanted,lua_tostring(L,-1))==0; lua_pop(L,1); if(match) {value=i; break;}}
                if(!value) {lua_pushnil(L); lua_pushliteral(L,"Initial menu option was not found"); return 2;}
            }
            callback++;
        }
    }
    if(callback<=supplied && !lua_isnil(L,callback)) luaL_checktype(L,callback,LUA_TFUNCTION);
    lua_newtable(L); int item=lua_gettop(L);
    int_field(L,item,"_id",++next_id); int_field(L,item,"_kind",kind); int_field(L,item,"_value",value);
    lua_pushvalue(L,title); lua_setfield(L,item,"_title");
    if(callback<=supplied) {lua_pushvalue(L,callback); lua_setfield(L,item,"_callback");}
    if(options) {
        lua_newtable(L); for(int i=1;i<=(int)lua_rawlen(L,options);i++) {lua_rawgeti(L,options,i); lua_rawseti(L,-2,i);}
        lua_setfield(L,item,"_options");
    }
    luaL_getmetatable(L,META); lua_setmetatable(L,item);
    lua_pushvalue(L,item); lua_rawseti(L,items,count+1);
    pd_sdkmenu_publish(L); lua_pushvalue(L,item); return 1;
}
static int get_menu(lua_State *L) {lua_getfield(L,LUA_REGISTRYINDEX,MENU); return 1;}
static int set_image(lua_State *L) {
    if(!lua_isnoneornil(L,1)) {
        LCDBitmap **bitmap=luaL_checkudata(L,1,"pd.bitmap");
        luaL_argcheck(L,*bitmap && (*bitmap)->width==400 && (*bitmap)->height==240,1,"Menu image must be 400 x 240");
    }
    int offset=(int)luaL_optinteger(L,2,0); luaL_argcheck(L,offset>=0 && offset<=200,2,"Menu image offset must be between 0 and 200");
    lua_pushvalue(L,1); lua_setfield(L,LUA_REGISTRYINDEX,IMAGE);
    lua_pushinteger(L,offset); lua_setfield(L,LUA_REGISTRYINDEX,OFFSET);
    pd_sdkmenu_publish(L); return 0;
}
void pd_sdkmenu_publish(lua_State *L) {
    if(!pd_host_active()) return;
    int top=lua_gettop(L);
    lua_newtable(L); int root=lua_gettop(L);
    lua_newtable(L); int array=lua_gettop(L);
    lua_getfield(L,LUA_REGISTRYINDEX,ITEMS); int items=lua_gettop(L);
    for(int i=1;i<=(int)lua_rawlen(L,items);i++) {
        lua_rawgeti(L,items,i); int item=lua_gettop(L);
        lua_newtable(L); int row=lua_gettop(L);
        int_field(L,row,"id",field_int(L,item,"_id")); int_field(L,row,"kind",field_int(L,item,"_kind")); int_field(L,row,"value",field_int(L,item,"_value"));
        lua_getfield(L,item,"_title"); lua_setfield(L,row,"title");
        lua_getfield(L,item,"_options"); lua_setfield(L,row,"options");
        lua_rawseti(L,array,i); lua_pop(L,1);
    }
    lua_pop(L,1); lua_setfield(L,root,"items");
    lua_getfield(L,LUA_REGISTRYINDEX,OFFSET); lua_setfield(L,root,"imageOffset");
    lua_getfield(L,LUA_REGISTRYINDEX,IMAGE);
    LCDBitmap **image=luaL_testudata(L,-1,"pd.bitmap");
    LCDBitmap *bitmap=image ? *image : NULL;
    lua_pushboolean(L,bitmap!=NULL); lua_setfield(L,root,"hasImage");
    uint8_t *bgra=NULL;
    if(bitmap) {
        bgra=malloc(400*240*4);
        for(int y=0;y<240;y++) for(int x=0;x<400;x++) {
            int bit=(bitmap->data[y*bitmap->rowbytes+x/8]>>(7-x%8))&1;
            if(bitmap->inverted) bit=!bit;
            int opaque=!bitmap->mask || ((bitmap->mask[y*bitmap->rowbytes+x/8]>>(7-x%8))&1);
            int p=(y*400+x)*4; bgra[p]=bgra[p+1]=bgra[p+2]=opaque && bit ? 0 : 255; bgra[p+3]=255;
        }
    }
    pd_json_encode_table(L,root); pd_host_menu(lua_tostring(L,-1),bgra); free(bgra); lua_settop(L,top);
}
static int execute_select(lua_State *L) {
    uint32_t id=(uint32_t)luaL_checkinteger(L,1); int value=(int)luaL_checkinteger(L,2);
    lua_getfield(L,LUA_REGISTRYINDEX,ITEMS); int items=lua_gettop(L);
    for(int i=1;i<=(int)lua_rawlen(L,items);i++) {
        lua_rawgeti(L,items,i); int item=lua_gettop(L);
        if((uint32_t)field_int(L,item,"_id")!=id) {lua_pop(L,1); continue;}
        int kind=field_int(L,item,"_kind");
        if(!kind) {
            lua_getfield(L,item,"_callback"); if(lua_isfunction(L,-1)) lua_call(L,0,0);
        } else {
            if(kind==1) value=value!=0;
            else {lua_getfield(L,item,"_options"); int n=(int)lua_rawlen(L,-1); lua_pop(L,1); if(value<1 || value>n) return 0;}
            if(value!=field_int(L,item,"_value")) {int_field(L,item,"_value",value); int_field(L,item,"_dirty",1);}
        }
        break;
    }
    pd_sdkmenu_publish(L); return 0;
}
static int execute_close(lua_State *L) {
    lua_getfield(L,LUA_REGISTRYINDEX,ITEMS); int items=lua_gettop(L);
    lua_newtable(L); int pending=lua_gettop(L), count=0;
    /* Snapshot before callbacks: they may remove or replace every menu item. */
    for(int i=1;i<=(int)lua_rawlen(L,items);i++) {
        lua_rawgeti(L,items,i); int item=lua_gettop(L);
        if(field_int(L,item,"_dirty")) {
            int_field(L,item,"_dirty",0); lua_newtable(L);
            lua_getfield(L,item,"_callback"); lua_rawseti(L,-2,1);
            lua_pushcfunction(L,get_value); lua_pushvalue(L,item); lua_call(L,1,1); lua_rawseti(L,-2,2);
            lua_rawseti(L,pending,++count);
        }
        lua_pop(L,1);
    }
    for(int i=1;i<=count;i++) {
        lua_rawgeti(L,pending,i); lua_rawgeti(L,-1,1);
        if(lua_isfunction(L,-1)) {lua_rawgeti(L,-2,2); lua_call(L,1,0);} else lua_pop(L,1);
        lua_pop(L,1);
    }
    pd_sdkmenu_publish(L); return 0;
}
static int protected_command(lua_State *L, lua_CFunction fn, uint32_t id, int value, int arguments) {
    int top=lua_gettop(L); lua_pushcfunction(L,fn);
    if(arguments) {lua_pushinteger(L,id); lua_pushinteger(L,value);}
    int ok=lua_pcall(L,arguments,0,0)==LUA_OK;
    if(!ok) {pd_host_fail(lua_tostring(L,-1)); fprintf(stderr,"[menu callback] %s\n",lua_tostring(L,-1));}
    lua_settop(L,top); return ok;
}
int pd_sdkmenu_select(lua_State *L, uint32_t id, int value) {return protected_command(L,execute_select,id,value,2);}
int pd_sdkmenu_close(lua_State *L) {return protected_command(L,execute_close,0,0,0);}
void pd_sdkmenu_register(lua_State *L) {
    const luaL_Reg methods[]={{"getTitle",get_title},{"setTitle",set_title},{"getValue",get_value},{"setValue",set_value},{"setCallback",set_callback},{"remove",remove_item},{NULL,NULL}};
    luaL_newmetatable(L,META); lua_newtable(L); luaL_setfuncs(L,methods,0); lua_setfield(L,-2,"methods");
    lua_pushcfunction(L,item_index); lua_setfield(L,-2,"__index"); lua_pushcfunction(L,item_newindex); lua_setfield(L,-2,"__newindex"); lua_pop(L,1);
    lua_newtable(L); lua_setfield(L,LUA_REGISTRYINDEX,ITEMS);
    lua_pushnil(L); lua_setfield(L,LUA_REGISTRYINDEX,IMAGE); lua_pushinteger(L,0); lua_setfield(L,LUA_REGISTRYINDEX,OFFSET);
    for(int legacy=0;legacy<=1;legacy++) {
        if(legacy) lua_getglobal(L,"playdate"); else lua_newtable(L);
        const char *names[]={"addMenuItem","addCheckmarkMenuItem","addOptionsMenuItem"};
        for(int kind=0;kind<3;kind++) {lua_pushinteger(L,legacy ? 1 : 2); lua_pushinteger(L,kind); lua_pushcclosure(L,add_item,2); lua_setfield(L,-2,names[kind]);}
        lua_pushcfunction(L,remove_item); lua_setfield(L,-2,"removeMenuItem"); lua_pushcfunction(L,remove_all); lua_setfield(L,-2,"removeAllMenuItems");
        if(legacy) {
            lua_pushcfunction(L,get_menu); lua_setfield(L,-2,"getSystemMenu"); lua_pushcfunction(L,set_image); lua_setfield(L,-2,"setMenuImage");
            lua_pushcfunction(L,get_title); lua_setfield(L,-2,"getMenuItemTitle"); lua_pushcfunction(L,set_title); lua_setfield(L,-2,"setMenuItemTitle");
            lua_pushcfunction(L,get_value); lua_setfield(L,-2,"getMenuItemValue"); lua_pushcfunction(L,set_value); lua_setfield(L,-2,"setMenuItemValue");
            lua_pop(L,1);
        } else {lua_pushcfunction(L,list_items); lua_setfield(L,-2,"getMenuItems"); lua_setfield(L,LUA_REGISTRYINDEX,MENU);}
    }
    pd_sdkmenu_publish(L);
}
