namespace android {

/**
    Singleton instance of EffectDapController

    Lifetime of this instance is managed by AudioFlinger class. A new instance is created in
    AudioFlinger constructor and it is destroyed in AudioFlinger destructor.
*/
AudioFlinger::EffectDapController *AudioFlinger::EffectDapController::mInstance = NULL;

/**
    Constructor for EffectDapController
*/
AudioFlinger::EffectDapController::EffectDapController(const sp<AudioFlinger>& audioFlinger)
    : mAudioFlinger(audioFlinger),
      mEffect(NULL),
      mBypassed(false),
      mDapVol(0),
      mMixerVol(0),
      mDirectVol(0),
      mOffloadVol(0),
      mVersion(""),
      mMaxMixerVol(0),
      mMaxDirectVol(0),
      mMaxOffloadVol(0),
#ifdef DOLBY_DAP_BYPASS_SOUND_TYPES
      mIsSoundTypesBypassEnabled(true)
#else
      mIsSoundTypesBypassEnabled(false)
#endif
{
    ALOGI("%s()", __FUNCTION__);
}

/**
    Capture the DAP effect pointer on creation.

    This function is called when an effect is created in AudioFlinger. This function checks if
    the effect matches the UUID of DAP effect and captures the effect pointer in mEffect, on
    condition that it is a global audio effect.
*/
void AudioFlinger::EffectDapController::effectCreated(const sp<AudioFlinger::EffectModule> &effect, const ThreadBase *thread)
{
    if (isDapEffect(effect) && effect->sessionId() == AUDIO_SESSION_OUTPUT_MIX) {
        mEffect = effect;
        mBypassed = false;
        mDapVol = 0;
        mMixerVol = 0;
        mDirectVol = 0;
        mOffloadVol = 0;
        updateOffload(thread);
        ALOGI("%s() DAP effect created on thread %d, offloadable %d", __FUNCTION__, thread->id(), mEffect->isOffloadable());
    }
}

/**
    Release the DAP effect pointer on a release.

    This function is called when an effect is released from AudioFlinger. This function checks if
    the effect matches the UUID of DAP effect, on condition that it is a global audio effect.
*/
void AudioFlinger::EffectDapController::effectReleased(const sp<AudioFlinger::EffectModule> &effect)
{
    Mutex::Autolock _l(mLock);
    if (isDapEffect(effect) && effect->sessionId() == AUDIO_SESSION_OUTPUT_MIX) {
        mEffect = NULL;
        ALOGI("%s() DAP effect released", __FUNCTION__);
    }
}

/**
    Notify DS service when DAP effect is suspended.
*/
void AudioFlinger::EffectDapController::effectSuspended(const sp<EffectBase> &effect, bool suspend)
{
    if (isDapEffect(effect)) {
        ALOGW("%s(suspend=%d)", __FUNCTION__, suspend);
    }
}

/**
    Set offload state for DS Effect.

    In AOSP offloaded effect is only used for offload thread. However, we have
    to ensure that offload effect is used for all outputs supported by DSP. This
    function is called whenever effect is attached to a thread.
*/
void AudioFlinger::EffectDapController::updateOffload(const ThreadBase *thread)
{
    if (mEffect != NULL) {
        ALOGVV("%s()", __FUNCTION__);
        sp<EffectChain> chain = mEffect->getCallback()->chain().promote();
        if (chain != 0) {
            wp<ThreadBase> effectChainThread = chain->thread();
            // Proceed if effect is valid and attached to the thread passed as argument.
            if (effectChainThread == NULL || effectChainThread.promote() == 0 ||
                thread == NULL || effectChainThread.promote()->id() != thread->id()) {
                return;
            } else if (effectChainThread != NULL && effectChainThread.promote() != 0) {
                setParam(EFFECT_PARAM_IO_HANDLE, effectChainThread.promote()->id());
            }
        } else {
            ALOGW("updateOffload() cannot promote chain for effect %p", mEffect.get());
            return;
        }

        if (!mEffect->isOffloadable()) {
            // Skip the EFFECT_CMD_OFFLOAD command if the effect is NOT offloadable
            return;
        }
        // Enable offload if the thread is offload or the output is not connected
        // to a device requiring software DAP.
        bool offload = false;
        if(thread->type() == ThreadBase::OFFLOAD) {
            offload = true;
        } else {
            DeviceTypeSet outDevTypes = thread->outDeviceTypes();
            offload = outDevTypes.find(NO_OFFLOAD_DEVICES) == outDevTypes.end(); //not containing
        }
        ALOGV("%s() => %d for thread %d type %d", __FUNCTION__, offload,
            thread->id(), thread->type());
        // Send the offload flag to DAP effect
        mEffect->setOffloaded(offload, thread->id());
    }
}

/**
    Return true if DAP effect should be bypassed for the track.
*/
bool AudioFlinger::EffectDapController::bypassTrack(PlaybackThread::Track* const &track) {
    ALOGVV("%s()", __FUNCTION__);
    if (mIsSoundTypesBypassEnabled) {
        return (track != NULL) && (
            (track->streamType() == AUDIO_STREAM_SYSTEM) ||
            (track->streamType() == AUDIO_STREAM_RING) ||
            (track->streamType() == AUDIO_STREAM_ALARM) ||
            (track->streamType() == AUDIO_STREAM_NOTIFICATION) ||
            (track->streamType() == AUDIO_STREAM_DTMF) ||
            (track->streamType() == AUDIO_STREAM_ASSISTANT) ||
            (track->streamType() == AUDIO_STREAM_CNT)); // Special stream type used for duplicating threads.
    } else {
        return false;
    }
}

/**
    Bypass DAP effect if any of the active tracks contain audio stream that should not be processed.
    Pass the audio flag on the active tracks to DAP effect.
*/
void AudioFlinger::EffectDapController::checkAudioTracks(const ThreadBase::ActiveTracks<PlaybackThread::Track> &activeTracks, audio_io_handle_t id)
{
    ALOGVV("%s(#tracks=%d)", __FUNCTION__, activeTracks.size());
    if (activeTracks.size() == 0) {
        // activeTracks size is zero, Don't update current bypass state to false.
        return;
    }

    if (mEffect == NULL) {
        ALOGW("Dolby Effect is NULL");
        return;
    } else {
        sp<EffectChain> chain = mEffect->getCallback()->chain().promote();
        if (chain != 0) {
            wp<ThreadBase> effectChainThread = chain->thread();
            if (effectChainThread == NULL) {
                ALOGW("Dolby Effect is attached nowhere");
                return;
            } else if (!mEffect->isOffloadable()) {
                if (!mEffect->isEnabled()) return;
                sp<ThreadBase> thread = effectChainThread.promote();
                if (thread == 0 || id != thread->id()) {
                    ALOGV("Mismatch Effect thread id for audio track check");
                    return;
                }
            }
        } else {
            ALOGW("checkAudioTracks() cannot promote chain for effect %p", mEffect.get());
            return;
        }
    }

    int flags = 0;
    for (size_t i = 0; i < activeTracks.size(); i++) {
        PlaybackThread::Track* const track = activeTracks[i].get();
        if (track != NULL && !track->isFastTrack() && track->isExternalTrack()) {
            flags |= (int)track->attributes().flags;
        }
    }
    if (mAudioFlags != flags) {
        ALOGD("%s(flags=0x%x)", __FUNCTION__, flags);
        setParam(EFFECT_PARAM_SET_AUDIO_FLAG, flags);
        mAudioFlags = flags;
    }

    bool bypass = false;
    if (mIsSoundTypesBypassEnabled) {
        for (size_t i = 0; i < activeTracks.size(); i++) {
            PlaybackThread::Track* const track = activeTracks[i].get();
            // Do not bypass if any music streams are active.
            if (track != NULL && track->streamType() == AUDIO_STREAM_MUSIC) {
                bypass = false;
                break;
            } else if (bypassTrack(track)) {
                bypass = true;
            }
        }

        // Send bypass parameter to DAP when bypass state changes.
        if (bypass != mBypassed) {
            ALOGD("%s(bypass=%d)", __FUNCTION__, bypass);
            setParam(EFFECT_PARAM_SET_BYPASS, bypass);
            mBypassed = bypass;
        }
    }
}

/**
    Bypass DAP effect if any of the active tracks contain audio stream that should not be processed.
*/
void AudioFlinger::EffectDapController::checkForBypass(const ThreadBase::ActiveTracks<PlaybackThread::Track> &activeTracks, audio_io_handle_t id)
{
    if (mIsSoundTypesBypassEnabled) {
        ALOGVV("%s(#tracks=%d)", __FUNCTION__, activeTracks.size());
        bool bypass = false;
        if (activeTracks.size() == 0) {
            // activeTracks size is zero, Don't update current bypass state to false.
            return;
        }

        if (mEffect == NULL) {
            ALOGW("Dolby Effect is NULL");
            return;
        } else {
            sp<EffectChain> chain = mEffect->getCallback()->chain().promote();
            if (chain != 0) {
                wp<ThreadBase> effectChainThread = chain->thread();
                if (effectChainThread == NULL) {
                    ALOGW("Dolby Effect is attached nowhere");
                    return;
                } else {
                    if (!mEffect->isEnabled()) return;
                    sp<ThreadBase> thread = effectChainThread.promote();
                    if (thread == 0 || id != thread->id()) {
                        ALOGV("Mismatch Effect thread id for bypass update");
                        return;
                    }
                }
            } else {
                ALOGW("checkForBypass() cannot promote chain for effect %p", mEffect.get());
                return;
            }
        }

        for (size_t i = 0; i < activeTracks.size(); i++) {
            const sp<PlaybackThread::Track> t = activeTracks[i];
            PlaybackThread::Track* const track = t.get();
            // Do not bypass if any music streams are active.
            if (track != NULL && track->streamType() == AUDIO_STREAM_MUSIC) {
                bypass = false;
                break;
            } else if (bypassTrack(track)) {
                bypass = true;
            }
        }

        // Send bypass parameter to DAP when bypass state changes.
        if (bypass != mBypassed) {
            mBypassed = bypass;
            updateBypassState();
        }
    }
}

/**
    Send pregain value to DAP based on thread volumes.
*/
void AudioFlinger::EffectDapController::updatePregain(
        ThreadBase::type_t thread_type,
        audio_io_handle_t id,
        audio_output_flags_t flags,
        uint32_t max_vol,
        bool is_active __unused)
{
    Mutex::Autolock _l(mLock);

    // we need skip this op in dolby 3.8
    std::string dolbyVersion = EffectDapController::instance()->getVersion();
    if (EffectDapController::versionCompare(dolbyVersion, DAX3_3POINT6) > 0) {
        return;
    }

    ALOGVV("%s(thread_type = %d, flags = 0x%X, max_vol = %u)", __FUNCTION__, thread_type, flags, max_vol);
    if(mEffect == NULL) {
        ALOGVV("Dolby Effect is null");
        return;
    }

    if(flags & AUDIO_OUTPUT_FLAG_FAST){
        ALOGVV("Dolby Effect not support fast");
        return;
    }

    sp<EffectChain> chain = mEffect->getCallback()->chain().promote();
    if (chain != 0) {
        wp<ThreadBase> effectChainThread = chain->thread();
        if (effectChainThread == NULL) {
            ALOGVV("Dolby Effect is not attached yet");
            return;
        } else if (!mEffect->isOffloadable()) {
            if (!mEffect->isEnabled()) return;
            sp<ThreadBase> thread = effectChainThread.promote();
            if (thread_type != ThreadBase::MIXER || thread == 0 || id != thread->id()) {
                ALOGVV("%s(): Thread type or id mismatch", __FUNCTION__);
                return;
            }
        } else {
            sp<ThreadBase> thread = effectChainThread.promote();
            if (thread == 0 || (id != thread->id() && (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) == 0)) {
                ALOGVV("%s(): Thread type or id mismatch", __FUNCTION__);
                return;
            }
        }
    } else {
        ALOGW("updatePregain() cannot promote chain for effect %p", mEffect.get());
        return;
    }

    // Update correct thread's volume
    switch (thread_type) {
        case ThreadBase::MIXER:
            mMixerVol = max_vol;
            ALOGVV("%s() Mixer thread volume set to %u", __FUNCTION__, mMixerVol);
            break;
        case ThreadBase::DIRECT:
            mDirectVol = max_vol;
            ALOGVV("%s() Direct output thread volume set to %u", __FUNCTION__, mDirectVol);
            break;
        case ThreadBase::OFFLOAD:
            mOffloadVol = max_vol;
            ALOGVV("%s() Offload thread volume set to %u", __FUNCTION__, mOffloadVol);
            break;
        default:
            ALOGVV("%s() called with unknown thread type: %d", __FUNCTION__, thread_type);
    }
    // Update max volume of DAP
    max_vol = std::max(mMixerVol, std::max(mDirectVol, mOffloadVol));
    if (max_vol == 0) {
        // Ignore the passed volume on condition that there's no active playback.
        ALOGV("%s(): No active tracks, volume 0 ignored", __FUNCTION__);
        return;
    }

    status_t status = setPregainWithStreamType(max_vol, flags);
    if (status == NO_ERROR) {
        mDapVol = max_vol;
        ALOGV("%s() Pregain set to %u", __FUNCTION__, max_vol);
    }
}

#ifdef DOLBY_DAP_POSTGAIN
/**
    Send postgain value to DAP.
*/
status_t AudioFlinger::EffectDapController::setPostgain(uint32_t max_vol)
{
    ALOGV("%s()", __FUNCTION__);
    return setParam(EFFECT_PARAM_SET_POSTGAIN, static_cast<int>(max_vol));
}
#endif // DOLBY_DAP_POSTGAIN_END

/**
    Skip the next effect hard bypass
*/
status_t AudioFlinger::EffectDapController::skipHardBypass()
{
    ALOGV("%s()", __FUNCTION__);
    return setParam(EFFECT_PARAM_SKIP_HARD_BYPASS, 1);
}

/**
    Send EFFECT_CMD_SET_PARAM to DAP effect with given parameter id and value.
*/
status_t AudioFlinger::EffectDapController::setParam(int32_t paramId, int32_t value)
{
    ALOGVV("%s(id=%d, value=%d)", __FUNCTION__, paramId, value);
    if (mEffect == NULL) {
        return NO_INIT;
    }

    std::vector<uint8_t> request(sizeof(effect_param_t) + 2 * sizeof(int32_t));
    effect_param_t *param = (effect_param_t*) request.data();
    param->psize = sizeof(int32_t);
    param->vsize = sizeof(int32_t);
    *(int32_t*)param->data = paramId;
    *((int32_t*)param->data + 1) = value;
    // Create a buffer to hold reply data
    std::vector<uint8_t> response;
    // Send the command to effect
    status_t status = mEffect->command(EFFECT_CMD_SET_PARAM, request, sizeof(int32_t), &response);
    if (status == NO_ERROR) {
        LOG_ALWAYS_FATAL_IF(response.size() != 4);
        status = *reinterpret_cast<const status_t*>(response.data());
    }
    return status;
}

// MIUI ADD
status_t AudioFlinger::EffectDapController::setPregainWithStreamType(int maxVol, audio_output_flags_t flags) {
    status_t status = NO_ERROR;
    audio_output_flags_t flag = AUDIO_OUTPUT_FLAG_NONE;

    if ((flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) && maxVol != mMaxMixerVol) {
        flag = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
    } else if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) && maxVol != mMaxDirectVol) {
        flag = AUDIO_OUTPUT_FLAG_DIRECT;
    } else if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) && maxVol != mMaxOffloadVol) {
        flag = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
    } else {
        return NO_ERROR;
    }
    ALOGD("%s(): pregain %d, flag %#x", __func__, maxVol, flag);

    std::vector<int32_t> values(2, 0);
    values[0] = maxVol;
    values[1] = (int32_t)flag;
    status = setParameters(EFFECT_PARAM_SET_PREGAIN, values);

    if (status == NO_ERROR){
        if (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
            mMaxMixerVol = maxVol;
        } else if (flags & AUDIO_OUTPUT_FLAG_DIRECT) {
            mMaxDirectVol = maxVol;
        } else if (flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
            mMaxOffloadVol = maxVol;
        }
    }

    return status;
}

