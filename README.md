# SoundVisualizationsNonEngine
Fork of UE-4 SoundVisualization Engine plugin modified to work with media framework

This repository contains a fork of the Unreal Engine SoundVisualizations plugin which is an engine internal plugin.
This version has been renamed to work as a non-engine plugin. In addition, it has been modified to capture sound samples
from a MediaPlayer - the original plugin only worked with .wav files apparently.

The following Blueprint functions and classes are provided:

    USpectrumAnalyzer *CreateSpectrumAnalyzer (UMediaPlayer *Player, float WindowDurationInSeconds, int SpectrumWidth, int AmplitudeBuckets)
  
The USpectrumAnalyzer class in turn provides the following functions:

    CalculateFrequencySpectrum(USpectrumAnalyzer *Target, int Channel, TArray<float> &OutSpectrum)
    GetAmplitude(USpectrumAnalyzer *Target, int Channel, TArray<float> &OutAmplitudes)
  
The intention is that CreateSpectrumAnalyzer should be called in the BeginPlay event to create an instance and saved in a variable.
Then in EventTick call the methods of USpectrumAnalyzer as necessary.

