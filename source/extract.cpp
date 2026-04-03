#include <iostream>
#include <filesystem>
#include <fstream>
#include <span>

#include "common.h"

namespace fs = std::filesystem;

// Defined in pack.cpp
NDSDirectory buildFntTree(u8* fnt, u32 dirID, u32 fntSize);

static void dumpFntTree(const std::vector<u32>& romU32, const NDSDirectory& dir, const fs::path& p, u32 fatOffset)
{
	for (u32 i = 0; i < dir.files.size(); i++)
	{
		fs::path fp = p / dir.files[i];

		if (!fs::exists(fp))
		{
			u16 fid = dir.firstFileID + i;
			u32 start = romU32[(fatOffset + fid * 8) / 4];
			u32 size = romU32[(fatOffset + fid * 8 + 4) / 4] - start;

			std::ofstream outFile(fp, std::ios::binary | std::ios::out);

			if (!outFile.is_open())
				throw std::runtime_error("failed to create file " + fp.native());

			outFile.write(reinterpret_cast<const char*>(romU32.data()) + start, size);
			outFile.close();
		}
		else
			std::cout << WARNING "File " << fp << " already exists\n";
	}

	for (u32 i = 0; i < dir.dirs.size(); i++)
	{
		fs::path sp = p / dir.dirs[i].dirName;

		if (!fs::exists(sp) || !fs::is_directory(sp))
			fs::create_directory(sp);
		else
			std::cout << WARNING "Directory " << sp << " already exists\n";

		dumpFntTree(romU32, dir.dirs[i], sp, fatOffset);
	}
}

void writeOutputFile(const fs::path& outputPath, const void* data, std::size_t size)
{
	std::ofstream outFile(outputPath, std::ios::binary | std::ios::out);

	if (!outFile.is_open())
		throw std::runtime_error("failed to open file " + outputPath.native());

	outFile.write(static_cast<const char*>(data), size);
}

void extract(const fs::path& ndsInputPath, const fs::path& fsOutputPath)
{
	fs::path dataPath = fsOutputPath / "root";
	fs::path ov7Path = fsOutputPath / "overlay7";
	fs::path ov9Path = fsOutputPath / "overlay9";

	u32 ndsFileSize = fs::file_size(ndsInputPath);

	if (ndsFileSize > oneGB)
		throw std::length_error("the input file is larger than 1 GB");

	if (fs::exists(fsOutputPath) && fs::is_directory(fsOutputPath))
		std::cout << WARNING "Filesystem output path " << fsOutputPath << " already exists, extracting missing files\n";

	std::ofstream outFile;
	std::ifstream ndsFile;
	fs::path outputPath;
	ndsFile.open(ndsInputPath, std::ios::in | std::ios::binary);

	if (!ndsFile.good())
		throw std::runtime_error("failed to open file " + ndsInputPath.native());

	std::vector<u32> romU32;
	romU32.resize((ndsFileSize + 3) >> 2);

	std::cout << "Reading NDS file " << ndsInputPath << '\n';

	ndsFile.read(reinterpret_cast<char*>(romU32.data()), ndsFileSize);
	ndsFile.close();

	std::span<u8> romU8 = {reinterpret_cast<u8*>(romU32.data()), ndsFileSize};

	u32 arm9Offset = romU32[8];
	u32 arm9Size   = romU32[11];
	u32 arm7Offset = romU32[12];
	u32 arm7Size   = romU32[15];
	u32 ovt9Offset = romU32[20];
	u32 ovt9Size   = romU32[21];
	u32 ovt7Offset = romU32[22];
	u32 ovt7Size   = romU32[23];
	u32 fntOffset  = romU32[16];
	u32 fntSize    = romU32[17];
	u32 fatOffset  = romU32[18];
	u32 fatSize    = romU32[19];
	u32 iconOffset = romU32[26];
	u32 rsaOffset  = romU32[32];
	u32 iconSize   = 0x840;
	u32 rsaSize    = 136;

	std::cout << "Creating directories\n";

	fs::create_directories(fsOutputPath);
	fs::create_directories(dataPath);
	fs::create_directories(ov7Path);
	fs::create_directories(ov9Path);

	outputPath = fsOutputPath / "header.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ROM header " << outputPath << '\n';

		writeOutputFile(outputPath, romU32.data(), 0x4000);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm9.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM9 binary " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[arm9Offset], arm9Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm7.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM7 binary " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[arm7Offset], arm7Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm9ovt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM9 Overlay Table " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[ovt9Offset], ovt9Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm7ovt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM7 Overlay Table " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[ovt7Offset], ovt7Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "banner.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting Icon / Title " << outputPath << '\n';

		if (iconOffset)
		{
			switch (readU16(romU8.data() + iconOffset))
			{
			default:
				std::cout << WARNING "Invalid Icon / Title ID, defaulting to 0x840\n";
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
			std::cout << "No Icon / Title found\n";
			iconSize = 0;
		}

		writeOutputFile(outputPath, &romU8[iconOffset], iconSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";
	
	outputPath = fsOutputPath / "fnt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting File Name Table " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[fntOffset], fntSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "fat.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting File Allocation Table " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[fatOffset], fatSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "rsasig.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting RSA signature " << outputPath << '\n';

		writeOutputFile(outputPath, &romU8[rsaOffset], rsaSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	std::cout << "Extracting ARM9 Overlays\n";

	if (ovt9Size)
	{
		for (u32 i = 0; i < ovt9Size / 32; i++)
		{
			u16 fid = readU16(romU8.data() + ovt9Offset + i * 32 + 24);
			u32 ovStart = romU32[(fatOffset + fid * 8) / 4];
			u32 ovSize = romU32[(fatOffset + fid * 8 + 4) / 4] - ovStart;
			
			outputPath = ov9Path / std::to_string(romU32[(ovt9Offset + i * 32) / 4]);
			outputPath += ".bin";

			if (!fs::exists(outputPath))
				writeOutputFile(outputPath, &romU8[ovStart], ovSize);
			else
				std::cout << WARNING "ARM9 Overlay " << outputPath << " already exists\n";
		}
	}

	std::cout << "Extracting ARM7 Overlays\n";

	if (ovt7Size)
	{
		for (u32 i = 0; i < ovt7Size / 32; i++)
		{
			u16 fid = readU16(romU8.data() + ovt7Offset + i * 32 + 24);
			u32 ovStart = romU32[(fatOffset + fid * 8) / 4];
			u32 ovSize = romU32[(fatOffset + fid * 8 + 4) / 4] - ovStart;

			outputPath = ov7Path / "overlay7_";
			outputPath += std::to_string(romU32[(ovt7Offset + i * 32) / 4]);
			outputPath += ".bin";

			if (!fs::exists(outputPath))
				writeOutputFile(outputPath, &romU8[ovStart], ovSize);
			else
				std::cout << WARNING "ARM7 Overlay " << outputPath << " already exists\n";
		}
	}

	std::cout << "Extracting Data files\n";

	NDSDirectory rootDir = buildFntTree(&romU8[fntOffset], 0xF000, fntSize);
	dumpFntTree(romU32, rootDir, dataPath, fatOffset);

	std::cout << "Done\n";
}
