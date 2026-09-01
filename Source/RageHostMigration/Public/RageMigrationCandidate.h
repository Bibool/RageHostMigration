// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Containers/UnrealString.h" /* For FString */
#include "GameFramework/OnlineReplStructs.h" /* For FUniqueNetIdRepl */
#include "RageMigrationCandidate.generated.h"

/* Represents a machine that could take over hosting. Order matters, 0 is the host, 1 is the successor. */
USTRUCT(BlueprintType)
struct FRageMigrationCandidate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rage|HostMigration")
	FUniqueNetIdRepl UniqueId = FUniqueNetIdRepl();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rage|HostMigration")
	FString ConnectAddress = FString();
 
	bool operator==(const FRageMigrationCandidate& Other) const
	{
		return UniqueId == Other.UniqueId;
	}
};
