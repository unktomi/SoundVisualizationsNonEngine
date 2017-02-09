#pragma once

#include "Engine.h"
#include "MediaPlayer.h"
#include "IMediaOutput.h"
#include "IMediaPlayer.h"
#include "IMediaAudioSink.h"
#include "MediaSoundWave.h"
#include "CircularBuffer.h"
#include "WeakObjectPtr.h"
#if PLATFORM_ANDROID
#include <jni.h>
#endif
#include "SpectrumAnalyzer.generated.h"

class SOUNDVISUALIZATIONSNONENGINE_API SinkDelegate : public IMediaAudioSink
{
	FWeakObjectPtr Analyzer;
	int32 SampleRate;
	int32 Channels;
public:
	SinkDelegate(class USpectrumAnalyzer *InAnalyzer = nullptr);
	UMediaSoundWave* GetSoundWave();
	int32 GetNumChannels() { return Channels; }
	int32 GetSamplesPerSecond() { return SampleRate; }

	// IMediaAudioSink overrides
	virtual void FlushAudioSink() override;
	virtual bool InitializeAudioSink(uint32 InChannels, uint32 InSampleRate);
	virtual int32 GetAudioSinkChannels() const override;
	virtual int32 GetAudioSinkSampleRate() const override;
	virtual void PauseAudioSink() override;
	virtual void PlayAudioSink(const uint8* Buffer, uint32 BufferSize, FTimespan Time) override;
	virtual void ResumeAudioSink() override;
	virtual void ShutdownAudioSink() override;

};


UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class SOUNDVISUALIZATIONSNONENGINE_API USpectrumAnalyzer : public UActorComponent
{
	GENERATED_UCLASS_BODY()
public:
	virtual ~USpectrumAnalyzer();
	UPROPERTY(Category = "SoundVisualization", EditAnywhere, BlueprintReadOnly)
		UMediaPlayer *MediaPlayer;
	UPROPERTY(Category = "SoundVisualization", EditAnywhere, BlueprintReadOnly)
		UMediaSoundWave *SoundWave;
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

	virtual void ProcessMediaSample(uint32 Channels, uint32 SampleRate, const uint8* Buffer, uint32 BufferSize, FTimespan Duration, FTimespan Time);

	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;

	virtual void EndPlay
	(
		const EEndPlayReason::Type EndPlayReason
	) override;

	UFUNCTION()
		void HandleMediaOpened(FString OpenedUrl);
	UFUNCTION()
		void HandleMediaClosed();

private:
	void ConnectSink();
	bool DoCalculateFrequencySpectrum(bool bSplitChannels, TArray<TArray<float> >& OutSpectrums);
	bool DoGetAmplitude(bool bSplitChannels, TArray<TArray<float> >& OutSpectrums);
	TCircularBuffer<int16>  *PCMData;
	FTimespan CurrentTime;
	FTimespan PlaybackTime;
	TSharedRef<SinkDelegate, ESPMode::ThreadSafe> Sink;
	uint32 CurSampleIndex;
	mutable FCriticalSection CriticalSection;
#if PLATFORM_ANDROID
public:
	void HandleCapture(const uint8* WaveForm, uint32 WaveFormSize);
	TArray<int16> ResampleBuffer;
	jobject Visualizer;
	static jclass VisualizerClass;
	static jmethodID CreateVisualizer;
	static jmethodID EnableVisualizer;
	static void InitMethodIds();
#endif
};
