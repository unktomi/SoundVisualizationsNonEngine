#include "SoundVisualizationsNonEnginePrivatePCH.h"
#include "SpectrumAnalyzer.h"
#include "kiss_fft.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpectrumAnalyzer, Log, All);

// Hacks for android which has degenerate support for IMediaPlayer.
// The Epic implementation AndroidMediaPlayer ultimately depends on the java MediaPlayer. 
// Our workaround is to use the android Visualizer to
// obtain 8-bit periodic samples.

#if PLATFORM_ANDROID
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#include <android_native_app_glue.h>

jmethodID USpectrumAnalyzer::CreateVisualizer = 0;
jmethodID USpectrumAnalyzer::EnableVisualizer = 0;
jclass USpectrumAnalyzer::VisualizerClass = 0;

void USpectrumAnalyzer::InitMethodIds()
{
	if (CreateVisualizer == 0)
	{
		if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
		{
			jmethodID method = FJavaWrapper::FindStaticMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_SoundVisualizationsNonEngineGetVisualizerClass", "()Ljava/lang/Class;", false);
			VisualizerClass = (jclass)Env->CallStaticObjectMethod(FJavaWrapper::GameActivityClassID, method);
			CreateVisualizer = FJavaWrapper::FindStaticMethod(Env, VisualizerClass, "createSoundVisualizer", "(JI)Lcom/soundVisualizationsNonEngine/SoundVisualizer;", false);
			EnableVisualizer = FJavaWrapper::FindMethod(Env, VisualizerClass, "setEnabled", "(Z)V", false);
		}
	}
}

#endif


USpectrumAnalyzer::USpectrumAnalyzer(const class FObjectInitializer& PCIP)
	: Super(PCIP),
	Sink(new SinkDelegate(this)),
	CurSampleIndex(0),
	CurrentTime(FTimespan(0)),
	PlaybackTime(FTimespan(0)),
	PCMData(nullptr),
	WindowDurationInSeconds(0.03333f),
	SpectrumWidth(10),
	AmplitudeBuckets(10)
{
#if PLATFORM_ANDROID
	this->Visualizer = 0;
#endif
}

void USpectrumAnalyzer::BeginDestroy()
{
	if (MediaPlayer != nullptr)
	{
		if (MediaPlayer->GetPlayer().IsValid())
		{
			MediaPlayer->GetPlayer()->GetOutput().SetAudioSink(nullptr);
		}
	}
	Super::BeginDestroy();
}



USpectrumAnalyzer::~USpectrumAnalyzer()
{
	delete PCMData;
}

SinkDelegate::
SinkDelegate(USpectrumAnalyzer *InAnalyzer) : Analyzer(InAnalyzer) {}

void SinkDelegate::
PlayAudioSink(const uint8* Buffer, uint32 BufferSize, FTimespan Time)
{
	USpectrumAnalyzer *U = (USpectrumAnalyzer*)Analyzer.Get();
	uint32 NumSamples = BufferSize / (sizeof(uint16) * Channels);
	FTimespan Duration = FTimespan::FromSeconds((double)NumSamples / (double)SampleRate);
	if (U != nullptr)
	{
		UMediaSoundWave* SoundWave = GetSoundWave();
		if (SoundWave != nullptr)
		{
			SoundWave->PlayAudioSink(Buffer, BufferSize, Time);
		}
		U->ProcessMediaSample(Channels, SampleRate, Buffer, BufferSize, Duration, Time);
	}
}

UMediaSoundWave*
SinkDelegate::GetSoundWave()
{
#if PLATFORM_ANDROID
	// no support for this on android yet
#else
	USpectrumAnalyzer* U = (USpectrumAnalyzer*)Analyzer.Get();
	if (U != nullptr)
	{
		return U->SoundWave;
	}
#endif
	return nullptr;
}

void SinkDelegate::FlushAudioSink()
{
	UMediaSoundWave* SoundWave = GetSoundWave();
	if (SoundWave != nullptr) SoundWave->FlushAudioSink();
}

