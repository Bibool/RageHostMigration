// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "HAL/Platform.h" /* For uint8 */
#include "RageHostMigrationState.h"
#include "Engine/EngineBaseTypes.h"

/* What the migration subsystem should do with a disconnect it has been offered. */
enum class ERageDisconnectClaim : uint8
{
	/* Ours. Nothing is running yet, so suppress the engine's travel and begin migrating. */
	BeginMigration,

	/** Ours. Migration claimed and in progress. A drop reaches the funnel several
	 * times over, and each has to be swallowed or the engine travels out from under the migration. */
	SwallowDuringMigration,

	DeclineMigrationDisabled,

	/* Another world's disconnect. */
	DeclineForeignWorld,

	/* Only a client that lost its host has anything to migrate away from. */
	DeclineNotClient,

	/* Nothing to migrate to. Claiming would suppress the menu travel and strand the player. */
	DeclineNoCandidates,
};

/* What the online session needs once a migration has put us somewhere. */
enum class ERagePostMigrationSessionAction : uint8
{
	/* We are the listen server now, but the online service still has us down as a guest. */
	RehostAsNewHost,

	/* We followed the new host by address. The session we hold belongs to the host that died. */
	DiscardDeadSession,

	/* The original host came back, so its session is presumably still ours to keep. */
	KeepSession,
};

/* The decisions URageHostMigrationSubsystem makes about disconnects. */
struct RAGEHOSTMIGRATION_API FRageHostMigrationPolicy final
{
	/** @param bMigrationEnabled Whether host migration is turned on at all.
	 * @param bIsOwningWorld    Whether the disconnected world is the one our game instance owns.
	 * @param CurrentState      Idle unless a migration is already running.
	 * @param NetMode           The disconnected world's net mode.
	 * @param NumCandidates     The cached candidate count. Index 0 is the departed host, so migrating
	 * needs at least one entry behind it. */
	static ERageDisconnectClaim DecideDisconnectClaim(
		bool bMigrationEnabled,
		bool bIsOwningWorld,
		ERageHostMigrationState CurrentState,
		ENetMode NetMode,
		int32 NumCandidates);

	/* Whether the caller should suppress the engine's RTM travel. */
	static bool ClaimsDisconnect(ERageDisconnectClaim Claim);

	/** Whether the player should be told the migration failed. Only the case that gets as far as
	 * looking for somewhere to go and finds nowhere: the rest never entered the state machine. */
	static bool ShouldReportMigrationFailed(ERageDisconnectClaim Claim);

	/** @param StateBeforeArrival The state the migration was in when the local player arrived. */
	static ERagePostMigrationSessionAction DecidePostMigrationSession(ERageHostMigrationState StateBeforeArrival);

	/* For logs. A plain enum class, so UEnum::GetValueAsString has nothing to look up. */
	static const TCHAR* ToString(ERagePostMigrationSessionAction Action);
};
