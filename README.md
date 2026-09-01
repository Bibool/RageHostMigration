# Rage Host Migration

P2P host migration for Unreal Engine. When the host is a player and that player quits,
crashes or drops, this keeps the match alive instead of dumping everyone in the main menu.

Every client continuously caches the replicated state it would need in order to be the host. When
the host goes, the survivors agree on who takes over, that machine opens a listen server with the
cached state, and everyone else travels to it.

Two properties carry the design:

- **Agreement is reached before the crash, not after.** The host picks its own successor while it is
  still alive and parks it at index 1 of an ordered list that replicates to everyone. Once the host
  is gone there is no channel left to reconcile a disagreement, and two clients with slightly
  different lists will both open a listen server.
- **State is mirrored, not derived.** Clients cache what the server told them, so every survivor
  holds an identical copy of the match they are resuming.

## Requirements

- Unreal Engine 5.x, C++ project.
- An `OnlineSubsystem` that yields a usable connect address per client (Steam, or IP for LAN/PIE).

### Recommendation
- `CommonLoadingScreen` is **optional**. If the plugin finds it, the subsystem registers as a loading
  processor and holds the screen up for the whole migration. Without it, migration still works, the
  player just watches it happen. It's Loading screen agnostic, you can hook it up to any system.

Drop it in `Plugins/`, add `"RageHostMigration"` to your module's dependencies, and enable it in your
`.uproject`.

```csharp
PublicDependencyModuleNames.Add("RageHostMigration");
```

## Setup
All code snippets shown bellow are from my own project. It goes without saying you'd implement those in your own classes.

### 1. Let the plugin intercept disconnects

`URageOnlineSession` is where a lost connection gets claimed before the engine can travel to the
menu. Without this override `GetOnlineSession()` is null and nothing is intercepted.

```cpp
TSubclassOf<UOnlineSession> URageGameInstance::GetOnlineSessionClass()
{
    return URageOnlineSession::StaticClass();
}
```

### 2. Implement the bridge on your GameInstance

`IRageHostMigrationBridge` is the handful of things a migration cannot do for itself, say something
to the player while it works, and square the online session afterwards. The plugin looks for it on
the GameInstance and nowhere else. It is completely optional and the system will function without it. Recommend to use it though.

```cpp
UCLASS()
class URageGameInstance : public UGameInstance, public IRageHostMigrationBridge
{
    GENERATED_BODY()

public:
    virtual void OnHostMigrationBegan(const FString& TargetLevelName) override;
    virtual void OnHostMigrationUnavailable() override;
    virtual void OnSettleSessionAfterMigration(ERagePostMigrationSessionAction Action) override;
    virtual void OnUnclaimedDisconnect() override;
};
```

```cpp
void URageGameInstance::OnSettleSessionAfterMigration(const ERagePostMigrationSessionAction Action)
{
    URageSessionSubsystem* SessionSubsystem = GetSubsystem<URageSessionSubsystem>();
    if (!IsValid(SessionSubsystem))
    {
        return;
    }

    switch (Action)
    {
    case ERagePostMigrationSessionAction::RehostAsNewHost:
        SessionSubsystem->AdoptSessionAsHost();
        break;

    case ERagePostMigrationSessionAction::DiscardDeadSession:
        SessionSubsystem->DiscardJoinedSession();
        break;

    case ERagePostMigrationSessionAction::KeepSession:
        break;
    }
}
```

`OnHostMigrationBegan` is the place to push a loading reason. Do it here rather than off the
delegate, because the widget is built after the load has already started.

### 3. Feed the candidate list

The plugin does not decide who the successor is but your GameState does while the host is alive. Order
being: **index 0 is the current host, index 1 the designated successor, 2 onwards is the fallback
order.**

Record a candidate per player in `PostLogin`, using the *server-side NetConnection* for the address.
It is the only place the answer is unambiguous, since it is the address the server is already
talking to that client on.

```cpp
void ARageGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    RageGameState->AddOrUpdateMigrationCandidate(RagePlayerState->GetUniqueId(), ResolveReconnectAddress(NewPlayer));
    RageGameState->RefreshDesignatedSuccessor();
}
```

