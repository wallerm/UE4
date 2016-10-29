// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieScenePropertyTrack.h"
#include "KeyParams.h"
#include "MovieSceneMarginTrack.generated.h"


/**
 * Handles manipulation of FMargins in a movie scene
 */
UCLASS( MinimalAPI )
class UMovieSceneMarginTrack : public UMovieScenePropertyTrack
{
	GENERATED_BODY()

public:

	UMovieSceneMarginTrack(const FObjectInitializer& Init);
	
	// UMovieSceneTrack interface

	virtual UMovieSceneSection* CreateNewSection() override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
};
