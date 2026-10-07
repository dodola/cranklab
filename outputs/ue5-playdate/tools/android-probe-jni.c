#include <jni.h>
#include <string.h>
#define main pd_probe_main
#include "android-probe.c"
#undef main
JNIEXPORT jint JNICALL Java_com_cranklab_probe_Probe_run(JNIEnv *env,jclass cls,jstring game,jstring save,jstring output,jint ticks) {
    JavaVM *vm=NULL; (*env)->GetJavaVM(env,&vm);
    jclass http=(*env)->FindClass(env,"com/cranklab/CrankHttp");
    if(!http || !crank_android_init(vm,env,http)) return 4;
    (*env)->DeleteLocalRef(env,http);
    const char *g=(*env)->GetStringUTFChars(env,game,NULL);
    const char *s=(*env)->GetStringUTFChars(env,save,NULL);
    const char *o=(*env)->GetStringUTFChars(env,output,NULL);
    char log[2048]; snprintf(log,sizeof(log),"%s.log",o);
    freopen(log,"w",stderr); setvbuf(stderr,NULL,_IOLBF,0);
    freopen(log,"a",stdout); setvbuf(stdout,NULL,_IOLBF,0);
    char duration[32]; snprintf(duration,sizeof(duration),"%d",ticks);
    char *argv[]={"crank-probe",(char *)g,(char *)s,(char *)o,duration,NULL};
    int result=pd_probe_main(5,argv);
    (*env)->ReleaseStringUTFChars(env,game,g);
    (*env)->ReleaseStringUTFChars(env,save,s);
    (*env)->ReleaseStringUTFChars(env,output,o);
    return result;
}
