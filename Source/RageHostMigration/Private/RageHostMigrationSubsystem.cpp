// Copyright (c) 2026 Abdallah Boutrif

#include "RageHostMigrationSubsystem.h"

#include "RageHostMigrationBridge.h"
#include "RageHostMigrationElection.h"
#include "RageHostMigrationLog.h"
#include "RageHostMigrationPolicy.h"
#include "RageHostMigrationSettings.h"
#include "RageHostMigrationStatics.h"
#include "RageMigrationCandidate.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#if WITH_COMMON_LOADING_SCREEN
#include "LoadingScreenManager.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RageHostMigrationSubsystem)

namespace
{
	FString CandidateListToStr(const TArray<FRageMigrationCandidate>& Candidates)
	{
		FString Result;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Result += FString::Printf(TEXT("%s[%d]=%s@%s"),
				Index == 0 ? TEXT("") : TEXT(", "),
				Index,
				*Candidates[Index].UniqueId.ToString(),
				*Candidates[Index].ConnectAddress);
		}
		return Result.IsEmpty() ? TEXT("<empty>") : Result;
	}

	/** ConnectAddress is replicated from the GameState, which means it is authored by whoever is currently hosting, 
	 * and in P2P that is another player, not a trusted server. Hence we should expect people to be naughty.
	 * So we use a permissive whitelist rather than a hunt for bad characters, because it has to
	 * pass every shape BuildTravelAddress legitimately handles: a bare SteamID64, an already-prefixed
	 * "steam.<id>", and the IPv4/IPv6/hostname forms PIE produces under IpNetDriver. What it rejects is
	 * anything that could smuggle a second console token or a URL option. */
	bool IsSafeTravelHost(const FString& ConnectAddress)
	{
		constexpr int32 MaxTravelHostLength = 128;

		if (ConnectAddress.IsEmpty() or ConnectAddress.Len() > MaxTravelHostLength)
		{
			return false;
		}

		for (const TCHAR Char : ConnectAddress)
		{
			const bool bIsAllowed =
				FChar::IsAlnum(Char) or
				Char == TEXT('.') or
				Char == TEXT(':') or
				Char == TEXT('-') or
				Char == TEXT('_') or
				Char == TEXT('[') or
				Char == TEXT(']');

			if (!bIsAllowed)
			{
				return false;
			}
		}

		return true;
	}
}

IRageHostMigrationBridge* URageHostMigrationSubsystem::GetBridge() const
{
	return Cast<IRageHostMigrationBridge>(GetGameInstance());
}

URageHostMigrationSubsystem* URageHostMigrationSubsystem::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	const UGameInstance* GameInstance = World->GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<URageHostMigrationSubsystem>();
}

void URageHostMigrationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
 
	if (URageHostMigrationSettings::Get()->bEnableHostMigration and IsValid(GEngine))
	{
		GEngine->OnNetworkFailure().AddUObject(this, &URageHostMigrationSubsystem::HandleNetworkFailure);
	}
	
#if WITH_COMMON_LOADING_SCREEN
	if (ULoadingScreenManager* LoadingScreenManager = Collection.InitializeDependency<ULoadingScreenManager>())
	{
		LoadingScreenManager->RegisterLoadingProcessor(this);
	}
#endif
}

void URageHostMigrationSubsystem::Deinitialize()
{
	if (IsValid(GEngine))
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
	}

#if WITH_COMMON_LOADING_SCREEN
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (ULoadingScreenManager* LoadingScreenManager = GameInstance->GetSubsystem<ULoadingScreenManager>())
		{
			LoadingScreenManager->UnregisterLoadingProcessor(this);
		}
	}
#endif

	ClearRetryTimer();
	ClearBecomeHostSafetyNet();
	ClearMigrationWatchdog();

	Super::Deinitialize();
}

#if WITH_COMMON_LOADING_SCREEN
bool URageHostMigrationSubsystem::ShouldShowLoadingScreen(FString& OutReason) const
{
	if (CurrentState == ERageHostMigrationState::Idle)
	{
		return false;
	}

	OutReason = TEXT("Host migration in progress");
	return true;
}
#endif

