#include <iostream>
#include <filesystem>
#include <fstream>

#include "common.h"

namespace fs = std::filesystem;


#define FSTREAM_VALID(x) \
			if(!x.good()){ \
				std::cout << DERROR << "Input file invalid, reached EOF\n"; \
				x.close(); \
				return -1; \
			}
#define FSTREAM_OPEN_CHECK \
			if (!outFile.is_open()) { \
				std::cout << DERROR << "Failed to open file " << outputPath << '\n'; \
				return -1; \
			}
#define FILE_EXISTS(x) \
			if(fs::exists(x)){ \
				std::cout << DWARNING << "File " << outputPath << " already exists\n"; \
			}

// Defined in pack.cpp
NDSDirectory buildFntTree(unsigned char* fnt, unsigned dirID, unsigned fntSize);

void dumpFntTree(const std::vector<unsigned char>& rom, const NDSDirectory& dir, const fs::path& p, unsigned fatOffset)
{
	const unsigned* urom = reinterpret_cast<const unsigned*>(rom.data());

	for (unsigned i = 0; i < dir.files.size(); i++)
	{
		fs::path fp = p / dir.files[i];

		if(!fs::exists(fp)){

			V_PRINT("Extracting file " << fp)

			unsigned short fid = dir.firstFileID + i;
			unsigned start = urom[(fatOffset + fid * 8) / 4];
			unsigned size = urom[(fatOffset + fid * 8 + 4) / 4] - start;

			std::ofstream outFile(fp, std::ios::binary | std::ios::out);
		
			if (!outFile.is_open())
			{
				std::cout << DERROR << "Failed to create file " << fp << '\n';
				std::exit(-1);
			}

			outFile.write(reinterpret_cast<const char*>(&rom[start]), size);
			outFile.close();
		}
		else
			std::cout << DWARNING << "File " << fp << " already exists\n";
	}

	for (unsigned i = 0; i < dir.dirs.size(); i++)
	{
		fs::path sp = p / dir.dirs[i].dirName;

		if(!fs::exists(sp) || (fs::exists(sp) && !fs::is_directory(sp))){

			V_PRINT("Creating data directory " << sp)

			try
			{
				if (!fs::create_directory(sp))
				{
					std::cout << DERROR << "Failed to create directory " << sp << '\n';
					std::exit(-1);
				}
			}
			catch (std::exception& e)
			{
				std::cout << DERROR << e.what() << '\n';
				std::exit(-1);
			}
		}
		else
			std::cout << DWARNING << "Directory " << sp << " already exists\n";

		dumpFntTree(rom, dir.dirs[i], sp, fatOffset);
	}
}

