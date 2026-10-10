#include "save_identity.h"
#include <cstdlib>
#include <cstdio>
static void require(bool ok,const char *message) {
    if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
int main() {
    char a[32],b[32];
    micro_save_identity("com.example.game","/littlefs/games/one",a);
    require(!strcmp(a,"com.example.game"),"Normal bundle IDs must keep their existing save path");
    micro_save_identity("","/littlefs/games/one",a);
    micro_save_identity("","/littlefs/games/two",b);
    require(strcmp(a,b)!=0,"Missing bundle IDs must not share a save");
    micro_save_identity(".","/littlefs/games/one",a);
    require(!strcmp(a,"one"),"A dot bundle ID must not target the save root");
    micro_save_identity("one/two","/littlefs/games/one",a);
    micro_save_identity("one\\two","/littlefs/games/two",b);
    require(!strchr(a,'/') && !strchr(b,'\\'),"Save components must not contain path separators");
    require(strcmp(a,b)!=0,"Sanitized bundle IDs must not collide");
    micro_save_identity("com.example.a.long.prefix.and.game.one","/littlefs/games/one",a);
    micro_save_identity("com.example.a.long.prefix.and.game.two","/littlefs/games/two",b);
    require(strlen(a)<=31 && strlen(b)<=31,"Save component exceeds the MCU filename bound");
    require(strcmp(a,b)!=0,"Truncated bundle IDs must not collide");
    std::puts("Save identity stability and isolation passed");
}
