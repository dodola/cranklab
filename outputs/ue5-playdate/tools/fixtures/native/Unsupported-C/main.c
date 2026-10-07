#include "pd_api.h"
int eventHandler(PlaydateAPI *pd,PDSystemEvent event,uint32_t argument){
    if(event==kEventInit){
        LCDBitmap *bitmap=pd->graphics->newBitmap(8,8,kColorWhite);
        pd->graphics->copyBitmap(bitmap);
    }
    return 0;
}
