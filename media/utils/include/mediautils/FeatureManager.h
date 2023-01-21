#ifndef ANDROID_FEATURE_MANAGER_H_
#define ANDROID_FEATURE_MANAGER_H_

#include <mutex>
#include <vector>

#define PROPERTY_DAX_SUPPORT "ro.vendor.audio.dolby.dax.support"

typedef enum feature_type{
    AUDIO_FEATURE_MIN = -1,
    AUDIO_DOLBY_ENABLE = 0,
    AUDIO_DOLBY_ATMOS_GAME = 1,
    AUDIO_DOLBY_VQE = 2,
    AUDIO_DOLBY_AC4_SPLIT_SEC = 3,
    AUDIO_FEATURE_MAX,
}feature_type;


class FeatureManager {
public:
    FeatureManager();
    static bool isFeatureEnable(int type);
    static void destoryInstance();

private:
    static FeatureManager* getInstance();
    static FeatureManager* createInstance();
    static FeatureManager* mInstance;
    static std::mutex mLock;

    bool isFeatureEnableInternal(int type);

    void initFeatureTable_l();

    std::vector<bool> mFeatureTable;
};

// TODO: reference ANDROID_SINGLETON_STATIC_INSTANCE(BatteryNotifier), we can use android impl.

#endif  // ANDROID_FEATURE_MANAGER_H_
