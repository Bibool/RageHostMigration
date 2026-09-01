// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Engine/TimerHandle.h" /* For FTimerHandle */
#include "HAL/Platform.h" /* For uint8, int32 */
#include "UObject/NameTypes.h" /* For FName */
#include "Containers/UnrealString.h" /* For FString */
#include "Containers/Map.h" /* For TMap */
#include "Containers/Array.h" /* For TArray */
#include "RageHostMigrationState.h"
#include "RageStatsSerialization.h"
#include "Containers/Ticker.h" /* For FTSTicker::FDelegateHandle */
#include "Subsystems/GameInstanceSubsystem.h"

#if WITH_COMMON_LOADING_SCREEN
#include "LoadingProcessInterface.h"
#endif

#include "RageHostMigrationSubsystem.generated.h"

struct FRageMigrationCandidate;
class FTimerManager;
class IRageHostMigrationBridge;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRageHostMigration);

/* Keeps the match alive when the host drops. Caches replicated state, then rehosts or follows the successor. */
UCLASS()
class RAGEHOSTMIGRATION_API URageHostMigrationSubsystem :
		public UGameInstanceSubsystem
#if WITH_COMMON_LOADING_SCREEN
		, public ILoadingProcessInterface
#endif
{
	GENERATED_BODY()

public:
	static URageHostMigrationSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

#if WITH_COMMON_LOADING_SCREEN
	virtual bool ShouldShowLoadingScreen(FString& OutReason) const override;
#endif

	/* Call on every machine once their local PlayerState exists. */
	void RegisterLocalPlayer(const FUniqueNetIdRepl& InLocalUniqueId);

	void SetOriginalHostAddress(const FString& InHostAddress);

	void CacheMigrationCandidates(const TArray<FRageMigrationCandidate>& InCandidates);
	
	const TArray<FRageMigrationCandidate>& GetCachedMigrationCandidates() const;
	const FString& GetCachedMatchLevelName() const;

	void CacheExtraMatchStats(const FName& Tag, const TArray<uint8>& Bytes);
	bool TryGetExtraMatchStats(const FName& Tag, TArray<uint8>& OutBytes) const;
	void CacheExtraPlayerStats(const FUniqueNetIdRepl& PlayerId, const FName& Tag, const TArray<uint8>& Bytes);
	bool TryGetExtraPlayerStats(const FUniqueNetIdRepl& PlayerId, const FName& Tag, TArray<uint8>& OutBytes) const;

	void ClearExtraPlayerStats(const FUniqueNetIdRepl& PlayerId, const FName& Tag);

	template <typename TStructType>
	static void CaptureExtraMatchStats(const UObject* WorldContextObject, const TStructType& Data)
	{
		if (URageHostMigrationSubsystem* Sub = Get(WorldContextObject))
		{
			TArray<uint8> Bytes;
			FRageStatsSerialization::ToBytes(Data, Bytes);
			Sub->CacheExtraMatchStats(TStructType::StaticStruct()->GetFName(), Bytes);
		}
	}

	template <typename TStructType>
	static bool TryRestoreExtraMatchStats(const UObject* WorldContextObject, TStructType& OutData)
	{
		if (const URageHostMigrationSubsystem* Sub = Get(WorldContextObject))
		{
			TArray<uint8> Bytes;
			if (Sub->TryGetExtraMatchStats(TStructType::StaticStruct()->GetFName(), Bytes))
			{
				FRageStatsSerialization::FromBytes(Bytes, OutData);
				return true;
			}
		}
		return false;
	}

	template <typename TStructType>
	static void CaptureExtraPlayerStats(const UObject* WorldContextObject, const FUniqueNetIdRepl& PlayerId, const TStructType& Data)
	{
		if (URageHostMigrationSubsystem* Sub = Get(WorldContextObject))
		{
			TArray<uint8> Bytes;
			FRageStatsSerialization::ToBytes(Data, Bytes);
			Sub->CacheExtraPlayerStats(PlayerId, TStructType::StaticStruct()->GetFName(), Bytes);
		}
	}

	template <typename TStructType>
	static bool TryRestoreExtraPlayerStats(const UObject* WorldContextObject, const FUniqueNetIdRepl& PlayerId, TStructType& OutData)
	{
		if (const URageHostMigrationSubsystem* Sub = Get(WorldContextObject))
		{
			TArray<uint8> Bytes;
			if (Sub->TryGetExtraPlayerStats(PlayerId, TStructType::StaticStruct()->GetFName(), Bytes))
			{
				FRageStatsSerialization::FromBytes(Bytes, OutData);
				return true;
			}
		}
		return false;
	}

	template <typename TStructType>
	static bool TryConsumeExtraPlayerStats(const UObject* WorldContextObject, const FUniqueNetIdRepl& PlayerId, TStructType& OutData)
	{
		if (URageHostMigrationSubsystem* Sub = Get(WorldContextObject))
		{
			TArray<uint8> Bytes;
			if (Sub->TryGetExtraPlayerStats(PlayerId, TStructType::StaticStruct()->GetFName(), Bytes))
			{
				FRageStatsSerialization::FromBytes(Bytes, OutData);
				Sub->ClearExtraPlayerStats(PlayerId, TStructType::StaticStruct()->GetFName());
				return true;
			}
		}
		return false;
	}

	ERageHostMigrationState GetCurrentState() const;
	
	bool TryClaimDisconnect(const UWorld* World);

	UPROPERTY(BlueprintAssignable, Category = "Rage|Delegates")
	FRageHostMigration HostMigrationStartedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Rage|Delegates")
	FRageHostMigration HostMigrationSucceededDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Rage|Delegates")
	FRageHostMigration HostMigrationFailedDelegate;

private:
	/* HM to Player bridge. Null is expected and fine. */
	IRageHostMigrationBridge* GetBridge() const;

	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	void BeginMigration();
	void AttemptReconnectToOriginalHost();
	void ElectAndProceed();
	void BecomeNewHost();
	void TravelToCandidate(const FRageMigrationCandidate& Candidate);
	void OnAttemptFailed();
	void AdvanceToNextCandidateOrGiveUp();
	void FailMigration();
	void NotifyLocalPlayerJoined();
	
	void SettleSessionAfterMigration(ERageHostMigrationState StateBeforeArrival);

	void ExecuteOpenCommand(const FString& Address);

	FString BuildTravelAddress(const FString& ConnectAddress) const;

	bool IsConnectionAttemptInFlight() const;

	bool HasArrivedInNetworkedGame() const;

	FTimerManager* GetMigrationTimerManager() const;
	void ArmRetryTimer();
	void ClearRetryTimer();

	bool TickBecomeHostSafetyNet(float DeltaTime);
	void ClearBecomeHostSafetyNet();

	bool TickMigrationWatchdog(float DeltaTime);
	void ArmMigrationWatchdog();
	void ClearMigrationWatchdog();

	ERageHostMigrationState CurrentState = ERageHostMigrationState::Idle;

	TArray<FRageMigrationCandidate> CachedMigrationCandidates;

	TMap<FName, TArray<uint8>> CachedExtraMatchStats;
	TMap<FUniqueNetIdRepl, TMap<FName, TArray<uint8>>> CachedExtraPlayerStats;

	FUniqueNetIdRepl LocalUniqueId;
	FString OriginalHostAddress;
	int32 AttemptsForCurrentCandidate = 0;

	FString CachedMatchLevelName;

	FTimerHandle RetryTimerHandle;
	FTSTicker::FDelegateHandle BecomeHostSafetyTickerHandle;
	FTSTicker::FDelegateHandle MigrationWatchdogTickerHandle;

	double CurrentAttemptStartTimeSeconds = 0.0;
	double MigrationStartTimeSeconds = 0.0;
};
