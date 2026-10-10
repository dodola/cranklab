#include "board.h"
extern "C" {
#include "pd_runtime.h"
}
#include <math.h>
#include <initializer_list>
struct Synth {float volume,frequency;uint64_t until;};
static Synth *synth(lua_State *L) {return (Synth*)luaL_checkudata(L,1,"micro.synth");}
static int synth_new(lua_State *L) {
    auto *s=(Synth*)lua_newuserdata(L,sizeof(Synth));*s={.5f,440,0};
    luaL_setmetatable(L,"micro.synth");return 1;
}
static int synth_volume(lua_State *L) {synth(L)->volume=luaL_checknumber(L,2);return 0;}
static int synth_get_volume(lua_State *L) {lua_pushnumber(L,synth(L)->volume);return 1;}
static int synth_play(lua_State *L) {
    auto *s=synth(L);float hz=luaL_optnumber(L,2,440),velocity=luaL_optnumber(L,3,1),duration=luaL_optnumber(L,4,.25);
    if(lua_toboolean(L,lua_upvalueindex(1))) hz=440*powf(2,(hz-69)/12);
    unsigned ms=duration>0 ? (unsigned)(duration*1000) : 1000;
    s->frequency=hz;s->until=pd_time_milliseconds()+ms;
    board_tone(hz,ms,s->volume*velocity);return 0;
}
static int synth_stop(lua_State *L) {synth(L)->until=0;board_audio_stop();return 0;}
static int synth_playing(lua_State *L) {lua_pushboolean(L,synth(L)->until>pd_time_milliseconds());return 1;}
static int noop(lua_State *L) {return 0;}
static int sound_time(lua_State *L) {lua_pushnumber(L,pd_time_milliseconds()/1000.);return 1;}
static int unsupported_sample(lua_State *L) {lua_pushnil(L);lua_pushliteral(L,"PCM sample playback is not implemented on this board yet");return 2;}
extern "C" void pd_sound_register(lua_State *L) {
    static const luaL_Reg funcs[]={{"setVolume",synth_volume},{"getVolume",synth_get_volume},
        {"stop",synth_stop},{"noteOff",synth_stop},{"isPlaying",synth_playing},
        {"setADSR",noop},{"setWaveform",noop},{"setAttack",noop},{"setDecay",noop},
        {"setSustain",noop},{"setRelease",noop},{NULL,NULL}};
    luaL_newmetatable(L,"micro.synth");lua_newtable(L);luaL_setfuncs(L,funcs,0);
    lua_pushboolean(L,0);lua_pushcclosure(L,synth_play,1);lua_setfield(L,-2,"playNote");
    lua_pushboolean(L,1);lua_pushcclosure(L,synth_play,1);lua_setfield(L,-2,"playMIDINote");
    lua_setfield(L,-2,"__index");lua_pop(L,1);
    lua_getglobal(L,"playdate");lua_newtable(L);
    lua_pushcfunction(L,sound_time);lua_setfield(L,-2,"getCurrentTime");
    lua_newtable(L);lua_pushcfunction(L,synth_new);lua_setfield(L,-2,"new");lua_setfield(L,-2,"synth");
    for(const char *name:{"sample","sampleplayer","fileplayer"}) {
        lua_newtable(L);lua_pushcfunction(L,unsupported_sample);lua_setfield(L,-2,"new");lua_setfield(L,-2,name);
    }
    lua_pushinteger(L,0);lua_setfield(L,-2,"kWaveSquare");
    lua_pushinteger(L,1);lua_setfield(L,-2,"kWaveTriangle");
    lua_pushinteger(L,2);lua_setfield(L,-2,"kWaveSine");
    lua_pushinteger(L,3);lua_setfield(L,-2,"kWaveNoise");
    lua_setfield(L,-2,"sound");lua_pop(L,1);
}
extern "C" void pd_sound_shutdown(void) {board_audio_stop();}
