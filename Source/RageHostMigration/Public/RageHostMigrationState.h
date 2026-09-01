// Copyright (c) 2026 Abdallah Boutrif

#pragma once

UENUM(BlueprintType)
enum class ERageHostMigrationState : uint8
{
	Idle,
	ReconnectingToOriginalHost,
	Electing,
	BecomingHost,
	TravelingToCandidate,
};
