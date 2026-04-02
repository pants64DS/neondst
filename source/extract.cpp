#include <iostream>
#include <filesystem>
#include <fstream>

#include "common.h"

namespace fs = std::filesystem;

// Defined in pack.cpp
NDSDirectory buildFntTree(u8* fnt, u32 dirID, u32 fntSize);

void dumpFntTree(const std::vector<u8>& rom, const NDSDirectory& dir, const fs::path& p, u32 fatOffset)
{
	const u32* urom = reinterpret_cast<const u32*>(rom.data());

	for (u32 i = 0; i < dir.files.size(); i++)
	{
		fs::path fp = p / dir.files[i];

		if (!fs::exists(fp))
		{
			u16 fid = dir.firstFileID + i;
			u32 start = urom[(fatOffset + fid * 8) / 4];
			u32 size = urom[(fatOffset + fid * 8 + 4) / 4] - start;

			std::ofstream outFile(fp, std::ios::binary | std::ios::out);

			if (!outFile.is_open())
				throw std::runtime_error("failed to create file " + fp.native());

			outFile.write(reinterpret_cast<const char*>(&rom[start]), size);
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

		dumpFntTree(rom, dir.dirs[i], sp, fatOffset);
	}
}

void writeOutputFile(const fs::path& outputPath, const u8* data, std::size_t size)
{
	std::ofstream outFile(outputPath, std::ios::binary | std::ios::out);

	if (!outFile.is_open())
		throw std::runtime_error("failed to open file " + outputPath.native());

	outFile.write(reinterpret_cast<const char*>(data), size);
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

	std::vector<u8> rom;
	rom.resize(ndsFileSize);

	std::cout << "Reading NDS file " << ndsInputPath << '\n';

	ndsFile.read(reinterpret_cast<char*>(rom.data()), ndsFileSize);
	ndsFile.close();

	u32* urom = reinterpret_cast<u32*>(rom.data());
	u16* srom = reinterpret_cast<u16*>(rom.data());

	u32 arm9Offset = urom[8];
	u32 arm9Size = urom[11];
	u32 arm7Offset = urom[12];
	u32 arm7Size = urom[15];
	u32 ovt9Offset = urom[20];
	u32 ovt9Size = urom[21];
	u32 ovt7Offset = urom[22];
	u32 ovt7Size = urom[23];
	u32 fntOffset = urom[16];
	u32 fntSize = urom[17];
	u32 fatOffset = urom[18];
	u32 fatSize = urom[19];
	u32 iconOffset = urom[26];
	u32 rsaOffset = urom[32];
	u32 iconSize = 0x840;
	u32 rsaSize = 136;

	std::cout << "Creating directories\n";

	fs::create_directories(fsOutputPath);
	fs::create_directories(dataPath);
	fs::create_directories(ov7Path);
	fs::create_directories(ov9Path);

	outputPath = fsOutputPath / "header.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ROM header " << outputPath << '\n';

		writeOutputFile(outputPath, rom.data(), 0x4000);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm9.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM9 binary " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[arm9Offset], arm9Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm7.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM7 binary " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[arm7Offset], arm7Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm9ovt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM9 Overlay Table " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[ovt9Offset], ovt9Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm7ovt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting ARM7 Overlay Table " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[ovt7Offset], ovt7Size);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "banner.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting Icon / Title " << outputPath << '\n';

		if (iconOffset)
		{
			switch (*reinterpret_cast<u16*>(&rom[iconOffset]))
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

		writeOutputFile(outputPath, &rom[iconOffset], iconSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";
	
	outputPath = fsOutputPath / "fnt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting File Name Table " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[fntOffset], fntSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "fat.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting File Allocation Table " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[fatOffset], fatSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "rsasig.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << "Extracting RSA signature " << outputPath << '\n';

		writeOutputFile(outputPath, &rom[rsaOffset], rsaSize);
	}
	else
		std::cout << WARNING "File " << outputPath << " already exists\n";

	std::cout << "Extracting ARM9 Overlays\n";

	if (ovt9Size)
	{
		for (u32 i = 0; i < ovt9Size / 32; i++)
		{
			u16 fid = srom[(ovt9Offset + i * 32 + 24) / 2];
			u32 ovStart = urom[(fatOffset + fid * 8) / 4];
			u32 ovSize = urom[(fatOffset + fid * 8 + 4) / 4] - ovStart;
			
			outputPath = ov9Path / std::to_string(urom[(ovt9Offset + i * 32) / 4]);
			outputPath += ".bin";

			if (!fs::exists(outputPath))
				writeOutputFile(outputPath, &rom[ovStart], ovSize);
			else
				std::cout << WARNING "ARM9 Overlay " << outputPath << " already exists\n";
		}
	}

	std::cout << "Extracting ARM7 Overlays\n";

	if (ovt7Size)
	{
		for (u32 i = 0; i < ovt7Size / 32; i++)
		{
			u16 fid = srom[(ovt7Offset + i * 32 + 24) / 2];
			u32 ovStart = urom[(fatOffset + fid * 8) / 4];
			u32 ovSize = urom[(fatOffset + fid * 8 + 4) / 4] - ovStart;

			outputPath = ov7Path / "overlay7_";
			outputPath += std::to_string(urom[(ovt7Offset + i * 32) / 4]);
			outputPath += ".bin";

			if (!fs::exists(outputPath))
				writeOutputFile(outputPath, &rom[ovStart], ovSize);
			else
				std::cout << WARNING "ARM7 Overlay " << outputPath << " already exists\n";
		}
	}

	std::cout << "Extracting Data files\n";

	NDSDirectory rootDir = buildFntTree(&rom[fntOffset], 0xF000, fntSize);
	dumpFntTree(rom, rootDir, dataPath, fatOffset);

	std::cout << "Done\n";
}
