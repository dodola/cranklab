#include <Arduino.h>
#include <LittleFS.h>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_heap_caps.h>
#include "board.h"
#include "micro_runtime.h"
extern "C" {
#include "pd_runtime.h"
#include <zlib.h>
}

static char games[24][64];
static int game_count,selected;
static bool playing;
static uint32_t frame_count,button_override;
static float crank_override;
static uint32_t next_frame,stats_at;
static char command[128];
static unsigned command_length;
static float menu_rotation;
static uint32_t last_buttons;
static uint32_t last_step_us,last_present_us;
static uint32_t pending_buttons;
static bool release_gate;

static void scan_games() {
    game_count=0;DIR *d=opendir("/littlefs/games");
    if(!d)return;struct dirent *entry;
    while((entry=readdir(d)) && game_count<24) {
        if(entry->d_name[0]=='.')continue;
        char path[256];struct stat st;
        snprintf(path,sizeof(path),"/littlefs/games/%s/pdxinfo",entry->d_name);
        if(stat(path,&st)==0) snprintf(games[game_count++],64,"%s",entry->d_name);
    }
    closedir(d);
    for(int i=0;i<game_count;i++)for(int j=i+1;j<game_count;j++) if(strcmp(games[i],games[j])>0) {
        char tmp[64];memcpy(tmp,games[i],64);memcpy(games[i],games[j],64);memcpy(games[j],tmp,64);
    }
}
static void show_library() {
    playing=false;button_override=0;crank_override=0;pending_buttons=0;micro_stop();
    char text[512];snprintf(text,sizeof(text),"Turn dial to select\nPress to launch\n\n");
    if(!game_count) strlcat(text,"No games. Upload filesystem.",sizeof(text));
    for(int i=max(0,selected-1);i<min(game_count,selected+3);i++) {
        strlcat(text,i==selected ? "> " : "  ",sizeof(text));strlcat(text,games[i],sizeof(text));strlcat(text,"\n",sizeof(text));
    }
    board_message("GAME LIBRARY",text);
}
static bool valid_name(const char *name) {
    if(!*name)return false;
    for(const char *p=name;*p;p++) if(!isalnum((unsigned char)*p) && *p!='-' && *p!='_' && *p!='.')return false;
    return !strstr(name,"..");
}
static void start_game(const char *name) {
    if(!valid_name(name))return;
    char path[160];snprintf(path,sizeof(path),"/littlefs/games/%s",name);
    playing=false;frame_count=0;button_override=0;crank_override=0;
    if(!micro_start(path)) {
        Serial.printf("ERROR %s\n",micro_error());board_message("CANNOT RUN",micro_error());return;
    }
    board_begin();playing=true;next_frame=millis();release_gate=true;pending_buttons=0;
    Serial.printf("RUN %s lua=%u free=%u\n",name,micro_lua_bytes(),ESP.getFreeHeap());
}
static void status() {
    const uint8_t *bits=micro_frame();
    Serial.printf("STATE {\"board\":\"%s\",\"playing\":%s,\"frames\":%u,\"luaBytes\":%u,\"heapFree\":%u,\"largestBlock\":%u,\"psram\":%u,\"buttons\":%u,\"crank\":%.2f,\"frameCRC\":%u,\"stepUs\":%u,\"presentUs\":%u}\n",
        board_config().id,playing ? "true" : "false",frame_count,micro_lua_bytes(),ESP.getFreeHeap(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),ESP.getPsramSize(),g_pd.buttons_current,
        g_pd.crank_angle,bits ? (unsigned)crc32(0,bits,12000) : 0,last_step_us,last_present_us);
}
static void process_command() {
    command[command_length]=0;
    if(!strcmp(command,"status"))status();
    else if(!strcmp(command,"list"))for(int i=0;i<game_count;i++) Serial.printf("GAME %s\n",games[i]);
    else if(!strncmp(command,"load ",5))start_game(command+5);
    else if(!strncmp(command,"buttons ",8))button_override=strtoul(command+8,NULL,0)&63;
    else if(!strncmp(command,"crank ",6))crank_override+=atof(command+6);
    else if(!strcmp(command,"menu"))show_library();
    else if(!strcmp(command,"frame") && micro_frame()) {
        Serial.print("FRAME ");
        const uint8_t *bits=micro_frame();
        for(int i=0;i<12000;i++)Serial.printf("%02x",bits[i]);Serial.println();
    }
    else if(!strcmp(command,"reboot"))ESP.restart();
    command_length=0;
}
void setup() {
    setenv("TZ","UTC0",1);tzset();
    Serial.begin(115200);delay(350);board_begin();
    Serial.printf("BOOT Playdate ESP32 board=%s flash=%u psram=%u free=%u\n",board_config().id,ESP.getFlashChipSize(),ESP.getPsramSize(),ESP.getFreeHeap());
    if(!LittleFS.begin(false,"/littlefs",10,"spiffs")) {
        board_message("NO GAME STORAGE","Upload the LittleFS image.\nStorage is never auto-formatted.");
        Serial.println("ERROR LittleFS mount failed");return;
    }
    Serial.printf("FS used=%u total=%u\n",(unsigned)LittleFS.usedBytes(),(unsigned)LittleFS.totalBytes());
    scan_games();show_library();
}
void loop() {
    while(Serial.available()) {
        char c=Serial.read();
        if(c=='\n') process_command();
        else if(c!='\r' && command_length<sizeof(command)-1)command[command_length++]=c;
    }
    BoardInput input=board_poll();
    if(input.menu_pressed){show_library();last_buttons=input.buttons;}
    if(!playing && game_count) {
        menu_rotation+=input.crank_delta;
        int move=(int)(menu_rotation/22.5f);
        if(move){menu_rotation-=move*22.5f;selected=(selected+move%game_count+game_count)%game_count;show_library();}
        if((input.buttons&32) && !(last_buttons&32))start_game(games[selected]);
    }
    last_buttons=input.buttons;
    if(playing) {
        if(release_gate) {
            if(!input.buttons)release_gate=false;
            input.buttons=0;
        }
        pending_buttons|=input.buttons;
    }
    if(playing && (int32_t)(millis()-next_frame)>=0) {
        uint32_t tick=micros();
        input.buttons|=button_override|pending_buttons;pending_buttons=0;
        input.crank_delta+=crank_override;crank_override=0;
        if(!micro_step(input.buttons,input.crank_delta,0)) {
            Serial.printf("ERROR %s\n",micro_error());playing=false;board_message("GAME ERROR",micro_error());
        } else {
            last_step_us=micros()-tick;tick=micros();
            board_present(micro_frame(),g_pd.inverted,g_pd.display_scale);frame_count++;
            last_present_us=micros()-tick;
            float rate=micro_refresh_rate();
            uint32_t frame_ms=rate>0 ? (uint32_t)(1000.f/rate) : 0;
            next_frame+=frame_ms;
            if((int32_t)(millis()-next_frame)>=(int32_t)frame_ms)next_frame=millis();
        }
    } else if(playing) crank_override+=input.crank_delta;
    if(millis()-stats_at>=2000) {stats_at=millis();status();}
    delay(1);
}
