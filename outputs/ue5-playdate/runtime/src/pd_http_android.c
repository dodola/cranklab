#include "pd_http_backend.h"
#include "crank_host.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JavaVM *vm;
static jclass helper;
static jmethodID execute;
int crank_android_init(void *java_vm, void *jni_env, void *http_class) {
    JNIEnv *env = jni_env;
    if (!java_vm || !env || !http_class) return 0;
    jclass next = (*env)->NewGlobalRef(env, (jclass)http_class);
    jmethodID method = (*env)->GetStaticMethodID(env, next, "execute",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[BI)[Ljava/lang/Object;");
    if ((*env)->ExceptionCheck(env) || !next || !method) {
        (*env)->ExceptionClear(env); if (next) (*env)->DeleteGlobalRef(env,next); return 0;
    }
    if (helper) (*env)->DeleteGlobalRef(env,helper);
    helper = next; execute = method; vm = java_vm;
    return 1;
}
static char *copy_string(JNIEnv *env, jstring string) {
    if (!string) return NULL;
    const char *value = (*env)->GetStringUTFChars(env,string,NULL);
    if (!value) return NULL;
    char *result = strdup(value);
    (*env)->ReleaseStringUTFChars(env,string,value);
    return result;
}
int pd_http_fetch(const PDHttpRequest *r, PDHttpResponse *out) {
    memset(out,0,sizeof(*out));
    if (!vm || !helper) { snprintf(out->error,sizeof(out->error),"Android HTTP bridge is not initialized"); return -1; }
    JNIEnv *env = NULL; int attached = 0;
    if ((*vm)->GetEnv(vm,(void **)&env,JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm,&env,NULL) != JNI_OK) {
            snprintf(out->error,sizeof(out->error),"Unable to attach HTTP worker to Java VM"); return -1;
        }
        attached = 1;
    }
    if ((*env)->PushLocalFrame(env,24)<0) {
        (*env)->ExceptionClear(env); if(attached) (*vm)->DetachCurrentThread(vm);
        snprintf(out->error,sizeof(out->error),"Unable to allocate HTTP JNI frame"); return -1;
    }
    size_t url_size = strlen(r->server)+strlen(r->path)+40;
    char *url = malloc(url_size);
    if (!url) { snprintf(out->error,sizeof(out->error),"Out of memory"); goto done; }
    snprintf(url,url_size,"%s://%s:%d%s",r->ssl?"https":"http",r->server,r->port,r->path);
    jstring jurl=(*env)->NewStringUTF(env,url); free(url);
    jstring method=(*env)->NewStringUTF(env,r->method);
    jstring headers=(*env)->NewStringUTF(env,r->headers?r->headers:"");
    jbyteArray body=(*env)->NewByteArray(env,(jsize)r->body_size);
    if(r->body_size && body) (*env)->SetByteArrayRegion(env,body,0,(jsize)r->body_size,(const jbyte *)r->body);
    if((*env)->ExceptionCheck(env) || !jurl || !method || !headers || !body) goto exception;
    jobjectArray result=(*env)->CallStaticObjectMethod(env,helper,execute,jurl,method,headers,body,r->timeout_ms);
    if((*env)->ExceptionCheck(env) || !result) goto exception;
    jobject status=(*env)->GetObjectArrayElement(env,result,0);
    jclass integer=(*env)->FindClass(env,"java/lang/Integer");
    jmethodID int_value=(*env)->GetMethodID(env,integer,"intValue","()I");
    out->status=(*env)->CallIntMethod(env,status,int_value);
    jstring raw_headers=(jstring)(*env)->GetObjectArrayElement(env,result,1);
    jbyteArray bytes=(jbyteArray)(*env)->GetObjectArrayElement(env,result,2);
    jstring error=(jstring)(*env)->GetObjectArrayElement(env,result,3);
    if((*env)->ExceptionCheck(env)) goto exception;
    out->headers=copy_string(env,raw_headers);
    if(bytes) {
        out->size=(size_t)(*env)->GetArrayLength(env,bytes);
        if(out->size) {
            out->body=malloc(out->size);
            if(!out->body) { snprintf(out->error,sizeof(out->error),"Out of memory"); goto done; }
            (*env)->GetByteArrayRegion(env,bytes,0,(jsize)out->size,(jbyte *)out->body);
        }
    }
    if(error) { char *message=copy_string(env,error); snprintf(out->error,sizeof(out->error),"%s",message?message:"Android HTTP failed"); free(message); }
    if((*env)->ExceptionCheck(env)) goto exception;
    goto done;
exception:
    (*env)->ExceptionClear(env); snprintf(out->error,sizeof(out->error),"Android HTTP JNI exception");
done:
    (*env)->PopLocalFrame(env,NULL);
    if(attached) (*vm)->DetachCurrentThread(vm);
    return out->error[0] ? -1 : 0;
}
