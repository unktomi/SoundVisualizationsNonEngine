package com.soundVisualizationsNonEngine;
import android.media.audiofx.Visualizer;

public class SoundVisualizer {
  Visualizer soundViz;
  static native void nativeSendWaveForm(long callbackObject, byte[] bytes, int sampleRate);

  static public SoundVisualizer createSoundVisualizer(long callbackObject, int sampleRate) {
    return new SoundVisualizer(callbackObject, sampleRate);
  }

  boolean bEnabled;

  static void error(String message) {
    android.util.Log.e("SoundVisualizer", message);
  }

  static void note(String message) {
    android.util.Log.d("SoundVisualizer", message);
  }

  public SoundVisualizer(final long callbackObject, final int sampleRate) {
    soundViz = new Visualizer(0);
    if (soundViz.setCaptureSize(Visualizer.getCaptureSizeRange()[1]) != Visualizer.SUCCESS) {
      error("invalid capture size: "+ Visualizer.getCaptureSizeRange()[1]);
    }
    final int maxRate = Visualizer.getMaxCaptureRate();
    note("maxRate="+maxRate);
    if (soundViz.setDataCaptureListener(new Visualizer.OnDataCaptureListener() {
        @Override
        public void onWaveFormDataCapture(Visualizer visualizer, byte[] bytes, int sampleRate) {
          //note("wave capture: "+ bytes.length + " bytes. SampleRate: "+sampleRate + " Callback: "+callbackObject);
          if (bEnabled) {
            nativeSendWaveForm(callbackObject, bytes, sampleRate);
          }
        }
        
        @Override
        public void onFftDataCapture(Visualizer visualizer, byte[] bytes, int i) {
          
        }
      }, maxRate, true, false) != Visualizer.SUCCESS) {
      error("setDataCaptureListenerFailed: "+sampleRate);
    }
  }

  public void setEnabled(final boolean value) {
    bEnabled = value;
    new Thread() {
      public void run() {
        soundViz.setEnabled(value);
      }
    }.start();
  }
}
