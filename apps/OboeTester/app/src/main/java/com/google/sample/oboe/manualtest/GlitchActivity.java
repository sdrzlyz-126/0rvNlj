/*
 * Copyright 2018 The Android Open Source Project
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

package com.google.sample.oboe.manualtest;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

/**
 * Activity to measure the number of glitches.
 */
public class GlitchActivity extends AnalyzerActivity {
    TextView mAnalyzerTextView;
    Button mStartButton;
    Button mStopButton;
//    private Object mLock = new Object();

    // These must match the values in LatencyAnalyzer.h
    final static int STATE_IDLE = 0;
    final static int STATE_MEASURE_NOISE =1;
    final static int STATE_IMMUNE = 2;
    final static int STATE_WAITING_FOR_SIGNAL = 3;
    final static int STATE_WAITING_FOR_LOCK = 4;
    final static int STATE_LOCKED = 5;

    native int getGlitchCount();
    native double getSignalToNoiseDB();

    // Note that these string must match the enum result_code in LatencyAnalyzer.h
    String stateToString(int resultCode) {
        switch (resultCode) {
            case STATE_IDLE:
                return "IDLE";
            case STATE_MEASURE_NOISE:
                return "MEASURE_NOISE";
            case STATE_IMMUNE:
                return "IMMUNE";
            case STATE_WAITING_FOR_SIGNAL:
                return "WAITING_FOR_SIGNAL";
            case STATE_WAITING_FOR_LOCK:
                return "WAITING_FOR_LOCK";
            case STATE_LOCKED:
                return "RUNNING";
            default:
                return "UNKNOWN";
        }
    }

    // Periodically query the status of the stream.
    protected class GlitchSniffer {
        public static final int SNIFFER_UPDATE_PERIOD_MSEC = 100;
        public static final int SNIFFER_UPDATE_DELAY_MSEC = 200;

        private long mTimeStarted;
        private long mTimeSinceLock;
        private double mMaxSecondsWithoutGlitches;
        private int mLastGlitchCount;
        private int mLastResetCount;
        private int mPreviousState = STATE_IDLE;
        private boolean mGotLock = false;
        private double mSignalToNoiseDB;
        private Handler mHandler = new Handler(Looper.getMainLooper()); // UI thread
        private volatile boolean mEnabled = true;
        private int mCounter = 0;

        // Display status info for the stream.
        private Runnable runnableCode = new Runnable() {
            @Override
            public void run() {
                int state = getAnalyzerState();
                int glitchCount = getGlitchCount();
                int resetCount = getResetCount();
                mSignalToNoiseDB = getSignalToNoiseDB();

                boolean locked = (state == STATE_LOCKED);
                if (locked && (mPreviousState != STATE_LOCKED)) {
                    mTimeSinceLock = System.currentTimeMillis();
                }
                mPreviousState = state;
                mGotLock = mGotLock || locked;
                if (glitchCount != mLastGlitchCount) {
                    mTimeSinceLock = System.currentTimeMillis();
                    mLastGlitchCount = glitchCount;
                }
                if (resetCount > mLastResetCount) {
                    mTimeSinceLock = System.currentTimeMillis();
                    mLastResetCount = resetCount;
                }

                updateStatusText(locked);

                // Reschedule so this task repeats
                if (mEnabled) {
                    mHandler.postDelayed(runnableCode, SNIFFER_UPDATE_PERIOD_MSEC);
                }
            }
        };

        private void updateStatusText(boolean locked) {
            long now = System.currentTimeMillis();
            double totalSeconds = (now - mTimeStarted) / 1000.0;
            double goodSeconds = (locked && (mTimeSinceLock > 0))
                    ? ((now - mTimeSinceLock) / 1000.0)
                    : 0.0;
            if (goodSeconds > mMaxSecondsWithoutGlitches) {
                mMaxSecondsWithoutGlitches = goodSeconds;
            }

            StringBuffer message = new StringBuffer();
            message.append("state = " + mPreviousState + " = " + stateToString(mPreviousState) + "\n");
            message.append(String.format("signal to noise ratio = %5.1f dB\n", mSignalToNoiseDB));
            message.append(String.format("time total = %8.2f seconds\n", totalSeconds));
            message.append(String.format("time without glitches = %8.2f sec\n",
                    goodSeconds));
            message.append(String.format("max time wout glitches = %8.2f sec\n",
                    mMaxSecondsWithoutGlitches));
            message.append("resetCount = " + mLastResetCount);

            if (mGotLock) {
                message.append(", glitchCount = " + mLastGlitchCount +"\n");
            } else {
                message.append("\n");
            }
            setAnalyzerText(message.toString());
        }

        private void startSniffer() {
            mTimeStarted = System.currentTimeMillis();
            mTimeSinceLock = 0;
            mLastGlitchCount = 0;
            mMaxSecondsWithoutGlitches = 0.0;
            mGotLock = false;
            // Start the initial runnable task by posting through the handler
            mEnabled = true;
            mHandler.postDelayed(runnableCode, SNIFFER_UPDATE_DELAY_MSEC);
        }

        private void stopSniffer() {
            mEnabled = false;
            if (mHandler != null) {
                mHandler.removeCallbacks(runnableCode);
            }

            final boolean locked = (mPreviousState == STATE_LOCKED);
            mPreviousState = STATE_IDLE;
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    updateStatusText(locked);
                }
            });
        }

        public double getMaxSecondsWithNoGlitch() {
            return mMaxSecondsWithoutGlitches;
        }

        public int geMLastGlitchCount() {
            return mLastGlitchCount;
        }
    }

    private GlitchSniffer mGlitchSniffer = new GlitchSniffer();

    private void setAnalyzerText(String s) {
        mAnalyzerTextView.setText(s);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mStartButton = (Button) findViewById(R.id.button_start);
        mStopButton = (Button) findViewById(R.id.button_stop);
        mStopButton.setEnabled(false);
        mAnalyzerTextView = (TextView) findViewById(R.id.text_analyzer_result);
        updateEnabledWidgets();

        hideSettingsViews();
    }

    @Override
    protected void onStart() {
        super.onStart();
        setActivityType(ACTIVITY_GLITCHES);
        mStartButton.setEnabled(true);
        mStopButton.setEnabled(false);
    }

    @Override
    protected void onStop() {
        stopAudioTest();
        super.onStop();
    }

    // Called on UI thread
    public void onStartAudioTest(View view) {
        startAudioTest();
        mStartButton.setEnabled(false);
        mStopButton.setEnabled(true);
    }

    public void startAudioTest() {
            openAudio();
            startAudio();
            mGlitchSniffer.startSniffer();
    }

    public void onCancel(View view) {
        stopAudioTest();
        mStartButton.setEnabled(true);
        mStopButton.setEnabled(false);
    }

    // Called on UI thread
    public void onStopAudioTest(View view) {
        stopAudioTest();
        onTestFinished();
    }

    // Must be called on UI thread.
    public void onTestFinished() {
        mStartButton.setEnabled(true);
        mStopButton.setEnabled(false);
    }

    public void stopAudioTest() {
        mGlitchSniffer.stopSniffer();
        stopAudio();
        closeAudio();
    }

    @Override
    boolean isOutput() {
        return false;
    }

    @Override
    public void setupEffects(int sessionId) {
    }

    public double getMaxSecondsWithNoGlitch() {
        return mGlitchSniffer.getMaxSecondsWithNoGlitch();
    }

    public int getLastGlitchCount() {
        return mGlitchSniffer.geMLastGlitchCount();
    }
}