Mirror that list into the subsystem from both the authority's setter and the client's `OnRep`, so
every machine's cache tracks replicated state rather than only where the value originated:

```cpp
void ARageGameState::MirrorMigrationCandidatesToSubsystem()
{
    if (URageHostMigrationSubsystem* HostMigrationSubsystem = URageHostMigrationSubsystem::Get(this))
    {
        HostMigrationSubsystem->CacheMigrationCandidates(MigrationCandidates);
        HostMigrationSubsystem->SetOriginalHostAddress(
            MigrationCandidates.IsValidIndex(0) ? MigrationCandidates[0].ConnectAddress : FString());
    }
}
```

> The mirror pattern is repeated for all snapshots essentially.

### 4. Register the local player

This tells the plugin which replicated candidate is the LP, and doubles as the "LP arrived" signal
that completes a migration. Call it on every machine once the local PlayerState exists. Safe to call multiple times.

```cpp
if (URageHostMigrationSubsystem* HostMigrationSubsystem = URageHostMigrationSubsystem::Get(this))
{
    HostMigrationSubsystem->RegisterLocalPlayer(RagePlayerState->GetUniqueId());
}
```

> A machine that never registers can never win an election. It will always only follow someone else instead.
> Done deliberatly: an unregistered id is invalid, and invalid compares equal to invalid, so
> letting it win would elect every unregistered survivor as host at once, which we don't want.

### 5. Restore on the new host

The new host opens the level with `?migrated=1`. This is how you can branch off initialization of game data.
I personally recommend InitGame in GameMode.

```cpp
void ARageGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    bIsMigratedMatch = UGameplayStatics::HasOption(Options, FRageHostMigrationStatics::MigratedKey);
    bPendingMigrationRestore = bIsMigratedMatch;
}
```

## Carrying your own state

The plugin is game and type agnostic. Anything you want to survive rides a byte cache keyed by
struct name, so any `USTRUCT` can go through it.

>KEEP IN MIND:

>- Asset references survive as their **path** and thus soft pointers come back pointing at the same asset.
  References to *spawned* objects do not, because the actors on the new host are different actors. I recommend to store ids for those.
>- The key is the struct's name, so one struct type is one slot. Two features sharing a struct type will overwrite each other.


```cpp
// While the host is alive, whenever the state changes:
URageHostMigrationSubsystem::CaptureExtraMatchStats(this, BuildMatchStatsSnapshot());

// On the new host, during the migrated restore:
FRageMatchStatsSnapshot Restored;
if (URageHostMigrationSubsystem::TryRestoreExtraMatchStats(this, Restored))
{
    RageGameState->ApplyMatchStatsSnapshot(Restored);
}
```

The player equivalents take a `FUniqueNetIdRepl`. `TryConsume` reads and clears, which is what you
want for anything that must not be applied twice if a player reconnects more than once:

```cpp
// Captured on the way out, from the started delegate:
URageHostMigrationSubsystem::CaptureExtraPlayerStats<FRagePlayerTransformSnapshot>(
    this, RagePlayerState->GetUniqueId(), {GetActorLocation(), GetActorRotation()});

// Applied once, on the new host:
FRagePlayerTransformSnapshot Restored;
if (URageHostMigrationSubsystem::TryConsumeExtraPlayerStats(this, RagePlayerState->GetUniqueId(), Restored))
{
    SetActorLocationAndRotation(Restored.Location, Restored.Rotation);
}
```

## Tracking and restoring stats

> API
```cpp
URageHostMigrationSubsystem::CaptureExtraMatchStats(WorldContext, Data);
URageHostMigrationSubsystem::TryRestoreExtraMatchStats(WorldContext, OutData);

URageHostMigrationSubsystem::CaptureExtraPlayerStats(WorldContext, PlayerId, Data);
URageHostMigrationSubsystem::TryRestoreExtraPlayerStats(WorldContext, PlayerId, OutData);
URageHostMigrationSubsystem::TryConsumeExtraPlayerStats(WorldContext, PlayerId, OutData);  // reads then clears
```

