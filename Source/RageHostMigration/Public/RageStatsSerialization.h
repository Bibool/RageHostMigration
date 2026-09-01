// Copyright (c) 2026 Abdallah Boutrif

#pragma once

#include "Containers/Array.h" /* For TArray */
#include "HAL/Platform.h" /* For uint8 */
#include "Serialization/MemoryReader.h" /* For FMemoryReader */
#include "Serialization/MemoryWriter.h" /* For FMemoryWriter */
#include "Serialization/ObjectAndNameAsStringProxyArchive.h" /* For FObjectAndNameAsStringProxyArchive */

/* Blits a USTRUCT to and from bytes for snapshot types to share one cache keyed by struct name. */
class FRageStatsSerialization final
{
	static constexpr bool bLoadIfFindFails = false;

public:
	template <typename TStructType>
	static void ToBytes(const TStructType& InStruct, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		FMemoryWriter Writer(OutBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Archive(Writer, bLoadIfFindFails);
		TStructType::StaticStruct()->SerializeItem(Archive, (void*)&InStruct, nullptr);
	}
 
	template <typename TStructType>
	static void FromBytes(const TArray<uint8>& InBytes, TStructType& OutStruct)
	{
		FMemoryReader Reader(InBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Archive(Reader, bLoadIfFindFails);
		TStructType::StaticStruct()->SerializeItem(Archive, &OutStruct, nullptr);
	}
};
 
