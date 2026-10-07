#include "pd_api.h"
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
_Static_assert(sizeof(SpriteCollisionInfo)==72 && offsetof(SpriteCollisionInfo,normal)==24,"Playdate ARM collision ABI");
static PlaydateAPI *pd;
static SDFile *events;
static PDMenuItem *check,*options;
static int frame;
static int spriteCallbacks;
static void require(int ok,const char *message){if(!ok)pd->system->error("ABI fixture: %s",message);}
static void event(const char *message){pd->file->write(events,message,strlen(message));pd->file->write(events,"\n",1);pd->file->flush(events);}
static int vf(char **out,const char *fmt,...){va_list ap;va_start(ap,fmt);int n=pd->system->vaFormatString(out,fmt,ap);va_end(ap);return n;}
static void listed(const char *path,void *data){if(!strcmp(path,"native-test.dat"))(*(int*)data)++;}
static void menu(void *ud){require(ud==(void*)0x1234,"menu userdata");event("MENU_ACTION");pd->system->setMenuItemUserdata(check,(void*)42);}
static void checked(void *ud){require(ud==(void*)42,"updated menu userdata");require(pd->system->getMenuItemValue(check)==1,"check value");event("MENU_CHECK");}
static void option(void *ud){require(ud==(void*)0x1234,"option userdata");require(pd->system->getMenuItemValue(options)==1,"zero based C option index");event("MENU_OPTION");}
static int button(PDButtons b,int down,uint32_t when,void *ud){require(ud==(void*)0x5678,"button userdata");if(b==kButtonA)event(down?"BUTTON_DOWN":"BUTTON_UP");return 1;}
static void spriteUpdate(LCDSprite *sprite){require(pd->sprite->getUserdata(sprite)==(void*)0x4321,"copied sprite callback identity");spriteCallbacks++;}
static void selftest(void){
    char *out;
    int n=pd->system->formatString(&out,"%d %.3f %s %08x %lld %hhu",-123,1.25,"ARM",0x1234,1234567890123LL,255);
    require(!strcmp(out,"-123 1.250 ARM 00001234 1234567890123 255"),"variadic registers, doubles, stack and integer widths");
    require(n==(int)strlen(out),"formatString length");pd->system->realloc(out,0);
    vf(&out,"%s %.2f %d %lld","VA",-2.5,17,987654321000LL);require(!strcmp(out,"VA -2.50 17 987654321000"),"guest va_list");pd->system->realloc(out,0);
    pd->system->logToConsole("FORMAT_OK %.3f %d",1.25,17);
    void *p=pd->system->realloc(NULL,16);memset(p,0x5a,16);p=pd->system->realloc(p,64);require(((unsigned char*)p)[15]==0x5a,"realloc copy");pd->system->realloc(p,0);
    LCDPattern pattern={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0};
    LCDBitmap *bitmap=pd->graphics->newBitmap(8,8,kColorWhite);pd->graphics->pushContext(bitmap);pd->graphics->fillRect(0,0,8,8,(LCDColor)pattern);pd->graphics->popContext();
    int rowBytes;uint8_t *pixels;pd->graphics->getBitmapData(bitmap,NULL,NULL,&rowBytes,NULL,&pixels);
    require(pixels[0]==0xaf && pixels[rowBytes]==0x5f,"device pattern polarity and coverage mask");pd->graphics->freeBitmap(bitmap);
    LCDSprite *sprite=pd->sprite->newSprite();pd->sprite->setUserdata(sprite,(void*)0x4321);pd->sprite->setUpdateFunction(sprite,spriteUpdate);
    pd->sprite->setBounds(sprite,PDRectMake(10.25f,20.5f,16.75f,18.25f));PDRect bounds=pd->sprite->getBounds(sprite);
    require(bounds.x==10.25f && bounds.y==20.5f && bounds.width==16.75f && bounds.height==18.25f,"PDRect argument and return HFA");
    LCDSprite *copy=pd->sprite->copy(sprite);pd->sprite->addSprite(copy);pd->sprite->updateAndDrawSprites();require(spriteCallbacks==1,"copied callback");pd->sprite->freeSprite(copy);pd->sprite->freeSprite(sprite);
    LCDSprite *wall=pd->sprite->newSprite(),*body=pd->sprite->newSprite();
    pd->sprite->setBounds(wall,PDRectMake(200,160,16,32));pd->sprite->setCollideRect(wall,PDRectMake(0,0,16,32));pd->sprite->addSprite(wall);
    pd->sprite->setBounds(body,PDRectMake(140,170,16,16));pd->sprite->setCollideRect(body,PDRectMake(0,0,16,16));pd->sprite->addSprite(body);
    float actualX,actualY;int hits;SpriteCollisionInfo *collisions=pd->sprite->moveWithCollisions(body,230,178,&actualX,&actualY,&hits);
    require(hits==1 && actualX==192 && actualY==178 && collisions[0].other==wall && collisions[0].normal.x==-1 && collisions[0].normal.y==0,"collision packet layout and normal");pd->system->realloc(collisions,0);pd->sprite->freeSprite(body);pd->sprite->freeSprite(wall);
    SDFile *f=pd->file->open("native-test.dat",kFileWrite);require(f!=NULL,"file open write");
    const char data[6]={'A',0,'R','M',42,10};require(pd->file->write(f,data,6)==6,"binary write");require(pd->file->tell(f)==6,"tell");require(pd->file->close(f)==0,"close");
    FileStat st;require(pd->file->stat("native-test.dat",&st)==0 && st.size==6 && !st.isdir,"stat");
    f=pd->file->open("native-test.dat",kFileReadData);require(f!=NULL,"file read save area");char buffer[8];require(pd->file->read(f,buffer,6)==6 && !memcmp(data,buffer,6),"binary read");
    require(pd->file->seek(f,2,SEEK_SET)==0 && pd->file->read(f,buffer,2)==2 && buffer[0]=='R',"seek");pd->file->close(f);
    int found=0;require(pd->file->listfiles("",listed,&found,0)==0 && found==1,"nested native file list callback");
    require(pd->file->rename("native-test.dat","renamed.dat")==0 && pd->file->unlink("renamed.dat",0)==0,"rename and unlink");
    events=pd->file->open("events.log",kFileWrite);require(events!=NULL,"event log");event("SELFTEST_OK");
    pd->system->addMenuItem("Action",menu,(void*)0x1234);
    check=pd->system->addCheckmarkMenuItem("Enabled",0,checked,(void*)0x1234);
    const char *titles[]={"First","Second","Third"};options=pd->system->addOptionsMenuItem("Quality",titles,3,option,(void*)0x1234);
    require(!strcmp(pd->system->getMenuItemTitle(options),"Quality"),"menu title");pd->system->setMenuItemTitle(options,"Quality changed");
    require(!strcmp(pd->system->getMenuItemTitle(options),"Quality changed"),"title update");
    pd->system->setButtonCallback(button,(void*)0x5678,8);
    pd->display->setRefreshRate(30);
}
static int update(void *ud){
    require(ud==(void*)0x9abc,"update userdata");frame++;
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawEllipse(70,60,100,80,3,30.0f,250.0f,kColorBlack);
    pd->graphics->fillRect(frame%360,120,20,20,kColorBlack);
    unsigned char *screen=pd->graphics->getFrame();screen[20*LCD_ROWSIZE+1]=0;
    pd->graphics->markUpdatedRows(20,20);return 1;
}
int eventHandler(PlaydateAPI *api,PDSystemEvent e,uint32_t arg){
    pd=api;
    if(e==kEventInit){selftest();pd->system->setUpdateCallback(update,(void*)0x9abc);}
    else if(e==kEventPause)event("PAUSE");else if(e==kEventResume)event("RESUME");
    else if(e==kEventLock)event("LOCK");else if(e==kEventUnlock)event("UNLOCK");
    else if(e==kEventKeyPressed && arg==kButtonA)event("KEY_DOWN");else if(e==kEventKeyReleased && arg==kButtonA)event("KEY_UP");
    else if(e==kEventTerminate){event("TERMINATE");pd->file->close(events);events=NULL;}
    return 0;
}
