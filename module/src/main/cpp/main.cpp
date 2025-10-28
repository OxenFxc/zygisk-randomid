#include "zygisk_device_random.h"
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "log.h"

#include "zygisk.hpp"

// 基于Zygisk API v2实现模块类
struct ZygiskModule : zygisk::ModuleBase {

    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        this->target_pkg = "com.example.game";
        LOGI("Zygisk module loaded, waiting for app specialize");
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 从AppSpecializeArgs获取目标包名（API v2标准方式）
        const char *pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (pkg == nullptr || strcmp(pkg, target_pkg.c_str()) != 0) {
            LOGI("Skip non-target process: %s", pkg ? pkg : "unknown");
            env->ReleaseStringUTFChars(args->nice_name, pkg);
            return;
        }
        LOGI("Load for target process: %s", pkg);
        env->ReleaseStringUTFChars(args->nice_name, pkg);

        // 执行设备标识Hook（改用Zygisk pltHook）
        LOGI("Start device ID randomization hook");
        hookAllDeviceIds();
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    std::string target_pkg;

    // 2. 所有Hook原函数指针加static（解决静态函数访问问题）
    // IMEI相关
    using GetDeviceIdFunc = const char* (*)(JNIEnv*, jobject);
    static GetDeviceIdFunc origGetDeviceId;

    using GetImeiFunc = const char* (*)(JNIEnv*, jobject, int);
    static GetImeiFunc origGetImei;

    // MAC相关
    using GetMacAddrFunc = const char* (*)(JNIEnv*, jobject);
    static GetMacAddrFunc origGetMacAddr;

    // Android ID相关
    using GetSettingsStringFunc = jstring (*)(JNIEnv*, jobject, jobject, jstring);
    static GetSettingsStringFunc origGetSettingsString;

    // Hardware ID相关
    using GetHardwareFunc = const char* (*)();
    static GetHardwareFunc origGetHardware;

    // 手机号相关
    using GetLine1NumberFunc = const char* (*)(JNIEnv*, jobject);
    static GetLine1NumberFunc origGetLine1Number;

    // Sim相关
    using GetSimSerialFunc = const char* (*)(JNIEnv*, jobject);
    static GetSimSerialFunc origGetSimSerial;

    using GetSimOperatorFunc = const char* (*)(JNIEnv*, jobject);
    static GetSimOperatorFunc origGetSimOperator;

    // MediaDrm相关
    using GetMediaDrmUniqueIdFunc = jbyteArray (*)(JNIEnv*, jobject);
    static GetMediaDrmUniqueIdFunc origGetMediaDrmUniqueId;

    // 3. 核心修改：用Zygisk pltHook替代xdl_hook_symbol（无第三方依赖）
    template <typename T>
    void hookSymbol(const char* libRegex, const char* symName, T hookFunc, T* origFunc) {
        // 注册PLT Hook：参数1=库正则（精确匹配），参数2=函数名，参数3=新函数，参数4=保存原函数地址
        api->pltHookRegister(libRegex, symName, (void*)hookFunc, (void**)origFunc);
        LOGI("Registered Hook: %s -> %s", libRegex, symName);
    }

    // 4. 各设备标识Hook实现（逻辑不变，适配static指针）
    static const char* hookGetDeviceId(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::IMEI();
        LOGI("Return random IMEI: %s", cache.c_str());
        return cache.c_str();
    }

    static const char* hookGetImei(JNIEnv* env, jobject thiz, int slot) {
        static thread_local std::string cache = RandUtil::IMEI();
        LOGI("Return random IMEI (slot %d): %s", slot, cache.c_str());
        return cache.c_str();
    }

    static const char* hookGetMacAddr(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::MAC();
        LOGI("Return random MAC: %s", cache.c_str());
        return cache.c_str();
    }

    static jstring hookGetSettingsString(JNIEnv* env, jobject thiz, jobject contentResolver, jstring key) {
        const char* keyStr = env->GetStringUTFChars(key, nullptr);
        if (keyStr && strcmp(keyStr, "android_id") == 0) {
            std::string androidId = RandUtil::Hex(16);
            LOGI("Return random Android ID: %s", androidId.c_str());
            env->ReleaseStringUTFChars(key, keyStr);
            return env->NewStringUTF(androidId.c_str());
        }
        env->ReleaseStringUTFChars(key, keyStr);
        // 引用static原函数指针，无访问错误
        return origGetSettingsString(env, thiz, contentResolver, key);
    }

