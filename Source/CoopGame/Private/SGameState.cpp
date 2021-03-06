// Fill out your copyright notice in the Description page of Project Settings.

#include "SGameState.h"
#include "Net/UnrealNetwork.h"

void ASGameState::SetWaveState(EWaveState NewState)
{
	if (this->GetLocalRole() == ROLE_Authority)
	{
		EWaveState OldState = WaveState;

		// This only call "OnRep" funcion on clients
		WaveState = NewState;

		// Call on server
		OnRep_WaveState(OldState);
	}
}

void ASGameState::OnRep_WaveState(EWaveState OldState)
{
	WaveStateChanged(WaveState, OldState);
}

void ASGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASGameState, WaveState);
}