bool SinkDelegate::InitializeAudioSink(uint32 InChannels, uint32 InSampleRate)
{
	Channels = InChannels;
	SampleRate = InSampleRate;
	bool bSoundWaveResult = true;
	UMediaSoundWave* SoundWave = GetSoundWave();
	if (SoundWave != nullptr) {
		bSoundWaveResult = SoundWave->InitializeAudioSink(InChannels, InSampleRate);
	}
#if PLATFORM_ANDROID	
	USpectrumAnalyzer *U = (USpectrumAnalyzer*)Analyzer.Get();
	USpectrumAnalyzer::InitMethodIds();
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	jobject Obj = Env->CallStaticObjectMethod(U->VisualizerClass, USpectrumAnalyzer::CreateVisualizer, (jlong)U, (int32)SampleRate);
	U->Visualizer = Env->NewGlobalRef(Obj);
	FJavaWrapper::CallVoidMethod(Env, U->Visualizer, USpectrumAnalyzer::EnableVisualizer, true);
	UE_LOG(LogSpectrumAnalyzer, Log, TEXT("Created Android Sound Visualizer %p, sample rate: %d"), U->Visualizer, SampleRate);
	Env->DeleteLocalRef(Obj);
#endif
	return bSoundWaveResult && Channels > 0 && SampleRate > 0;
}

int32 SinkDelegate::GetAudioSinkChannels() const { return Channels; }
int32 SinkDelegate::GetAudioSinkSampleRate() const { return SampleRate; }

void SinkDelegate::PauseAudioSink()
{
	UMediaSoundWave* SoundWave = GetSoundWave();
	if (SoundWave != nullptr) SoundWave->PauseAudioSink();
}

void SinkDelegate::ResumeAudioSink()
{
	UMediaSoundWave* SoundWave = GetSoundWave();
	if (SoundWave != nullptr) SoundWave->ResumeAudioSink();
}

void SinkDelegate::ShutdownAudioSink()
{
	UMediaSoundWave* SoundWave = GetSoundWave();
	if (SoundWave != nullptr) SoundWave->ShutdownAudioSink();
#if PLATFORM_ANDROID
	USpectrumAnalyzer* U = (USpectrumAnalyzer*)Analyzer.Get();
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	FJavaWrapper::CallVoidMethod(Env, U->Visualizer, USpectrumAnalyzer::EnableVisualizer, false);
	Env->DeleteGlobalRef(U->Visualizer);
	U->Visualizer = 0;
#endif
}

void USpectrumAnalyzer::
ProcessMediaSample(uint32 NumChannels, uint32 SamplesPerSecond, const uint8* Buffer, volatile uint32 BufferSize, FTimespan Duration, FTimespan Time)
{
	FScopeLock ScopeLock(&CriticalSection);
	// buffer a few seconds
	uint32 SamplesNeeded = SamplesPerSecond*NumChannels * 3;// WindowDurationInSeconds;
	uint32 PoT = 2;
	while (PoT < SamplesNeeded) PoT *= 2;
	if (PCMData == nullptr || PCMData->Capacity() < PoT)
	{
		delete PCMData;
		PCMData = new TCircularBuffer<int16>(PoT, (int16)0);
		CurSampleIndex = 0;
	}
	CurrentTime = Time + Duration;
	uint32 SampleCount = Duration.GetTotalSeconds() * SamplesPerSecond;
	uint32 SamplesAvailable = BufferSize / sizeof(int16);
	int16 *SampleBuffer = (int16*)(Buffer);
	uint32 Start = CurSampleIndex;
	for (uint32 i = 0; i < SamplesAvailable; i++)
	{
		(*PCMData)[CurSampleIndex] = SampleBuffer[i];
		CurSampleIndex = PCMData->GetNextIndex(CurSampleIndex);
	}
	//uint32 End = CurSampleIndex;
	//UE_LOG(LogSpectrumAnalyzer, Warning, TEXT("Samples available %d, Buffered %f seconds, Current time %f"), SamplesAvailable, (CurrentTime-PlaybackTime).GetTotalSeconds(), Time.GetTotalSeconds());

}

