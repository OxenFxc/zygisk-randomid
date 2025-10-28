#pragma once
#include <zygisk.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>
#include <thread>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ZygiskDeviceRand", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ZygiskDeviceRand", __VA_ARGS__)

// 全局线程安全随机数引擎
static std::mt19937_64 s_rand_engine(std::random_device{}());

// 随机工具函数（生成符合格式的设备标识）
namespace RandUtil {
    // 生成指定长度十六进制字符串（MAC/Android ID 等）
    inline std::string Hex(int len) {
        std::uniform_int_distribution<int> dist(0, 15);
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < len; i++) ss << std::setw(1) << dist(s_rand_engine);
        return ss.str();
    }

    // 随机 IMEI（15位，带校验位）
    inline std::string IMEI() {
        std::uniform_int_distribution<int> dist(0, 9);
        std::string imei;
        for (int i = 0; i < 14; i++) imei += std::to_string(dist(s_rand_engine));
        // 计算校验位
        int sum = 0;
        for (int i = 0; i < 14; i++) {
            int num = imei[i] - '0';
            sum += (i % 2 == 0) ? num : (num * 2 > 9 ? num * 2 - 9 : num * 2);
        }
        imei += std::to_string((10 - (sum % 10)) % 10);
        return imei;
    }

    // 随机手机号（中国号段）
    inline std::string Mobile() {
        const std::string prefixes[] = {"130","131","132","133","134","135","136","137","138","139",
                                        "150","151","152","153","155","156","157","158","159",
                                        "170","171","173","175","176","177","178","180","181","182",
                                        "183","184","185","186","187","188","189"};
        std::uniform_int_distribution<int> p_dist(0, sizeof(prefixes)/sizeof(prefixes[0])-1);
        std::uniform_int_distribution<int> n_dist(0, 9);
        std::string mobile = prefixes[p_dist(s_rand_engine)];
        for (int i = 0; i < 8; i++) mobile += std::to_string(n_dist(s_rand_engine));
        return mobile;
    }

    // 随机 Sim Serial（20位数字）
    inline std::string SimSerial() {
        std::uniform_int_distribution<int> dist(0, 9);
        std::string serial;
        for (int i = 0; i < 20; i++) serial += std::to_string(dist(s_rand_engine));
        return serial;
    }

    // 随机 MAC 地址（带分隔符）
    inline std::string MAC() {
        std::string hex = Hex(12);
        return hex.substr(0,2)+":"+hex.substr(2,2)+":"+hex.substr(4,2)+":"+
               hex.substr(6,2)+":"+hex.substr(8,2)+":"+hex.substr(10,2);
    }

    // 随机 MediaDrm ID（32位十六进制，符合 Widevine 格式）
    inline std::string MediaDrmID() { return Hex(32); }

    // 随机 Sim Operator（中国运营商代码：46000/46001/46003）
    inline std::string SimOperator() {
        const std::string ops[] = {"46000", "46001", "46003"};
        std::uniform_int_distribution<int> dist(0, 2);
        return ops[dist(s_rand_engine)];
    }

    // 随机 Hardware ID（格式：厂商_随机8位，如 "qcom_abc12345"）
    inline std::string HardwareID() {
        const std::string vendors[] = {"qcom", "mtk", "exynos", "kirin"};
        std::uniform_int_distribution<int> dist(0, 3);
        return vendors[dist(s_rand_engine)] + "_" + Hex(8);
    }
}
