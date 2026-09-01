// Copyright (c) 2026 Abdallah Boutrif

#include "RageHostMigrationElection.h"

FRageMigrationElectionResult FRageHostMigrationElection::Elect(
	const TArray<FRageMigrationCandidate>& Candidates,
	const FUniqueNetIdRepl& LocalUniqueId)
{
	FRageMigrationElectionResult Result;

	/** Nothing is really elected here despite the name. The host picked the winner while it was
	 * still alive in ARageGameState::RefreshDesignatedSuccessor and parked it at index 1 of an
	 * ordered list every survivor received identically, so all this does is read that answer.
	 *
	 * Deriving the winner here instead, after the host is gone and with no channel left to reconcile
	 * a disagreement, is what lets two clients with slightly different lists both open a listen
	 * server. Agreeing while there is still a server to agree through removes the race rather
	 * than narrowing it. Order never comes from PlayerArray either, which each client fills
	 * independently as PlayerStates begin play and is not guaranteed to match anywhere.
	 *
	 * Index 0 is always whoever we most recently tried: the departed host on the first pass, a
	 * candidate that failed to come up on later ones. Drop it, promote the new index 0, hand the
	 * result back. Index 2+ is the fallback order, only reached if the successor is unreachable
	 * too. Keeping the reordering in this one place means the new host's GameState can apply
	 * GetCachedMigrationCandidates() verbatim. */
	if (Candidates.Num() < 2)
	{
		Result.Outcome = ERageMigrationElectionOutcome::NoCandidatesRemain;
		return Result;
	}

	TArray<FRageMigrationCandidate> Remaining = Candidates;
	Remaining.RemoveAt(0);

	/** An entry with no valid id can be neither elected nor travelled to, and leaving it in is
	 * actively dangerous. PostLogin records whatever id the PlayerState had at login and nothing
	 * repairs it later, so an unresolved id stays invalid forever with an empty address to match.
	 *
	 * The dangerous part is that FUniqueNetIdWrapper::operator== treats two invalid ids as equal,
	 * so the "am I the winner?" test below answers yes on every machine whose own LocalUniqueId is
	 * also unset. That is N self-elected hosts, which is the exact split this list exists to stop.
	 *
	 * Filtering here rather than at promotion keeps it deterministic: every machine applies the
	 * same predicate to the same replicated list, so they still agree on the order. */
	Result.NumDroppedUnusable = Remaining.RemoveAll([](const FRageMigrationCandidate& Candidate)
	{
		return !Candidate.UniqueId.IsValid();
	});

	if (Remaining.Num() == 0)
	{
		Result.Outcome = ERageMigrationElectionOutcome::NoUsableCandidatesRemain;
		return Result;
	}

	Result.Promoted = Remaining[0];
	Result.RemainingCandidates = MoveTemp(Remaining); /* Promoted candidate is already at index 0. */

	/** The IsValid() check is load-bearing, not defensive. RegisterLocalPlayer only stores an id it
	 * was handed as valid, so a machine that never registered still holds a default-constructed
	 * one, and invalid compares equal to invalid, which would let it win an election it has no
	 * business winning. Requiring validity means the worst case is following someone else instead
	 * of becoming a second host, which everyone can recover from. */
	Result.Outcome = (LocalUniqueId.IsValid() && Result.Promoted.UniqueId == LocalUniqueId)
		? ERageMigrationElectionOutcome::LocalBecomesHost
		: ERageMigrationElectionOutcome::FollowPromoted;

	return Result;
}