static float GetFFTInValue(const int16 SampleValue, const int16 SampleIndex, const int16 SampleCount)
{
	float FFTValue = SampleValue;

	// Apply the Hann window
	FFTValue *= 0.5f * (1 - FMath::Cos(2 * PI * SampleIndex / (SampleCount - 1)));

	return FFTValue;
}

void USpectrumAnalyzer::
CalculateFrequencySpectrum(int32 Channel, TArray<float> &OutSpectrum)
{
	TArray< TArray<float> > Spectrums;

	volatile bool did = DoCalculateFrequencySpectrum(Channel != 0, Spectrums);
	if (!did)
	{
		//UE_LOG(LogSpectrumAnalyzer, Warning, TEXT("CalculateFrequencySpectrum: skipped"));
		OutSpectrum.AddZeroed(SpectrumWidth);
		return;
	}
	
	if (Spectrums.Num() > 0)
	{
		if (Channel == 0)
		{
			OutSpectrum = Spectrums[0];
		}
		else if (Channel <= Spectrums.Num())
		{
			OutSpectrum = Spectrums[Channel - 1];
		}
		else
		{
			UE_LOG(LogSpectrumAnalyzer, Error, TEXT("Requested channel %d, sound only has %d channels"), SoundWave->NumChannels);
		}
	}
	for (int32 i = 0; i < OutSpectrum.Num(); i++)
	{
		if (!FMath::IsFinite(OutSpectrum[i]))
		{
			OutSpectrum[i] = 0.0f;
		}
	}
}

void USpectrumAnalyzer::BeginPlay()
{
#if PLATFORM_ANDROID
	InitMethodIds();
#endif
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->SetSoundWave(nullptr);
		MediaPlayer->OnMediaOpened.AddDynamic(this, &USpectrumAnalyzer::HandleMediaOpened);
		MediaPlayer->OnMediaClosed.AddDynamic(this, &USpectrumAnalyzer::HandleMediaClosed);
	}
}

void USpectrumAnalyzer::HandleMediaOpened(FString OpenedUrl)
{
	CurrentTime = MediaPlayer->GetTime();
	ConnectSink();
}


void USpectrumAnalyzer::ConnectSink()
{
	if (MediaPlayer != nullptr)
	{
		TSharedPtr<IMediaPlayer> Player = MediaPlayer->GetPlayer();
		if (Player.IsValid())
		{
			Player->GetOutput().SetAudioSink(&Sink.Get());
		}
	}
}

void USpectrumAnalyzer::HandleMediaClosed()
{
	if (MediaPlayer != nullptr)
	{
		TSharedPtr<IMediaPlayer> Player = MediaPlayer->GetPlayer();
		if (Player.IsValid())
		{
			Player->GetOutput().SetAudioSink(nullptr);
		}
	}
}

void USpectrumAnalyzer::EndPlay(EEndPlayReason::Type Reason)
{
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &USpectrumAnalyzer::HandleMediaOpened);
		MediaPlayer->OnMediaClosed.RemoveDynamic(this, &USpectrumAnalyzer::HandleMediaClosed);
	}
}

