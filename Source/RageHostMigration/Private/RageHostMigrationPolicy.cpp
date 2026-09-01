// Copyright (c) 2026 Abdallah Boutrif

#include "RageHostMigrationPolicy.h"

ERageDisconnectClaim FRageHostMigrationPolicy::DecideDisconnectClaim(
	const bool bMigrationEnabled,
	const bool bIsOwningWorld,
	const ERageHostMigrationState CurrentState,
	const ENetMode NetMode,
	const int32 NumCandidates)
{
	if (!bMigrationEnabled)
	{
		return ERageDisconnectClaim::DeclineMigrationDisabled;
	}

	if (!bIsOwningWorld)
	{
		return ERageDisconnectClaim::DeclineForeignWorld;
	}

	/** Ahead of the net mode check on purpose. A machine that is mid-migration has usually already
	 * opened its own listen server, so it is no longer a client, and the late disconnects from the
	 * drop that started the migration are still arriving. Checking net mode first would decline
	 * those and let the engine travel away from a migration that is working. */
	if (CurrentState != ERageHostMigrationState::Idle)
	{
		return ERageDisconnectClaim::SwallowDuringMigration;
	}

	/** On the authority a lost connection is somebody leaving us, which is deliberate and nothing to
	 * migrate away from. Standalone lands here too, which is what a join that died before the match
	 * world came up looks like. */
	if (NetMode != NM_Client)
	{
		return ERageDisconnectClaim::DeclineNotClient;
	}

	if (NumCandidates < 2)
	{
		return ERageDisconnectClaim::DeclineNoCandidates;
	}

	return ERageDisconnectClaim::BeginMigration;
}

bool FRageHostMigrationPolicy::ClaimsDisconnect(const ERageDisconnectClaim Claim)
{
	return Claim == ERageDisconnectClaim::BeginMigration || Claim == ERageDisconnectClaim::SwallowDuringMigration;
}

bool FRageHostMigrationPolicy::ShouldReportMigrationFailed(const ERageDisconnectClaim Claim)
{
	return Claim == ERageDisconnectClaim::DeclineNoCandidates;
}

ERagePostMigrationSessionAction FRageHostMigrationPolicy::DecidePostMigrationSession(const ERageHostMigrationState StateBeforeArrival)
{
	switch (StateBeforeArrival)
	{
	case ERageHostMigrationState::BecomingHost:
		return ERagePostMigrationSessionAction::RehostAsNewHost;

	case ERageHostMigrationState::TravelingToCandidate:
		return ERagePostMigrationSessionAction::DiscardDeadSession;

	default:
		/** ReconnectingToOriginalHost is the one that arrives without the host having changed, so its
		 * session is the one we already hold. Electing cannot be the state on arrival, because it is
		 * synchronous and leaves as one of the two above, but it belongs here rather than in a
		 * branch that would destroy a live session if it ever did. */
		return ERagePostMigrationSessionAction::KeepSession;
	}
}

const TCHAR* FRageHostMigrationPolicy::ToString(const ERagePostMigrationSessionAction Action)
{
	switch (Action)
	{
	case ERagePostMigrationSessionAction::RehostAsNewHost:    return TEXT("RehostAsNewHost");
	case ERagePostMigrationSessionAction::DiscardDeadSession: return TEXT("DiscardDeadSession");
	case ERagePostMigrationSessionAction::KeepSession:        return TEXT("KeepSession");
	default:                                                 return TEXT("<unknown>");
	}
}
