/*
 * Copyright 2017 The Android Open Source Project
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

#include "NativeAudioContext.h"

#define SECONDS_TO_RECORD   10

static oboe::AudioApi convertNativeApiToAudioApi(int nativeApi) {
    switch (nativeApi) {
        default:
        case NATIVE_MODE_UNSPECIFIED:
            return oboe::AudioApi::Unspecified;
        case NATIVE_MODE_AAUDIO:
            return oboe::AudioApi::AAudio;
        case NATIVE_MODE_OPENSLES:
            return oboe::AudioApi::OpenSLES;
    }
}

NativeAudioContext::NativeAudioContext()
    : sineOscillators(MAX_SINE_OSCILLATORS)
    , sawtoothOscillators(MAX_SINE_OSCILLATORS) {
}

void NativeAudioContext::close(int32_t streamIndex) {
    stopBlockingIOThread();

    LOGD("%s() delete stream %d ", __func__, streamIndex);
    if (mOboeStreams[streamIndex] != nullptr) {
        mOboeStreams[streamIndex]->close();
        delete mOboeStreams[streamIndex];
        freeStreamIndex(streamIndex);
    }

    LOGD("%s() delete nodes", __func__);
    manyToMulti.reset(nullptr);
    monoToMulti.reset(nullptr);
    audioStreamGateway.reset(nullptr);
    mSinkFloat.reset();
    mSinkI16.reset();
}

bool NativeAudioContext::isMMapUsed(int32_t streamIndex) {
    oboe::AudioStream *oboeStream = getStream(streamIndex);
    if (oboeStream != nullptr && oboeStream->usesAAudio()) {
        if (mAAudioStream_isMMap == nullptr) {
            mLibHandle = dlopen(LIB_AAUDIO_NAME, 0);
            if (mLibHandle == nullptr) {
                LOGI("%s() could not find " LIB_AAUDIO_NAME, __func__);
                return false;
            }

            mAAudioStream_isMMap = (bool (*)(AAudioStream *stream))
                    dlsym(mLibHandle, FUNCTION_IS_MMAP);

            if(mAAudioStream_isMMap == nullptr) {
                LOGI("%s() could not find " FUNCTION_IS_MMAP, __func__);
                return false;
            }
        }
        AAudioStream *aaudioStream = (AAudioStream *) oboeStream->getUnderlyingStream();
        return mAAudioStream_isMMap(aaudioStream);
    }
    return false;
}

void NativeAudioContext::connectTone() {
//    if (monoToMulti != nullptr) {
//        LOGI("%s() mToneType = %d", __func__, mToneType);
//        switch (mToneType) {
//            case ToneType::SawPing:
//                sawPingGenerator.output.connect(&(monoToMulti->input));
//                monoToMulti->output.connect(&(mSinkFloat.get()->input));
//                monoToMulti->output.connect(&(mSinkI16.get()->input));
//                break;
//            case ToneType::Sine:
//                for (int i = 0; i < mChannelCount; i++) {
//                    sineOscillators[i].output.connect(manyToMulti->inputs[i].get());
//                }
//                manyToMulti->output.connect(&(mSinkFloat.get()->input));
//                manyToMulti->output.connect(&(mSinkI16.get()->input));
//                break;
//            case ToneType::Impulse:
//                impulseGenerator.output.connect(&(monoToMulti->input));
//                monoToMulti->output.connect(&(mSinkFloat.get()->input));
//                monoToMulti->output.connect(&(mSinkI16.get()->input));
//                break;
//            case ToneType::Sawtooth:
//                for (int i = 0; i < mChannelCount; i++) {
//                    sawtoothOscillators[i].output.connect(manyToMulti->inputs[i].get());
//                }
//                manyToMulti->output.connect(&(mSinkFloat.get()->input));
//                manyToMulti->output.connect(&(mSinkI16.get()->input));
//                break;
//        }
//    }
}

void NativeAudioContext::setChannelEnabled(int channelIndex, bool enabled) {
    if (manyToMulti == nullptr) {
        return;
    }
    if (enabled) {
        switch (mToneType) {
            case ToneType::Sine:
                sineOscillators[channelIndex].output.connect(manyToMulti->inputs[channelIndex].get());
                break;
            case ToneType::Sawtooth:
                sawtoothOscillators[channelIndex].output.connect(manyToMulti->inputs[channelIndex].get());
                break;
            default:
                break;
        }
    } else {
        manyToMulti->inputs[channelIndex]->disconnect();
    }
}

int32_t NativeAudioContext::allocateStreamIndex() {
    int32_t streamIndex = -1;
    for (int32_t i = 0; i < kMaxStreams; i++) {
        if (mOboeStreams[i] == nullptr) {
            streamIndex = i;
            break;
        }
    }
    return streamIndex;
}

void NativeAudioContext::freeStreamIndex(int32_t streamIndex) {
    mOboeStreams[streamIndex] = nullptr;
}

int NativeAudioContext::open(
        jint nativeApi,
        jint sampleRate,
        jint channelCount,
        jint format,
        jint sharingMode,
        jint performanceMode,
        jint deviceId,
        jint sessionId,
        jint framesPerBurst, jboolean isInput) {

    oboe::AudioApi audioApi = oboe::AudioApi::Unspecified;
    switch (nativeApi) {
        case NATIVE_MODE_UNSPECIFIED:
        case NATIVE_MODE_AAUDIO:
        case NATIVE_MODE_OPENSLES:
            audioApi = convertNativeApiToAudioApi(nativeApi);
            break;
        default:
            return (jint) oboe::Result::ErrorOutOfRange;
    }

    int32_t streamIndex = allocateStreamIndex();
    if (streamIndex < 0) {
        LOGE("NativeAudioContext::open() stream array full");
        return (jint) oboe::Result::ErrorNoFreeHandles;
    }

    if (channelCount < 0 || channelCount > 256) {
        LOGE("NativeAudioContext::open() channels out of range");
        return (jint) oboe::Result::ErrorOutOfRange;
    }

    // Create an audio output stream.
    LOGD("NativeAudioContext::open() try to create OboeStream #%d", streamIndex);
    oboe::AudioStreamBuilder builder;
    builder.setChannelCount(channelCount)
            ->setDirection(isInput ? oboe::Direction::Input : oboe::Direction::Output)
            ->setSharingMode((oboe::SharingMode) sharingMode)
            ->setPerformanceMode((oboe::PerformanceMode) performanceMode)
            ->setDeviceId(deviceId)
            ->setSessionId((oboe::SessionId) sessionId)
            ->setSampleRate(sampleRate)
            ->setFormat((oboe::AudioFormat) format);

    if (mActivityType == ActivityType::Echo) {
        if (mFullDuplexEcho.get() == nullptr) {
            mFullDuplexEcho = std::make_unique<FullDuplexEcho>();
        }
        // only output uses a callback, input is polled
        if (!isInput) {
            builder.setCallback(mFullDuplexEcho.get());
        }
    } else {
        // We needed the proxy because we did not know the channelCount when we setup the Builder.
        if (useCallback) {
            LOGD("NativeAudioContext::open() set callback to use oboeCallbackProxy, size = %d",
                 callbackSize);
            builder.setCallback(&oboeCallbackProxy);
            builder.setFramesPerCallback(callbackSize);
        }
    }

    if (audioApi == oboe::AudioApi::OpenSLES) {
        builder.setFramesPerCallback(framesPerBurst);
    }
    builder.setAudioApi(audioApi);

    // Open a stream based on the builder settings.
    oboe::AudioStream *oboeStream = nullptr;
    oboe::Result result = builder.openStream(&oboeStream);
    LOGD("NativeAudioContext::open() open(b) returned %d", result);
    if (result != oboe::Result::OK) {
        delete oboeStream;
        oboeStream = nullptr;
        freeStreamIndex(streamIndex);
        streamIndex = -1;
    } else {
        mOboeStreams[streamIndex] = oboeStream;

        mChannelCount = oboeStream->getChannelCount(); // FIXME store per stream
        mFramesPerBurst = oboeStream->getFramesPerBurst();
        mSampleRate = oboeStream->getSampleRate();

        if (mActivityType == ActivityType::Echo) {
            if (isInput) {
                mFullDuplexEcho->setInputStream(oboeStream);
            } else {
                mFullDuplexEcho->setOutputStream(oboeStream);
            }
        }
    }

    return ((int)result < 0) ? (int)result : streamIndex;
}

oboe::AudioStream * NativeAudioContext::getOutputStream() {
    for (int32_t i = 0; i < kMaxStreams; i++) {
        oboe::AudioStream *oboeStream = mOboeStreams[i];
        if (oboeStream != nullptr) {
            if (oboeStream->getDirection() == oboe::Direction::Output) {
                return oboeStream;
            }
        }
    }
    return nullptr;
}

oboe::AudioStream * NativeAudioContext::getInputStream() {
    for (int32_t i = 0; i < kMaxStreams; i++) {
        oboe::AudioStream *oboeStream = mOboeStreams[i];
        if (oboeStream != nullptr) {
            if (oboeStream->getDirection() == oboe::Direction::Input) {
                return oboeStream;
            }
        }
    }
    return nullptr;
}

void NativeAudioContext::configureForActivityType() {
    oboe::AudioStream *outputStream = nullptr;

    manyToMulti = std::make_unique<ManyToMultiConverter>(mChannelCount);
    monoToMulti = std::make_unique<MonoToMultiConverter>(mChannelCount);

    mSinkFloat = std::make_unique<SinkFloat>(mChannelCount);
    mSinkI16 = std::make_unique<SinkI16>(mChannelCount);

    switch(mActivityType) {
        case ActivityType::Undefined:
            break;
        case ActivityType::TestInput:
        case ActivityType::RecordPlay:
            mInputAnalyzer.reset();
            if (useCallback) {
                oboeCallbackProxy.setCallback(&mInputAnalyzer);
            }
            mRecording = std::make_unique<MultiChannelRecording>(mChannelCount,
                                                                 SECONDS_TO_RECORD * mSampleRate);
            mInputAnalyzer.setRecording(mRecording.get());
            break;

        case ActivityType::TestOutput:
            outputStream = getOutputStream();
            {
                double frequency = 440.0;
                for (int i = 0; i < mChannelCount; i++) {
                    sineOscillators[i].setSampleRate(outputStream->getSampleRate());
                    sineOscillators[i].frequency.setValue(frequency);
                    frequency *= 4.0 / 3.0; // each sine is at a higher frequency
                    sineOscillators[i].amplitude.setValue(AMPLITUDE_SINE);
                    sineOscillators[i].output.connect(manyToMulti->inputs[i].get());
                }
            }

            manyToMulti->output.connect(&(mSinkFloat.get()->input));
            manyToMulti->output.connect(&(mSinkI16.get()->input));
            break;

        case ActivityType::TapToTone:
            outputStream = getOutputStream();
            sawPingGenerator.setSampleRate(outputStream->getSampleRate());
            sawPingGenerator.frequency.setValue(FREQUENCY_SAW_PING);
            sawPingGenerator.amplitude.setValue(AMPLITUDE_SAW_PING);

            sawPingGenerator.output.connect(&(monoToMulti->input));
            monoToMulti->output.connect(&(mSinkFloat.get()->input));
            monoToMulti->output.connect(&(mSinkI16.get()->input));

            sawPingGenerator.setEnabled(false);
            break;

        case ActivityType::Echo:
            break;
    }

    if (outputStream != nullptr) {

        mSinkFloat->start();
        mSinkI16->start();

        // We needed the proxy because we did not know the channelCount
        // when we setup the Builder.
        audioStreamGateway = std::make_unique<AudioStreamGateway>(mChannelCount);
        if (outputStream->getFormat() == oboe::AudioFormat::I16) {
            audioStreamGateway->setAudioSink(mSinkI16);
        } else if (outputStream->getFormat() == oboe::AudioFormat::Float) {
            audioStreamGateway->setAudioSink(mSinkFloat);
        }

        if (useCallback) {
            oboeCallbackProxy.setCallback(audioStreamGateway.get());
        }

        // Set starting size of buffer.
        constexpr int kDefaultNumBursts = 2; // "double buffer"
        int32_t numBursts = kDefaultNumBursts;
        // callbackSize is used for both callbacks and blocking write
        numBursts = (callbackSize <= mFramesPerBurst)
                    ? kDefaultNumBursts
                    : ((callbackSize * kDefaultNumBursts) + mFramesPerBurst - 1)
                      / mFramesPerBurst;
        outputStream->setBufferSizeInFrames(numBursts * mFramesPerBurst);
    }

    if (!useCallback) {
        int numSamples = getFramesPerBlock() * mChannelCount;
        dataBuffer = std::make_unique<float[]>(numSamples);
    }
}

oboe::Result NativeAudioContext::start() {
    oboe::Result result = oboe::Result::OK;
    oboe::AudioStream *inputStream = getInputStream();
    oboe::AudioStream *outputStream = getOutputStream();
    if (inputStream == nullptr && outputStream == nullptr) {
        return oboe::Result::ErrorInvalidState; // not open
    }

    stop();

    LOGD("NativeAudioContext: %s() called", __func__);
    configureForActivityType();

    switch(mActivityType) {
        case ActivityType::Undefined:
            break;
        case ActivityType::TestInput:
        case ActivityType::RecordPlay:
            result = inputStream->requestStart();
            if (!useCallback && result == oboe::Result::OK) {
                LOGD("start thread for blocking I/O");
                // Instead of using the callback, start a thread that readsthe stream.
                threadEnabled.store(true);
                dataThread = new std::thread(threadCallback, this);
            }
            break;

        case ActivityType::TestOutput:
        case ActivityType::TapToTone:
            result = outputStream->requestStart();
            if (!useCallback && result == oboe::Result::OK) {
                LOGD("start thread for blocking I/O");
                // Instead of using the callback, start a thread that writes the stream.
                threadEnabled.store(true);
                dataThread = new std::thread(threadCallback, this);
            }
            break;

        case ActivityType::Echo:
            result = mFullDuplexEcho->start();
            break;
    }

    LOGD("OboeAudioStream_start: start returning %d", result);
    return result;
}

void NativeAudioContext::runBlockingIO() {
    int32_t framesPerBlock = getFramesPerBlock();
    oboe::DataCallbackResult callbackResult = oboe::DataCallbackResult::Continue;

    // TODO rethink which stream gets the callback for full duplex
    oboe::AudioStream *oboeStream = nullptr;
    for (int32_t i = 0; i < kMaxStreams; i++) {
        if (mOboeStreams[i] != nullptr) {
            oboeStream = mOboeStreams[i];
            break;
        }
    }
    if (oboeStream == nullptr) {
        LOGE("%s() : no stream found\n", __func__);
        return;
    }

    while (threadEnabled.load()
           && callbackResult == oboe::DataCallbackResult::Continue) {
        if (oboeStream->getDirection() == oboe::Direction::Input) {
            // read from input
            auto result = oboeStream->read(dataBuffer.get(),
                                           framesPerBlock,
                                           NANOS_PER_SECOND);
            if (!result) {
                LOGE("%s() : read() returned %s\n", __func__, convertToText(result.error()));
                break;
            }
            int32_t framesRead = result.value();
            if (framesRead < framesPerBlock) { // timeout?
                LOGE("%s() : read() read %d of %d\n", __func__, framesRead, framesPerBlock);
                break;
            }

            // analyze input
            callbackResult = mInputAnalyzer.onAudioReady(oboeStream,
                                                         dataBuffer.get(),
                                                         framesRead);
        } else if (audioStreamGateway != nullptr) {  // OUTPUT?
            // generate output by calling the callback
            callbackResult = audioStreamGateway->onAudioReady(oboeStream,
                                                              dataBuffer.get(),
                                                              framesPerBlock);

            auto result = oboeStream->write(dataBuffer.get(),
                                            framesPerBlock,
                                            NANOS_PER_SECOND);

            if (!result) {
                LOGE("%s() returned %s\n", __func__, convertToText(result.error()));
                break;
            }
            int32_t framesWritten = result.value();
            if (framesWritten < framesPerBlock) {
                LOGE("%s() : write() wrote %d of %d\n", __func__, framesWritten, framesPerBlock);
                break;
            }
        }
    }

}