void URageHostMigrationSubsystem::RegisterLocalPlayer(const FUniqueNetIdRepl& InLocalUniqueId)
{
	if (InLocalUniqueId.IsValid())
	{
		LocalUniqueId = InLocalUniqueId;
	}

	NotifyLocalPlayerJoined();
}

void URageHostMigrationSubsystem::SetOriginalHostAddress(const FString& InHostAddress)
{
	OriginalHostAddress = InHostAddress;
}

void URageHostMigrationSubsystem::CacheMigrationCandidates(const TArray<FRageMigrationCandidate>& InCandidates)
{
	for (const FRageMigrationCandidate& OldCandidate : CachedMigrationCandidates)
	{
		const bool bStillPresent = InCandidates.ContainsByPredicate([&OldCandidate](const FRageMigrationCandidate& Candidate)
		{
			return Candidate.UniqueId == OldCandidate.UniqueId;
		});

		if (!bStillPresent and CachedExtraPlayerStats.Remove(OldCandidate.UniqueId) > 0)
		{
			RAGE_HM_LOG(Verbose, "Purged cached extra stats for {id}, which is now no longer in the migration candidate list.", OldCandidate.UniqueId.ToString());
		}
	}

	CachedMigrationCandidates = InCandidates;
}

const TArray<FRageMigrationCandidate>& URageHostMigrationSubsystem::GetCachedMigrationCandidates() const
{
	return CachedMigrationCandidates;
}

const FString& URageHostMigrationSubsystem::GetCachedMatchLevelName() const
{
	return CachedMatchLevelName;
}

void URageHostMigrationSubsystem::CacheExtraMatchStats(const FName& Tag, const TArray<uint8>& Bytes)
{
	if (!Tag.IsNone())
	{
		CachedExtraMatchStats.Add(Tag, Bytes);
	}
}
 
bool URageHostMigrationSubsystem::TryGetExtraMatchStats(const FName& Tag, TArray<uint8>& OutBytes) const
{
	if (const TArray<uint8>* Found = CachedExtraMatchStats.Find(Tag))
	{
		OutBytes = *Found;
		return true;
	}
	
	return false;
}
 
void URageHostMigrationSubsystem::CacheExtraPlayerStats(const FUniqueNetIdRepl& PlayerId, const FName& Tag, const TArray<uint8>& Bytes)
{
	if (PlayerId.IsValid() and !Tag.IsNone())
	{
		CachedExtraPlayerStats.FindOrAdd(PlayerId).Add(Tag, Bytes);
	}
}
 
bool URageHostMigrationSubsystem::TryGetExtraPlayerStats(const FUniqueNetIdRepl& PlayerId, const FName& Tag, TArray<uint8>& OutBytes) const
{
	if (const TMap<FName, TArray<uint8>>* PlayerBlobs = CachedExtraPlayerStats.Find(PlayerId))
	{
		if (const TArray<uint8>* Found = PlayerBlobs->Find(Tag))
		{
			OutBytes = *Found;
			return true;
		}
	}
	
	return false;
}

void URageHostMigrationSubsystem::ClearExtraPlayerStats(const FUniqueNetIdRepl& PlayerId, const FName& Tag)
{
	if (TMap<FName, TArray<uint8>>* PlayerBlobs = CachedExtraPlayerStats.Find(PlayerId))
	{
		PlayerBlobs->Remove(Tag);
	}
}

ERageHostMigrationState URageHostMigrationSubsystem::GetCurrentState() const
{
	return CurrentState;
}

