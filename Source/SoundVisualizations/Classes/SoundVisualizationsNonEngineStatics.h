// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SpectrumAnalyzer.h"
#include "SoundVisualizationsNonEngineStatics.generated.h"

UCLASS()
class USoundVisualizationsNonEngineStatics : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Category = "SoundVisualization")
		static USpectrumAnalyzer *CreateSpectrumAnalyzer(UMediaPlayer *Player, const float WindowDurationInSeconds, int32 SpectrumWidth, int32 AmplitudeBuckets)
	{
		USpectrumAnalyzer *Result = NewObject<USpectrumAnalyzer>();
		Result->MediaPlayer = Player;
		Result->WindowDurationInSeconds = WindowDurationInSeconds;
		Result->SpectrumWidth = SpectrumWidth;
		Result->AmplitudeBuckets = AmplitudeBuckets;
		return Result;
	}
};
