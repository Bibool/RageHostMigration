// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "GameFramework/OnlineSession.h"
#include "RageOnlineSession.generated.h"

/** Intercepts the engine's "connection lost" signal so a migration can take over instead.
 * This is the most important part of the Host Migration system, as otherwise players get sent back to the default map. */
UCLASS()
class RAGEHOSTMIGRATION_API URageOnlineSession : public UOnlineSession
{
	GENERATED_BODY()

public:
	virtual void HandleDisconnect(UWorld* World, UNetDriver* NetDriver) override;
};
