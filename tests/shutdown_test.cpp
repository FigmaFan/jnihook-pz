#include <jnihook.h>
#include <jni.h>
#include <iostream>

static jclass Target_class;
static jmethodID orig_Target_sayHello;
static int hook_calls;

JNIEXPORT void JNICALL hk_Target_sayHello(JNIEnv *env, jobject obj)
{
        ++hook_calls;
        env->CallNonvirtualVoidMethod(obj, Target_class, orig_Target_sayHello);
}

int main()
{
        JavaVMOption options[1];
        options[0].optionString = const_cast<char*>("-Djava.class.path=.");

        JavaVMInitArgs vm_args;
        vm_args.version = JNI_VERSION_1_8;
        vm_args.nOptions = 1;
        vm_args.options = options;
        vm_args.ignoreUnrecognized = JNI_FALSE;

        JNIEnv *env;
        JavaVM *jvm;
        if (JNI_CreateJavaVM(&jvm, reinterpret_cast<void **>(&env), &vm_args) != JNI_OK) {
                std::cerr << "Failed to create JVM" << std::endl;
                return 1;
        }

        Target_class = env->FindClass("dummy/Target");
        if (!Target_class) {
                std::cerr << "Failed to find Target class" << std::endl;
                return 1;
        }

        jmethodID ctor = env->GetMethodID(Target_class, "<init>", "()V");
        if (!ctor) {
                std::cerr << "Failed to get Target constructor" << std::endl;
                return 1;
        }
        jobject target = env->NewObject(Target_class, ctor);
        if (!target) {
                std::cerr << "Failed to create Target instance" << std::endl;
                return 1;
        }

        jmethodID sayHello = env->GetMethodID(Target_class, "sayHello", "()V");
        if (!sayHello) {
                std::cerr << "Failed to get sayHello" << std::endl;
                return 1;
        }

        // Baseline: call original method
        hook_calls = 0;
        env->CallVoidMethod(target, sayHello);
        if (hook_calls != 0) {
                std::cerr << "Hook unexpectedly called before attach" << std::endl;
                return 1;
        }

        if (JNIHook_Init(jvm) != JNIHOOK_OK) {
                std::cerr << "JNIHook_Init failed" << std::endl;
                return 1;
        }
        if (JNIHook_Attach(sayHello, reinterpret_cast<void *>(hk_Target_sayHello), &orig_Target_sayHello) != JNIHOOK_OK) {
                std::cerr << "JNIHook_Attach failed" << std::endl;
                return 1;
        }

        // Ensure hook and caches are populated
        hook_calls = 0;
        env->CallVoidMethod(target, sayHello);
        if (hook_calls != 1) {
                std::cerr << "Hook not called after attach" << std::endl;
                return 1;
        }
        if (JNIHook_HookedMethodCount() == 0 || JNIHook_CachedClassCount() == 0) {
                std::cerr << "Internal state not populated after attach" << std::endl;
                return 1;
        }

        if (JNIHook_Shutdown() != JNIHOOK_OK) {
                std::cerr << "JNIHook_Shutdown failed" << std::endl;
                return 1;
        }

        // After shutdown the hook should no longer be called
        hook_calls = 0;
        env->CallVoidMethod(target, sayHello);
        if (hook_calls != 0) {
                std::cerr << "Hook called after shutdown" << std::endl;
                return 1;
        }
        if (JNIHook_HookedMethodCount() != 0 || JNIHook_CachedClassCount() != 0) {
                std::cerr << "Internal caches not cleared" << std::endl;
                return 1;
        }

        jvm->DestroyJavaVM();
        return 0;
}
