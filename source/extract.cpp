#include <iostream>
#include <filesystem>
#include <fstream>
#include <span>

#include "common.h"

static void dumpFntTree(
	Extractor& extractor,
	const std::vector<u32>& romU32,
	const NDSDirectory& dir,
	const fs::path& p,
	u32 fatOffset
)
{
	extractor.writeDir(p);

	for (u32 i = 0; i < dir.files.size(); i++)
	{
		const u16 fid = dir.firstFileID + i;
		const u32 start = romU32[(fatOffset + fid * 8) / 4];
		const u32 size = romU32[(fatOffset + fid * 8 + 4) / 4] - start;

		extractor.writeFile(p / dir.files[i], reinterpret_cast<const char*>(romU32.data()) + start, size);
	}

	for (u32 i = 0; i < dir.dirs.size(); i++)
		dumpFntTree(extractor, romU32, dir.dirs[i], p / dir.dirs[i].dirName, fatOffset);
}

void Extractor::extract(const fs::path& ndsInputPath)
{
	const u32 ndsFileSize = fs::file_size(ndsInputPath);

	if (ndsFileSize > oneGB)
		throw std::length_error("the input file is larger than 1 GB");

	std::ifstream ndsFile;
	fs::path outputPath;
	ndsFile.open(ndsInputPath, std::ios::in | std::ios::binary);

	if (!ndsFile.good())
		throw std::runtime_error("failed to open file " + ndsInputPath.native());

	std::vector<u32> romU32;
	romU32.resize((ndsFileSize + 3) >> 2);

	ndsFile.read(reinterpret_cast<char*>(romU32.data()), ndsFileSize);
	ndsFile.close();

	std::span<u8> romU8 = {reinterpret_cast<u8*>(romU32.data()), ndsFileSize};

	const u32 arm9Offset = romU32[8];
	const u32 arm9Size   = romU32[11];
	const u32 arm7Offset = romU32[12];
	const u32 arm7Size   = romU32[15];
	const u32 ovt9Offset = romU32[20];
	const u32 ovt9Size   = romU32[21];
	const u32 ovt7Offset = romU32[22];
	const u32 ovt7Size   = romU32[23];
	const u32 fntOffset  = romU32[16];
	const u32 fntSize    = romU32[17];
	const u32 fatOffset  = romU32[18];
	const u32 fatSize    = romU32[19];
	const u32 iconOffset = romU32[26];
	const u32 rsaOffset  = romU32[32];
	const u32 rsaSize    = 136;

	const fs::path ov7Path = "overlay7";
	const fs::path ov9Path = "overlay9";

	writeDir({});
	writeDir(ov7Path);
	writeDir(ov9Path);

	writeFile("header.bin", romU32.data(), 0x4000);
	writeFile("arm9.bin",    romU8.data() + arm9Offset, arm9Size);
	writeFile("arm7.bin",    romU8.data() + arm7Offset, arm7Size);
	writeFile("arm9ovt.bin", romU8.data() + ovt9Offset, ovt9Size);
	writeFile("arm7ovt.bin", romU8.data() + ovt7Offset, ovt7Size);

	u32 iconSize;

	if (iconOffset)
	{
		switch (readU16(romU8.data() + iconOffset))
		{
		default:
			std::cout << WARNING "invalid icon / title ID, defaulting to 0x840\n";
			[[fallthrough]];
		case 0x0001:
			iconSize = 0x0840;
			break;
		case 0x0002:
			iconSize = 0x0940;
			break;
		case 0x0003:
			iconSize = 0x0A40;
			break;
		case 0x0103:
			iconSize = 0x23C0;
			break;
		}
	}
	else
	{
		std::cout << WARNING "no Icon / Title found\n";
		iconSize = 0;
	}

	writeFile("banner.bin", &romU8[iconOffset], iconSize);
	writeFile("fnt.bin", &romU8[fntOffset], fntSize);
	writeFile("fat.bin", &romU8[fatOffset], fatSize);
	writeFile("rsasig.bin", &romU8[rsaOffset], rsaSize);

	if (ovt9Size)
	{
		for (u32 i = 0; i < ovt9Size / 32; i++)
		{
			u16 fid = readU16(romU8.data() + ovt9Offset + i * 32 + 24);
			u32 ovStart = romU32[(fatOffset + fid * 8) / 4];
			u32 ovSize = romU32[(fatOffset + fid * 8 + 4) / 4] - ovStart;
			
			fs::path outputPath = ov9Path / std::to_string(romU32[(ovt9Offset + i * 32) / 4]);
			outputPath += ".bin";

			writeFile(outputPath, &romU8[ovStart], ovSize);
		}
	}

	if (ovt7Size)
	{
		for (u32 i = 0; i < ovt7Size / 32; i++)
		{
			u16 fid = readU16(romU8.data() + ovt7Offset + i * 32 + 24);
			u32 ovStart = romU32[(fatOffset + fid * 8) / 4];
			u32 ovSize = romU32[(fatOffset + fid * 8 + 4) / 4] - ovStart;

			fs::path outputPath = ov7Path / std::to_string(romU32[(ovt7Offset + i * 32) / 4]);
			outputPath += ".bin";

			writeFile(outputPath, &romU8[ovStart], ovSize);
		}
	}

	NDSDirectory rootDir = buildFntTree(&romU8[fntOffset], 0xF000, fntSize);
	dumpFntTree(*this, romU32, rootDir, "root", fatOffset);
}
