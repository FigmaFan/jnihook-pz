#include <jnihook.h>
#include <jnihook.hpp>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

jclass Target_class;
jclass StaticArgTarget_class;

jmethodID orig_Target_sayHello;
jmethodID orig_StaticArgTarget_addInts;
jmethodID orig_StaticArgTarget_concatStrings;
jmethodID orig_StaticArgTarget_sumIntArray;
jmethodID orig_StaticArgTarget_passThrough;

JNIEXPORT void JNICALL hk_Target_sayHello(JNIEnv *jni, jobject obj)
{
        std::cout << "Target::sayHello HOOK CALLED!" << std::endl;
        std::cout << "Calling original method..." << std::endl;
        jni->CallNonvirtualVoidMethod(obj, Target_class, orig_Target_sayHello);
}

JNIEXPORT jint JNICALL hk_StaticArgTarget_addInts(JNIEnv *jni, jclass clazz, jint a, jint b)
{
        assert(a == 2);
        assert(b == 3);
        jint result = jni->CallStaticIntMethod(clazz, orig_StaticArgTarget_addInts, a, b);
        assert(result == 5);
        return result;
}

JNIEXPORT jstring JNICALL hk_StaticArgTarget_concatStrings(JNIEnv *jni, jclass clazz, jstring a, jstring b)
{
        const char *sa = jni->GetStringUTFChars(a, nullptr);
        const char *sb = jni->GetStringUTFChars(b, nullptr);
        assert(std::string(sa) == "foo");
        assert(std::string(sb) == "bar");
        jni->ReleaseStringUTFChars(a, sa);
        jni->ReleaseStringUTFChars(b, sb);

        jstring result = static_cast<jstring>(
            jni->CallStaticObjectMethod(clazz, orig_StaticArgTarget_concatStrings, a, b));
        const char *sr = jni->GetStringUTFChars(result, nullptr);
        assert(std::string(sr) == "foobar");
        jni->ReleaseStringUTFChars(result, sr);
        return result;
}

JNIEXPORT jint JNICALL hk_StaticArgTarget_sumIntArray(JNIEnv *jni, jclass clazz, jintArray arr)
{
        jsize len = jni->GetArrayLength(arr);
        assert(len == 3);
        jint *elems = jni->GetIntArrayElements(arr, nullptr);
        assert(elems[0] == 1 && elems[1] == 2 && elems[2] == 3);
        jni->ReleaseIntArrayElements(arr, elems, JNI_ABORT);

        jint result = jni->CallStaticIntMethod(clazz, orig_StaticArgTarget_sumIntArray, arr);
        assert(result == 6);
        return result;
}

JNIEXPORT jobject JNICALL hk_StaticArgTarget_passThrough(JNIEnv *jni, jclass clazz, jobject obj)
{
        assert(obj != nullptr);
        jobject result = jni->CallStaticObjectMethod(clazz, orig_StaticArgTarget_passThrough, obj);
        assert(result == obj);
        return result;
}

