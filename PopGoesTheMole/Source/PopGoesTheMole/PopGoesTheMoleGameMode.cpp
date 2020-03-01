// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PopGoesTheMoleGameMode.h"
#include "PopGoesTheMoleCharacter.h"
#include "UObject/ConstructorHelpers.h"

APopGoesTheMoleGameMode::APopGoesTheMoleGameMode()
{
	// set default pawn class to our Blueprinted character
	//static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("Blueprint'/Game/SideScrollerBP/Blueprints/SideScrollerCharacter.SideScrollerCharacter'"));
	//if (PlayerPawnBPClass.Class != NULL)
	//{
	//	DefaultPawnClass = PlayerPawnBPClass.Class;
	//}
}
