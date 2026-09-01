// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Logging/StructuredLog.h" /* For UE_LOGFMT */
#include "Misc/AssertionMacros.h" /* For FDebug::DumpStackTraceToLog */

DECLARE_LOG_CATEGORY_EXTERN(LogRageHostMigration, Log, All);

#define RAGE_HM_LOG(Verbosity, Format, ...) \
UE_LOGFMT(LogRageHostMigration, Verbosity, Format, ##__VA_ARGS__)

#define RAGE_HM_DUMP_STACK_TRACE(Heading, Verbosity) \
FDebug::DumpStackTraceToLog(TEXT(Heading), Verbosity);
