#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>

#define WARNING "\x1B[1;95mwarning: \033[0m"
#define ERROR   "\x1B[1;91merror: \033[0m"

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

namespace fs = std::filesystem;

struct Extractor
{
	fs::path romPath;
	Extractor(const fs::path& romPath) : romPath(romPath) {}
	void extract();

	virtual void writeFile(const fs::path& shortPath, const void* data, std::size_t size) = 0;
	virtual void writeDir (const fs::path& shortPath) = 0;
};

struct NDSDirectory
{
	u16 firstFileID;
	u16 directoryID;
	std::string dirName;
	std::vector<std::string> files;
	std::vector<NDSDirectory> dirs;
};

NDSDirectory buildFntTree(u8* fnt, u32 dirID, u32 fntSize);

constexpr std::size_t oneGB = 1ull << 30;

inline u32 readU16(const u8* p)
{
	return p[0] | p[1] << 8;
}

inline u32 readU32(const u8* p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

inline void writeU32(u8* p, u32 v)
{
	p[0] = v       & 0xff;
	p[1] = v >>  8 & 0xff;
	p[2] = v >> 16 & 0xff;
	p[3] = v >> 24;
}
