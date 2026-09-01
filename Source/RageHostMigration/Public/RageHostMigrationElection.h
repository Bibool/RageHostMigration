// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Containers/Array.h" /* For TArray */
#include "HAL/Platform.h" /* For uint8, int32 */
#include "RageMigrationCandidate.h" /* For FRageMigrationCandidate, FUniqueNetIdRepl */
#include "GameFramework/OnlineReplStructs.h"

/* What one election pass decided. */
enum class ERageMigrationElectionOutcome : uint8
{
	/* Fewer than two entries went in, so there is nobody left to hand off to. */
	NoCandidatesRemain,

	/* Everything behind the machine we just tried had an unusable id and got filtered out. */
	NoUsableCandidatesRemain,

	/* The promoted candidate is us, so open a listen server. */
	LocalBecomesHost,

	/* The promoted candidate is somebody else, so travel to it. */
	FollowPromoted,
};

/* The outcome of FRageHostMigrationElection::Elect. */
struct FRageMigrationElectionResult
{
	ERageMigrationElectionOutcome Outcome = ERageMigrationElectionOutcome::NoCandidatesRemain;

	/* The new list with the promoted candidate already at index 0, ready to be stored back verbatim. */
	TArray<FRageMigrationCandidate> RemainingCandidates;

	/* Cached RemainingCandidates[0] so callers do not have to index it again. */
	FRageMigrationCandidate Promoted;

	/* How many entries were dropped for having no valid unique id, so the caller can log it. */
	int32 NumDroppedUnusable = 0;

	bool Succeeded() const
	{
		return Outcome == ERageMigrationElectionOutcome::LocalBecomesHost or Outcome == ERageMigrationElectionOutcome::FollowPromoted;
	}
};

/* The ordering half of URageHostMigrationSubsystem::ElectAndProceed */
struct RAGEHOSTMIGRATION_API FRageHostMigrationElection final
{
	/** @param Candidates    The cached list as replicated. Index 0 is whoever we most recently tried.
	 * @param LocalUniqueId This machine's registered id. Default ctor equals to "never registered",
	 * which can never win. */
	static FRageMigrationElectionResult Elect(
		const TArray<FRageMigrationCandidate>& Candidates,
		const FUniqueNetIdRepl& LocalUniqueId);
};
