#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* The embedded filesystem uses a bounded directory component. Keep regular
   bundle IDs stable; missing IDs belong to the staged game directory. */
static inline void micro_save_identity(const char *bundle,const char *directory,char component[32]) {
    const char *identity=bundle;
    if(!identity || !*identity || !strcmp(identity,".") || !strcmp(identity,"..")) {
        const char *base=strrchr(directory,'/');identity=base ? base+1 : directory;
    }
    if(!*identity || !strcmp(identity,".") || !strcmp(identity,".."))identity="game";
    uint32_t hash=2166136261u;int changed=0;
    size_t length=strlen(identity);
    for(size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)identity[i];hash=(hash^c)*16777619u;
        int safe=isalnum(c) || c=='.' || c=='-' || c=='_';
        if(!safe)changed=1;
        if(i<31)component[i]=safe ? c : '_';
    }
    component[length<31 ? length : 31]=0;
    if(changed || length>31) {
        size_t prefix=length<22 ? length : 22;
        snprintf(component+prefix,32-prefix,"-%08x",(unsigned)hash);
    }
}
