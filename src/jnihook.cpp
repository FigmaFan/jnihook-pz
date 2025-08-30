/*
 *  -----------------------------------
 * |         JNIHook - by rdbo         |
 * |      Java VM Hooking Library      |
 *  -----------------------------------
 */

/*
 * Copyright (C) 2023    Rdbo
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License version 3
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <jnihook.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "classfile.hpp"
#include "uuid.hpp"

typedef struct jnihook_t {
        JavaVM   *jvm;
        jvmtiEnv *jvmti;
} jnihook_t;

typedef struct method_info_t {
        std::string name;
        std::string signature;
        jint access_flags;
} method_info_t;

typedef struct hook_info_t {
        method_info_t method_info;
        void *native_hook_method;
} hook_info_t;

static std::unique_ptr<jnihook_t> g_jnihook = nullptr;
static std::unordered_map<std::string, std::vector<hook_info_t>> g_hooks;
static std::unordered_map<std::string, std::unique_ptr<ClassFile>> g_class_file_cache;
static std::unordered_map<std::string, jclass> g_original_classes;

static std::string
get_class_signature(jvmtiEnv *jvmti, jclass clazz)
{
        char *sig;
        
        if (jvmti->GetClassSignature(clazz, &sig, NULL) != JVMTI_ERROR_NONE) {
                return "";
        }

        std::string signature = std::string(sig, &sig[strlen(sig)]);

        jvmti->Deallocate(reinterpret_cast<unsigned char *>(sig));

        return signature;
}

static std::string
get_class_name(JNIEnv *env, jclass clazz)
{
        jclass klass = env->FindClass("java/lang/Class");
        if (env->ExceptionCheck() || !klass) {
                env->ExceptionClear();
                return "";
        }

        jmethodID getName_method = env->GetMethodID(klass, "getName", "()Ljava/lang/String;");
        if (env->ExceptionCheck() || !getName_method) {
                env->ExceptionClear();
                env->DeleteLocalRef(klass);
                return "";
        }

        jstring name_obj = reinterpret_cast<jstring>(env->CallObjectMethod(clazz, getName_method));
        if (env->ExceptionCheck() || !name_obj) {
                env->ExceptionClear();
                env->DeleteLocalRef(klass);
                return "";
        }

        const char *c_name = env->GetStringUTFChars(name_obj, 0);
        if (env->ExceptionCheck() || !c_name) {
                env->ExceptionClear();
                env->DeleteLocalRef(name_obj);
                env->DeleteLocalRef(klass);
                return "";
        }

        std::string name(c_name, &c_name[strlen(c_name)]);

        env->ReleaseStringUTFChars(name_obj, c_name);
        env->DeleteLocalRef(name_obj);
        env->DeleteLocalRef(klass);

        std::replace(name.begin(), name.end(), '.', '/');

        return name;
}

static std::unique_ptr<method_info_t>
get_method_info(jvmtiEnv *jvmti, jmethodID method)
{
        char *name;
        char *sig;
        jint access_flags;
        
        if (jvmti->GetMethodName(method, &name, &sig, NULL) != JVMTI_ERROR_NONE)
                return nullptr;

        if (jvmti->GetMethodModifiers(method, &access_flags) != JVMTI_ERROR_NONE)
                return nullptr;

        std::string name_str(name, &name[strlen(name)]);
        std::string signature_str(sig, &sig[strlen(sig)]);

        jvmti->Deallocate(reinterpret_cast<unsigned char *>(name));
        jvmti->Deallocate(reinterpret_cast<unsigned char *>(sig));

        return std::make_unique<method_info_t>(method_info_t { name_str, signature_str, access_flags });
}

class LocalFrameGuard {
        JNIEnv *env;
public:
        jint status;
        LocalFrameGuard(JNIEnv *e, jint capacity) : env(e) { status = env->PushLocalFrame(capacity); }
        ~LocalFrameGuard() { if (status >= 0) env->PopLocalFrame(NULL); }
};

class ThreadSuspender {
public:
        jnihook_result_t status;
        ThreadSuspender(jvmtiEnv *, JNIEnv *) : status(JNIHOOK_OK) {}
        ~ThreadSuspender() = default;
};

void JNICALL JNIHook_ClassFileLoadHook(jvmtiEnv *jvmti_env,
                                       JNIEnv* jni_env,
                                       jclass class_being_redefined,
                                       jobject loader,
                                       const char* name,
                                       jobject protection_domain,
                                       jint class_data_len,
                                       const unsigned char* class_data,
                                       jint* new_class_data_len,
                                       unsigned char** new_class_data)
{
        auto class_name = get_class_name(jni_env, class_being_redefined);

        // Don't do anything for unhooked classes
        if (class_name == "" || g_hooks.find(class_name) == g_hooks.end() || g_hooks[class_name].size() == 0)
                return;

        // Cache parsed ClassFile if it's not cached yet
        if (g_class_file_cache.find(class_name) == g_class_file_cache.end()) {
                auto cf = ClassFile::load(class_data);
                if (!cf)
                        return;

                g_class_file_cache[class_name] = std::move(cf);
        }

        return;
}

// Patches up a class with the current hooks (if any)
// and redefines it using JVMTI
jnihook_result_t
ReapplyClass(jclass clazz, std::string clazz_name)
{
        jvmtiClassDefinition class_definition;

        auto cf = *g_class_file_cache[clazz_name];

        // Build lookup table for hooked methods
        std::unordered_set<std::string> hooked_methods;
        hooked_methods.reserve(g_hooks[clazz_name].size());
        for (auto &hk_info : g_hooks[clazz_name]) {
                auto &minfo = hk_info.method_info;
                hooked_methods.insert(minfo.name + minfo.signature);
        }

        // Patch class file
        // NOTE: The `methods` attribute only has the methods defined by the main class of this ClassFile
        //       Method references are not included here
        //       If the source file has more than one class, they are compiled as separate ClassFiles
        for (auto &method : cf.get_methods()) {
                auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.name_index).bytes.data()
                );

                auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.descriptor_index).bytes.data()
                );

                auto name = std::string(name_ci->bytes, &name_ci->bytes[name_ci->length]);
                auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                // Check if the current method is a method that should be hooked
                if (hooked_methods.find(name + descriptor) == hooked_methods.end())
                        continue;

                // Skip methods that are already native or abstract
                if ((method.access_flags & (ACC_NATIVE | ACC_ABSTRACT)) != 0)
                        continue;

                // Set method to native
                method.access_flags |= ACC_NATIVE;

                // Remove "Code" attribute
                for (size_t i = 0; i < method.attributes.size(); ++i) {
                        auto attr = method.attributes[i];
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(attr.attribute_name_index).bytes.data()
                        );
                        auto attr_name = std::string(attr_name_ci->bytes, &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name == "Code") {
                                method.attributes.erase(method.attributes.begin() + i);
                                break;
                        }
                }
        }

        // Redefine class with modified ClassFile
        auto cf_bytes = cf.bytes();

        class_definition.klass = clazz;
        class_definition.class_byte_count = cf_bytes.size();
        class_definition.class_bytes = cf_bytes.data();
        if (g_jnihook->jvmti->RedefineClasses(1, &class_definition) != JVMTI_ERROR_NONE)
                return JNIHOOK_ERR_JVMTI_OPERATION;

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Init(JavaVM *jvm)
{
        jvmtiEnv *jvmti;
        jvmtiCapabilities capabilities;
        jvmtiEventCallbacks callbacks = {};

        if (jvm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
                return JNIHOOK_ERR_GET_JVMTI;
        }

        if (jvmti->GetPotentialCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_ADD_JVMTI_CAPS;
        }

        capabilities.can_redefine_classes = 1;
        capabilities.can_redefine_any_class = 1;
        capabilities.can_retransform_classes = 1;
        capabilities.can_retransform_any_class = 1;

        if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_ADD_JVMTI_CAPS;
        }

        callbacks.ClassFileLoadHook = JNIHook_ClassFileLoadHook;
        if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
        }

        g_jnihook = std::make_unique<jnihook_t>(jnihook_t { jvm, jvmti });

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Attach(jmethodID method, void *native_hook_method, jmethodID *original_method)
{
        jclass clazz;
        std::string clazz_name;
        hook_info_t hook_info;
        jobject class_loader;
        JNIEnv *env;

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvmti->GetMethodDeclaringClass(method, &clazz) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        clazz_name = get_class_name(env, clazz);
        if (clazz_name.length() == 0) {
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        auto method_info = get_method_info(g_jnihook->jvmti, method);
        if (!method_info) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        hook_info.method_info = *method_info;
        hook_info.native_hook_method = native_hook_method;

        // Force caching of the class being hooked
        if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                if (g_jnihook->jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
                        return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
                }

                // Temporarily register hook in g_hooks so that `ClassFileLoadHook` can see it
                // Leaving it there could be a problem if this hook fails, it will still patch
                // the class when JNIHook_Attach is called again for that same class, but won't
                // register the native method, causing `java.lang.UnsatisfiedLinkError`.
                g_hooks[clazz_name].push_back(hook_info);
                auto result = g_jnihook->jvmti->RetransformClasses(1, &clazz);
                g_hooks[clazz_name].pop_back();

                // NOTE: We disable the ClassFileLoadHook here because it breaks
                //       any `env->DefineClass()` calls. Also, it's not necessary
                //       to keep it active at all times, we just have to use it for caching
                //       uncached hooked classes.
                // TODO: Investigate why it breaks it (possibly NullPointerException in
                //       JNIHook_ClassFileLoadHook)
                if (g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
                        return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
                }

                if (result != JVMTI_ERROR_NONE)
                        return JNIHOOK_ERR_CLASS_FILE_CACHE;

                if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                        return JNIHOOK_ERR_CLASS_FILE_CACHE;
                }
        }

        // Make copy of the class prior to hooking it
        // (allows calling the original functions)
        if (g_original_classes.find(clazz_name) == g_original_classes.end()) {
                std::string class_copy_name = clazz_name + "_" + GenerateUuid();
                std::string class_shortname = class_copy_name.substr(class_copy_name.find_last_of('/') + 1);
                std::string class_copy_source_name = class_shortname + ".java";
                jclass class_copy;
                auto cf = *g_class_file_cache[clazz_name];

                // Patch source file name (Java will refuse to define the class otherwise)
                for (auto &attr : cf.get_attributes()) {
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(attr.attribute_name_index).bytes.data()
                        );
                        auto attr_name = std::string(attr_name_ci->bytes, &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name != "SourceFile")
                                continue;

                        u2 attr_index_be = ((attr.attribute_name_index >> 8) & 0xff) |
                                           ((attr.attribute_name_index & 0xff) << 8);
                        u2 source = *reinterpret_cast<u2 *>(attr.info.data());

                        // Some classes have 'SourceFile' attribute be equal to 'SourceFile',
                        // and not 'ClassName.java'. For those, we won't set a custom source.
                        if (source == attr_index_be)
                                break;

                        // Overwrite constant pool item
                        CONSTANT_Utf8_info ci;
                        cp_info sourcefile_cpi;
                        ci.tag = CONSTANT_Utf8;
                        ci.length = static_cast<u2>(class_copy_source_name.size());

                        sourcefile_cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                        memcpy(sourcefile_cpi.bytes.data(), &ci, sizeof(ci));
                        memcpy(&sourcefile_cpi.bytes.data()[sizeof(ci)], class_copy_source_name.c_str(), ci.length);

                        cf.set_constant_pool_item_be(source, sourcefile_cpi);
                }

                // Patch class name (Java will refuse to define the class otherwise)
                for (auto &cpi : cf.get_constant_pool()) {
                        if (cpi.bytes[0] != CONSTANT_Class)
                                continue;

                        auto class_ci = reinterpret_cast<CONSTANT_Class_info *>(
                                cpi.bytes.data()
                        );

                        auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(class_ci->name_index).bytes.data()
                        );

                        auto name = std::string(name_ci->bytes, &name_ci->bytes[name_ci->length]);

                        if (name == clazz_name) {
                                // Overwrite constant pool item
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(class_copy_name.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], class_copy_name.c_str(), ci.length);

                                cf.set_constant_pool_item(class_ci->name_index, cpi);
                                break; // TODO: Assure that the ClassName can only happen once per ClassFile!
                        }
                }

                // Patch NameAndType things that instance the current class
                // NOTE: This is an attempt to fix the following exception when
                //       trying to get the method ID after defining the class:
                //
                // Type 'OrigClass' (current frame, stack[0]) is not assignable to 'OrigClass_<UUID>'
                auto constant_pool = cf.get_constant_pool();
                for (auto &item : constant_pool) {
                        if (item.bytes[0] != CONSTANT_NameAndType)
                                continue;

                        auto nt_ci = reinterpret_cast<CONSTANT_NameAndType_info *>(item.bytes.data());
                        auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(nt_ci->descriptor_index).bytes.data()
                        );
                        auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                        std::string clazz_desc = std::string("L") + clazz_name + ";";
                        std::string clazz_copy_desc = std::string("L") + class_copy_name + ";";
                        if (auto index = descriptor.find(clazz_desc); index != descriptor.npos) {
                                // Overwrite constant pool item
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;
                                std::string new_descriptor = descriptor.replace(index, clazz_desc.size(), clazz_copy_desc);

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(new_descriptor.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], new_descriptor.c_str(), ci.length);

                                cf.set_constant_pool_item(nt_ci->descriptor_index, cpi);
                        }
                }

                // Patch method descriptors
                // NOTE: Not every Type or return Type is referenced by a NameAndType
                //       So we have to check the method descriptors as well
                auto methods = cf.get_methods();
                for (auto& method : methods)
                {
                        auto descriptor_index = method.descriptor_index;

                        auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info*>(
                                cf.get_constant_pool_item(descriptor_index).bytes.data()
                                );

                        auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                        std::string clazz_desc = std::string("L") + clazz_name + ";";
                        std::string clazz_copy_desc = std::string("L") + class_copy_name + ";";

                        for (size_t index; (index = descriptor.find(clazz_desc)) != descriptor.npos;)
                        {
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;
                                std::string new_descriptor = descriptor.replace(index, clazz_desc.size(), clazz_copy_desc);

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(new_descriptor.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], new_descriptor.c_str(), ci.length);

                                cf.set_constant_pool_item(descriptor_index, cpi);
                        }
                }

                auto class_data = cf.bytes();

                if (g_jnihook->jvmti->GetClassLoader(clazz, &class_loader) != JVMTI_ERROR_NONE)
                        return JNIHOOK_ERR_JVMTI_OPERATION;

                class_copy = env->DefineClass(NULL, class_loader,
                                              reinterpret_cast<const jbyte *>(class_data.data()),
                                              class_data.size());

                if (!class_copy)
                        return JNIHOOK_ERR_JNI_OPERATION;

                g_original_classes[clazz_name] = class_copy;
        }

        // Verify that everything was cached correctly
        if (g_original_classes.find(clazz_name) == g_original_classes.end()) {
                return JNIHOOK_ERR_CLASS_FILE_CACHE;
        }

        // Get original method before applying hooks, because this is fallible
        if (original_method) {
                jclass orig_class = g_original_classes[clazz_name];
                jmethodID orig;

                if ((method_info->access_flags & ACC_STATIC) == ACC_STATIC) {
                        orig = env->GetStaticMethodID(orig_class, method_info->name.c_str(),
                                                      method_info->signature.c_str());
                } else {
                        orig = env->GetMethodID(orig_class, method_info->name.c_str(),
                                                method_info->signature.c_str());
                }

                if (!orig || env->ExceptionOccurred()) {
                        env->ExceptionClear();
                        return JNIHOOK_ERR_JAVA_EXCEPTION;
                }

                *original_method = orig;
        }

        LocalFrameGuard frame_guard(env, 16);
        if (frame_guard.status < 0)
                return JNIHOOK_ERR_JNI_OPERATION;

        ThreadSuspender susp(g_jnihook->jvmti, env);
        if (susp.status != JNIHOOK_OK)
                return susp.status;

        // Apply current hooks
        g_hooks[clazz_name].push_back(hook_info);
        jnihook_result_t ret;
        if ((ret = ReapplyClass(clazz, clazz_name)) != JNIHOOK_OK) {
                g_hooks[clazz_name].pop_back();
                return ret;
        }

        // Register native method for JVM lookup
        JNINativeMethod native_method;
        native_method.name = const_cast<char *>(method_info->name.c_str());
        native_method.signature = const_cast<char *>(method_info->signature.c_str());
        native_method.fnPtr = native_hook_method;

        if (env->RegisterNatives(clazz, &native_method, 1) < 0) {
                g_hooks[clazz_name].pop_back();
                ReapplyClass(clazz, clazz_name); // Attempt to restore class to previous state
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Detach(jmethodID method)
{
        JNIEnv *env;
        jclass clazz;
        std::string clazz_name;

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvmti->GetMethodDeclaringClass(method, &clazz) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        clazz_name = get_class_name(env, clazz);
        if (clazz_name.length() == 0) {
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        if (g_hooks.find(clazz_name) == g_hooks.end() || g_hooks[clazz_name].size() == 0) {
                return JNIHOOK_OK;
        }

        auto method_info = get_method_info(g_jnihook->jvmti, method);
        if (!method_info) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        for (size_t i = 0; i < g_hooks[clazz_name].size(); ++i) {
                auto &hook_info = g_hooks[clazz_name][i];
                if (hook_info.method_info.name != method_info->name ||
                    hook_info.method_info.signature != method_info->signature)
                        continue;

                g_hooks[clazz_name].erase(g_hooks[clazz_name].begin() + i);
        }

        return ReapplyClass(clazz, clazz_name);
}


JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Shutdown()
{
        JNIEnv *env;
        jvmtiEventCallbacks callbacks = {};

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        for (auto &[key, _value] : g_class_file_cache) {
                jclass clazz = env->FindClass(key.c_str());

                g_hooks[key].clear();

                if (!clazz)
                        continue;

                // Reapplying the class with empty hooks will just restore the original one.
                ReapplyClass(clazz, key);
        }

        g_class_file_cache.clear();

        // TODO: Fully cleanup defined classes in `g_original_classes` by deleting them from the JVM memory
        //       (if possible without doing crazy hacks)
        g_original_classes.clear();

        g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
        g_jnihook->jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        g_jnihook = nullptr;

        return JNIHOOK_OK;
}