bool USpectrumAnalyzer::
DoCalculateFrequencySpectrum(bool bSplitChannels, TArray<TArray<float> > &OutSpectrums)
{
	if (MediaPlayer == nullptr || MediaPlayer->IsPaused())
	{
		return false;
	}
	FScopeLock ScopeLock(&CriticalSection);
	if (PCMData == nullptr) return false;
	PlaybackTime = MediaPlayer->GetTime();
	//UE_LOG(LogSpectrumAnalyzer, Log, TEXT("PlaybackTime %f, CurrentTime %f"), PlaybackTime.GetTotalSeconds(), CurrentTime.GetTotalSeconds());
	FTimespan Delta = CurrentTime - PlaybackTime;
	uint32 NumChannels = Sink->GetNumChannels();
	uint32 SamplesPerSecond = Sink->GetSamplesPerSecond();
	int32 DeltaSamples = Delta.GetTotalSeconds() * SamplesPerSecond * NumChannels;
	uint32 SamplesNeeded = SamplesPerSecond * NumChannels * WindowDurationInSeconds;
	if (CurSampleIndex < SamplesNeeded)
	{
		//return false;
	}
	int32 SampleCount = SamplesNeeded;
	int32 LastSample = CurSampleIndex - DeltaSamples - 1;
	int32 FirstSample = LastSample - SampleCount;
	//UE_LOG(LogSpectrumAnalyzer, Log, TEXT("Samples %d .. %d"), FirstSample, LastSample);

	volatile int32 SamplesToRead = LastSample - FirstSample;
	// Setup the output data
	OutSpectrums.AddZeroed((bSplitChannels ? NumChannels : 1));
	for (int32 ChannelIndex = 0; ChannelIndex < OutSpectrums.Num(); ++ChannelIndex)
	{
		OutSpectrums[ChannelIndex].AddZeroed(SpectrumWidth);
	}
	if (SamplesToRead > 0)
	{
		// Shift the window enough so that we get a power of 2
		int32 PoT = 2;
		while (SamplesToRead > PoT) PoT *= 2;
		FirstSample = FMath::Max(0, FirstSample - (PoT - SamplesToRead) / 2);
		SamplesToRead = PoT;
		LastSample = FirstSample + SamplesToRead;
		if (LastSample > SampleCount)
		{
			FirstSample = LastSample - SamplesToRead;
		}
		if (FirstSample < 0)
		{
			// If we get to this point we can't create a reasonable window so just give up
			//SoundWave->RawData.Unlock();
			return false;
		}

		kiss_fft_cpx* buf[10] = { 0 };
		kiss_fft_cpx* out[10] = { 0 };
		kiss_fft_cfg stf = kiss_fft_alloc(SamplesToRead, 1, 0, 0);


		//int16* SamplePtr = reinterpret_cast<int16*>(WaveInfo.SampleDataStart);
		int32 SamplePtr = FirstSample;
		if (NumChannels <= 2)
		{
			for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				buf[ChannelIndex] = (kiss_fft_cpx *)KISS_FFT_MALLOC(sizeof(kiss_fft_cpx) * SamplesToRead);
				out[ChannelIndex] = (kiss_fft_cpx *)KISS_FFT_MALLOC(sizeof(kiss_fft_cpx) * SamplesToRead);
			}

			//SamplePtr += (FirstSample * NumChannels);
			const TCircularBuffer<int16> &Sampler = *PCMData;
			for (int32 SampleIndex = 0; SampleIndex < SamplesToRead; ++SampleIndex)
			{
				for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
				{
					int32 Index = SamplePtr;
					int16 Value = Sampler[Index];
					buf[ChannelIndex][SampleIndex].r = GetFFTInValue(Value, SampleIndex, SamplesToRead);
					buf[ChannelIndex][SampleIndex].i = 0.f;

					SamplePtr++;
				}
			}
		}
		else
		{
			return false;
		}

		for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
		{
			if (buf[ChannelIndex])
			{
				kiss_fft(stf, buf[ChannelIndex], out[ChannelIndex]);
			}
		}

		int32 SamplesPerSpectrum = SamplesToRead / (2 * SpectrumWidth);
		int32 ExcessSamples = SamplesToRead % (2 * SpectrumWidth);

		int32 FirstSampleForSpectrum = 1;
		for (int32 SpectrumIndex = 0; SpectrumIndex < SpectrumWidth; ++SpectrumIndex)
		{
			static bool doLog = false;

			int32 SamplesRead = 0;
			double SampleSum = 0;
			int32 SamplesForSpectrum = SamplesPerSpectrum + (ExcessSamples-- > 0 ? 1 : 0);

			for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				if (out[ChannelIndex])
				{
					if (bSplitChannels)
					{
						SampleSum = 0;
					}

					for (int32 SampleIndex = 0; SampleIndex < SamplesForSpectrum; ++SampleIndex)
					{
						float PostScaledR = out[ChannelIndex][FirstSampleForSpectrum + SampleIndex].r * 2.f / SamplesToRead;
						float PostScaledI = out[ChannelIndex][FirstSampleForSpectrum + SampleIndex].i * 2.f / SamplesToRead;
						//float Val = FMath::Sqrt(FMath::Square(PostScaledR) + FMath::Square(PostScaledI));
						float Val = 10.f * FMath::LogX(10.f, FMath::Square(PostScaledR) + FMath::Square(PostScaledI));
						//UE_LOG(LogSoundVisualization, Log, TEXT("%.2f"), Val);
						SampleSum += Val;
					}

					if (bSplitChannels)
					{
						OutSpectrums[ChannelIndex][SpectrumIndex] = (float)(SampleSum / SamplesForSpectrum);
					}
					SamplesRead += SamplesForSpectrum;
				}
			}

			if (!bSplitChannels)
			{
				OutSpectrums[0][SpectrumIndex] = (float)(SampleSum / SamplesRead);
			}

			FirstSampleForSpectrum += SamplesForSpectrum;
		}

		KISS_FFT_FREE(stf);
		for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
		{
			if (buf[ChannelIndex])
			{
				KISS_FFT_FREE(buf[ChannelIndex]);
				KISS_FFT_FREE(out[ChannelIndex]);
			}
		}
	}
	return true;
}