void
start()
{
        JavaVM *jvm;
        JNIEnv *env;
        jsize jvm_count;
        jmethodID Target_sayHello_mid;
        jmethodID Target_newTarget_mid;
        jmethodID StaticArgTarget_addInts_mid;
        jmethodID StaticArgTarget_concatStrings_mid;
        jmethodID StaticArgTarget_sumIntArray_mid;
        jmethodID StaticArgTarget_passThrough_mid;
        jint add_res;
        jstring js1 = nullptr;
        jstring js2 = nullptr;
        jstring concat_res = nullptr;
        const char *concat_c = nullptr;
        jintArray arr = nullptr;
        jint elems[3] = {0, 0, 0};
        jint sum_res;
        jobject tgt = nullptr;
        jobject pass_res = nullptr;

        // Setup JVM
        std::cout << "[*] Library loaded!" << std::endl;

        if (JNI_GetCreatedJavaVMs(&jvm, 1, &jvm_count) != JNI_OK) {
                std::cerr << "[!] Failed to get created Java VMs!" << std::endl;
                return;
        }

        std::cout << "[*] JavaVM: " << jvm << std::endl;

        if (jvm->AttachCurrentThread(reinterpret_cast<void **>(&env), NULL) != JNI_OK) {
                std::cerr << "[!] Failed to attach current thread to JVM!" << std::endl;
                return;
        }

        // Get classes and methods
        Target_class = env->FindClass("dummy/Target");
        std::cout << "[*] Class dummy.Target: " << Target_class << std::endl;

        Target_sayHello_mid = env->GetMethodID(Target_class, "sayHello", "()V");
        Target_newTarget_mid = env->GetStaticMethodID(Target_class, "newTarget", "()Ldummy/Target;");

        StaticArgTarget_class = env->FindClass("dummy/StaticArgTarget");
        StaticArgTarget_addInts_mid = env->GetStaticMethodID(StaticArgTarget_class, "addInts", "(II)I");
        StaticArgTarget_concatStrings_mid = env->GetStaticMethodID(StaticArgTarget_class, "concatStrings", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        StaticArgTarget_sumIntArray_mid = env->GetStaticMethodID(StaticArgTarget_class, "sumIntArray", "([I)I");
        StaticArgTarget_passThrough_mid = env->GetStaticMethodID(StaticArgTarget_class, "passThrough", "(Ldummy/Target;)Ldummy/Target;");

        // Place hooks
        if (auto result = JNIHook_Init(jvm); result != JNIHOOK_OK) {
                std::cerr << "[!] Failed to initialize JNIHook: " << result << std::endl;
                jvm->DetachCurrentThread();
                return;
        }

        if (auto result = JNIHook_Attach(Target_sayHello_mid, reinterpret_cast<void *>(hk_Target_sayHello), &orig_Target_sayHello); result != JNIHOOK_OK) {
                std::cerr << "[!] Failed to attach hook: " << result << std::endl;
                jvm->DetachCurrentThread();
                return;
        }
        if (auto result = JNIHook_Attach(StaticArgTarget_addInts_mid, reinterpret_cast<void *>(hk_StaticArgTarget_addInts), &orig_StaticArgTarget_addInts); result != JNIHOOK_OK) {
                std::cerr << "[!] Failed to attach addInts hook: " << result << std::endl;
                jvm->DetachCurrentThread();
                return;
        }
        if (auto result = JNIHook_Attach(StaticArgTarget_concatStrings_mid, reinterpret_cast<void *>(hk_StaticArgTarget_concatStrings), &orig_StaticArgTarget_concatStrings); result != JNIHOOK_OK) {
                std::cerr << "[!] Failed to attach concatStrings hook: " << result << std::endl;
                jvm->DetachCurrentThread();
                return;
        }
        if (auto result = JNIHook_Attach(StaticArgTarget_sumIntArray_mid, reinterpret_cast<void *>(hk_StaticArgTarget_sumIntArray), &orig_StaticArgTarget_sumIntArray); result != JNIHOOK_OK) {
                std::cerr << "[!] Failed to attach sumIntArray hook: " << result << std::endl;
                jvm->DetachCurrentThread();
                return;
        }
        if (auto result = JNIHook_Attach(StaticArgTarget_passThrough_mid, reinterpret_cast<void *>(hk_StaticArgTarget_passThrough), &orig_StaticArgTarget_passThrough); result != JNIHOOK_OK) {
                std::cerr << "[!] Failed to attach passThrough hook: " << result << std::endl;
                jvm->DetachCurrentThread();
                return;
        }

        std::cout << "[*] Hooks attached" << std::endl;

        // Invoke methods to trigger hooks
        add_res = env->CallStaticIntMethod(StaticArgTarget_class, StaticArgTarget_addInts_mid, 2, 3);
        assert(add_res == 5);

        js1 = env->NewStringUTF("foo");
        js2 = env->NewStringUTF("bar");
        concat_res = static_cast<jstring>(
            env->CallStaticObjectMethod(StaticArgTarget_class, StaticArgTarget_concatStrings_mid, js1, js2));
        concat_c = env->GetStringUTFChars(concat_res, nullptr);
        assert(std::string(concat_c) == "foobar");
        env->ReleaseStringUTFChars(concat_res, concat_c);

        arr = env->NewIntArray(3);
        elems[0] = 1; elems[1] = 2; elems[2] = 3;
        env->SetIntArrayRegion(arr, 0, 3, elems);
        sum_res = env->CallStaticIntMethod(StaticArgTarget_class, StaticArgTarget_sumIntArray_mid, arr);
        assert(sum_res == 6);

        tgt = env->CallStaticObjectMethod(Target_class, Target_newTarget_mid);
        pass_res = env->CallStaticObjectMethod(StaticArgTarget_class, StaticArgTarget_passThrough_mid, tgt);
        assert(pass_res == tgt);

        // Trigger instance hook
        env->CallVoidMethod(tgt, Target_sayHello_mid);

        // JNIHook_Shutdown();
        // std::cout << "[*] JNIHook has been shut down" << std::endl;

        jvm->DetachCurrentThread(); // NOTE: The JNIEnv must live until JNIHook_Shutdown() is called!
                                    //       (or if you won't call JNIHook again).
}

#ifdef _WIN32
#include <windows.h>
DWORD WINAPI WinThread(LPVOID lpParameter)
{
        start();
        return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
        switch (dwReason) {
        case DLL_PROCESS_ATTACH:
                CreateThread(nullptr, 0, WinThread, nullptr, 0, nullptr);
                break;
        }
        
        return TRUE;
}
#else
void *main_thread(void *arg)
{
        start();
        return NULL;
}

void __attribute__((constructor))
dl_entry()
{
        pthread_t th;
        pthread_create(&th, NULL, main_thread, NULL);
}
#endif
