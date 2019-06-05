/*
 * Copyright 2019 The Android Open Source Project
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

#include <oboe/AudioStreamBuilder.h>
#include <oboe/Oboe.h>
#include "QuirksManager.h"

using namespace oboe;

QuirksManager *QuirksManager::mInstance = nullptr;

bool QuirksManager::isConversionNeeded(
        const AudioStreamBuilder &builder,
        AudioStreamBuilder &childBuilder) {
    bool conversionNeeded = false;
    const bool isLowLatency = builder.getPerformanceMode() == PerformanceMode::LowLatency;
    const bool isInput = builder.getDirection() == Direction::Input;
    const bool isFloat = builder.getFormat() == AudioFormat::Float;

    // If a SAMPLE RATE is specified then let the native code choose an optimal rate.
    if (builder.getSampleRate() != oboe::Unspecified
            && builder.getSampleRateConversionType() != SampleRateConversionQuality::None
            && isLowLatency
            ) {
        childBuilder.setSampleRate(oboe::Unspecified); // native API decides the best sample rate
        conversionNeeded = true;
    }

    // Data Format
    // OpenSL ES and AAudio <P do not support FAST for FLOAT capture.
    if (builder.getFormat() != AudioFormat::Unspecified
            && builder.isFormatConversionAllowed()
            && isInput
            && (!builder.willUseAAudio() || (getSdkVersion() < __ANDROID_API_P__))
            && isLowLatency
            && isFloat
            ) {
        childBuilder.setFormat(AudioFormat::I16); // needed for FAST track
        conversionNeeded = true;
    }

    // Channel Count
    if (builder.getChannelCount() != oboe::Unspecified && builder.isChannelConversionAllowed()) {
        if (builder.getChannelCount() == 2 // stereo?
                && isInput
                && isLowLatency
                && (!builder.willUseAAudio() && (getSdkVersion() == __ANDROID_API_O__))) {
            // temporary heap size regression, b/66967812
            childBuilder.setChannelCount(1);
            conversionNeeded = true;
        } else if (builder.getChannelCount() == 1 // mono?
                   && !isInput
                   && isLowLatency
                   // TODO isMMapEnabled
                   && (builder.willUseAAudio() && (getSdkVersion() < __ANDROID_API_P__))) {
            // MMAP does not support mono in 8.1
            childBuilder.setChannelCount(2);
            conversionNeeded = true;
        }
    }
    return conversionNeeded;
}
