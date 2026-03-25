#ifndef COMMON_H
#define COMMON_H

#include <vector>
#include <string>
#include <cstdint>

#define DINFO		("[I]  ")
#define DWARNING	("[W]  ")
#define DERROR		("[E]  ")
#define DINDENT		("     ")

//#define PRINT_VERBOSE

#ifdef PRINT_VERBOSE
#define V_PRINT(x) \
			std::cout << DINFO << x << std::endl;
#else
#define V_PRINT(x)
#endif

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

struct NDSDirectory
{
	unsigned short firstFileID;
	unsigned short directoryID;
	std::string dirName;
	std::vector<std::string> files;
	std::vector<NDSDirectory> dirs;
};

constexpr std::size_t oneGB = 1ull << 30;

inline u32 readU32(const u8* p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

#endif  // COMMON_H