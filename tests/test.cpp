#include <jnihook.h>
#include <jni.h>

#include <cassert>
#include <string>

// Globale Klassen-/Methoden-Handles & Hook-Backups
static jclass Target_class = nullptr;
static jclass StaticArgTarget_class = nullptr;

static jmethodID orig_Target_sayHello = nullptr;
static jmethodID orig_StaticArgTarget_addInts = nullptr;
static jmethodID orig_StaticArgTarget_concatStrings = nullptr;
static jmethodID orig_StaticArgTarget_sumIntArray = nullptr;
static jmethodID orig_StaticArgTarget_passThrough = nullptr;

static bool hook_called = false;

// -------- Hook-Implementierungen --------

JNIEXPORT void JNICALL hk_Target_sayHello(JNIEnv *jni, jobject obj) {
    hook_called = true;
    jni->CallNonvirtualVoidMethod(obj, Target_class, orig_Target_sayHello);
}

JNIEXPORT jint JNICALL hk_StaticArgTarget_addInts(JNIEnv *jni, jclass clazz, jint a, jint b) {
    assert(a == 2);
    assert(b == 3);
    jint result = jni->CallStaticIntMethod(clazz, orig_StaticArgTarget_addInts, a, b);
    assert(result == 5);
    return result;
}

JNIEXPORT jstring JNICALL hk_StaticArgTarget_concatStrings(JNIEnv *jni, jclass clazz, jstring a, jstring b) {
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

JNIEXPORT jint JNICALL hk_StaticArgTarget_sumIntArray(JNIEnv *jni, jclass clazz, jintArray arr) {
    jsize len = jni->GetArrayLength(arr);
    assert(len == 3);
    jint *elems = jni->GetIntArrayElements(arr, nullptr);
    assert(elems[0] == 1 && elems[1] == 2 && elems[2] == 3);
    jni->ReleaseIntArrayElements(arr, elems, JNI_ABORT);

    jint result = jni->CallStaticIntMethod(clazz, orig_StaticArgTarget_sumIntArray, arr);
    assert(result == 6);
    return result;
}

JNIEXPORT jobject JNICALL hk_StaticArgTarget_passThrough(JNIEnv *jni, jclass clazz, jobject obj) {
    assert(obj != nullptr);
    jobject result = jni->CallStaticObjectMethod(clazz, orig_StaticArgTarget_passThrough, obj);
    assert(result == obj);
    return result;
}

// -------- Java-Runner (für die Dummy-Tests) --------

extern "C" JNIEXPORT jboolean JNICALL
Java_dummy_HookTests_run(JNIEnv *env, jclass) {
    JavaVM *jvm = nullptr;
    if (env->GetJavaVM(&jvm) != JNI_OK)
        return JNI_FALSE;

    if (JNIHook_Init(jvm) != JNIHOOK_OK)
        return JNI_FALSE;

    // Target-Klasse & Methoden
    jclass localTarget = env->FindClass("dummy/Target");
    if (!localTarget)
        return JNI_FALSE;
    Target_class = static_cast<jclass>(env->NewGlobalRef(localTarget));

    jmethodID sayHello_mid = env->GetMethodID(Target_class, "sayHello", "()V");
    if (!sayHello_mid)
        return JNI_FALSE;

    // Instanz für Target besorgen (newTarget() bevorzugt, sonst AllocObject)
    jobject target = nullptr;
    jmethodID newTarget_mid = env->GetStaticMethodID(Target_class, "newTarget", "()Ldummy/Target;");
    if (newTarget_mid) {
        target = env->CallStaticObjectMethod(Target_class, newTarget_mid);
    } else {
        target = env->AllocObject(Target_class);
    }
    if (!target)
        return JNI_FALSE;

    // Hook für sayHello anbringen
    if (JNIHook_Attach(sayHello_mid,
                       reinterpret_cast<void *>(hk_Target_sayHello),
                       &orig_Target_sayHello) != JNIHOOK_OK)
        return JNI_FALSE;

    // Erste Invocation -> Hook muss feuern
    hook_called = false;
    env->CallVoidMethod(target, sayHello_mid);
    if (!hook_called)
        return JNI_FALSE;

    // "Already hooked" prüfen
    jmethodID tmp = nullptr;
    if (JNIHook_Attach(sayHello_mid,
                       reinterpret_cast<void *>(hk_Target_sayHello),
                       &tmp) != JNIHOOK_ERR_ALREADY_HOOKED)
        return JNI_FALSE;

    // -------- StaticArgTarget-Setup & Hooks --------
    StaticArgTarget_class = env->FindClass("dummy/StaticArgTarget");
    if (!StaticArgTarget_class)
        return JNI_FALSE;

    jmethodID StaticArgTarget_addInts_mid =
        env->GetStaticMethodID(StaticArgTarget_class, "addInts", "(II)I");
    jmethodID StaticArgTarget_concatStrings_mid =
        env->GetStaticMethodID(StaticArgTarget_class, "concatStrings", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    jmethodID StaticArgTarget_sumIntArray_mid =
        env->GetStaticMethodID(StaticArgTarget_class, "sumIntArray", "([I)I");
    jmethodID StaticArgTarget_passThrough_mid =
        env->GetStaticMethodID(StaticArgTarget_class, "passThrough", "(Ldummy/Target;)Ldummy/Target;");

    if (!StaticArgTarget_addInts_mid || !StaticArgTarget_concatStrings_mid ||
        !StaticArgTarget_sumIntArray_mid || !StaticArgTarget_passThrough_mid)
        return JNI_FALSE;

    if (JNIHook_Attach(StaticArgTarget_addInts_mid,
                       reinterpret_cast<void *>(hk_StaticArgTarget_addInts),
                       &orig_StaticArgTarget_addInts) != JNIHOOK_OK)
        return JNI_FALSE;

    if (JNIHook_Attach(StaticArgTarget_concatStrings_mid,
                       reinterpret_cast<void *>(hk_StaticArgTarget_concatStrings),
                       &orig_StaticArgTarget_concatStrings) != JNIHOOK_OK)
        return JNI_FALSE;

    if (JNIHook_Attach(StaticArgTarget_sumIntArray_mid,
                       reinterpret_cast<void *>(hk_StaticArgTarget_sumIntArray),
                       &orig_StaticArgTarget_sumIntArray) != JNIHOOK_OK)
        return JNI_FALSE;

    if (JNIHook_Attach(StaticArgTarget_passThrough_mid,
                       reinterpret_cast<void *>(hk_StaticArgTarget_passThrough),
                       &orig_StaticArgTarget_passThrough) != JNIHOOK_OK)
        return JNIHOOK_OK;

    // ---- Aufrufe zum Auslösen der Static-Arg-Hooks ----
    jint add_res = env->CallStaticIntMethod(StaticArgTarget_class, StaticArgTarget_addInts_mid, 2, 3);
    assert(add_res == 5);

    jstring js1 = env->NewStringUTF("foo");
    jstring js2 = env->NewStringUTF("bar");
    jstring concat_res = static_cast<jstring>(
        env->CallStaticObjectMethod(StaticArgTarget_class, StaticArgTarget_concatStrings_mid, js1, js2));
    const char *concat_c = env->GetStringUTFChars(concat_res, nullptr);
    assert(std::string(concat_c) == "foobar");
    env->ReleaseStringUTFChars(concat_res, concat_c);

    jintArray arr = env->NewIntArray(3);
    jint elems[3] = {1, 2, 3};
    env->SetIntArrayRegion(arr, 0, 3, elems);
    jint sum_res = env->CallStaticIntMethod(StaticArgTarget_class, StaticArgTarget_sumIntArray_mid, arr);
    assert(sum_res == 6);

    jobject pass_res = env->CallStaticObjectMethod(StaticArgTarget_class, StaticArgTarget_passThrough_mid, target);
    assert(pass_res == target);

    // Nochmals sayHello -> Hook muss noch feuern
    hook_called = false;
    env->CallVoidMethod(target, sayHello_mid);
    if (!hook_called)
        return JNI_FALSE;

    // Detach des Instance-Hooks und prüfen, dass er wirklich weg ist
    if (JNIHook_Detach(sayHello_mid) != JNIHOOK_OK)
        return JNI_FALSE;

    hook_called = false;
    env->CallVoidMethod(target, sayHello_mid);
    if (hook_called)
        return JNI_FALSE;

    // Optional: Static-Hooks ebenfalls lösen (nicht zwingend)
    // JNIHook_Detach(StaticArgTarget_addInts_mid);
    // JNIHook_Detach(StaticArgTarget_concatStrings_mid);
    // JNIHook_Detach(StaticArgTarget_sumIntArray_mid);
    // JNIHook_Detach(StaticArgTarget_passThrough_mid);

    return JNI_TRUE;
}
