// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Containers/UnrealString.h" /* For FString */
#include "RageHostMigrationPolicy.h"
#include "UObject/Interface.h"
#include "RageHostMigrationBridge.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class URageHostMigrationBridge : public UInterface
{
	GENERATED_BODY()
};

/** Expects to be on the GameInstance, which is where URageHostMigrationSubsystem looks for it,
 * and the only object as well as being the only object guaranteed to outlive everything.
 * This is the bridge between the player and the Host Migration, allowing the game to inform the player of what's going on. */
class RAGEHOSTMIGRATION_API IRageHostMigrationBridge
{
	GENERATED_BODY()

public:
	/** A migration has just started and the player is going to be travelling.
	 * @param TargetLevelName The map every travel from here lands on, captured while the match world
	 * was still the live one. */
	virtual void OnHostMigrationBegan(const FString& TargetLevelName) {}

	/** No migration is going to happen, or the one that was running failed. The player is RTM from Engine disconnect.
	 * Fires for the disconnect that was declined for having nowhere to go as well as for a real
	 * failure, because they are the same event from the player's side and should read the same.
	 * HostMigrationFailedDelegate only covers the second. */
	virtual void OnHostMigrationUnavailable() {}

	/** Square the online session with wherever the migration put us, now that we have arrived.
	 * A migration moves the match without telling the online service anything, so the session every
	 * survivor holds still belongs to the host that died. */
	virtual void OnSettleSessionAfterMigration(ERagePostMigrationSessionAction Action) {}

	/** A disconnect the migration did not claim, so the engine is about to RTM.
	 * The session joined for that connection outlives the travel unless somebody says otherwise,
	 * because it is registered with the online service rather than with the world.
	 * Runs before the engine's travel starts, so anything set here is in place for the load it
	 * describes. */
	virtual void OnUnclaimedDisconnect() {}
};
