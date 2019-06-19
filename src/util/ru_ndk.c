#include <stdlib.h>
#include <android_native_app_glue.h>
#include <jni.h>

// local
#include "alloc.h"
#include "ru_ndk.h"

char *
ru_activity_get_package_name(ANativeActivity *activity) {
   JavaVM *vm = activity->vm;
   JNIEnv *env;
   jint err;

   // ANativeActivity::clazz is misnamed. It's not the NativeActivity class (a
   // jclass), but an instance of that class (a jobject).
   jobject activity_obj = activity->clazz;

   err = (*vm)->AttachCurrentThread(vm, &env, NULL);
   if (err)
      abort();

   jclass activity_class = (*env)->GetObjectClass(env, activity_obj);
   if (!activity_class)
       abort();

   jmethodID mid = (*env)->GetMethodID(env, activity_class, "getPackageName", "()Ljava/lang/String;");
   if (!mid)
       abort();

   jstring name0 = (jstring) (*env)->CallObjectMethod(env, activity_obj, mid);
   if (!name0)
       abort();

   const char *name1 = (*env)->GetStringUTFChars(env, name0, NULL);
   if (!name1)
       abort();

   char *name2 = xstrdup(name1);

   (*env)->ReleaseStringUTFChars(env, name0, name1);
   (*vm)->DetachCurrentThread(vm);

   return name2;
}

char *
ru_activity_get_string_extra(ANativeActivity *activity, const char *name) {
   JavaVM *vm = activity->vm;
   JNIEnv *env;
   char *result = NULL;
   jint err;

   // ANativeActivity::clazz is misnamed. It's not the NativeActivity class (a
   // jclass), but an instance of that class (a jobject).
   jobject activity_obj = activity->clazz;

   err = (*vm)->AttachCurrentThread(vm, &env, NULL);
   if (err)
      abort();

   jclass activity_class = (*env)->GetObjectClass(env, activity_obj);
   if (!activity_class)
       abort();

   jmethodID midGetIntent = (*env)->GetMethodID(env, activity_class, "getIntent", "()Landroid/content/Intent;");
   if (!midGetIntent)
       abort();

   jobject intent_obj = (*env)->CallObjectMethod(env, activity_obj, midGetIntent);
   if (!intent_obj)
       abort();

   jclass intent_class = (*env)->GetObjectClass(env, intent_obj);
   if (!intent_class)
       abort();

   jmethodID midGetStringExtra = (*env)->GetMethodID(env, intent_class, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
   if (!midGetStringExtra)
       abort();

   jvalue args[1];
   args[0].l = (*env)->NewStringUTF(env, name);
   if (!args[0].l)
       abort();

   jstring value0 = (jstring) (*env)->CallObjectMethodA(env, intent_obj, midGetStringExtra, args);
   if (!value0)
       goto fail_new_string_utf;

   const char *value1 = (*env)->GetStringUTFChars(env, value0, NULL);
   if (!value1)
       abort();

   result = xstrdup(value1);

   (*env)->ReleaseStringUTFChars(env, value0, value1);
 fail_new_string_utf:
   (*vm)->DetachCurrentThread(vm);

   return result;
}