/** MIUI ADD
    Send EFFECT_CMD_SET_PARAM to DAP effect with given parameter id and mtil values.
*/
status_t AudioFlinger::EffectDapController::setParameters(int32_t paramId, const std::vector<int32_t>& values)
{
    if (values.empty()) {
        return BAD_VALUE;
    }
    if (mEffect == NULL) {
        return NO_INIT;
    }

    const int psize = sizeof(int32_t);
    const int vsize = sizeof(int32_t) * values.size();

    std::vector<uint8_t> request(sizeof(effect_param_t) + psize + vsize);
    effect_param_t *param = (effect_param_t*) request.data();
    param->psize = psize;
    param->vsize = vsize;
    *(int32_t*)param->data = paramId;
    for (int i = 0; i < values.size(); ++i){
        *((int32_t*)param->data + 1 + i) = values[i];
    }

    std::vector<uint8_t> response;
    status_t status = mEffect->command(EFFECT_CMD_SET_PARAM, request, sizeof(int32_t), &response);
    if (status == NO_ERROR) {
        LOG_ALWAYS_FATAL_IF(response.size() != 4);
        status = *reinterpret_cast<const status_t*>(response.data());
    }
    return status;
}

/**
    Send bypass command to DAP based on the bypass state and processed audio state
*/
status_t AudioFlinger::EffectDapController::updateBypassState()
{
    ALOGD("%s(bypass=%d)", __FUNCTION__, mBypassed);
    // TODO: Should we notify DS Service that effect is bypassed?
    return setParam(EFFECT_PARAM_SET_BYPASS, mBypassed);
}

