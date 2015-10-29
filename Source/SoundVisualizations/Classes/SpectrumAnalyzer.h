#pragma once

#include "Engine.h"
#include "MediaPlayer.h"
#include "IMediaAudioTrack.h"
#include "IMediaSink.h"
#include "CircularBuffer.h"
#include "WeakObjectPtr.h"
#include "SpectrumAnalyzer.generated.h"

struct SinkDelegate : public IMediaSink
{
	FWeakObjectPtr Analyzer;
	SinkDelegate(class USpectrumAnalyzer *A = nullptr);
	virtual void ProcessMediaSample(const void* Buffer, uint32 BufferSize, FTimespan Duration, FTimespan Time) override;	
};


UCLASS(BlueprintType, Blueprintable)
class USpectrumAnalyzer: public UObject
{
  GENERATED_UCLASS_BODY()
public:
	virtual ~USpectrumAnalyzer();
	UPROPERTY(Category = "SoundVisualization", EditAnywhere, BlueprintReadOnly)
		UMediaPlayer *MediaPlayer;
	UPROPERTY(Category = "SoundVisualization", EditAnywhere, BlueprintReadOnly)
		float WindowDurationInSeconds;
	UPROPERTY(Category = "SoundVisualization", EditAnywhere, BlueprintReadOnly)
		int32 SpectrumWidth;
	UPROPERTY(Category = "SoundVisualization", EditAnywhere, BlueprintReadOnly)
		int32 AmplitudeBuckets;

	UFUNCTION(BlueprintCallable, Category = "SoundVisualization")	
    void CalculateFrequencySpectrum(int32 Channel, TArray<float>& OutSpectrum);
	UFUNCTION(BlueprintCallable, Category = "SoundVisualization")
	void GetAmplitude(int32 Channel, TArray<float>& OutAmplitudes);
	
	virtual void ProcessMediaSample(const void* Buffer, uint32 BufferSize, FTimespan Duration, FTimespan Time);

 private:
	bool DoCalculateFrequencySpectrum(bool bSplitChannels, TArray<TArray<float> >& OutSpectrums);
	bool DoGetAmplitude(bool bSplitChannels, TArray<TArray<float> >& OutSpectrums);
    TCircularBuffer<int16>  *PCMData;
	FTimespan CurrentTime;
	FTimespan PlaybackTime;
	IMediaAudioTrackPtr CurrentTrack;
	TSharedRef<SinkDelegate, ESPMode::ThreadSafe> Sink;
	uint32 SampleIndex;
	mutable FCriticalSection CriticalSection;
};