bool URageHostMigrationSubsystem::TryClaimDisconnect(const UWorld* World)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const bool bOwningWorld = IsValid(World) and IsValid(GameInstance) and World == GameInstance->GetWorld();

	const ERageDisconnectClaim Claim = FRageHostMigrationPolicy::DecideDisconnectClaim(
		URageHostMigrationSettings::Get()->bEnableHostMigration,
		bOwningWorld,
		CurrentState,
		bOwningWorld ? World->GetNetMode() : NM_Standalone,
		CachedMigrationCandidates.Num());

	switch (Claim)
	{
		case ERageDisconnectClaim::BeginMigration:
			RAGE_HM_LOG(Warning, "Host connection lost, claiming the disconnect and starting host migration.");
			BeginMigration();
		break;

		case ERageDisconnectClaim::SwallowDuringMigration:
			RAGE_HM_LOG(Verbose, "Disconnect while already migrating (state={state}), claimed with no action taken.", UEnum::GetValueAsString(CurrentState));
		break;
	
		case ERageDisconnectClaim::DeclineNoCandidates:
			RAGE_HM_LOG(Warning, "Host connection lost but no migration candidate remains ({candidates}), letting the engine RTM.", CandidateListToStr(CachedMigrationCandidates));
		break;
	
		default:
		break;
	}
	
	if (FRageHostMigrationPolicy::ShouldReportMigrationFailed(Claim))
	{
		if (IRageHostMigrationBridge* Bridge = GetBridge())
		{
			Bridge->OnHostMigrationUnavailable();
		}
	}

	return FRageHostMigrationPolicy::ClaimsDisconnect(Claim);
}

void URageHostMigrationSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	const bool bConnectionLoss =
		FailureType == ENetworkFailure::ConnectionLost or
		FailureType == ENetworkFailure::ConnectionTimeout or
		FailureType == ENetworkFailure::FailureReceived;

	if (!bConnectionLoss)
	{
		return;
	}
	
	RAGE_HM_LOG(Verbose, "Network failure (code {code}): {error}.", FailureType, ErrorString);
	TryClaimDisconnect(World);
}
 
void URageHostMigrationSubsystem::BeginMigration()
{
	CurrentState = ERageHostMigrationState::ReconnectingToOriginalHost;
	AttemptsForCurrentCandidate = 0;
	CurrentAttemptStartTimeSeconds = 0.0;
	MigrationStartTimeSeconds = FPlatformTime::Seconds();

	ArmMigrationWatchdog();
	
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UWorld* World = GameInstance->GetWorld())
		{
			CachedMatchLevelName = UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString=*/true);
		}
	}
	
	RAGE_HM_LOG(Warning, "Host migration beginning. Cached level '{level}', candidates: {candidates}", CachedMatchLevelName, CandidateListToStr(CachedMigrationCandidates));

	if (IRageHostMigrationBridge* Bridge = GetBridge())
	{
		Bridge->OnHostMigrationBegan(CachedMatchLevelName);
	}

	HostMigrationStartedDelegate.Broadcast();
	AttemptReconnectToOriginalHost();
}
 
void URageHostMigrationSubsystem::AttemptReconnectToOriginalHost()
{
	const int32 MaxOriginalHostAttempts = URageHostMigrationSettings::Get()->MaxOriginalHostReconnectAttempts;

	if (MaxOriginalHostAttempts <= 0)
	{
		RAGE_HM_LOG(Log, "Original-host reconnect disabled (MaxOriginalHostReconnectAttempts=0), going straight to election.");
		CurrentState = ERageHostMigrationState::Electing;
		ElectAndProceed();
		return;
	}

	if (OriginalHostAddress.IsEmpty())
	{
		RAGE_HM_LOG(Warning, "No original host address cached, skipping straight to election.");
		CurrentState = ERageHostMigrationState::Electing;
		ElectAndProceed();
		return;
	}

	++AttemptsForCurrentCandidate;
	
	const FString Address = BuildTravelAddress(OriginalHostAddress);
	RAGE_HM_LOG(Log, "Reconnect attempt {curr}/{max} to original host {host}", AttemptsForCurrentCandidate, MaxOriginalHostAttempts, Address);

	ExecuteOpenCommand(Address);
	ArmRetryTimer();
}

