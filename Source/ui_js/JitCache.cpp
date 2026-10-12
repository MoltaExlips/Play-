#include "JitCache.h"
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "BasicBlock.h"

namespace
{
	std::mutex g_mutex;
	std::unordered_map<std::string, std::vector<uint8>> g_entries;
	std::vector<std::string> g_pending;
	std::vector<uint8_t> g_exportBuffer;

	bool Lookup(const uint8* key, const uint8*& code, size_t& size)
	{
		std::lock_guard lock(g_mutex);
		auto entry = g_entries.find(std::string(reinterpret_cast<const char*>(key), 16));
		if(entry == std::end(g_entries)) return false;
		//Entries are never removed while a game runs, so the pointer stays valid.
		code = entry->second.data();
		size = entry->second.size();
		return true;
	}

	void Store(const uint8* key, const uint8* code, size_t size)
	{
		std::lock_guard lock(g_mutex);
		std::string keyString(reinterpret_cast<const char*>(key), 16);
		auto [entry, inserted] = g_entries.try_emplace(keyString, code, code + size);
		if(inserted) g_pending.push_back(keyString);
	}
}

void JitCache::Install()
{
	g_jitCacheLookup = &Lookup;
	g_jitCacheStore = &Store;
}

unsigned int JitCache::Import(const uint8_t* data, size_t size)
{
	std::lock_guard lock(g_mutex);
	unsigned int added = 0;
	size_t offset = 0;
	while(offset + 20 <= size)
	{
		std::string key(reinterpret_cast<const char*>(data + offset), 16);
		uint32_t codeSize = 0;
		memcpy(&codeSize, data + offset + 16, 4);
		offset += 20;
		if(offset + codeSize > size) break;
		if(g_entries.try_emplace(key, data + offset, data + offset + codeSize).second) added++;
		offset += codeSize;
	}
	return added;
}

size_t JitCache::ExportPending(const uint8_t*& data)
{
	std::lock_guard lock(g_mutex);
	g_exportBuffer.clear();
	for(const auto& key : g_pending)
	{
		const auto& code = g_entries[key];
		uint32_t codeSize = static_cast<uint32_t>(code.size());
		g_exportBuffer.insert(g_exportBuffer.end(), key.begin(), key.end());
		g_exportBuffer.insert(g_exportBuffer.end(), reinterpret_cast<uint8_t*>(&codeSize), reinterpret_cast<uint8_t*>(&codeSize) + 4);
		g_exportBuffer.insert(g_exportBuffer.end(), code.begin(), code.end());
	}
	g_pending.clear();
	data = g_exportBuffer.data();
	return g_exportBuffer.size();
}

size_t JitCache::GetEntryCount()
{
	std::lock_guard lock(g_mutex);
	return g_entries.size();
}

void JitCache::Clear()
{
	std::lock_guard lock(g_mutex);
	g_entries.clear();
	g_pending.clear();
}
