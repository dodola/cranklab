if(name=="sound.getCurrentTime") {result((uint32_t)((pd_time_milliseconds()-g_pd.start_time)*44100/1000));return;}
if(name=="sound.sample.load") {
    auto path=a.word();function("playdate.sound.sample.new");text(path);call(1,1);
    if(!pd_sound_native_loaded(L,-1)){result(0);return;}result(hold(-1,"sample"));return;
}
if(name=="sound.sample.getLength") {auto p=a.word();push(p,"sample");lua_getfield(L,-1,"getLength");lua_insert(L,-2);call(1,1);number((float)lua_tonumber(L,-1));return;}
if(name=="sound.sample.freeSample") {auto p=a.word();push(p,"sample");pd_sound_native_dispose(L,-1);drop(p);return;}
if(name=="sound.sampleplayer.newPlayer" || name=="sound.fileplayer.newPlayer") {
    bool file=name=="sound.fileplayer.newPlayer";function(file?"playdate.sound.fileplayer.new":"playdate.sound.sampleplayer.new");call(0,1);result(hold(-1,file?"fileplayer":"sampleplayer"));return;
}
if(name.rfind("sound.sampleplayer.",0)==0 || name.rfind("sound.fileplayer.",0)==0 || name.rfind("sound.source.",0)==0) {
    auto p=a.word();auto type=resource(p).type;if(type!="sampleplayer" && type!="fileplayer")throw NativeError("Invalid native sound source");
    auto method=name.substr(name.find('.',6)+1);
    auto soundMethod=[&](const char *key){push(p);lua_getfield(L,-1,key);if(!lua_isfunction(L,-1))throw NativeError("Missing sound method");lua_insert(L,-2);};
    if(method=="freePlayer"){push(p);pd_sound_native_dispose(L,-1);sounds.erase(p);drop(p);return;}
    if(method=="setSample") {auto sample=a.word();soundMethod("setSample");push(sample,"sample");call(2,0);resource(p).userdata=sample;return;}
    if(method=="loadIntoPlayer") {auto path=a.word();soundMethod("load");text(path);call(2,1);result(lua_toboolean(L,-1));return;}
    if(method=="play") {
        int repeat=a.integer();float rate=type=="sampleplayer"?a.number():1.f;
        if(repeat<0 || !std::isfinite(rate) || rate<=0 || rate>16)throw NativeError("Invalid native sample repeat/rate");
        if(type=="sampleplayer" && resource(p).userdata && !objects.count(resource(p).userdata))throw NativeError("SamplePlayer references a freed sample");
        soundMethod("play");pushInt(repeat);if(type=="sampleplayer")pushNum(rate);call(type=="sampleplayer"?3:2,0);
        soundMethod("isPlaying");call(1,1);bool playing=lua_toboolean(L,-1)!=0;result(playing);sounds[p].playing=playing;return;
    }
    if(method=="setFinishCallback"){auto callback=a.word(),ud=a.word();sounds[p].callback=callback;sounds[p].userdata=ud;return;}
    if(method=="setPaused"){int paused=a.integer();push(p);pd_sound_native_pause(L,-1,paused);return;}
    if(method=="setVolume"){float left=a.number(),right=a.number();if(!std::isfinite(left) || !std::isfinite(right))throw NativeError("Invalid native volume");push(p);pd_sound_native_volume(L,-1,left,right);return;}
    if(method=="getVolume"){auto left=a.word(),right=a.word();soundMethod("getVolume");call(1,2);write<float>(left,(float)lua_tonumber(L,-2));write<float>(right,(float)lua_tonumber(L,-1));return;}
    if(method=="setRate" && type=="sampleplayer"){float rate=a.number();if(!std::isfinite(rate) || rate<=0 || rate>16)throw NativeError("Invalid sample rate");soundMethod("setRate");pushNum(rate);call(2,0);return;}
    if(method=="stop" || method=="pause"){soundMethod(method.c_str());call(1,0);if(method=="stop")sounds[p].playing=false;return;}
    if(method=="getLength" || method=="getRate" || method=="getOffset" || method=="isPlaying"){soundMethod(method.c_str());call(1,1);if(method=="isPlaying")result(lua_toboolean(L,-1));else number((float)lua_tonumber(L,-1));return;}
}
throw NativeError("Unimplemented C API: "+name);