void USpectrumAnalyzer::
GetAmplitude(int32 Channel, TArray<float> &OutSpectrum)
{
	TArray< TArray<float> > Spectrums;
	volatile bool did = DoGetAmplitude(Channel != 0, Spectrums);
	if (!did)
	{
		OutSpectrum.AddZeroed(AmplitudeBuckets);
		//UE_LOG(LogSpectrumAnalyzer, Warning, TEXT("GetAmplitude: skipped"));
		return;
	}
	if (Spectrums.Num() > 0)
	{
		if (Channel == 0)
		{
			OutSpectrum = Spectrums[0];
		}
		else if (Channel <= Spectrums.Num())
		{
			OutSpectrum = Spectrums[Channel - 1];
		}
		else
		{
			UE_LOG(LogSoundVisualization, Error, TEXT("Requested channel %d, sound only has %d channels"), SoundWave->NumChannels);
		}
	}
	for (int32 i = 0; i < OutSpectrum.Num(); i++)
	{
		if (!FMath::IsFinite(OutSpectrum[i]))
		{
			OutSpectrum[i] = 0.0f;
		}
	}
}

bool USpectrumAnalyzer::
DoGetAmplitude(bool bSplitChannels, TArray<TArray<float> > &OutAmplitudes)
{

	if (MediaPlayer != nullptr)
	{
		TSharedPtr<IMediaPlayer> Player = MediaPlayer->GetPlayer();
		if (Player.IsValid())
		{
			Player->GetOutput().SetAudioSink(&Sink.Get());
		}
	}
	if (MediaPlayer->IsPaused())
	{
		return false;
	}
	FScopeLock ScopeLock(&CriticalSection);
	if (PCMData == nullptr) return false;
	uint32 NumChannels = Sink->GetNumChannels();
	uint32 SamplesPerSecond = Sink->GetSamplesPerSecond();
	uint32 SamplesNeeded = SamplesPerSecond * NumChannels * WindowDurationInSeconds;
	if (CurSampleIndex < SamplesNeeded)
	{
		//return;
	}
	int32 SampleCount = SamplesNeeded;
	PlaybackTime = MediaPlayer->GetTime();

	FTimespan Delta = CurrentTime - PlaybackTime;
	int32 DeltaSamples = Delta.GetTotalSeconds() * SamplesPerSecond * NumChannels;
	int32 LastSample = CurSampleIndex - DeltaSamples - 1;
	int32 FirstSample = LastSample - SampleCount;

	volatile int32 SamplesToRead = LastSample - FirstSample;
	if (SamplesToRead <= 0)
	{
		//UE_LOG(LogSpectrumAnalyzer, Warning, TEXT("GetAmplitudes PlaybackTime %f, CurrentTime %f"), PlaybackTime.GetTotalSeconds(), CurrentTime.GetTotalSeconds());
		return false;
	}
	else
	{
		//UE_LOG(LogSpectrumAnalyzer, Display, TEXT("PlaybackTime %f"), PlaybackTime.GetTotalSeconds());
	}
	OutAmplitudes.Empty();

	if (AmplitudeBuckets > 0 && NumChannels > 0)
	{
		// Setup the output data
		OutAmplitudes.AddZeroed((bSplitChannels ? NumChannels : 1));
		for (int32 ChannelIndex = 0; ChannelIndex < OutAmplitudes.Num(); ++ChannelIndex)
		{
			OutAmplitudes[ChannelIndex].AddZeroed(AmplitudeBuckets);
		}
		const TCircularBuffer<int16> &Sampler = *PCMData;

		int32 SamplePtr = FirstSample;
		uint32 SamplesPerAmplitude = (LastSample - FirstSample) / AmplitudeBuckets;
		uint32 ExcessSamples = (LastSample - FirstSample) % AmplitudeBuckets;

		for (int32 AmplitudeIndex = 0; AmplitudeIndex < AmplitudeBuckets; ++AmplitudeIndex)
		{
			if (NumChannels <= 2)
			{
				int64 SampleSum[2] = { 0 };
				SamplesToRead = SamplesPerAmplitude + (ExcessSamples-- > 0 ? 1 : 0);
				for (int32 SampleIndex = 0; SampleIndex < SamplesToRead; ++SampleIndex)
				{
					for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
					{
						SampleSum[ChannelIndex] += FMath::Abs(Sampler[SamplePtr]);
						SamplePtr++;
					}
				}
				for (uint32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
				{
					OutAmplitudes[(bSplitChannels ? ChannelIndex : 0)][AmplitudeIndex] = SampleSum[ChannelIndex] / (float)SamplesToRead;
				}
			}
			else
			{
				return false;
			}
		}
	}
	return true;
}


#if PLATFORM_ANDROID

void USpectrumAnalyzer::HandleCapture(const uint8* WaveForm, uint32 WaveFormSize)
{
	int32 NumChannels = Sink->GetNumChannels();
	int32 SamplesPerSecond = Sink->GetSamplesPerSecond();
	ResampleBuffer.Reset(WaveFormSize*NumChannels);
	for (uint32 i = 0; i < WaveFormSize; i++)
	{
		for (int32 j = 0; j < NumChannels; j++)
		{
			ResampleBuffer.Add((int16)(WaveForm[i] - 0x80) << 8);
		}
	}
	FTimespan Duration = FTimespan::FromSeconds(WaveFormSize / (double)SamplesPerSecond);
	Sink->PlayAudioSink((const uint8*)ResampleBuffer.GetData(), ResampleBuffer.Num(), PlaybackTime + Duration);

}

extern "C"
{

	JNIEXPORT void JNICALL
		Java_com_soundVisualizationsNonEngine_SoundVisualizer_nativeSendWaveForm(JNIEnv* Env, jclass clazz, jlong callbackObject, jbyteArray bytes, jint sampleRate)
	{
		//UE_LOG(LogSpectrumAnalyzer, Log, TEXT("Env %p, clazz %p, callbackObject %p, bytes %p, sampleRate %d "), Env, clazz, callbackObject, bytes, sampleRate);

		jint length = Env->GetArrayLength(bytes);
		jboolean bIsCopy;
		jbyte* rawBytes = Env->GetByteArrayElements(bytes, &bIsCopy);
		if (rawBytes != NULL)
		{
			((USpectrumAnalyzer*)callbackObject)->HandleCapture((const uint8*)rawBytes, length);
		}
		Env->ReleaseByteArrayElements(bytes, rawBytes, JNI_ABORT);

	}
}

#endif