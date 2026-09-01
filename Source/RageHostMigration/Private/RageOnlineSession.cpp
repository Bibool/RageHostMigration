// Copyright (c) 2026 Abdallah Boutrif

#include "RageOnlineSession.h"

#include "RageHostMigrationBridge.h"
#include "RageHostMigrationLog.h"
#include "RageHostMigrationSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RageOnlineSession)

/** Every path gets routed here by the engine for handling players inside a lost connection. Notably;
 * HandleNetworkFailure, HandleTravelFailure via CallHandleDisconnectForFailure, UGameInstance::ReturnToMainMenu, and ClientReturnToMainMenuWithTextReason.
 * The Super calls GEngine->HandleDisconnect which causes the players to get sent to GameDefaultMap.
 * This is the main reason Super should not be called unless for a catastrophic failure.
 * Skipping Super allows us to have control over the player's travel destination. */
void URageOnlineSession::HandleDisconnect(UWorld* World, UNetDriver* NetDriver)
{
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : Cast<UGameInstance>(GetOuter());
	if (!IsValid(GameInstance))
	{
		Super::HandleDisconnect(World, NetDriver);
		return;
	}

	URageHostMigrationSubsystem* HostMigrationSubsystem = GameInstance->GetSubsystem<URageHostMigrationSubsystem>();
	if (IsValid(HostMigrationSubsystem) && HostMigrationSubsystem->TryClaimDisconnect(World))
	{
		RAGE_HM_LOG(Log, "Disconnect claimed by host migration, suppressing the engine's travel.");
		return;
	}

	/* Unclaimed, cache the reason and allow the engine to take over. */
	if (IRageHostMigrationBridge* Bridge = Cast<IRageHostMigrationBridge>(GameInstance))
	{
		Bridge->OnUnclaimedDisconnect();
	}

	Super::HandleDisconnect(World, NetDriver);
}