void URageHostMigrationSubsystem::ElectAndProceed()
{
	const FRageMigrationElectionResult Election = FRageHostMigrationElection::Elect(CachedMigrationCandidates, LocalUniqueId);

	if (Election.NumDroppedUnusable > 0)
	{
		RAGE_HM_LOG(Warning, "Dropped {num} candidate(s) with no valid unique id before electing.", Election.NumDroppedUnusable);
	}

	if (!Election.Succeeded())
	{
		if (Election.Outcome == ERageMigrationElectionOutcome::NoCandidatesRemain)
		{
			RAGE_HM_LOG(Error, "No remaining migration candidates: {candidates}", CandidateListToStr(CachedMigrationCandidates));
		}
		else
		{
			RAGE_HM_LOG(Error, "No usable migration candidates remain after filtering.");
		}

		FailMigration();
		return;
	}

	CachedMigrationCandidates = Election.RemainingCandidates;

	AttemptsForCurrentCandidate = 0;
	CurrentAttemptStartTimeSeconds = 0.0;
	
	RAGE_HM_LOG(Warning, "Election result: promoted {id}@{address}. Remaining order: {candidates}",
		Election.Promoted.UniqueId.ToString(), Election.Promoted.ConnectAddress, CandidateListToStr(CachedMigrationCandidates));

	if (!LocalUniqueId.IsValid())
	{
		RAGE_HM_LOG(Warning, "No local unique id registered, so this machine cannot win an election and will follow the promoted candidate. "
			"If this is unexpected, ARagePlayerController::TryRegisterForHostMigration never landed.");
	}

	if (Election.Outcome == ERageMigrationElectionOutcome::LocalBecomesHost)
	{
		RAGE_HM_LOG(Log, "Locally elected as new host.");
		CurrentState = ERageHostMigrationState::BecomingHost;
		BecomeNewHost();
	}
	else
	{
		RAGE_HM_LOG(Log, "Waiting for promoted host to come online.");
		CurrentState = ERageHostMigrationState::TravelingToCandidate;
		TravelToCandidate(Election.Promoted);
	}
}
 
void URageHostMigrationSubsystem::BecomeNewHost()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		RAGE_HM_LOG(Error, "No valid world to open a listen server from.");
		RAGE_HM_DUMP_STACK_TRACE("URageHostMigrationSubsystem::BecomeNewHost had no valid world. This should be structurally impossible while a migration is in flight.", ELogVerbosity::Error);
		AdvanceToNextCandidateOrGiveUp();
		return;
	}

	const bool bUsedCachedLevelName = !CachedMatchLevelName.IsEmpty();
	const FString LevelName = bUsedCachedLevelName ? CachedMatchLevelName : UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString=*/true);

	const FString TravelOptions = FString::Printf(TEXT("listen?%s=1"), *FRageHostMigrationStatics::MigratedKey);
	
	RAGE_HM_LOG(Log, "Opening {level} as the new listen server (used {source} level name) with options '{options}'.",
		LevelName, bUsedCachedLevelName ? TEXT("cached") : TEXT("freshly-queried"), TravelOptions);

	UGameplayStatics::OpenLevel(World, FName(*LevelName), /*bAbsolute=*/true, TravelOptions);
	
	ClearBecomeHostSafetyNet();

	const float SafetyNetSeconds = URageHostMigrationSettings::Get()->BecomeHostTimeoutSeconds;
	BecomeHostSafetyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &URageHostMigrationSubsystem::TickBecomeHostSafetyNet), SafetyNetSeconds);
}

bool URageHostMigrationSubsystem::TickBecomeHostSafetyNet(float DeltaTime)
{
	BecomeHostSafetyTickerHandle.Reset();
	
	if (HasArrivedInNetworkedGame())
	{
		RAGE_HM_LOG(Warning, "Become-host safety net fired, but we are already in a working networked game, so treating it as success. The arrival signal never landed; see HasArrivedInNetworkedGame.");
		NotifyLocalPlayerJoined();
		return false;
	}
	
	if (CachedMigrationCandidates.Num() < 2)
	{
		RAGE_HM_LOG(Error, "Become-host safety net fired with no other candidate to hand off to (state={state}), staying put rather than failing a migration that may still complete.", UEnum::GetValueAsString(CurrentState));
		return false;
	}

	RAGE_HM_LOG(Warning, "Listen server did not come up in time after becoming host (state={state}), handing off to the next candidate.", UEnum::GetValueAsString(CurrentState));
	AdvanceToNextCandidateOrGiveUp();
	return false; /* one-shot */
}

