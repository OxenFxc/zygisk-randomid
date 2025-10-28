#include "zygisk_device_random.h"
#include <dlfcn.h>
#include <cstring>
#include "zygisk.hpp"
#include <cstring>
#include <thread>
#include "xdl.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "zygisk.hpp"
#include "log.h"

struct ZygiskModule : zygisk::ModuleBase {

    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        const char* pkg = api->getProcessName();
        if (strcmp(pkg, "com.example.game") != 0) {
            LOGI("Skip non-target process: %s", pkg);
            api->setOption(zygisk::Option::DENY_LOAD);
            return;
        }
        LOGI("Load for target process: %s", pkg);
    }


    void preAppInit() override {
        LOGI("Start device ID randomization hook");

        hookAllDeviceIds();
    }

private:
    zygisk::Api *api;
    JNIEnv *env;


    template <typename T>
    bool hookSymbol(const char* libName, const char* symName, T hookFunc, T* origFunc) {
        void* lib = api->dlopen(libName, RTLD_NOW);
        if (!lib) {
            LOGE("dlopen %s failed: %s", libName, dlerror());
            return false;
        }
        *origFunc = (T)api->dlsym(lib, symName);
        if (!*origFunc) {
            LOGE("dlsym %s in %s failed: %s", symName, libName, dlerror());
            api->dlclose(lib);
            return false;
        }
        bool ret = api->hookFunction((void*)*origFunc, (void*)hookFunc, (void**)origFunc);
        api->dlclose(lib);
        if (ret) LOGI("Hook %s::%s success", libName, symName);
        else LOGE("Hook %s::%s failed", libName, symName);
        return ret;
    }

    // -------------------------- 各设备标识 Hook 实现 --------------------------
    // 1. IMEI Hook（兼容 Android 8-14，覆盖 getDeviceId/getImei）
    using GetDeviceIdFunc = const char* (*)(JNIEnv*, jobject);
    GetDeviceIdFunc origGetDeviceId;
    static const char* hookGetDeviceId(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::IMEI();
        LOGI("Return random IMEI: %s", cache.c_str());
        return cache.c_str();
    }

    using GetImeiFunc = const char* (*)(JNIEnv*, jobject, int);
    GetImeiFunc origGetImei;
    static const char* hookGetImei(JNIEnv* env, jobject thiz, int slot) {
        static thread_local std::string cache = RandUtil::IMEI();
        LOGI("Return random IMEI (slot %d): %s", slot, cache.c_str());
        return cache.c_str();
    }

    // 2. MAC 地址 Hook（WiFiInfo.getMacAddress/getBssid）
    using GetMacAddrFunc = const char* (*)(JNIEnv*, jobject);
    GetMacAddrFunc origGetMacAddr;
    static const char* hookGetMacAddr(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::MAC();
        LOGI("Return random MAC: %s", cache.c_str());
        return cache.c_str();
    }

    // 3. Android ID Hook（Settings.Secure.getString）
    using GetSettingsStringFunc = jstring (*)(JNIEnv*, jobject, jobject, jstring);
    GetSettingsStringFunc origGetSettingsString;
    static jstring hookGetSettingsString(JNIEnv* env, jobject thiz, jobject contentResolver, jstring key) {
        const char* keyStr = env->GetStringUTFChars(key, nullptr);
        if (keyStr && strcmp(keyStr, "android_id") == 0) {
            std::string androidId = RandUtil::Hex(16);
            LOGI("Return random Android ID: %s", androidId.c_str());
            env->ReleaseStringUTFChars(key, keyStr);
            return env->NewStringUTF(androidId.c_str());
        }
        env->ReleaseStringUTFChars(key, keyStr);
        return origGetSettingsString(env, thiz, contentResolver, key);
    }

    // 4. Hardware ID Hook（Build.HARDWARE）
    using GetHardwareFunc = const char* (*)();
    GetHardwareFunc origGetHardware;
    static const char* hookGetHardware() {
        static thread_local std::string cache = RandUtil::HardwareID();
        LOGI("Return random Hardware ID: %s", cache.c_str());
        return cache.c_str();
    }

    // 5. 手机号 Hook（TelephonyManager.getLine1Number）
    using GetLine1NumberFunc = const char* (*)(JNIEnv*, jobject);
    GetLine1NumberFunc origGetLine1Number;
    static const char* hookGetLine1Number(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::Mobile();
        LOGI("Return random Mobile No: %s", cache.c_str());
        return cache.c_str();
    }

    // 6. Sim 相关 Hook（Serial/Operator/SubId）
    using GetSimSerialFunc = const char* (*)(JNIEnv*, jobject);
    GetSimSerialFunc origGetSimSerial;
    static const char* hookGetSimSerial(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::SimSerial();
        LOGI("Return random Sim Serial: %s", cache.c_str());
        return cache.c_str();
    }

    using GetSimOperatorFunc = const char* (*)(JNIEnv*, jobject);
    GetSimOperatorFunc origGetSimOperator;
    static const char* hookGetSimOperator(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::SimOperator();
        LOGI("Return random Sim Operator: %s", cache.c_str());
        return cache.c_str();
    }

    // 7. MediaDrm ID Hook（MediaDrm.getUniqueId）
    using GetMediaDrmUniqueIdFunc = jbyteArray (*)(JNIEnv*, jobject);
    GetMediaDrmUniqueIdFunc origGetMediaDrmUniqueId;
    static jbyteArray hookGetMediaDrmUniqueId(JNIEnv* env, jobject thiz) {
        std::string drmId = RandUtil::MediaDrmID();
        LOGI("Return random MediaDrm ID: %s", drmId.c_str());
        jbyteArray arr = env->NewByteArray(drmId.size());
        env->SetByteArrayRegion(arr, 0, drmId.size(), (const jbyte*)drmId.c_str());
        return arr;
    }

    // 统一初始化所有 Hook
    void hookAllDeviceIds() {
        // IMEI 相关
        hookSymbol("libandroid_runtime.so", "_ZN7android19TelephonyManager_getDeviceIdEP7_JNIEnvP8_jobject", hookGetDeviceId, &origGetDeviceId);
        hookSymbol("libandroid_runtime.so", "_ZN7android17TelephonyManager_getImeiEP7_JNIEnvP8_jobjecti", hookGetImei, &origGetImei);
        // MAC 相关
        hookSymbol("libandroid_runtime.so", "_ZN7android13WifiInfo_getMacAddressEP7_JNIEnvP8_jobject", hookGetMacAddr, &origGetMacAddr);
        hookSymbol("libandroid_runtime.so", "_ZN7android11WifiInfo_getBssidEP7_JNIEnvP8_jobject", hookGetMacAddr, &origGetMacAddr); // Bssid 复用 MAC 逻辑
        // Android ID
        hookSymbol("libandroid_runtime.so", "_ZN7android17Settings_Secure_getStringEP7_JNIEnvP8_jobjectP8_jobjectP8_jstring", hookGetSettingsString, &origGetSettingsString);
        // Hardware ID
        hookSymbol("libandroid_runtime.so", "_ZN7android5Build_getHardwareEv", hookGetHardware, &origGetHardware);
        // 手机号
        hookSymbol("libandroid_runtime.so", "_ZN7android23TelephonyManager_getLine1NumberEP7_JNIEnvP8_jobject", hookGetLine1Number, &origGetLine1Number);
        // Sim 相关
        hookSymbol("libandroid_runtime.so", "_ZN7android25TelephonyManager_getSimSerialNumberEP7_JNIEnvP8_jobject", hookGetSimSerial, &origGetSimSerial);
        hookSymbol("libandroid_runtime.so", "_ZN7android24TelephonyManager_getSimOperatorEP7_JNIEnvP8_jobject", hookGetSimOperator, &origGetSimOperator);
        // MediaDrm ID
        hookSymbol("libmediadrm.so", "_ZN7android7MediaDrm11getUniqueIdEP7_JNIEnvP8_jobject", hookGetMediaDrmUniqueId, &origGetMediaDrmUniqueId);
    }
};

// Zygisk 模块入口（注册模块）
static void zygisk_module_entry(zygisk::Api *api, const zygisk::ModuleArgs *args) {
    api->setModule(new ZygiskModule());
}

// 定义 Zygisk 模块元数据（必须）
ZYGISK_DECLARE_MODULE(zygisk_module_entry);
// 模块信息（可选，用于 Magisk 模块列表显示）