    static const char* hookGetHardware() {
        static thread_local std::string cache = RandUtil::HardwareID();
        LOGI("Return random Hardware ID: %s", cache.c_str());
        return cache.c_str();
    }

    static const char* hookGetLine1Number(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::Mobile();
        LOGI("Return random Mobile No: %s", cache.c_str());
        return cache.c_str();
    }

    static const char* hookGetSimSerial(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::SimSerial();
        LOGI("Return random Sim Serial: %s", cache.c_str());
        return cache.c_str();
    }

    static const char* hookGetSimOperator(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::SimOperator();
        LOGI("Return random Sim Operator: %s", cache.c_str());
        return cache.c_str();
    }

    static jbyteArray hookGetMediaDrmUniqueId(JNIEnv* env, jobject thiz) {
        std::string drmId = RandUtil::MediaDrmID();
        LOGI("Return random MediaDrm ID: %s", drmId.c_str());
        jbyteArray arr = env->NewByteArray(drmId.size());
        env->SetByteArrayRegion(arr, 0, drmId.size(), (const jbyte*)drmId.c_str());
        return arr;
    }

    // 5. 统一注册所有Hook并提交（Zygisk pltHook需commit才生效）
    void hookAllDeviceIds() {
        // 批量注册Hook（库正则精确匹配，避免误Hook）
        hookSymbol("^libandroid_runtime.so$", "_ZN7android19TelephonyManager_getDeviceIdEP7_JNIEnvP8_jobject", hookGetDeviceId, &origGetDeviceId);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android17TelephonyManager_getImeiEP7_JNIEnvP8_jobjecti", hookGetImei, &origGetImei);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android13WifiInfo_getMacAddressEP7_JNIEnvP8_jobject", hookGetMacAddr, &origGetMacAddr);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android11WifiInfo_getBssidEP7_JNIEnvP8_jobject", hookGetMacAddr, &origGetMacAddr);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android17Settings_Secure_getStringEP7_JNIEnvP8_jobjectP8_jobjectP8_jstring", hookGetSettingsString, &origGetSettingsString);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android5Build_getHardwareEv", hookGetHardware, &origGetHardware);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android23TelephonyManager_getLine1NumberEP7_JNIEnvP8_jobject", hookGetLine1Number, &origGetLine1Number);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android25TelephonyManager_getSimSerialNumberEP7_JNIEnvP8_jobject", hookGetSimSerial, &origGetSimSerial);
        hookSymbol("^libandroid_runtime.so$", "_ZN7android24TelephonyManager_getSimOperatorEP7_JNIEnvP8_jobject", hookGetSimOperator, &origGetSimOperator);
        hookSymbol("^libmediadrm.so$", "_ZN7android7MediaDrm11getUniqueIdEP7_JNIEnvP8_jobject", hookGetMediaDrmUniqueId, &origGetMediaDrmUniqueId);

        // 提交所有Hook（关键步骤，未提交则Hook不生效）
        bool commitOk = api->pltHookCommit();
        if (commitOk) {
            LOGI("All device ID hooks committed successfully");
        } else {
            LOGE("Failed to commit device ID hooks");
        }
    }
};

// 6. 静态原函数指针初始化（C++必做，否则链接错误）
ZygiskModule::GetDeviceIdFunc ZygiskModule::origGetDeviceId = nullptr;
ZygiskModule::GetImeiFunc ZygiskModule::origGetImei = nullptr;
ZygiskModule::GetMacAddrFunc ZygiskModule::origGetMacAddr = nullptr;
ZygiskModule::GetSettingsStringFunc ZygiskModule::origGetSettingsString = nullptr;
ZygiskModule::GetHardwareFunc ZygiskModule::origGetHardware = nullptr;
ZygiskModule::GetLine1NumberFunc ZygiskModule::origGetLine1Number = nullptr;
ZygiskModule::GetSimSerialFunc ZygiskModule::origGetSimSerial = nullptr;
ZygiskModule::GetSimOperatorFunc ZygiskModule::origGetSimOperator = nullptr;
ZygiskModule::GetMediaDrmUniqueIdFunc ZygiskModule::origGetMediaDrmUniqueId = nullptr;

// 7. 注册Zygisk模块（API v2标准宏）
REGISTER_ZYGISK_MODULE(ZygiskModule);
