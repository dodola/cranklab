#include "crank_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) { fprintf(stderr, "Usage: crank-probe game.pdx save-dir output-prefix [ticks]\n"); return 1; }
    int ticks=argc==5?atoi(argv[4]):300;
    if(ticks<180 || ticks>3600) return 1;
    if (!crank_start(argv[1], argv[2])) return 2;
    uint8_t *frame = calloc(400*240*4, 1);
    int16_t audio[8192]; char path[2048], error[2048];
    snprintf(path, sizeof(path), "%s.pcm", argv[3]); FILE *pcm = fopen(path, "wb");
    uint32_t serial = 0, samples = 0;
    for (int i=0; i<ticks && crank_status()!=3; i++) {
        if(i==ticks-180) {
            crank_copy_frame(frame,400*240*4);
            snprintf(path,sizeof(path),"%s.before.bgra",argv[3]); FILE *before=fopen(path,"wb");
            if(before) { fwrite(frame,1,400*240*4,before); fclose(before); }
            crank_set_buttons(32);
        }
        if(i==ticks-170) crank_set_buttons(0);
        if(i>=ticks-150 && i<ticks-90) crank_add_crank(2.0f, 0);
        serial = crank_copy_frame(frame,400*240*4);
        uint32_t n = crank_copy_audio(audio,8192); samples += n;
        if(pcm && n) fwrite(audio,sizeof(int16_t),n,pcm);
        usleep(16667);
    }
    int status = crank_status(); crank_last_error(error,sizeof(error));
    uint32_t info_size=crank_copy_native_info(NULL,0);
    if(info_size && info_size<65536) {
        char *info=calloc(info_size,1);crank_copy_native_info(info,info_size);
        snprintf(path,sizeof(path),"%s.native.json",argv[3]);FILE *output=fopen(path,"wb");
        if(output){fwrite(info,1,info_size-1,output);fclose(output);}free(info);
    }
    crank_stop();
    if(pcm) fclose(pcm);
    snprintf(path,sizeof(path),"%s.bgra",argv[3]); FILE *fb=fopen(path,"wb");
    if(fb) { fwrite(frame,1,400*240*4,fb); fclose(fb); } free(frame);
    printf("{\"status\":%d,\"frames\":%u,\"audioSamples\":%u,\"stopped\":%d}\n",status,serial,samples,crank_status());
    if(error[0]) fprintf(stderr,"%s\n",error);
    return status==2 && serial>=20 && !error[0] && crank_status()==0 ? 0 : 3;
}
