#include <gtest/gtest.h>
#include <jnihook.h>
#include <cstring>

// Global mock JVMTI environment used by GetEnv mock
static jvmtiEnv* g_mock_jvmti_env = nullptr;

static jint JNICALL MockGetEnvSuccess(JavaVM *, void **penv, jint) {
    *penv = g_mock_jvmti_env;
    return JNI_OK;
}

static jint JNICALL MockGetEnvFail(JavaVM *, void **penv, jint) {
    return JNI_ERR;
}

static jvmtiError JNICALL MockGetPotentialCapabilities(jvmtiEnv *, jvmtiCapabilities *caps) {
    std::memset(caps, 0, sizeof(*caps));
    return JVMTI_ERROR_NONE;
}

static jvmtiError JNICALL MockAddCapabilitiesSuccess(jvmtiEnv *, const jvmtiCapabilities *) {
    return JVMTI_ERROR_NONE;
}

static jvmtiError JNICALL MockAddCapabilitiesFail(jvmtiEnv *, const jvmtiCapabilities *) {
    return JVMTI_ERROR_OUT_OF_MEMORY;
}

static jvmtiError JNICALL MockSetEventCallbacks(jvmtiEnv *, const jvmtiEventCallbacks *, jint) {
    return JVMTI_ERROR_NONE;
}

TEST(JNIHookInitTest, InitializesSuccessfully) {
    // Setup JVMTI function table
    jvmtiInterface_1_ jvmti_funcs = {};
    jvmti_funcs.GetPotentialCapabilities = MockGetPotentialCapabilities;
    jvmti_funcs.AddCapabilities = MockAddCapabilitiesSuccess;
    jvmti_funcs.SetEventCallbacks = MockSetEventCallbacks;
    _jvmtiEnv jvmti_env = { &jvmti_funcs };
    g_mock_jvmti_env = &jvmti_env;

    // Setup JavaVM with successful GetEnv
    JNIInvokeInterface_ jni_funcs = {};
    jni_funcs.GetEnv = MockGetEnvSuccess;
    JavaVM_ jvm = { &jni_funcs };

    EXPECT_EQ(JNIHook_Init(reinterpret_cast<JavaVM*>(&jvm)), JNIHOOK_OK);
}

TEST(JNIHookInitTest, GetEnvFailure) {
    // JavaVM where GetEnv fails
    JNIInvokeInterface_ jni_funcs = {};
    jni_funcs.GetEnv = MockGetEnvFail;
    JavaVM_ jvm = { &jni_funcs };

    EXPECT_EQ(JNIHook_Init(reinterpret_cast<JavaVM*>(&jvm)), JNIHOOK_ERR_GET_JVMTI);
}

TEST(JNIHookInitTest, AddCapabilitiesFailure) {
    // Setup JVMTI function table with failing AddCapabilities
    jvmtiInterface_1_ jvmti_funcs = {};
    jvmti_funcs.GetPotentialCapabilities = MockGetPotentialCapabilities;
    jvmti_funcs.AddCapabilities = MockAddCapabilitiesFail;
    jvmti_funcs.SetEventCallbacks = MockSetEventCallbacks;
    _jvmtiEnv jvmti_env = { &jvmti_funcs };
    g_mock_jvmti_env = &jvmti_env;

    // JavaVM with successful GetEnv
    JNIInvokeInterface_ jni_funcs = {};
    jni_funcs.GetEnv = MockGetEnvSuccess;
    JavaVM_ jvm = { &jni_funcs };

    EXPECT_EQ(JNIHook_Init(reinterpret_cast<JavaVM*>(&jvm)), JNIHOOK_ERR_ADD_JVMTI_CAPS);
}