void URageHostMigrationSubsystem::ClearBecomeHostSafetyNet()
{
	if (BecomeHostSafetyTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(BecomeHostSafetyTickerHandle);
		BecomeHostSafetyTickerHandle.Reset();
	}
}
 
void URageHostMigrationSubsystem::TravelToCandidate(const FRageMigrationCandidate& Candidate)
{
	++AttemptsForCurrentCandidate;
	const FString Address = BuildTravelAddress(Candidate.ConnectAddress);
 
	if (Address.IsEmpty())
	{
		RAGE_HM_LOG(Warning, "Promoted candidate has no usable address, waiting briefly.");
	}
	else
	{
		RAGE_HM_LOG(Log, "Traveling to promoted host at {address} (try {curr}/{max}).", Address, AttemptsForCurrentCandidate, URageHostMigrationSettings::Get()->MaxReconnectAttemptsPerCandidate);
		ExecuteOpenCommand(Address);
	}

	ArmRetryTimer();
}

void URageHostMigrationSubsystem::OnAttemptFailed()
{
	ClearRetryTimer();
	
	if (IsConnectionAttemptInFlight())
	{
		const double InFlightSeconds = FPlatformTime::Seconds() - CurrentAttemptStartTimeSeconds;
		const float MaxAttemptSeconds = URageHostMigrationSettings::Get()->MaxConnectionAttemptSeconds;

		if (InFlightSeconds < MaxAttemptSeconds)
		{
			RAGE_HM_LOG(Verbose, "Connection attempt still in flight after {elapsed}s of {max}s, leaving it to finish rather than restarting it.", InFlightSeconds, MaxAttemptSeconds);
			ArmRetryTimer();
			return;
		}

		RAGE_HM_LOG(Warning, "Connection attempt still unresolved after {elapsed}s (limit {max}s), writing it off.", InFlightSeconds, MaxAttemptSeconds);
	}
	
	if (HasArrivedInNetworkedGame())
	{
		RAGE_HM_LOG(Warning, "Retry poll found us already in a working networked game (state={state}), completing the migration here. "
			"The normal arrival signal never landed; see HasArrivedInNetworkedGame.", UEnum::GetValueAsString(CurrentState));
		NotifyLocalPlayerJoined();
		return;
	}

	switch (CurrentState)
	{
		case ERageHostMigrationState::ReconnectingToOriginalHost:
			if (AttemptsForCurrentCandidate < URageHostMigrationSettings::Get()->MaxOriginalHostReconnectAttempts)
			{
				AttemptReconnectToOriginalHost();
			}
			else
			{
				RAGE_HM_LOG(Log, "Original host unreachable after {num} attempts. Starting election.", AttemptsForCurrentCandidate);
				CurrentState = ERageHostMigrationState::Electing;
				ElectAndProceed();
			}
		break;
 
		case ERageHostMigrationState::TravelingToCandidate:
			if (AttemptsForCurrentCandidate < URageHostMigrationSettings::Get()->MaxReconnectAttemptsPerCandidate and CachedMigrationCandidates.IsValidIndex(0))
			{
				TravelToCandidate(CachedMigrationCandidates[0]);
			}
			else
			{
				RAGE_HM_LOG(Log, "Promoted candidate {id}@{address} unreachable after {num} attempts. Advancing.",
					CachedMigrationCandidates.IsValidIndex(0) ? CachedMigrationCandidates[0].UniqueId.ToString() : TEXT("<none>"),
					CachedMigrationCandidates.IsValidIndex(0) ? CachedMigrationCandidates[0].ConnectAddress : FString(),
					AttemptsForCurrentCandidate);
				AdvanceToNextCandidateOrGiveUp();
			}
		break;

		case ERageHostMigrationState::BecomingHost:
			RAGE_HM_LOG(Warning, "OnAttemptFailed reached in BecomingHost outside the usual safety-net path, advancing anyway.");
			AdvanceToNextCandidateOrGiveUp();
		break;

		default:
		break;
	}
}