/* static */
std::vector<std::string> AudioFlinger::EffectDapController::strSplit(std::string str, std::string sep)
{
    char *current, *remain;
    char* cstr = const_cast<char*>(str.c_str());
    std::vector<std::string> arr;
    current = strtok_r(cstr, sep.c_str(), &remain);
    while (current != NULL) {
        arr.push_back(current);
        current = strtok_r(NULL, sep.c_str(), &remain);
    }
    return arr;
}

/* static */
int AudioFlinger::EffectDapController::versionCompare(std::string version_1, std::string version_2)
{
    const std::string sep = ".";
    std::vector<std::string> version_splits_1 = AudioFlinger::EffectDapController::strSplit(version_1, sep);
    std::vector<std::string> version_splits_2 = AudioFlinger::EffectDapController::strSplit(version_2, sep);
    int min_len = std::min(version_splits_1.size(), version_splits_2.size());
    for (int i = 0; i < min_len; ++i) {
        if (version_splits_1[i] < version_splits_2[i]) {
            return -1;
        } else if (version_splits_1[i] > version_splits_2[i]) {
            return 1;
        }
    }
    return 0;
}

// NOTE: need a lock?
std::string AudioFlinger::EffectDapController::getVersion()
{
    if (mVersion != ""){
        return mVersion;
    }

    char value[PROPERTY_VALUE_MAX];
    if (property_get(PROPERTY_DAX_VERSION, value, "")) {
        if (strcmp(value, "") != 0) {
            mVersion = std::string(value);
        } else {
            mVersion = std::string(DAX3_3POINT6);
        }
    }

    return mVersion;
}

}
