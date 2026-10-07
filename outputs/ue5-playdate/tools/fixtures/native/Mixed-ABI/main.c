#include "pd_api.h"
#include <string.h>
#include <math.h>
static PlaydateAPI *pd;
static LuaUDObject *retained;
static int collected;
static void require(int ok,const char *message){if(!ok)pd->system->error("Mixed ABI fixture: %s",message);}
static int newObject(lua_State *L){void *p=pd->system->realloc(NULL,16);LuaUDObject *ud=pd->lua->pushObject(p,"abi.object",1);pd->lua->pushString("owned value");pd->lua->setUserValue(ud,1);return 1;}
static int dealloc(lua_State *L){void *p=pd->lua->getArgObject(1,"abi.object",NULL);require(p!=NULL,"GC object identity");pd->system->realloc(p,0);collected++;return 0;}
static int indexObject(lua_State *L){if(pd->lua->indexMetatable())return 1;const char *key=pd->lua->getArgString(2);if(!strcmp(key,"value")){LuaUDObject *ud;pd->lua->getArgObject(1,"abi.object",&ud);pd->lua->getUserValue(ud,1);return 1;}pd->lua->pushNil();return 1;}
static int retainObject(lua_State *L){const char *type;require(pd->lua->getArgType(1,&type)==kTypeObject && !strcmp(type,"abi.object"),"outClass");LuaUDObject *ud;pd->lua->getArgObject(1,"abi.object",&ud);retained=pd->lua->retainObject(ud);return 0;}
static int releaseObject(lua_State *L){pd->lua->releaseObject(retained);retained=NULL;return 0;}
static int count(lua_State *L){pd->lua->pushInt(collected);return 1;}
static int echoFloat(lua_State *L){require(pd->lua->getArgCount()==1,"echo arg count");float value=pd->lua->getArgFloat(1);pd->lua->pushFloat(value*1.5f);require(pd->lua->getArgCount()==1,"arg count changed after push");return 1;}
static int nested(lua_State *L){pd->lua->pushFloat(2.5f);const char *err=NULL;require(pd->lua->callFunction("abiNestedCallback",1,&err),err?err:"nested callback");require(!pd->lua->callFunction("noSuchLuaTable.func",0,&err) && err!=NULL,"missing Lua callback error result");pd->lua->pushInt(17);return 1;}
static int binary(lua_State *L){size_t len;const char *bytes=pd->lua->getArgBytes(1,&len);require(len==5 && bytes[1]==0,"binary argument");pd->lua->pushBytes(bytes,len);return 1;}
static int bitmap(lua_State *L){LCDBitmap *image=pd->graphics->newBitmap(16,16,kColorWhite);int stride;uint8_t *data;pd->graphics->getBitmapData(image,NULL,NULL,&stride,NULL,&data);data[5*stride]=0;pd->lua->pushBitmap(image);pd->graphics->freeBitmap(image);return 1;}
static const lua_reg methods[]={{"new",newObject},{"__gc",dealloc},{"__index",indexObject},{"retain",retainObject},{NULL,NULL}};
int eventHandler(PlaydateAPI *api,PDSystemEvent e,uint32_t arg){pd=api;if(e==kEventInitLua){const char *err=NULL;require(pd->lua->registerClass("abi.object",methods,NULL,0,&err),"class registration");pd->lua->addFunction(releaseObject,"abi.release",&err);pd->lua->addFunction(count,"abi.collected",&err);pd->lua->addFunction(echoFloat,"abi.echo",&err);pd->lua->addFunction(nested,"abi.nested",&err);pd->lua->addFunction(binary,"abi.binary",&err);pd->lua->addFunction(bitmap,"abi.bitmap",&err);}return 0;}