void URageHostMigrationSubsystem::AdvanceToNextCandidateOrGiveUp()
{
	RAGE_HM_LOG(Log, "Advancing past the current candidate; re-running the election over: {candidates}", CandidateListToStr(CachedMigrationCandidates));
	CurrentState = ERageHostMigrationState::Electing;
	ElectAndProceed();
}

void URageHostMigrationSubsystem::FailMigration()
{
	const double ElapsedSeconds = FPlatformTime::Seconds() - MigrationStartTimeSeconds;
	RAGE_HM_LOG(Error, "Host migration failed after {elapsed}s. No reachable candidate remained. Final list: {candidates}",
		ElapsedSeconds, CandidateListToStr(CachedMigrationCandidates));

	RAGE_HM_DUMP_STACK_TRACE("URageHostMigrationSubsystem::FailMigration", ELogVerbosity::Error);

	CurrentState = ERageHostMigrationState::Idle;
	ClearRetryTimer();
	ClearBecomeHostSafetyNet();
	ClearMigrationWatchdog();
	
	if (IRageHostMigrationBridge* Bridge = GetBridge())
	{
		Bridge->OnHostMigrationUnavailable();
	}

	HostMigrationFailedDelegate.Broadcast();
}
 
void URageHostMigrationSubsystem::NotifyLocalPlayerJoined()
{
	const bool bWasMigrating = CurrentState != ERageHostMigrationState::Idle;
	const ERageHostMigrationState StateBeforeArrival = CurrentState;

	ClearRetryTimer();
	ClearBecomeHostSafetyNet();
	ClearMigrationWatchdog();

	CurrentState = ERageHostMigrationState::Idle;
	AttemptsForCurrentCandidate = 0;
	CurrentAttemptStartTimeSeconds = 0.0;
	CachedMatchLevelName.Empty();

	if (bWasMigrating)
	{
		const double ElapsedSeconds = FPlatformTime::Seconds() - MigrationStartTimeSeconds;
		RAGE_HM_LOG(Log, "Host migration complete in {elapsed}s (last state before arrival: {state}).", ElapsedSeconds, UEnum::GetValueAsString(StateBeforeArrival));

		SettleSessionAfterMigration(StateBeforeArrival);

		HostMigrationSucceededDelegate.Broadcast();
	}
}

void URageHostMigrationSubsystem::SettleSessionAfterMigration(const ERageHostMigrationState StateBeforeArrival)
{
	const ERagePostMigrationSessionAction Action = FRageHostMigrationPolicy::DecidePostMigrationSession(StateBeforeArrival);

	RAGE_HM_LOG(Log, "Post-migration session action: {action}.", FRageHostMigrationPolicy::ToString(Action));

	if (IRageHostMigrationBridge* Bridge = GetBridge())
	{
		Bridge->OnSettleSessionAfterMigration(Action);
	}
}

void URageHostMigrationSubsystem::ExecuteOpenCommand(const FString& Address)
{
	if (Address.IsEmpty())
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		RAGE_HM_LOG(Error, "No valid world to travel from.");
		RAGE_HM_DUMP_STACK_TRACE("URageHostMigrationSubsystem::ExecuteOpenCommand had no valid world.", ELogVerbosity::Error);
		return;
	}
	
	const FString Command = FString::Printf(TEXT("open %s"), *Address);

	CurrentAttemptStartTimeSeconds = FPlatformTime::Seconds();

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
	{
		PlayerController->ConsoleCommand(Command, /*bWriteToLog=*/true);
	}
	else if (IsValid(GEngine))
	{
		GEngine->Exec(World, *Command);
	}
}
 
