/* Reuse metadata and case-insensitive path handling; replace the desktop
   eager archive loader with a bounded, indexed file reader. */
#define pdz_load pdz_eager_unused
#define pdz_free pdz_eager_free_unused
#define pdz_find pdz_eager_find_unused
#include "../../runtime/src/pd_pdx.c"
#undef pdz_load
#undef pdz_free
#undef pdz_find
PDZFile *pdz_load(const char *path);
void pdz_free(PDZFile *pdz);
PDZEntry *pdz_find(PDZFile *pdz,const char *name);

typedef struct { uint32_t offset, length; } EntryLocation;
typedef struct {
    PDZFile base;
    FILE *file;
    EntryLocation *locations;
    PDZEntry *decoded;
} MicroPDZ;
static uint32_t read_u32(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
static PDZFile *shared_corelibs;
PDZFile *micro_corelibs(void) {
    if(!shared_corelibs) shared_corelibs=pdz_load("/littlefs/CoreLibs.pdz");
    return shared_corelibs;
}
void micro_corelibs_close(void) {pdz_free(shared_corelibs);shared_corelibs=NULL;}
PDZEntry *micro_pdz_import_entry(PDZFile *base,const char *name) {
    if(!base)return NULL;
    for(int i=0;i<base->entry_count;i++) {
        PDZEntry *entry=&base->entries[i];
        if(!strcmp(entry->filename,name)) {
            /* Asset import declares a compile dependency. Decode assets only
               when image/font/sample constructors actually request them. */
            return entry->entry_type==1 ? pdz_find(base,name) : entry;
        }
    }
    return NULL;
}

void micro_pdz_release(PDZFile *base) {
    if(!base) return;
    MicroPDZ *pdz=(MicroPDZ*)base;
    if(pdz->decoded) {free(pdz->decoded->data);pdz->decoded->data=NULL;pdz->decoded=NULL;}
}
void pdz_free(PDZFile *base) {
    if(!base) return;
    MicroPDZ *pdz=(MicroPDZ*)base; micro_pdz_release(base);
    for(int i=0;i<base->entry_count;i++) free(base->entries[i].filename);
    free(base->entries);free(pdz->locations);if(pdz->file)fclose(pdz->file);free(pdz);
}
PDZFile *pdz_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long end=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t header[16];
    if(fread(header,1,16,f)!=16 || memcmp(header,"Playdate PDZ",12) || read_u32(header+12)&1) {fclose(f);return NULL;}
    MicroPDZ *pdz=calloc(1,sizeof(*pdz));if(!pdz){fclose(f);return NULL;}
    pdz->file=f;
    while(ftell(f)<end) {
        if(pdz->base.entry_count>=2048 || fread(header,1,4,f)!=4) goto fail;
        unsigned len=header[1]|header[2]<<8|header[3]<<16;
        unsigned flags=header[0]; char name[256]; unsigned n=0;int c;
        while((c=fgetc(f))>0) {if(n>=255)goto fail;name[n++]=(char)c;}
        if(c!=0)goto fail;name[n]=0;
        fseek(f,(ftell(f)+3)&~3L,SEEK_SET);
        int rate=0,format=0;
        if((flags&127)==5) {
            if(len<4 || fread(header,1,4,f)!=4)goto fail;
            rate=header[0]|header[1]<<8|header[2]<<16;format=header[3];len-=4;
        }
        long offset=ftell(f);if(offset<0 || len>(unsigned)(end-offset))goto fail;
        int i=pdz->base.entry_count;
        PDZEntry *entries=realloc(pdz->base.entries,(i+1)*sizeof(PDZEntry));
        if(!entries)goto fail;pdz->base.entries=entries;
        EntryLocation *loc=realloc(pdz->locations,(i+1)*sizeof(EntryLocation));
        if(!loc)goto fail;pdz->locations=loc;
        memset(&entries[i],0,sizeof(PDZEntry));entries[i].filename=strdup(name);
        if(!entries[i].filename)goto fail;
        pdz->base.entry_count++;entries[i].entry_type=flags&127;entries[i].is_compressed=flags>>7;
        entries[i].audio_sample_rate=rate;entries[i].audio_format=format;
        entries[i].size=len;loc[i]=(EntryLocation){offset,len};
        fseek(f,offset+len,SEEK_SET);
    }
    return &pdz->base;
fail:
    pdz_free(&pdz->base);return NULL;
}
PDZEntry *pdz_find(PDZFile *base,const char *name) {
    if(!base)return NULL;
    MicroPDZ *pdz=(MicroPDZ*)base;
    for(int i=0;i<base->entry_count;i++) {
        PDZEntry *e=&base->entries[i];
        if(strcmp(e->filename,name))continue;
        if(e==pdz->decoded)return e;
        micro_pdz_release(base);
        EntryLocation loc=pdz->locations[i];fseek(pdz->file,loc.offset,SEEK_SET);
        size_t size=loc.length;
        if(e->is_compressed) {
            uint8_t h[4];if(size<4 || fread(h,1,4,pdz->file)!=4)return NULL;
            size=read_u32(h);
        }
        if(size>512*1024 || !size) {pd_host_fail("PDZ entry exceeds MCU memory limit");return NULL;}
        uint8_t *data=malloc(size);if(!data){pd_host_fail("Not enough RAM for PDZ entry");return NULL;}
        int ok=1;
        if(!e->is_compressed) ok=fread(data,1,size,pdz->file)==size;
        else {
            z_stream zs={0};uint8_t input[1024];unsigned remaining=loc.length-4;
            int result=inflateInit(&zs);zs.next_out=data;zs.avail_out=size;
            if(result==Z_OK) {
                while(remaining && result==Z_OK) {
                    unsigned chunk=remaining>sizeof(input) ? sizeof(input) : remaining;
                    if(fread(input,1,chunk,pdz->file)!=chunk)break;
                    remaining-=chunk;zs.next_in=input;zs.avail_in=chunk;
                    result=inflate(&zs,Z_NO_FLUSH);
                }
                ok=result==Z_STREAM_END && zs.total_out==size;inflateEnd(&zs);
            } else ok=0;
        }
        if(!ok){free(data);pd_host_fail("Invalid or truncated compressed PDZ entry");return NULL;}
        e->data=data;e->size=size;pdz->decoded=e;return e;
    }
    return NULL;
}
