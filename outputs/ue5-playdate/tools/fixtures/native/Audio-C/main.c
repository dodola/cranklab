#include "pd_api.h"
#include <math.h>
static PlaydateAPI *pd;
static AudioSample *sample;
static SamplePlayer *player;
static int finished;
static void require(int ok,const char *message){if(!ok)pd->system->error("Audio fixture: %s",message);}
static void finish(SoundSource *source,void *ud){require(source==(SoundSource*)player && ud==(void*)0x1234,"finish callback identity");finished++;SDFile *f=pd->file->open("audio-ok",kFileWrite);require(f!=NULL,"audio marker");pd->file->write(f,"done",4);pd->file->close(f);}
static int update(void *ud){pd->graphics->clear(kColorWhite);pd->graphics->fillRect(40,40,finished?160:80,80,kColorBlack);return 1;}
int eventHandler(PlaydateAPI *api,PDSystemEvent e,uint32_t arg){
    pd=api;
    if(e==kEventInit){sample=pd->sound->sample->load("tone");require(sample!=NULL,"sample load");require(pd->sound->sample->getLength(sample)>.1f,"sample duration");
        player=pd->sound->sampleplayer->newPlayer();pd->sound->sampleplayer->setSample(player,sample);
        pd->sound->sampleplayer->setVolume(player,.5f,.25f);float l,r;pd->sound->sampleplayer->getVolume(player,&l,&r);require(l==.5f && r==.25f,"stereo volume");
        pd->sound->sampleplayer->setFinishCallback(player,finish,(void*)0x1234);
        require(pd->sound->sampleplayer->play(player,1,1.25f)==1,"play result");require(pd->sound->sampleplayer->isPlaying(player),"isPlaying");
        pd->system->setUpdateCallback(update,NULL);
    }else if(e==kEventTerminate){pd->sound->sampleplayer->freePlayer(player);pd->sound->sample->freeSample(sample);}
    return 0;
}
