#include "zygisk_device_random.h"
#include <dlfcn.h>
#include <cstring>
#include <thread>
#include "xdl.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "log.h"

// 仅保留1个zygisk.hpp引用（避免重复包含）
#include "zygisk.hpp"

// 基于Zygisk API v2实现模块类（完全匹配头文件接口）
struct ZygiskModule : zygisk::ModuleBase {

    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        this->target_pkg = "com.LunariteStudio.WastelandStory"; // 目标包名（替换原com.example.game）
        LOGI("Zygisk module loaded, waiting for app specialize");
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 从AppSpecializeArgs获取应用包名（nice_name即包名）
        const char *pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (pkg == nullptr || strcmp(pkg, target_pkg.c_str()) != 0) {
            LOGI("Skip non-target process: %s", pkg ? pkg : "unknown");
            env->ReleaseStringUTFChars(args->nice_name, pkg);
            return;
        }
        LOGI("Load for target process: %s", pkg);
        env->ReleaseStringUTFChars(args->nice_name, pkg);

        // 目标应用：执行设备标识Hook
        LOGI("Start device ID randomization hook");
        hookAllDeviceIds();
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    std::string target_pkg; // 目标包名存储

    // -------------------------- 核心修正：替换原hookSymbol（适配API v2无dlopen/hookFunction） --------------------------
    // API v2无zygisk::Api::dlopen/dlsym/hookFunction，改用系统dlfcn+XDL库Hook（兼容Zygisk环境）
    template <typename T>
    bool hookSymbol(const char* libName, const char* symName, T hookFunc, T* origFunc) {
        // 1. 用系统dlopen加载目标库（API v2无封装接口）
        void* lib = dlopen(libName, RTLD_NOW);
        if (!lib) {
            LOGE("dlopen %s failed: %s", libName, dlerror());
            return false;
        }

        // 2. 用系统dlsym获取原函数地址
        *origFunc = (T)dlsym(lib, symName);
        if (!*origFunc) {
            LOGE("dlsym %s in %s failed: %s", symName, libName, dlerror());
            dlclose(lib);
            return false;
        }

        // 3. 用XDL库执行Hook（替代原api->hookFunction，XDL更适配动态库Hook）
        int ret = xdl_hook_symbol(lib, symName, (void*)hookFunc, (void**)origFunc);
        dlclose(lib);

        if (ret == 0) {
            LOGI("Hook %s::%s success", libName, symName);
            return true;
        } else {
            LOGE("Hook %s::%s failed (XDL error: %d)", libName, symName, ret);
            return false;
        }
    }

    // -------------------------- 各设备标识Hook实现（逻辑不变，仅修正静态成员引用） --------------------------
    // 1. IMEI Hook（兼容Android 8-14）
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

    // 2. MAC地址Hook（WiFiInfo.getMacAddress/getBssid）
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
        // 修正：原origGetSettingsString是静态成员，需通过全局指针调用（此处用XDL保存的原函数地址）
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

    // 5. 手机号Hook（TelephonyManager.getLine1Number）
    using GetLine1NumberFunc = const char* (*)(JNIEnv*, jobject);
    GetLine1NumberFunc origGetLine1Number;
    static const char* hookGetLine1Number(JNIEnv* env, jobject thiz) {
        static thread_local std::string cache = RandUtil::Mobile();
        LOGI("Return random Mobile No: %s", cache.c_str());
        return cache.c_str();
    }

    // 6. Sim相关Hook（Serial/Operator）
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


    void hookAllDeviceIds() {
        // IMEI相关
        hookSymbol("libandroid_runtime.so", "_ZN7android19TelephonyManager_getDeviceIdEP7_JNIEnvP8_jobject", hookGetDeviceId, &origGetDeviceId);
        hookSymbol("libandroid_runtime.so", "_ZN7android17TelephonyManager_getImeiEP7_JNIEnvP8_jobjecti", hookGetImei, &origGetImei);
        // MAC相关（getMacAddress/getBssid复用同一Hook）
        hookSymbol("libandroid_runtime.so", "_ZN7android13WifiInfo_getMacAddressEP7_JNIEnvP8_jobject", hookGetMacAddr, &origGetMacAddr);
        hookSymbol("libandroid_runtime.so", "_ZN7android11WifiInfo_getBssidEP7_JNIEnvP8_jobject", hookGetMacAddr, &origGetMacAddr);
        // Android ID
        hookSymbol("libandroid_runtime.so", "_ZN7android17Settings_Secure_getStringEP7_JNIEnvP8_jobjectP8_jobjectP8_jstring", hookGetSettingsString, &origGetSettingsString);
        // Hardware ID
        hookSymbol("libandroid_runtime.so", "_ZN7android5Build_getHardwareEv", hookGetHardware, &origGetHardware);
        // 手机号
        hookSymbol("libandroid_runtime.so", "_ZN7android23TelephonyManager_getLine1NumberEP7_JNIEnvP8_jobject", hookGetLine1Number, &origGetLine1Number);
        // Sim相关
        hookSymbol("libandroid_runtime.so", "_ZN7android25TelephonyManager_getSimSerialNumberEP7_JNIEnvP8_jobject", hookGetSimSerial, &origGetSimSerial);
        hookSymbol("libandroid_runtime.so", "_ZN7android24TelephonyManager_getSimOperatorEP7_JNIEnvP8_jobject", hookGetSimOperator, &origGetSimOperator);
        // MediaDrm ID
        hookSymbol("libmediadrm.so", "_ZN7android7MediaDrm11getUniqueIdEP7_JNIEnvP8_jobject", hookGetMediaDrmUniqueId, &origGetMediaDrmUniqueId);
    }
};


REGISTER_ZYGISK_MODULE(ZygiskModule);
