#pragma once

#include <string>

//Compiled code cache for EE blocks, persisted by the web page (IndexedDB) across sessions.
//Records are serialized as: 16-byte key, 4-byte little-endian size, code bytes.
namespace JitCache
{
	void Install();
	//Adds serialized records (e.g. loaded from storage); returns how many were added.
	unsigned int Import(const uint8_t* data, size_t size);
	//Serializes records added since the last call (newly compiled blocks) into an internal
	//buffer and returns its size; the buffer stays valid until the next call.
	size_t ExportPending(const uint8_t*& data);
	size_t GetEntryCount();
	void Clear();
}
