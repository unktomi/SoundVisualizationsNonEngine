#include "SoundVisualizationsNonEnginePrivatePCH.h"
#include "SpectrumAnalyzer.h"
#include "IMediaPlayer.h"
#include "IMediaAudioTrack.h"
#include "kiss_fft.h"
DEFINE_LOG_CATEGORY_STATIC(LogSpectrumAnalyzer, Log, All);

USpectrumAnalyzer::USpectrumAnalyzer(const class FObjectInitializer& PCIP)
	: Super(PCIP),
	Sink(new SinkDelegate(this)),
	SampleIndex(0),
	CurrentTime(FTimespan(0)),
	PlaybackTime(FTimespan(0)),
	PCMData(nullptr)
{
}

USpectrumAnalyzer::~USpectrumAnalyzer()
{
	delete PCMData;
}

SinkDelegate::
SinkDelegate(USpectrumAnalyzer *Ptr) : Analyzer(Ptr) {}

void SinkDelegate::
ProcessMediaSample(const void* Buffer, uint32 BufferSize, FTimespan Duration, FTimespan Time)
{
	UObject *U = Analyzer.Get();
	if (U != nullptr) ((USpectrumAnalyzer*)U)->ProcessMediaSample(Buffer, BufferSize, Duration, Time);
}

void USpectrumAnalyzer::
ProcessMediaSample(const void* Buffer, volatile uint32 BufferSize, FTimespan Duration, FTimespan Time)
{
	FScopeLock ScopeLock(&CriticalSection);
	uint32 NumChannels = CurrentTrack->GetNumChannels();
	uint32 SamplesPerSecond =CurrentTrack->GetSamplesPerSecond();
	// buffer a few seconds
	uint32 SamplesNeeded = SamplesPerSecond*NumChannels *3;// WindowDurationInSeconds;
	uint32 PoT = 2;
	while (PoT < SamplesNeeded) PoT *= 2;
	if (PCMData == nullptr || PCMData->Capacity() < PoT)
	{
		delete PCMData;
		PCMData = new TCircularBuffer<int16>(PoT, (int16)0);
		SampleIndex = 0;
	}
	CurrentTime = Time + Duration;
	uint32 SampleCount = Duration.GetTotalSeconds() * SamplesPerSecond;
	uint32 SamplesAvailable = BufferSize / sizeof(int16);
	int16 *SampleBuffer = (int16*)(Buffer);
	uint32 Start = SampleIndex;
	for (uint32 i = 0; i < SamplesAvailable; i++)
	{
		(*PCMData)[SampleIndex] = SampleBuffer[i];
		SampleIndex = PCMData->GetNextIndex(SampleIndex);
	}
	uint32 End = SampleIndex;
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
			//UE_LOG(LogSoundVisualization, Warning, TEXT("Requested channel %d, sound only has %d channels"), SoundWave->NumChannels);
		}
	}
}

bool USpectrumAnalyzer::
DoCalculateFrequencySpectrum(bool bSplitChannels, TArray<TArray<float> > &OutSpectrums)
{

	if (MediaPlayer != nullptr)
	{
		TSharedPtr<IMediaPlayer> Player = MediaPlayer->GetPlayer();
		if (Player.IsValid())
		{
			IMediaAudioTrackPtr Track = Player->GetAudioTracks().Num() == 0 ? IMediaAudioTrackPtr(nullptr) : Player->GetAudioTracks()[0];
			if (CurrentTrack != Track)
			{
				if (CurrentTrack.IsValid())
				{
					CurrentTrack->GetStream().RemoveSink(Sink);
				}
				CurrentTrack = Track;
				if (CurrentTrack.IsValid())
				{
					CurrentTrack->GetStream().AddSink(Sink);
				}
			}
		}
	}
	if (!CurrentTrack.IsValid())
	{
		return false;
	}
	if (MediaPlayer->IsPaused())
	{
		return false;
	}
	FScopeLock ScopeLock(&CriticalSection);
	if (PCMData == nullptr) return false;
	
	
	uint32 NumChannels = CurrentTrack->GetNumChannels();
	uint32 SamplesPerSecond = CurrentTrack->GetSamplesPerSecond();
	PlaybackTime = MediaPlayer->GetPlayer()->GetTime();
	//UE_LOG(LogSpectrumAnalyzer, Warning, TEXT("PlaybackTime %f, CurrentTime %f"), PlaybackTime.GetTotalSeconds(), CurrentTime.GetTotalSeconds());
	FTimespan Delta = CurrentTime - PlaybackTime;
	int32 DeltaSamples = Delta.GetTotalSeconds() * SamplesPerSecond * NumChannels;
	uint32 SamplesNeeded = SamplesPerSecond * NumChannels * WindowDurationInSeconds;
	if (SampleIndex < SamplesNeeded)
	{
		//return false;
	}
	int32 SampleCount = SamplesNeeded;
	int32 LastSample = SampleIndex-DeltaSamples-1;
	int32 FirstSample = LastSample - SampleCount;
	//UE_LOG(LogSpectrumAnalyzer, Warning, TEXT("Samples %d .. %d"), FirstSample, LastSample);

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
			//UE_LOG(LogSoundVisualization, Warning, TEXT("Requested channel %d, sound only has %d channels"), SoundWave->NumChannels);
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
			IMediaAudioTrackPtr Track = Player->GetAudioTracks().Num() == 0 ? IMediaAudioTrackPtr(nullptr) : Player->GetAudioTracks()[0];
			if (CurrentTrack != Track)
			{
				if (CurrentTrack.IsValid())
				{
					CurrentTrack->GetStream().RemoveSink(Sink);
				}
				CurrentTrack = Track;
				if (CurrentTrack.IsValid())
				{
					CurrentTrack->GetStream().AddSink(Sink);
				}
			}
		}
	}
	if (!CurrentTrack.IsValid())
	{
		return false;
	}
	if (MediaPlayer->IsPaused())
	{
		return false;
	}
	FScopeLock ScopeLock(&CriticalSection);
	if (PCMData == nullptr) return false;
	uint32 NumChannels = CurrentTrack->GetNumChannels();
	uint32 SamplesPerSecond = CurrentTrack->GetSamplesPerSecond();
	uint32 SamplesNeeded = SamplesPerSecond * NumChannels * WindowDurationInSeconds;
	if (SampleIndex < SamplesNeeded)
	{
		//return;
	}
	int32 SampleCount = SamplesNeeded;
	PlaybackTime = MediaPlayer->GetPlayer()->GetTime();
	
	FTimespan Delta = CurrentTime - PlaybackTime;
	int32 DeltaSamples = Delta.GetTotalSeconds() * SamplesPerSecond * NumChannels;
	int32 LastSample = SampleIndex-DeltaSamples-1;
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
				uint32 SamplesToRead = SamplesPerAmplitude + (ExcessSamples-- > 0 ? 1 : 0);
				for (uint32 SampleIndex = 0; SampleIndex < SamplesToRead; ++SampleIndex)
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