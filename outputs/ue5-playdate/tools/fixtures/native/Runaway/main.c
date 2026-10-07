#include "pd_api.h"
static volatile unsigned int counter;
static int update(void *ud){for(;;)counter++;return 1;}
int eventHandler(PlaydateAPI *pd,PDSystemEvent e,uint32_t arg){if(e==kEventInit)pd->system->setUpdateCallback(update,NULL);return 0;}
