// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Containers/UnrealString.h" /* For FString */

struct RAGEHOSTMIGRATION_API FRageHostMigrationStatics final
{
	/* URL option the new host opens with, allowing GameMode to know if a HM occured. */
	static FString MigratedKey;
};