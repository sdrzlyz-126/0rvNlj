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
package com.google.oboe.sample.drumthumper

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Bundle
import android.util.Log
import android.widget.SeekBar
import android.widget.Toast

import androidx.appcompat.app.AppCompatActivity

import java.util.*

import java.time.LocalDateTime;

import kotlin.concurrent.schedule
import kotlin.math.roundToInt

class DrumThumperActivity : AppCompatActivity(),
        TriggerPad.DrumPadTriggerListener,
        SeekBar.OnSeekBarChangeListener {
    private val TAG = "DrumThumperActivity"

    private var mAudioMgr: AudioManager? = null

    private var mDrumPlayer = DrumPlayer()

    private val mUseDeviceChangeFallback = false
    private val mSwitchTimerMs = 500L

    private var mDevicesInitialized = false

    private var mDeviceListener: DeviceListener = DeviceListener()

    init {
        // Load the library containing the a native code including the JNI  functions
        System.loadLibrary("drumthumper")
    }

    inner class DeviceListener: AudioDeviceCallback() {
        fun logDevices(label: String, devices: Array<AudioDeviceInfo> ) {
            Log.i(TAG, label + " " + devices.size)
            for(device in devices) {
                Log.i(TAG, "  " + device.getProductName().toString()
                    + " type:" + device.getType()
                    + " source:" + device.isSource()
                    + " sink:" + device.isSink())
            }
        }

        override fun onAudioDevicesAdded(addedDevices: Array<AudioDeviceInfo> ) {
            // Note: This will get called when the callback is installed.
            if (mDevicesInitialized) {
                logDevices("onAudioDevicesAdded", addedDevices)
                // This is not the initial callback, so devices have changed
                Toast.makeText(applicationContext, "Added Device", Toast.LENGTH_LONG).show()
                resetOutput()
            }
            mDevicesInitialized = true
        }

        override fun onAudioDevicesRemoved(removedDevices: Array<AudioDeviceInfo> ) {
            logDevices("onAudioDevicesRemoved", removedDevices)
            Toast.makeText(applicationContext, "Removed Device", Toast.LENGTH_LONG).show()
            resetOutput()
        }

        private fun resetOutput() {
            Log.i(TAG, "resetOutput() time:" + LocalDateTime.now() + " native reset:" + mDrumPlayer.getOutputReset());
            if (mDrumPlayer.getOutputReset()) {
                // the (native) stream has been reset by the onErrorAfterClose() callback
                mDrumPlayer.clearOutputReset()
            } else {
                // give the (native) stream a chance to close it.
                val timer = Timer("stream restart timer time:" + LocalDateTime.now(),
                        false)
                // schedule a single event
                timer.schedule(mSwitchTimerMs) {
                    if (!mDrumPlayer.getOutputReset()) {
                        // still didn't get reset, so lets do it ourselves
                        Log.i(TAG, "restartStream() time:" + LocalDateTime.now())
                        mDrumPlayer.restartStream()
                    }
                }
            }
        }
    }

    // UI Helpers
    fun gainPosToGainVal(pos: Int) : Float {
        // map 0 -> 200 to 0.0f -> 2.0f
        return pos.toFloat() / 100.0f
    }

    fun gainValToGainPos(value: Float) : Int {
        return (value * 100.0f).toInt()
    }

    fun panPosToPanVal(pos: Int) : Float {
        // map 0 -> 200 to -1.0f -> 1..0f
        return (pos.toFloat() - 100.0f) / 100.0f
    }

    fun panValToPanPos(value: Float) : Int {
        // map -1.0f -> 1.0f to 0 -> 200
        return ((value * 200.0f) + 100.0f).toInt()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        mAudioMgr = getSystemService(Context.AUDIO_SERVICE) as AudioManager

        // mDrumPlayer.allocSampleData()
        mDrumPlayer.loadWavAssets(getAssets())
    }

    override fun onStart() {
        super.onStart()

        mDrumPlayer.setupAudioStream()

        if (mUseDeviceChangeFallback) {
            mAudioMgr!!.registerAudioDeviceCallback(mDeviceListener, null)
        }
    }

    override fun onResume() {
        super.onResume()

        // UI
        setContentView(R.layout.drumthumper_activity)

        // hookup the UI
        var sb : SeekBar;

        // "Kick" drum
        (findViewById(R.id.kickPad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.kickPan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.BASSDRUM)))
        sb = (findViewById(R.id.kickGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.BASSDRUM)))

        // Snare drum
        (findViewById(R.id.snarePad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.snarePan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.SNAREDRUM)))
        sb = (findViewById(R.id.snareGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.SNAREDRUM)))

        // Mid tom
        (findViewById(R.id.midTomPad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.midTomPan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.MIDTOM)))
        sb = (findViewById(R.id.midTomGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain((DrumPlayer.MIDTOM))))

        // Low tom
        (findViewById(R.id.lowTomPad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.lowTomPan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.LOWTOM)))
        sb = (findViewById(R.id.lowTomGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.LOWTOM)))

        // Open hihat
        (findViewById(R.id.hihatOpenPad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.hihatOpenPan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.HIHATOPEN)))
        sb = (findViewById(R.id.hihatOpenGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.HIHATOPEN)))

        // Closed hihat
        (findViewById(R.id.hihatClosedPad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.hihatClosedPan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.HIHATCLOSED)))
        sb = (findViewById(R.id.hihatClosedGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.HIHATCLOSED)))

        // Ride cymbal
        (findViewById(R.id.ridePad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.ridePan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.RIDECYMBAL)))
        sb = (findViewById(R.id.rideGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.RIDECYMBAL)))

        // Crash cymbal
        (findViewById(R.id.crashPad) as TriggerPad).addListener(this)
        sb = (findViewById(R.id.crashPan) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(panValToPanPos(mDrumPlayer.getPan(DrumPlayer.CRASHCYMBAL)))
        sb = (findViewById(R.id.crashGain) as SeekBar)
        sb.setOnSeekBarChangeListener(this)
        sb.setProgress(gainValToGainPos(mDrumPlayer.getGain(DrumPlayer.CRASHCYMBAL)))
    }

    override fun onPause() {
        super.onPause()
    }

    override fun onStop() {
        if (mUseDeviceChangeFallback) {
            mAudioMgr!!.unregisterAudioDeviceCallback(mDeviceListener)
        }

        mDrumPlayer.teardownAudioStream()

        super.onStop()
    }

    override fun onDestroy() {
        mDrumPlayer.unloadWavAssets();
        super.onDestroy()
    }

    //
    // DrumPad.DrumPadTriggerListener
    //
    override fun triggerDown(pad: TriggerPad) {
        // trigger the sound based on the pad
        when (pad.id) {
            R.id.kickPad -> mDrumPlayer.trigger(DrumPlayer.BASSDRUM)
            R.id.snarePad -> mDrumPlayer.trigger(DrumPlayer.SNAREDRUM)
            R.id.midTomPad -> mDrumPlayer.trigger(DrumPlayer.MIDTOM)
            R.id.lowTomPad -> mDrumPlayer.trigger(DrumPlayer.LOWTOM)
            R.id.hihatOpenPad -> mDrumPlayer.trigger(DrumPlayer.HIHATOPEN)
            R.id.hihatClosedPad -> mDrumPlayer.trigger(DrumPlayer.HIHATCLOSED)
            R.id.ridePad -> mDrumPlayer.trigger(DrumPlayer.RIDECYMBAL)
            R.id.crashPad -> mDrumPlayer.trigger(DrumPlayer.CRASHCYMBAL)
        }
    }

    override fun triggerUp(pad: TriggerPad) {
        // NOP
    }

    //
    // SeekBar.OnSeekBarChangeListener
    //
    override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
        when (seekBar!!.id) {
            // BASSDRUM
            R.id.kickGain -> mDrumPlayer.setGain(DrumPlayer.BASSDRUM, gainPosToGainVal(progress))
            R.id.kickPan -> mDrumPlayer.setPan(DrumPlayer.BASSDRUM, panPosToPanVal(progress))

            // SNAREDRUM
            R.id.snareGain -> mDrumPlayer.setGain(DrumPlayer.SNAREDRUM, gainPosToGainVal(progress))
            R.id.snarePan -> mDrumPlayer.setPan(DrumPlayer.SNAREDRUM, panPosToPanVal(progress))

            // MIDTOM
            R.id.midTomGain -> mDrumPlayer.setGain(DrumPlayer.MIDTOM, gainPosToGainVal(progress))
            R.id.midTomPan -> mDrumPlayer.setPan(DrumPlayer.MIDTOM, panPosToPanVal(progress))

            // LOWTOM
            R.id.lowTomGain -> mDrumPlayer.setGain(DrumPlayer.LOWTOM, gainPosToGainVal(progress))
            R.id.lowTomPan -> mDrumPlayer.setPan(DrumPlayer.LOWTOM, panPosToPanVal(progress))

            // HIHATOPEN
            R.id.hihatOpenGain -> mDrumPlayer.setGain(DrumPlayer.HIHATOPEN, gainPosToGainVal(progress))
            R.id.hihatOpenPan -> mDrumPlayer.setPan(DrumPlayer.HIHATOPEN, panPosToPanVal(progress))

            // HIHATCLOSED
            R.id.hihatClosedGain -> mDrumPlayer.setGain(DrumPlayer.HIHATCLOSED, gainPosToGainVal(progress))
            R.id.hihatClosedPan -> mDrumPlayer.setPan(DrumPlayer.HIHATCLOSED, panPosToPanVal(progress))

            // RIDECYMBAL
            R.id.rideGain -> mDrumPlayer.setGain(DrumPlayer.RIDECYMBAL, gainPosToGainVal(progress))
            R.id.ridePan -> mDrumPlayer.setPan(DrumPlayer.RIDECYMBAL, panPosToPanVal(progress))

            // CRASHCYMBAL
            R.id.crashGain -> mDrumPlayer.setGain(DrumPlayer.CRASHCYMBAL, gainPosToGainVal(progress))
            R.id.crashPan -> mDrumPlayer.setPan(DrumPlayer.CRASHCYMBAL, panPosToPanVal(progress))
        }
    }

    override fun onStartTrackingTouch(seekBar: SeekBar?) {
        // NOP
    }

    override fun onStopTrackingTouch(seekBar: SeekBar?) {
        // NOP
    }

}
