// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SoundVisualizationsNonEnginePrivatePCH.h"
#include "SoundDefinitions.h"
#include "kiss_fft.h" // Kiss FFT for Real component...

/////////////////////////////////////////////////////
// USoundVisualizationStatics

DEFINE_LOG_CATEGORY_STATIC(LogSoundVisualization, Log, All);

USoundVisualizationsNonEngineStatics::USoundVisualizationsNonEngineStatics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}
