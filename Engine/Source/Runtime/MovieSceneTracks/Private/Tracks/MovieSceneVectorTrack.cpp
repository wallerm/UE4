// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTracksPrivatePCH.h"
#include "MovieSceneVectorSection.h"
#include "MovieSceneVectorTrack.h"
#include "IMovieScenePlayer.h"
#include "Evaluation/MovieScenePropertyTemplates.h"


UMovieSceneVectorTrack::UMovieSceneVectorTrack( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	NumChannelsUsed = 0;
}


UMovieSceneSection* UMovieSceneVectorTrack::CreateNewSection()
{
	UMovieSceneVectorSection* NewSection = NewObject<UMovieSceneVectorSection>(this, UMovieSceneVectorSection::StaticClass());
	NewSection->SetChannelsUsed(NumChannelsUsed);
	return NewSection;
}


FMovieSceneEvalTemplatePtr UMovieSceneVectorTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneVectorPropertySectionTemplate(*CastChecked<UMovieSceneVectorSection>(&InSection), *this);
}