Mostly what varies is **when you capture** and **where you restore**, and that follows from the three kind of state. 

### Per player stats
>Capture on change, restore in PostLogin

Typically your PlayerState stats, such as Kills, Death, Assist, Score. These have to survive on *every* machine, because any of them might be
the one that ends up hosting.

I suggest to capture from the `OnRep`s, and make sure it happens for Auth too. A candidate that
only ever cached its own stats comes up as host with an empty scoreboard for everyone else.

```cpp
void ARagePlayerState::MirrorStatsToSubsystem()
{
    const FRagePlayerStatsSnapshot Snapshot{
        .Kills = Kills, .Deaths = Deaths, .Assists = Assists, .Score = GetScore(), .bWinner = bWinner};

    URageHostMigrationSubsystem::CaptureExtraPlayerStats(this, GetUniqueId(), Snapshot);
}
```

Restore as each survivor rejoins, not once at match start as players trickle back in over several
seconds and each arrival needs its own row put back:

```cpp
void ARageGameMode::OnRestoreMigratedPlayerStats(ARagePlayerState* NewPlayerState)
{
    FRagePlayerStatsSnapshot RestoredStats;
    if (URageHostMigrationSubsystem::TryRestoreExtraPlayerStats(this, NewPlayerState->GetUniqueId(), RestoredStats))
    {
        NewPlayerState->ApplyPlayerStatsSnapshot(RestoredStats);
    }
}
```

Same for GAS attributes. Mirror them off the attribute changed delegate, and apply them
through the ASC on the far side rather than writing the attribute set directly:

```cpp
void ARagePlayerState::MirrorAttributes()
{
    FRageAttributesSnapshot Snapshot;
    Snapshot.Health = RageAbilitySystemComponent->GetNumericAttribute(URageAttributeSet::GetHealthAttribute());
    Snapshot.Shield = RageAbilitySystemComponent->GetNumericAttribute(URageAttributeSet::GetShieldAttribute());
    Snapshot.Energy = RageAbilitySystemComponent->GetNumericAttribute(URageAttributeSet::GetEnergyAttribute());

    URageHostMigrationSubsystem::CaptureExtraPlayerStats(this, GetUniqueId(), Snapshot);
}
```

### Transient state 
> Capture at migration start, consume once

Where a player's pawn was, and anything else that is only true at the instant the host leaves.
Avoid capturing on tick, it's unnecessary work, instead us `HostMigrationStartedDelegate` instead,
which fires before any travel while the old world is still alive.

```cpp
void ARagePlayerCharacter::HandleHostMigrationStarted()
{
    if (!IsLocallyControlled())
    {
        return;
    }

    URageHostMigrationSubsystem::CaptureExtraPlayerStats<FRagePlayerTransformSnapshot>(
        this, RagePlayerState->GetUniqueId(), {GetActorLocation(), GetActorRotation()});
}
```

Use `TryConsume` on the far side for anything that must not be applied twice if a player reconnects
more than once:

```cpp
FRagePlayerTransformSnapshot Restored;
if (URageHostMigrationSubsystem::TryConsumeExtraPlayerStats(this, RagePlayerState->GetUniqueId(), Restored))
{
    SetActorLocationAndRotation(Restored.Location, Restored.Rotation);
}
```

### Match state 
> Mirror it, but refresh anything time-derived on the way out

Match clock, match state, match phase. Mirror from the GameState's `OnRep`s so every machine
holds the same server-authored copy.

It goes without saying that a value mirrored in OnRep is only as fresh as the last time replicated.
A match deadline is written once, at the whistle, so a remainder computed in its `OnRep` is frozen at
whatever the clock read a second into the match and migrating twenty minutes later restores a full
clock. Re-mirror from the started delegate, which is the one instant the number has to be right:

