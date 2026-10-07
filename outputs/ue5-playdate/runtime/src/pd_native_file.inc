// Included in Native::dispatch: file handles use the same Lua-owned FILE
// userdata as the Lua API, so session shutdown always closes remaining files.
auto setFileError=[&](const std::string &message){fileError=message;blocks.erase(fileErrorData);fileErrorData=0;};
if(name=="file.geterr") {if(!fileErrorData)fileErrorData=copyString(fileError);result(fileErrorData);return;}
if(name=="file.open") {
    auto path=string(a.word());auto mode=a.word();bool writing=(mode&12)!=0;
    if(!(mode&15) || (mode&~15))throw NativeError("Invalid file access mode");
    FILE *f=nullptr;
    if(writing) {auto full=filePath(path,g_pd.save_dir);f=fopen(full.c_str(),(mode&8)?"ab":"wb");}
    else {
        if(mode&2) {auto full=filePath(path,g_pd.save_dir);f=fopen(full.c_str(),"rb");}
        if(!f && (mode&1)) {auto full=filePath(path,g_pd.pdx_dir);f=fopen(full.c_str(),"rb");}
    }
    if(!f){setFileError(strerror(errno));result(0);return;}
    auto ud=(FILE**)lua_newuserdatauv(L,sizeof(FILE*),0);*ud=f;luaL_setmetatable(L,"pd.file");result(hold(-1,"file"));setFileError("");return;
}
if(name=="file.close") {auto handle=a.word();auto f=file(handle);int value=fclose(*f);*f=nullptr;drop(handle);if(value)setFileError(strerror(errno));result(value);return;}
if(name=="file.read" || name=="file.write") {
    auto f=file(a.word());auto buffer=a.word(),length=a.word();if(length>64*1024*1024)throw NativeError("File transfer exceeds 64 MB");
    size_t count=name=="file.read"?fread(writable(buffer,length),1,length,*f):fwrite(memory(buffer,length),1,length,*f);
    if(ferror(*f)){setFileError(strerror(errno));result((uint32_t)-1);}else result((uint32_t)count);return;
}
if(name=="file.flush" || name=="file.tell" || name=="file.seek") {
    auto f=file(a.word());long value;
    if(name=="file.flush")value=fflush(*f);else if(name=="file.tell")value=ftell(*f);
    else {int offset=a.integer(),whence=a.integer();if(whence<0 || whence>2)throw NativeError("Invalid file seek origin");value=fseek(*f,offset,whence);}
    if(value<0)setFileError(strerror(errno));result((uint32_t)value);return;
}
namespace fs=std::filesystem;
if(name=="file.mkdir" || name=="file.unlink" || name=="file.rename") {
    auto path=filePath(string(a.word()),g_pd.save_dir);std::error_code error;
    if(name=="file.mkdir"){if(!fs::create_directory(path,error) && !error && !fs::is_directory(path))error=std::make_error_code(std::errc::file_exists);}
    else if(name=="file.rename"){auto destination=filePath(string(a.word()),g_pd.save_dir);fs::rename(path,destination,error);}
    else {bool recursive=a.integer()!=0;if(recursive)fs::remove_all(path,error);else fs::remove(path,error);}
    setFileError(error.message());result(error?(uint32_t)-1:0);return;
}
if(name=="file.stat") {
    auto path=string(a.word());auto out=a.word();std::error_code error;
    auto full=filePath(path,g_pd.save_dir);auto status=fs::status(full,error);
    if(error || !fs::exists(status)){error.clear();full=filePath(path,g_pd.pdx_dir);status=fs::status(full,error);}
    if(error || !fs::exists(status)){setFileError(error?error.message():"File not found");result((uint32_t)-1);return;}
    bool directory=fs::is_directory(status);uintmax_t size=directory?0:fs::file_size(full,error);
    if(error || size>UINT32_MAX){setFileError(error?error.message():"File too large");result((uint32_t)-1);return;}
    int32_t fields[8]={directory?1:0,(int32_t)size,0,0,0,0,0,0};
    auto modified=fs::last_write_time(full,error);
    if(!error){auto system=std::chrono::time_point_cast<std::chrono::system_clock::duration>(modified-fs::file_time_type::clock::now()+std::chrono::system_clock::now());time_t value=std::chrono::system_clock::to_time_t(system);auto date=localtime(&value);if(date){fields[2]=date->tm_year+1900;fields[3]=date->tm_mon+1;fields[4]=date->tm_mday;fields[5]=date->tm_hour;fields[6]=date->tm_min;fields[7]=date->tm_sec;}}
    memcpy(writable(out,sizeof(fields)),fields,sizeof(fields));result(0);return;
}
if(name=="file.listfiles") {
    auto path=string(a.word());auto callback=a.word(),ud=a.word();bool hidden=a.integer()!=0;
    if(!callback)throw NativeError("Missing file list callback");
    std::map<std::string,bool> names;std::error_code lastError;
    bool found=false;
    for(auto base:{g_pd.pdx_dir,g_pd.save_dir}) {
        auto full=filePath(path,base);std::error_code error;fs::directory_iterator entries(full,error);
        if(error){lastError=error;continue;}found=true;
        for(const auto &entry:entries){auto filename=entry.path().filename().string();if(!hidden && !filename.empty() && filename[0]=='.')continue;names[filename]=entry.is_directory(error);if(error){lastError=error;break;}}
    }
    if(!found){setFileError(lastError.message());result((uint32_t)-1);return;}
    for(const auto &entry:names) {uint32_t temporary=copyString(entry.first+(entry.second?"/":""));try{invoke(callback,{temporary,ud});}catch(...){blocks.erase(temporary);throw;}blocks.erase(temporary);}
    result(0);return;
}
throw NativeError("Unimplemented C API: "+name);
