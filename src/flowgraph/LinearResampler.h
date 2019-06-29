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

#ifndef OBOE_LINEAR_RESAMPLER_H
#define OBOE_LINEAR_RESAMPLER_H

#include <memory>
#include <sys/types.h>
#include <unistd.h>
#include "ContinuousResampler.h"

namespace flowgraph {

class LinearResampler : public ContinuousResampler {
public:
    LinearResampler(int32_t channelCount,
                             int32_t inputRate,
                             int32_t outputRate);

    void writeFrame(const float *frame) override;

    void readFrame(float *frame) override;

private:
    std::unique_ptr<float[]> mPreviousFrame;
    std::unique_ptr<float[]> mCurrentFrame;
};

}
#endif //OBOE_LINEAR_RESAMPLER_H
