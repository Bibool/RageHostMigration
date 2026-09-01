// Copyright (c) 2026 Abdallah Boutrif

#include "RageHostMigrationLog.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRageHostMigration);

/** Its own category rather than the host project's. Everything this plugin logs is meant to be diffed
 * line by line against the same run on another machine, and a shared category buries those lines in
 * everything else the game had to say while the host was dropping. */
IMPLEMENT_MODULE(FDefaultModuleImpl, RageHostMigration)
