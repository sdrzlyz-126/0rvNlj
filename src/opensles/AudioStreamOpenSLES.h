/*
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OBOE_AUDIO_STREAM_OPENSL_ES_H_
#define OBOE_AUDIO_STREAM_OPENSL_ES_H_

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "oboe/Oboe.h"
#include "common/MonotonicCounter.h"
#include "opensles/AudioStreamBuffered.h"
#include "opensles/EngineOpenSLES.h"

namespace oboe {

constexpr int kBitsPerByte = 8;
constexpr int kBufferQueueLength = 2; // double buffered for callbacks

/**
 * INTERNAL USE ONLY
 *
 * A stream that wraps OpenSL ES.
 *
 * Do not instantiate this class directly.
 * Use an OboeStreamBuilder to create one.
 */

class AudioStreamOpenSLES : public AudioStreamBuffered {
public:

    AudioStreamOpenSLES();
    explicit AudioStreamOpenSLES(const AudioStreamBuilder &builder);

    virtual ~AudioStreamOpenSLES();

    virtual Result open() override;
    virtual Result close() override;

    /**
     * Query the current state, eg. OBOE_STREAM_STATE_PAUSING
     *
     * @return state or a negative error.
     */
    StreamState getState() override { return mState; }

    int32_t getFramesPerBurst() override;


    AudioApi getAudioApi() const override {
        return AudioApi::OpenSLES;
    }

    /**
     * Process next OpenSL ES buffer.
     * Called by by OpenSL ES framework.
     *
     * This is public, but don't call it directly.
     */
    void processBufferCallback(SLAndroidSimpleBufferQueueItf bq);

    Result waitForStateChange(StreamState currentState,
                              StreamState *nextState,
                              int64_t timeoutNanoseconds) override;

protected:

    SLuint32 channelCountToChannelMaskDefault(int channelCount);

    virtual Result onBeforeDestroy() { return Result::OK; };
    virtual Result onAfterDestroy() { return Result::OK; };

    static SLuint32 getDefaultByteOrder();

    SLresult registerBufferQueueCallback();

    SLresult enqueueCallbackBuffer(SLAndroidSimpleBufferQueueItf bq);

    SLresult configurePerformanceMode(SLAndroidConfigurationItf configItf);

    SLresult updateStreamParameters(SLAndroidConfigurationItf configItf);

    PerformanceMode convertPerformanceMode(SLuint32 openslMode) const;
    SLuint32 convertPerformanceMode(PerformanceMode oboeMode) const;

    /**
     * Internal use only.
     * Use this instead of directly setting the internal state variable.
     */
    void setState(StreamState state) {
        mState = state;
    }

    int64_t getFramesProcessedByServer() const;

    // OpenSLES stuff
    SLObjectItf                   mObjectInterface = nullptr;
    SLAndroidSimpleBufferQueueItf mSimpleBufferQueueInterface = nullptr;

    uint8_t              *mCallbackBuffer = nullptr;
    int32_t               mBytesPerCallback = oboe::kUnspecified;
    int32_t               mFramesPerBurst = DefaultStreamValues::FramesPerBurst;
    StreamState           mState = StreamState::Uninitialized;

    MonotonicCounter      mPositionMillis; // for tracking OpenSL ES service position
};

} // namespace oboe

#endif // OBOE_AUDIO_STREAM_OPENSL_ES_H_
