#include <jnihook.h>
#include <jni.h>

static jclass Target_class;
static jmethodID orig_Target_sayHello;
static bool hook_called = false;

JNIEXPORT void JNICALL hk_Target_sayHello(JNIEnv *jni, jobject obj)
{
    hook_called = true;
    jni->CallNonvirtualVoidMethod(obj, Target_class, orig_Target_sayHello);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dummy_HookTests_run(JNIEnv *env, jclass)
{
    JavaVM *jvm;
    if (env->GetJavaVM(&jvm) != JNI_OK)
        return JNI_FALSE;

    if (JNIHook_Init(jvm) != JNIHOOK_OK)
        return JNI_FALSE;

    jclass local = env->FindClass("dummy/Target");
    if (!local)
        return JNI_FALSE;

    Target_class = (jclass)env->NewGlobalRef(local);
    jmethodID sayHello = env->GetMethodID(Target_class, "sayHello", "()V");
    if (!sayHello)
        return JNI_FALSE;

    jobject target = env->AllocObject(Target_class);
    if (!target)
        return JNI_FALSE;

    if (JNIHook_Attach(sayHello, reinterpret_cast<void *>(hk_Target_sayHello), &orig_Target_sayHello) != JNIHOOK_OK)
        return JNI_FALSE;

    hook_called = false;
    env->CallVoidMethod(target, sayHello);
    if (!hook_called)
        return JNI_FALSE;

    jmethodID tmp;
    if (JNIHook_Attach(sayHello, reinterpret_cast<void *>(hk_Target_sayHello), &tmp) != JNIHOOK_ERR_ALREADY_HOOKED)
        return JNI_FALSE;

    hook_called = false;
    env->CallVoidMethod(target, sayHello);
    if (!hook_called)
        return JNI_FALSE;

    if (JNIHook_Detach(sayHello) != JNIHOOK_OK)
        return JNI_FALSE;

    hook_called = false;
    env->CallVoidMethod(target, sayHello);
    if (hook_called)
        return JNI_FALSE;

    return JNI_TRUE;
}