```cpp
void ARageGameState::BeginPlay()
{
    Super::BeginPlay();

    if (URageHostMigrationSubsystem* HostMigrationSubsystem = URageHostMigrationSubsystem::Get(this))
    {
        HostMigrationSubsystem->HostMigrationStartedDelegate.AddDynamic(this, &ARageGameState::HandleHostMigrationStarted);
    }
}

void ARageGameState::HandleHostMigrationStarted()
{
    // GetServerWorldTimeSeconds keeps advancing locally off the last replicated value, so the
    // remainder is still accurate with the host already gone.
    MirrorMatchStatsToSubsystem();
}
```

Store a **remainder**, not an absolute deadline. The new host's world clock starts from zero, so an
absolute server time means nothing to it.

Restore it once, before the match state machine runs, and make sure your match-start handler does not
then reset what you just restored:

```cpp
void ARageGameMode::StartPlay()
{
    TryRestoreMatchStatsIfNeeded();   // sets bResumedFromMigration
    Super::StartPlay();
}

void ARageGameMode::HandleMatchHasStarted()
{
    Super::HandleMatchHasStarted();

    if (bResumedFromMigration)
    {
        bResumedFromMigration = false;
        return;   // the restored clock is the whole story, do not stamp a fresh one over it
    }

    RageGameState->SetTargetTime(GetResolvedGameModeData().MatchTimeLimitSeconds);
}
```

```cpp
HostMigrationSubsystem->HostMigrationStartedDelegate.AddDynamic(this, &ARageGameState::HandleHostMigrationStarted);
```

## Delegates

```cpp
FRageHostMigration HostMigrationStartedDelegate;    // capture anything you need on the way out
FRageHostMigration HostMigrationSucceededDelegate;
FRageHostMigration HostMigrationFailedDelegate;     // route the player out. The plugin does not decide where
```

> The plugin deliberately does not send anyone to the menu on failure. It broadcasts and stops.

## Settings

In Editor: `Project Settings → Game → Rage - Host Migration`, or `[/Script/RageHostMigration.RageHostMigrationSettings]`
in `DefaultGame.ini`.

| Setting | Default | What it is |
|---|---|---|
| `bEnableHostMigration` | `true` | Master switch |
| `MaxReconnectAttemptsPerCandidate` | `5` | Tries against the promoted candidate before re-electing. Needs real headroom: the new host has to load the level and bring up its listen server first |
| `MaxOriginalHostReconnectAttempts` | `0` | Tries against the original host before writing it off. `0` skips the phase. Keep it low — the host usually quit deliberately |
| `ReconnectRetryInterval` | `3s` | How often the retry poll looks at an attempt |
| `MaxConnectionAttemptSeconds` | `12s` | How long one attempt may stay in flight. Stops a dead peer waiting out the net driver's own 120s timeout |
| `BecomeHostTimeoutSeconds` | `45s` | Grace period for our own listen server. Measures a blocking level load, so keep it generous |
| `MaxTotalMigrationSeconds` | `90s` | Hard ceiling on the whole migration. A backstop against a wedged state machine, not a budget |

## Architecture

| Type | Role                                                                                             |
|---|--------------------------------------------------------------------------------------------------|
| `URageHostMigrationSubsystem` | The state machine, the cache, and the travel. On the GameInstance, so it outlives every map load |
| `URageOnlineSession` | Intercepts the engine's disconnect handling before it bounces to the menu                        |
| `FRageHostMigrationElection` | Ordering: who wins, and the fallback order. No state, no travel                                  |
| `FRageHostMigrationPolicy` | Decisions: claim this disconnect? keep or destroy the session on arrival?                        |
| `IRageHostMigrationBridge` | The four things the plugin has to ask of the game around it                                      |

## Notes

- **Addresses are treated as hostile.** `ConnectAddress` is authored by whoever is hosting, which in
  P2P is another player, and it ends up in an `open <address>` console command on every survivor.
  Anything that could smuggle a second console token or a URL option past the `open` is rejected and
  the candidate skipped.
- **Every safety net checks whether we already arrived before acting.** Ejecting someone from a game
  they are actually playing, because a signal went missing rather than because anything broke, is the
  worst thing this system can do.
- Following a new host by address means the online service knows nothing about it, so invites and
  friend-joins point at nothing until something replicates the new session to its clients.

## License

MIT.