FString URageHostMigrationSubsystem::BuildTravelAddress(const FString& ConnectAddress) const
{
	if (ConnectAddress.IsEmpty())
	{
		return ConnectAddress;
	}
	
	if (!IsSafeTravelHost(ConnectAddress))
	{
		RAGE_HM_LOG(Error, "Refusing to travel to candidate address '{address}' - not a well-formed host. Skipping this candidate.", ConnectAddress.Left(64));
		return FString();
	}

	if (ConnectAddress.StartsWith(TEXT("steam.")))
	{
		return ConnectAddress;
	}
	
	bool bIsAllDigits = true;
	for (const TCHAR Char : ConnectAddress)
	{
		if (!FChar::IsDigit(Char))
		{
			bIsAllDigits = false;
			break;
		}
	}

	if (bIsAllDigits)
	{
		return FString::Printf(TEXT("steam.%s"), *ConnectAddress);
	}
	
	return ConnectAddress;
}

bool URageHostMigrationSubsystem::IsConnectionAttemptInFlight() const
{
	if (!IsValid(GEngine))
	{
		return false;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		return false;
	}

	const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World);
	if (!WorldContext)
	{
		return false;
	}
	
	return !WorldContext->TravelURL.IsEmpty() or WorldContext->PendingNetGame != nullptr;
}

bool URageHostMigrationSubsystem::HasArrivedInNetworkedGame() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		return false;
	}

	if (World->GetNetMode() == NM_ListenServer)
	{
		return true;
	}
	
	if (World->GetNetMode() == NM_Client)
	{
		const UNetDriver* NetDriver = World->GetNetDriver();
		return IsValid(NetDriver) and NetDriver->ServerConnection != nullptr and NetDriver->ServerConnection->GetConnectionState() == USOCK_Open;
	}

	return false;
}

FTimerManager* URageHostMigrationSubsystem::GetMigrationTimerManager() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return IsValid(GameInstance) ? &GameInstance->GetTimerManager() : nullptr;
}

void URageHostMigrationSubsystem::ArmRetryTimer()
{
	FTimerManager* TimerManager = GetMigrationTimerManager();
	if (!TimerManager)
	{
		RAGE_HM_LOG(Error, "No timer manager to schedule the migration retry on, so failing the migration rather than stalling on the loading screen.");
		FailMigration();
		return;
	}

	TimerManager->SetTimer(RetryTimerHandle, this, &URageHostMigrationSubsystem::OnAttemptFailed,URageHostMigrationSettings::Get()->ReconnectRetryInterval, false);
}

void URageHostMigrationSubsystem::ClearRetryTimer()
{
	if (FTimerManager* TimerManager = GetMigrationTimerManager())
	{
		TimerManager->ClearTimer(RetryTimerHandle);
	}
}

bool URageHostMigrationSubsystem::TickMigrationWatchdog(float DeltaTime)
{
	MigrationWatchdogTickerHandle.Reset();

	const double ElapsedSeconds = FPlatformTime::Seconds() - MigrationStartTimeSeconds;
	
	if (HasArrivedInNetworkedGame())
	{
		RAGE_HM_LOG(Warning, "Migration watchdog hit its {max}s ceiling after {elapsed}s, but we are in a working networked game, so completing rather than ejecting to the menu.",
			URageHostMigrationSettings::Get()->MaxTotalMigrationSeconds, ElapsedSeconds);
		NotifyLocalPlayerJoined();
		return false;
	}

	RAGE_HM_LOG(Error, "Host migration exceeded its {max}s ceiling ({elapsed}s elapsed, state={state}), giving up. "
		"Reaching this means a step never handed control back; the state above is where it wedged.",
		URageHostMigrationSettings::Get()->MaxTotalMigrationSeconds, ElapsedSeconds, UEnum::GetValueAsString(CurrentState));

	FailMigration();
	return false; /* one-shot */
}

void URageHostMigrationSubsystem::ArmMigrationWatchdog()
{
	ClearMigrationWatchdog();

	MigrationWatchdogTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &URageHostMigrationSubsystem::TickMigrationWatchdog),
		URageHostMigrationSettings::Get()->MaxTotalMigrationSeconds);
}

void URageHostMigrationSubsystem::ClearMigrationWatchdog()
{
	if (MigrationWatchdogTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(MigrationWatchdogTickerHandle);
		MigrationWatchdogTickerHandle.Reset();
	}
}
