#define LOG_TAG "FeatureManager"
//#define LOG_NDEBUG 0

#include "include/mediautils/FeatureManager.h"
#include <utils/Log.h>
#include <cutils/properties.h>

/*static*/ std::mutex FeatureManager::mLock;
/*static*/ FeatureManager* FeatureManager::mInstance = nullptr;

FeatureManager::FeatureManager() {
    mFeatureTable = std::vector<bool>();
}

bool FeatureManager::isFeatureEnableInternal(int type) {
    if (mFeatureTable.empty()) {
        std::lock_guard<std::mutex> lock(FeatureManager::mLock);
        initFeatureTable_l();
    }

    switch (type) {
        case AUDIO_DOLBY_ENABLE:
            return mFeatureTable[AUDIO_DOLBY_ENABLE];
            break;
        case AUDIO_DOLBY_ATMOS_GAME:
            return mFeatureTable[AUDIO_DOLBY_ATMOS_GAME];
        case AUDIO_DOLBY_VQE:
            return mFeatureTable[AUDIO_DOLBY_VQE];
        case AUDIO_DOLBY_AC4_SPLIT_SEC:
            return mFeatureTable[AUDIO_DOLBY_AC4_SPLIT_SEC];
        default:
            ALOGW("%s(): unknown feature %d", __func__, type);
    }

    return false;
}

void FeatureManager::initFeatureTable_l() {
    mFeatureTable = std::vector<bool>(AUDIO_FEATURE_MAX + 1, false);
    bool value = property_get_bool(PROPERTY_DAX_SUPPORT, false);
    if (value) {
        mFeatureTable[AUDIO_DOLBY_ENABLE] = true;
        mFeatureTable[AUDIO_DOLBY_ATMOS_GAME] = true;
        mFeatureTable[AUDIO_DOLBY_VQE] = true;
        mFeatureTable[AUDIO_DOLBY_AC4_SPLIT_SEC] = true;
    }
}

/*static*/ bool FeatureManager::isFeatureEnable(int type) {
    return FeatureManager::getInstance()->isFeatureEnableInternal(type);
}

/*static*/ FeatureManager* FeatureManager::getInstance() {
    if (FeatureManager::mInstance == nullptr) {
        std::lock_guard<std::mutex> lock(FeatureManager::mLock);
        if (FeatureManager::mInstance){
            return FeatureManager::mInstance;
        }
        FeatureManager::mInstance = FeatureManager::createInstance();
    }
    return FeatureManager::mInstance;
}

/*static*/ FeatureManager* FeatureManager::createInstance() {
    return new FeatureManager();
}

/*static*/ void FeatureManager::destoryInstance() {
    if (FeatureManager::mInstance != nullptr) {
        delete FeatureManager::mInstance;
        FeatureManager::mInstance = nullptr;
    }
}

// TODO: reference ANDROID_SINGLETON_STATIC_INSTANCE(BatteryNotifier);
