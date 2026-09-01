// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "HAL/Platform.h" /* For int32 */
#include "Engine/DeveloperSettings.h"
#include "RageHostMigrationSettings.generated.h"

/* All settings used by Host Migration. Tuned by default for a 7s new host boot. */
UCLASS(config = Game, DefaultConfig, meta=(DisplayName="Rage - Host Migration Settings"))
class RAGEHOSTMIGRATION_API URageHostMigrationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static URageHostMigrationSettings* Get();

	/* Master switch to use Host Migration. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	bool bEnableHostMigration = true;

	/* Attempts spent travelling to the promoted candidate before giving up and re-electing. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	int32 MaxReconnectAttemptsPerCandidate = 5;

	/* Attempts spent retrying the original host before considering him gone. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	int32 MaxOriginalHostReconnectAttempts = 0;

	/* How often the retry poll looks at an attempt. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	float ReconnectRetryInterval = 3.f;

	/* How long one connection attempt may stay in flight before it is written off. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	float MaxConnectionAttemptSeconds = 12.f;

	/* Grace period for our own listen server to come up after BecomeNewHost. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	float BecomeHostTimeoutSeconds = 45.f;

	/* Hard ceiling on the whole migration. A backstop against a wedged state machine. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Rage|HostMigration")
	float MaxTotalMigrationSeconds = 90.f;
};