int extract(const fs::path& ndsInputPath, const fs::path& fsOutputPath)
{
	// TODO: read these from a text file!
	fs::path dataPath = fsOutputPath / "root";
	fs::path ov7Path = fsOutputPath / "overlay7";
	fs::path ov9Path = fsOutputPath / "overlay9";

	unsigned ndsFileSize = fs::file_size(ndsInputPath);

	if (!fs::exists(ndsInputPath) || !fs::is_regular_file(ndsInputPath))
	{
		std::cout << DERROR << "NDS input file " << ndsInputPath << " is not a valid file\n";
		return -1;
	}

	if (ndsFileSize > oneGB)
	{
		std::cout << DERROR << "NDS input file " << ndsInputPath << " is larger than 1GB\n";
		return -1;
	}

	if (fs::exists(fsOutputPath) && fs::is_directory(fsOutputPath))
		std::cout << DWARNING << "Filesystem output path " << fsOutputPath << " already exists, extracting missing files\n";

	std::ofstream outFile;
	std::ifstream ndsFile;
	fs::path outputPath;
	ndsFile.open(ndsInputPath, std::ios::in | std::ios::binary);

	if (!ndsFile.is_open())
	{
		std::cout << DERROR << "Failed to open file " << ndsInputPath << '\n';
		return -1;
	}

	FSTREAM_VALID(ndsFile)

	std::vector<unsigned char> rom;
	rom.resize(ndsFileSize);

	std::cout << DINFO << "Reading NDS file " << ndsInputPath << '\n';

	ndsFile.read(reinterpret_cast<char*>(rom.data()), ndsFileSize);
	ndsFile.close();

	unsigned* urom = reinterpret_cast<unsigned*>(rom.data());
	unsigned short* srom = reinterpret_cast<unsigned short*>(rom.data());

	unsigned arm9Offset = urom[8];
	unsigned arm9Size = urom[11];
	unsigned arm7Offset = urom[12];
	unsigned arm7Size = urom[15];
	unsigned ovt9Offset = urom[20];
	unsigned ovt9Size = urom[21];
	unsigned ovt7Offset = urom[22];
	unsigned ovt7Size = urom[23];
	unsigned fntOffset = urom[16];
	unsigned fntSize = urom[17];
	unsigned fatOffset = urom[18];
	unsigned fatSize = urom[19];
	unsigned iconOffset = urom[26];
	unsigned rsaOffset = urom[32];
	unsigned iconSize = 0x840;
	unsigned rsaSize = 136;

	std::cout << DINFO << "Creating directories\n";

	try
	{
		fs::create_directories(fsOutputPath);
		fs::create_directories(dataPath);
		fs::create_directories(ov7Path);
		fs::create_directories(ov9Path);
	}
	catch (std::exception& e)
	{
		std::cout << DERROR << e.what() << '\n';

		return -1;
	}

	outputPath = fsOutputPath / "header.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting ROM header " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(rom.data()), 0x4000);
		outFile.close();
	}
	else	
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm9.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting ARM9 binary " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[arm9Offset]), arm9Size);
		outFile.close();
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm7.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting ARM7 binary " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[arm7Offset]), arm7Size);
		outFile.close();
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm9ovt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting ARM9 Overlay Table " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[ovt9Offset]), ovt9Size);
		outFile.close();
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "arm7ovt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting ARM7 Overlay Table " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[ovt7Offset]), ovt7Size);
		outFile.close();	
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "banner.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting Icon / Title " << outputPath << '\n';

		if (iconOffset)
		{
			switch (*reinterpret_cast<unsigned short*>(&rom[iconOffset]))
			{
			default:
				std::cout << DWARNING << "Invalid Icon / Title ID, defaulting to 0x840\n";
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
			std::cout << DINFO << "No Icon / Title found\n";
			iconSize = 0;
		}

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[iconOffset]), iconSize);
		outFile.close();
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";
	
	outputPath = fsOutputPath / "fnt.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting File Name Table " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[fntOffset]), fntSize);
		outFile.close();
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "fat.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting File Allocation Table " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[fatOffset]), fatSize);
		outFile.close();
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	outputPath = fsOutputPath / "rsasig.bin";

	if (!fs::exists(outputPath))
	{
		std::cout << DINFO << "Extracting RSA signature " << outputPath << '\n';

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[rsaOffset]), rsaSize);
		outFile.close();	
	}
	else
		std::cout << DWARNING << "File " << outputPath << " already exists\n";

	std::cout << DINFO << "Extracting ARM9 Overlays\n";

	if (ovt9Size)
	{
		for (unsigned i = 0; i < ovt9Size / 32; i++)
		{
			unsigned short fid = srom[(ovt9Offset + i * 32 + 24) / 2];
			unsigned ovStart = urom[(fatOffset + fid * 8) / 4];
			unsigned ovSize = urom[(fatOffset + fid * 8 + 4) / 4] - ovStart;
			
			outputPath = ov9Path / std::to_string(urom[(ovt9Offset + i * 32) / 4]);
			outputPath += ".bin";

			if (!fs::exists(outputPath))
			{
				V_PRINT("Extracting ARM9 Overlay " << outputPath)

				outFile.open(outputPath, std::ios::binary | std::ios::out);
				FSTREAM_OPEN_CHECK

				outFile.write(reinterpret_cast<const char*>(&rom[ovStart]), ovSize);
				outFile.close();
			}
			else
				std::cout << DWARNING << "ARM9 Overlay " << outputPath << " already exists\n";
		}
	}

	std::cout << DINFO << "Extracting ARM7 Overlays\n";

	if (ovt7Size)
	{
		for (unsigned i = 0; i < ovt7Size / 32; i++)
		{
			unsigned short fid = srom[(ovt7Offset + i * 32 + 24) / 2];
			unsigned ovStart = urom[(fatOffset + fid * 8) / 4];
			unsigned ovSize = urom[(fatOffset + fid * 8 + 4) / 4] - ovStart;

			outputPath = ov7Path / "overlay7_";
			outputPath += std::to_string(urom[(ovt7Offset + i * 32) / 4]);
			outputPath += ".bin";

			if (!fs::exists(outputPath))
			{
				V_PRINT("Extracting ARM7 Overlay " << outputPath)

				outFile.open(outputPath, std::ios::binary | std::ios::out);
				FSTREAM_OPEN_CHECK

				outFile.write(reinterpret_cast<const char*>(&rom[ovStart]), ovSize);
				outFile.close();
			}
			else
				std::cout << DWARNING << "ARM7 Overlay " << outputPath << " already exists\n";
		}
	}

	std::cout << DINFO << "Extracting Data files\n";

	NDSDirectory rootDir = buildFntTree(&rom[fntOffset], 0xF000, fntSize);
	dumpFntTree(rom, rootDir, dataPath, fatOffset);

	std::cout << DINFO << "Done\n";

	return 0;
}
