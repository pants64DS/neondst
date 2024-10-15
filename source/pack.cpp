#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <map>
#include <cmath>
#include <cstring>

#include "common.h"
#include "crc.h"

#define RKEEP ("KEEP")
#define RADJUST ("ADJUST")
#define RCALC ("CALC")
#define REMPTY ("")
#define AFILE (1)
#define ADIR (2)
#define AVALUE (4)
#define AKEEP (8)
#define AADJUST (16)
#define ACALC (32)

#define FSTREAM_OPEN_CHECK(x) \
			if (!fileStream.is_open()) { \
				std::cout << DERROR << "Failed to open file " << x##Path << "\n"; \
				return -1; \
			}
#define FILESIZE_CHECK(x, y) \
			if (x##Size > y) { \
				std::cout << DERROR << "File size of " << x##Path << " with " << x##Size << " bytes exceeds " << y << " bytes\n"; \
				return -1; \
			}

namespace fs = std::filesystem;

struct RuleParams
{
	std::string val;
	unsigned type;
};

const char* typeNames[] = {
	"Regular file",
	"Directory",
	"Hex value",
	"KEEP directive",
	"ADJUST directive",
	"CALC directive"
};

struct OverlayEntry
{
	unsigned start;
	unsigned end;
	unsigned short fileID;
};

void romCheckBounds(std::vector<unsigned char>& rom, unsigned offset, unsigned size)
{
	while (rom.size() < offset + size)
	{
		if (rom.size() >= oneGB)
		{
			std::cout << DERROR << "Nitro ROM trying to grow larger than 1GB, aborting\n";
			rom.clear();
			std::exit(-1);
		}
		else
		{
			std::cout << DWARNING << "Nitro ROM size specified in header too small, resizing from " << rom.size() << " to " << (rom.size() * 2) << " bytes\n";
			rom.resize(rom.size() * 2, 0xFF);
		}
	}
}

NDSDirectory buildFntTree(unsigned char* fnt, unsigned dirID, unsigned fntSize)
{
	NDSDirectory dir;
	unsigned dirOffset = (dirID & 0xFFF) * 8;
	unsigned subOffset = *reinterpret_cast<unsigned*>(&fnt[dirOffset]);
	dir.firstFileID = *reinterpret_cast<unsigned short*>(&fnt[dirOffset + 4]);
	dir.directoryID = dirID;

	unsigned relOffset = 0;
	unsigned char len = 0;
	std::string name;

	while (subOffset + relOffset < fntSize)
	{
		len = fnt[subOffset + relOffset];
		relOffset++;

		if (len == 0x80)
		{
			std::cout << DWARNING << "FNT identifier 0x80 detected (reserved), skipping dir node\n";
			break;
		}
		else if (len == 0x00)
			break;

		bool isSubdir = len & 0x80;
		len &= 0x7F;

		name = std::string(reinterpret_cast<const char*>(&fnt[subOffset + relOffset]), len);
		relOffset += len;

		if (isSubdir)
		{
			NDSDirectory subDir = buildFntTree(fnt, *reinterpret_cast<unsigned short*>(&fnt[subOffset + relOffset]), fntSize);
			subDir.dirName = name;
			dir.dirs.push_back(subDir);
			relOffset += 2;
		}
		else
			dir.files.push_back(name);
	}

	return dir;
}

unsigned short fntFindNextFreeFileID(const NDSDirectory& dir)
{
	unsigned short fileFree = dir.firstFileID + dir.files.size();

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		fileFree = std::max(fileFree, fntFindNextFreeFileID(dir.dirs[i]));

	return fileFree;
}

unsigned short fntFindNextFreeDirID(const NDSDirectory& dir)
{
	unsigned short dirFree = dir.directoryID + 1;
	
	for (unsigned i = 0; i < dir.dirs.size(); i++)
		dirFree = std::max(dirFree, fntFindNextFreeDirID(dir.dirs[i]));

	return dirFree;
}

unsigned fntDirectoryIndex(NDSDirectory& parent, const std::string& dataDir)
{
	for (unsigned i = 0; i < parent.dirs.size(); i++)
		if (dataDir == parent.dirs[i].dirName)
			return i;

	return -1;
}

void fntAddNewFiles(NDSDirectory& ndsDir, const fs::path& dataDir, unsigned short& freeFileID, unsigned short& freeDirID)
{
	for (const fs::path& p : fs::directory_iterator(dataDir))
	{
		if (!fs::is_directory(p))
			continue;

		unsigned i = fntDirectoryIndex(ndsDir, p.filename().string());

		if (i == ~0u)
		{
			NDSDirectory dir;
			dir.firstFileID = freeFileID;
			dir.directoryID = freeDirID;
			dir.dirName = p.filename().string();
			
			for (const fs::path& sp : fs::directory_iterator(p))
			{
				if (fs::is_regular_file(sp))
				{
					std::cout << DINFO << "File " << sp << " obtained File ID " << (dir.firstFileID + dir.files.size()) << '\n';
					dir.files.push_back(sp.filename().string());
				}
			}

			freeFileID += dir.files.size();
			freeDirID++;

			fntAddNewFiles(dir, p, freeFileID, freeDirID);
			
			ndsDir.dirs.push_back(dir);
		}
		else
			fntAddNewFiles(ndsDir.dirs[i], p, freeFileID, freeDirID);
	}
}

void fntPrintDirs(const NDSDirectory& dir, const fs::path& path)
{
	for (unsigned i = 0; i < dir.files.size(); i++)
		V_PRINT("File ID " << (dir.firstFileID + i) << ": " << path / dir.files[i])

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		fntPrintDirs(dir.dirs[i], path / dir.dirs[i].dirName);
}

unsigned fntDirectoryCount(const NDSDirectory& dir)
{
	unsigned count = dir.dirs.size();

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		count += fntDirectoryCount(dir.dirs[i]);

	return count;
}

unsigned fntByteCountFn(const NDSDirectory& dir)
{
	unsigned bytes = 0;

	for (unsigned i = 0; i < dir.files.size(); i++)
		bytes += dir.files[i].length() + 1;

	for (unsigned i = 0; i < dir.dirs.size(); i++)
	{
		bytes += dir.dirs[i].dirName.length() + 3;
		bytes += fntByteCountFn(dir.dirs[i]);
	}

	bytes++;

	return bytes;
}

unsigned fntByteCountHeader(const NDSDirectory& root)
{
	return (fntDirectoryCount(root) + 1) * 8;
}

unsigned fntWriteDirectory(const NDSDirectory& dir, unsigned char* fnt, unsigned offset, unsigned parentID)
{
	unsigned* ufnt = reinterpret_cast<unsigned*>(fnt);
	unsigned short* sfnt = reinterpret_cast<unsigned short*>(fnt);
	ufnt[(dir.directoryID & 0xFFF) * 2] = offset;
	sfnt[(dir.directoryID & 0xFFF) * 4 + 2] = dir.firstFileID;
	sfnt[(dir.directoryID & 0xFFF) * 4 + 3] = parentID;

	for (unsigned i = 0; i < dir.files.size(); i++)
	{
		const std::string& filename = dir.files[i];

		fnt[offset] = filename.length();
		filename.copy(reinterpret_cast<char*>(&fnt[offset + 1]), filename.length());
		offset += filename.length() + 1;
	}

	for (unsigned i = 0; i < dir.dirs.size(); i++)
	{
		const NDSDirectory& subdir = dir.dirs[i];
		const std::string& dirname = subdir.dirName;

		fnt[offset] = dirname.length() + 0x80;
		dirname.copy(reinterpret_cast<char*>(&fnt[offset + 1]), dirname.length());
		fnt[offset + dirname.length() + 1] = subdir.directoryID & 0xFF;
		fnt[offset + dirname.length() + 2] = (subdir.directoryID & 0xFF00) >> 8;
		offset += dirname.length() + 3;
	}

	fnt[offset] = 0x00;
	offset++;

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		offset = fntWriteDirectory(dir.dirs[i], fnt, offset, dir.directoryID);

	return offset;

}

void fntRebuild(std::vector<unsigned char>& rom, unsigned fntOffset, const NDSDirectory& root, unsigned& size)
{
	unsigned fntHeaderSize = fntByteCountHeader(root);
	unsigned fntFnSize = fntByteCountFn(root);
	size = fntHeaderSize + fntFnSize;

	romCheckBounds(rom, fntOffset, size);
	fntWriteDirectory(root, &rom[fntOffset], fntHeaderSize, fntHeaderSize / 8);
}

unsigned alignAddress(unsigned address, unsigned align)
{
	return ((address + align - 1) & ~(align - 1));
}

unsigned alignAndClear(std::vector<unsigned char>& rom, unsigned address, unsigned align)
{
	unsigned alignedAddress = alignAddress(address, align);
	romCheckBounds(rom, alignedAddress, 4);
	std::fill(&rom[address], &rom[alignedAddress], 0);

	return alignedAddress;
}

void nfsAddAndLink(std::vector<unsigned char>& rom, unsigned fatOffset, const NDSDirectory& dir, const fs::path& p, unsigned& romOffset)
{
	unsigned short dirFileID = dir.firstFileID;

	for (unsigned i = 0; i < dir.files.size(); i++)
	{
		fs::path filePath = p / dir.files[i];
		unsigned fileSize = fs::file_size(filePath);

		if (fileSize > oneGB)
		{
			std::cout << DWARNING << "File size of " << filePath << " with " << fileSize << " bytes exceeds 1GB, skipping\n";
			dirFileID++;
			continue;
		}

		std::ifstream fileStream(filePath, std::ios::binary | std::ios::in);

		if (!fileStream.is_open())
		{
			std::cout << DWARNING << "Failed to open file " << filePath << ", skipping\n";
			dirFileID++;
			continue;
		}

		romCheckBounds(rom, romOffset, fileSize);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fileSize);
		fileStream.close();

		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);
		fatPtr[dirFileID * 2] = romOffset;
		fatPtr[dirFileID * 2 + 1] = romOffset + fileSize;

		V_PRINT("Added and linked " << filePath << " (File ID " << dirFileID << ") to FAT")

		romOffset += fileSize;
		romOffset = alignAddress(romOffset, 4);
		dirFileID++;
	}

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		nfsAddAndLink(rom, fatOffset, dir.dirs[i], p / dir.dirs[i].dirName, romOffset);
}

void fntGenRootDir(NDSDirectory& rootDir, const fs::path& rootPath, unsigned short fid)
{
	rootDir.firstFileID = fid;
	rootDir.directoryID = 0xF000;
	rootDir.dirName = "";

	for (const fs::path& p : fs::directory_iterator(rootPath))
		if (fs::is_regular_file(p))
			rootDir.files.push_back(p.filename().string());
}

void processBuildRule(
	std::unordered_map<std::string, RuleParams>& rules,
	const fs::path& dir,
	const std::string& name,
	std::string& arg)
{
	auto ruleIt = rules.find(name);
	if (ruleIt == rules.end())
	{
		std::cout << DWARNING << "Unknown rule '" << name << "'\n";
		return;
	}

	RuleParams& rule = ruleIt->second;

	if (rule.type & (AFILE | ADIR))
		arg = (dir / arg).string();

	if ((rule.type & AFILE) && fs::exists(arg) && fs::is_regular_file(arg))
	{
		rule.val = arg;
		rule.type = AFILE;
	}
	else if ((rule.type & ADIR) && fs::exists(arg) && fs::is_directory(arg))
	{
		rule.val = arg;
		rule.type = ADIR;
	}
	else if ((rule.type & AKEEP) && arg == RKEEP)
	{
		rule.val = arg;
		rule.type = AKEEP;
	}
	else if ((rule.type & ACALC) && arg == RCALC)
	{
		rule.val = arg;
		rule.type = ACALC;
	}
	else if ((rule.type & AADJUST) && arg == RADJUST)
	{
		rule.val = arg;
		rule.type = AADJUST;
	}
	else if (rule.type & AVALUE)
	{
		unsigned value = -1;

		value = std::stoul(arg, nullptr, 16);

		if (value == ~0u)
			throw std::runtime_error("No conversion performed");

		std::stringstream ss;
		std::string sValue = "0x";

		ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << value;
		ss >> sValue;

		rule.val = sValue;
		rule.type = AVALUE;
	}
	else
	{
		std::cout << DERROR << "Unable to parse argument " << arg << " for rule '" << name << "'\n";
		std::cout << DINDENT << name << " must be one of the following:\n";

		for (unsigned i = 0; i < 6; i++)
		{
			unsigned bit = 1 << i;

			if (rule.type & bit)
				std::cout << "\t- " << typeNames[i] << '\n';
		}

		throw std::runtime_error("TODO: fix this error message");
	}
}

int pack(const fs::path& buildRulePath, const fs::path& ndsOutputPath)
{
	if (!fs::exists(buildRulePath) || !fs::is_regular_file(buildRulePath))
	{
		std::cout << DERROR << "Build rule file " << buildRulePath << " is not a valid file\n";
		return -1;
	}

	std::ifstream buildRuleFile;
	buildRuleFile.open(buildRulePath);

	if (!buildRuleFile.is_open())
	{
		std::cout << DERROR << "Failed to open file " << buildRulePath << '\n';
		return -1;
	}

	std::cout << DINFO << "Reading build rules from " << buildRulePath << '\n';

	std::unordered_map<std::string, RuleParams> rules;
	rules["header"]        = RuleParams{REMPTY, AFILE};
	rules["arm9_entry"]    = RuleParams{RKEEP, AVALUE | AKEEP};
	rules["arm9_load"]     = RuleParams{RKEEP, AVALUE | AKEEP};
	rules["arm7_entry"]    = RuleParams{RKEEP, AVALUE | AKEEP};
	rules["arm7_load"]     = RuleParams{RKEEP, AVALUE | AKEEP};
	rules["fnt"]           = RuleParams{REMPTY, AFILE};
	rules["file_mode"]     = RuleParams{RADJUST, AKEEP | AADJUST | ACALC};
	rules["arm9"]          = RuleParams{REMPTY, AFILE};
	rules["arm7"]          = RuleParams{REMPTY, AFILE};
	rules["arm9ovt"]       = RuleParams{REMPTY, AFILE};
	rules["arm7ovt"]       = RuleParams{REMPTY, AFILE};
	rules["icon"]          = RuleParams{REMPTY, AFILE};
	rules["rsa_sig"]       = RuleParams{REMPTY, AFILE};
	rules["data"]          = RuleParams{REMPTY, ADIR};
	rules["ovt_repl_flag"] = RuleParams{REMPTY, AVALUE};
	rules["ov9"]           = RuleParams{REMPTY, ADIR};
	rules["ov7"]           = RuleParams{REMPTY, ADIR};

	while (buildRuleFile.good())
	{
		std::string line, name, arg;
		std::stringstream stream;

		std::getline(buildRuleFile, line);
		stream << line;
		stream >> name >> arg;

		if (!name.empty() && !arg.empty())
			processBuildRule(rules, buildRulePath.parent_path(), name, arg);
	}

	buildRuleFile.close();

	for (auto& rule : rules)
	{	
		if (rule.second.val == REMPTY)
		{
			std::cout << DERROR << "Missing value for rule '" << rule.first << "'\n";
			return -1;
		}
	}

	std::cout << DINFO << "Starting rebuild with the following parameters:\n";

	for (auto& rule : rules)
		std::cout << DINDENT << "\t- " << rule.first << ": " << rule.second.val << '\n';

	std::vector<unsigned char> rom;
	NDSDirectory rootDir;

	std::ifstream fileStream;
	std::map<unsigned, OverlayEntry> ov7Entries;
	std::map<unsigned, OverlayEntry> ov9Entries;

	unsigned short freeOvFileID = 0;
	unsigned short freeFileID = 0;
	unsigned char ovUpdateID = static_cast<unsigned char>(std::stoul(rules["ovt_repl_flag"].val, nullptr, 16));
	unsigned ovt9Offset, ovt7Offset, arm9Offset, arm7Offset, fntOffset, iconOffset, fatOffset;
	unsigned romHeaderSize, fntSize, ovt7Size, ovt9Size, fatSize, arm7Size, arm9Size, romOffset, iconSize, rsaSize, dataSize;

	fs::path romHeaderPath(rules["header"].val);
	fs::path fntPath(rules["fnt"].val);
	fs::path rootPath(rules["data"].val);
	fs::path ovt7Path(rules["arm7ovt"].val);
	fs::path ovt9Path(rules["arm9ovt"].val);
	fs::path arm7Path(rules["arm7"].val);
	fs::path arm9Path(rules["arm9"].val);
	fs::path ov7Path(rules["ov7"].val);
	fs::path ov9Path(rules["ov9"].val);
	fs::path iconPath(rules["icon"].val);
	fs::path rsaPath(rules["rsa_sig"].val);

	std::cout << DINFO << "Reading ROM header\n";

	romHeaderSize = fs::file_size(romHeaderPath);

	if (romHeaderSize != 0x200 && romHeaderSize != 0x4000)
	{
		std::cout << DERROR << "Invalid size of ROM header: Must be 0x200 or 0x400\n";
		return -1;
	}

	fileStream.open(romHeaderPath, std::ios::binary | std::ios::in);

	FSTREAM_OPEN_CHECK(romHeader)

	auto romHeader = std::make_unique<unsigned char[]>(0x4000);
	fileStream.read(reinterpret_cast<char*>(romHeader.get()), romHeaderSize);
	fileStream.close();

	if (romHeaderSize == 0x200)
		std::fill(romHeader.get() + 0x200, romHeader.get() + 0x4000, 0);

	if (romHeader[20] > 13)
	{
		std::cout << DERROR << "Final ROM size in header exceeds 1GB\n";
		return -1;
	}

	std::cout << DINFO << "Creating ROM from ROM header\n";

	rom.resize(0x20000 << romHeader[20], 0xFF);
	romOffset = 0;
	std::memcpy(rom.data(), romHeader.get(), 0x4000);
	romOffset += 0x4000;

	std::cout << DINFO << "Adding ARM9 binary " << arm9Path << '\n';

	arm9Size = fs::file_size(arm9Path);
	FILESIZE_CHECK(arm9, 0x3BFE00)

	fileStream.open(arm9Path, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(arm9)
	romCheckBounds(rom, romOffset, arm9Size);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm9Size);
	fileStream.close();

	arm9Offset = romOffset;
	romOffset += arm9Size;
	romOffset = std::max(0x8000U, romOffset);

	std::cout << DINFO << "Adding ARM9 Overlay Table " << ovt9Path << '\n';

	ovt9Size = fs::file_size(ovt9Path);
	FILESIZE_CHECK(ovt9, oneGB)

	if (ovt9Size % 0x20)
	{
		std::cout << DERROR << "File " << ovt9Path << " does not represent a valid ARM9 Overlay Table: Entries must have a size of 0x20 bytes\n";
		return -1;
	}

	if (ovt9Size)
		romOffset = alignAddress(romOffset, 16);
	else
		romOffset = alignAddress(romOffset, 4);

	romCheckBounds(rom, romOffset, 4);
	ovt9Offset = romOffset;

	if (ovt9Size)
	{
		fileStream.open(ovt9Path, std::ios::binary | std::ios::in);
		FSTREAM_OPEN_CHECK(ovt9)
		romCheckBounds(rom, romOffset, ovt9Size);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt9Offset]), ovt9Size);
		fileStream.close();

		for (unsigned i = 0; i < ovt9Size / 32; i++)
		{
			OverlayEntry e = { 0, 0, -1 };

			if (rom[ovt9Offset + i * 32 + 31] != ovUpdateID)
			{
				unsigned short fid = *reinterpret_cast<unsigned short*>(&rom[ovt9Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;
			}
			
			ov9Entries[*reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32])] = e;
		}
	}

	romOffset += ovt9Size;

	std::cout << DINFO << "Adding ARM9 Overlay files\n";

	for (const auto& e : ov9Entries)
	{
		unsigned ovID = e.first;
		fs::path dataPath = ov9Path / std::to_string(ovID);
		dataPath += ".bin";

		if (fs::exists(dataPath) && fs::is_regular_file(dataPath))
		{
			dataSize = fs::file_size(dataPath);
			FILESIZE_CHECK(data, oneGB)

			fileStream.open(dataPath, std::ios::binary | std::ios::in);
			FSTREAM_OPEN_CHECK(data)
			romCheckBounds(rom, romOffset, dataSize);
			fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), dataSize);
			fileStream.close();

			ov9Entries[ovID].start = romOffset;
			ov9Entries[ovID].end = romOffset + dataSize;

			romOffset += dataSize;

			V_PRINT("Added " << dataPath)
		}
		else
		{
			std::cout << DERROR << "Could not find ARM9 Overlay file " << ovID << ": Filename must be the overlay ID in decimal\n";
			return -1;
		}
	}

	romOffset = alignAddress(romOffset, 512);

	std::cout << DINFO << "Adding ARM7 binary " << arm7Path << '\n';

	arm7Size = fs::file_size(arm7Path);
	FILESIZE_CHECK(arm7, 0x3BFE00)

	fileStream.open(arm7Path, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(arm7)
	romCheckBounds(rom, romOffset, arm7Size);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm7Size);
	fileStream.close();

	arm7Offset = romOffset;
	romOffset += arm7Size;
	romOffset = alignAddress(romOffset, 4);

	std::cout << DINFO << "Adding ARM7 Overlay Table " << ovt7Path << '\n';

	ovt7Size = fs::file_size(ovt7Path);
	FILESIZE_CHECK(ovt7, oneGB)

	if (ovt7Size % 0x20)
	{
		std::cout << DERROR << "File " << ovt7Path << " does not represent a valid ARM7 Overlay Table: Entries must have a size of 0x20 bytes\n";
		return -1;
	}

	if (ovt7Size)
		romOffset = alignAddress(romOffset, 16);
	else
		romOffset = alignAddress(romOffset, 4);

	romCheckBounds(rom, romOffset, 4);
	ovt7Offset = romOffset;

	if (ovt7Size)
	{
		fileStream.open(ovt7Path, std::ios::binary | std::ios::in);
		FSTREAM_OPEN_CHECK(ovt7)
		romCheckBounds(rom, romOffset, ovt7Size);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt7Offset]), ovt7Size);
		fileStream.close();

		for (unsigned i = 0; i < ovt7Size / 32; i++)
		{
			OverlayEntry e = { 0, 0, -1 };

			if (rom[ovt7Offset + i * 32 + 31] != ovUpdateID)
			{
				unsigned short fid = *reinterpret_cast<unsigned short*>(&rom[ovt7Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;
			}

			ov7Entries[*reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32])] = e;
		}
	}

	romOffset += ovt7Size;

	std::cout << DINFO << "Adding ARM7 Overlay files\n";

	for (const auto& e : ov7Entries)
	{
		unsigned ovID = e.first;
		fs::path dataPath = ov7Path / std::to_string(ovID);
		dataPath += ".bin";

		if (fs::exists(dataPath) && fs::is_regular_file(dataPath))
		{
			dataSize = fs::file_size(dataPath);
			FILESIZE_CHECK(data, oneGB)

			fileStream.open(dataPath, std::ios::binary | std::ios::in);
			FSTREAM_OPEN_CHECK(data)
			romCheckBounds(rom, romOffset, dataSize);
			fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), dataSize);
			fileStream.close();

			ov7Entries[ovID].start = romOffset;
			ov7Entries[ovID].end = romOffset + dataSize;

			romOffset += dataSize;

			V_PRINT("Added " << dataPath)
		}
		else
		{
			std::cout << DERROR << "Could not find ARM7 Overlay file " << ovID << ": Filename must be the overlay ID in decimal\n";
			return -1;
		}
	}

	romOffset = alignAddress(romOffset, 4);

	std::cout << DINFO << "Entering file mode " << rules["file_mode"].val << '\n';

	switch (rules["file_mode"].type)
	{
	case AADJUST:
		{
			std::cout << DINFO << "Reading File Name Table " << fntPath << '\n';

			fntSize = fs::file_size(fntPath);
			FILESIZE_CHECK(fnt, oneGB)

			fileStream.open(fntPath, std::ios::binary | std::ios::in);
			FSTREAM_OPEN_CHECK(fnt)
			romCheckBounds(rom, romOffset, fntSize);
			fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fntSize);
			fileStream.close();

			std::cout << DINFO << "Building FNT directory tree\n";

			rootDir = buildFntTree(&rom[romOffset], 0xF000, fntSize);
			freeFileID = std::max(freeOvFileID, fntFindNextFreeFileID(rootDir));

			unsigned short freeDirID = fntFindNextFreeDirID(rootDir);

			std::cout << DINFO << "Assigning file IDs to Overlays\n";

			for (unsigned i = 0; i < ovt9Size / 32; i++)
			{
				if (rom[ovt9Offset + i * 32 + 31] == ovUpdateID)
				{
					unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
					rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
					rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
					rom[ovt9Offset + i * 32 + 26] = 0;
					rom[ovt9Offset + i * 32 + 27] = 0;
					rom[ovt9Offset + i * 32 + 31] = 3;
					ov9Entries[ovID].fileID = freeFileID;
					std::cout << DINFO << "ARM9 Overlay " << ovID << " obtained file ID " << freeFileID << '\n';
					freeFileID++;
				}
			}

			for (unsigned i = 0; i < ovt7Size / 32; i++)
			{
				if (rom[ovt7Offset + i * 32 + 31] == ovUpdateID)
				{
					unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
					rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
					rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
					rom[ovt7Offset + i * 32 + 26] = 0;
					rom[ovt7Offset + i * 32 + 27] = 0;
					rom[ovt7Offset + i * 32 + 31] = 3;
					ov7Entries[ovID].fileID = freeFileID;
					std::cout << DINFO << "ARM7 Overlay " << ovID << " obtained file ID " << freeFileID << '\n';
					freeFileID++;
				}
			}

			fntAddNewFiles(rootDir, rootPath, freeFileID, freeDirID);
			fntRebuild(rom, romOffset, rootDir, fntSize);
			fntOffset = romOffset;

			romOffset += fntSize;
			romOffset = alignAddress(romOffset, 4);

			fntPrintDirs(rootDir, rootPath.string());
		}
		break;

	case ACALC:
		{
			unsigned short freeDirID = fntFindNextFreeDirID(rootDir);
			freeFileID = freeOvFileID;

			std::cout << DINFO << "Assigning file IDs to Overlays\n";

			for (unsigned i = 0; i < ovt9Size / 32; i++)
			{
				if (rom[ovt9Offset + i * 32 + 31] == ovUpdateID)
				{
					unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
					rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
					rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
					rom[ovt9Offset + i * 32 + 26] = 0;
					rom[ovt9Offset + i * 32 + 27] = 0;
					rom[ovt9Offset + i * 32 + 31] = 3;
					ov9Entries[ovID].fileID = freeFileID;
					std::cout << DINFO << "ARM9 Overlay " << ovID << " obtained file ID " << freeFileID << '\n';
					freeFileID++;
				}
			}

			for (unsigned i = 0; i < ovt7Size / 32; i++)
			{
				if (rom[ovt7Offset + i * 32 + 31] == ovUpdateID)
				{
					unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
					rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
					rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
					rom[ovt7Offset + i * 32 + 26] = 0;
					rom[ovt7Offset + i * 32 + 27] = 0;
					rom[ovt7Offset + i * 32 + 31] = 3;
					ov7Entries[ovID].fileID = freeFileID;
					std::cout << DINFO << "ARM7 Overlay " << ovID << " obtained file ID " << freeFileID << '\n';
					freeFileID++;
				}
			}

			std::cout << DINFO << "Building FNT directory tree\n";

			fntGenRootDir(rootDir, rootPath, freeFileID);
			freeFileID += rootDir.files.size();
			fntAddNewFiles(rootDir, rootPath, freeFileID, freeDirID);

			fntOffset = romOffset;
			fntRebuild(rom, fntOffset, rootDir, fntSize);

			romOffset += fntSize;
			romOffset = alignAddress(romOffset, 4);

			fntPrintDirs(rootDir, rootPath.string());
		}
		break;

	case AKEEP:
		{
			std::cout << DINFO << "Reading File Name Table " << fntPath << '\n';

			fntSize = fs::file_size(fntPath);
			FILESIZE_CHECK(fnt, oneGB)

			fileStream.open(fntPath, std::ios::binary | std::ios::in);
			FSTREAM_OPEN_CHECK(fnt)
			romCheckBounds(rom, romOffset, fntSize);
			fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fntSize);
			fileStream.close();

			std::cout << DINFO << "Extracting FNT directory tree\n";

			rootDir = buildFntTree(&rom[romOffset], 0xF000, fntSize);
			freeFileID = std::max(freeOvFileID, fntFindNextFreeFileID(rootDir));
			fntOffset = romOffset;

			romOffset += fntSize;
			romOffset = alignAddress(romOffset, 4);

			std::cout << DINFO << "Assigning file IDs to Overlays\n";

			for (unsigned i = 0; i < ovt9Size / 32; i++)
			{
				if (rom[ovt9Offset + i * 32 + 31] == ovUpdateID)
				{
					unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
					rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
					rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
					rom[ovt9Offset + i * 32 + 26] = 0;
					rom[ovt9Offset + i * 32 + 27] = 0;
					rom[ovt9Offset + i * 32 + 31] = 3;
					ov9Entries[ovID].fileID = freeFileID;
					std::cout << DINFO << "ARM9 Overlay " << ovID << " obtained file ID " << freeFileID << '\n';
					freeFileID++;
				}
			}

			for (unsigned i = 0; i < ovt7Size / 32; i++)
			{
				if (rom[ovt7Offset + i * 32 + 31] == ovUpdateID)
				{
					unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
					rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
					rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
					rom[ovt7Offset + i * 32 + 26] = 0;
					rom[ovt7Offset + i * 32 + 27] = 0;
					rom[ovt7Offset + i * 32 + 31] = 3;
					ov7Entries[ovID].fileID = freeFileID;
					std::cout << DINFO << "ARM7 Overlay " << ovID << " obtained file ID " << freeFileID << '\n';
					freeFileID++;
				}
			}
			fntPrintDirs(rootDir, rootPath.string());
		}
		break;

	default:
		std::cout << DERROR << "Invalid file mode\n";
		return -1;
	}

	std::cout << DINFO << "Allocating File Allocation Table\n";

	fatSize = freeFileID * 8;
	romCheckBounds(rom, romOffset, fatSize);

	fatOffset = romOffset;
	std::memset(&rom[fatOffset], 0x00, fatSize);

	romOffset += fatSize;
	romOffset = alignAddress(romOffset, 512);

	std::cout << DINFO << "Linking Overlays to FAT\n";

	for (const auto& ov : ov9Entries)
	{
		const OverlayEntry& ov9e = ov.second;
		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);

		fatPtr[ov9e.fileID * 2] = ov9e.start;
		fatPtr[ov9e.fileID * 2 + 1] = ov9e.end;

		V_PRINT("Linked ARM9 Overlay " << ov.first << " with file ID " << ov9e.fileID << " to FAT")
	}

	for (const auto& ov : ov7Entries)
	{
		const OverlayEntry& ov7e = ov.second;
		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);

		fatPtr[ov7e.fileID * 2] = ov7e.start;
		fatPtr[ov7e.fileID * 2 + 1] = ov7e.end;

		V_PRINT("Linked ARM7 Overlay " << ov.first << " with file ID " << ov7e.fileID << " to FAT")
	}

	std::cout << DINFO << "Adding Icon / Title " << iconPath << '\n';

	iconSize = fs::file_size(iconPath);

	fileStream.open(iconPath, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(icon)

	unsigned short version;
	fileStream.read(reinterpret_cast<char*>(&version), 2);

	switch (version)
	{
	default:
		std::cout << DWARNING << "Invalid Icon / Title ID, defaulting to 0x840\n";
		[[fallthrough]];
	case 0x0001:
		FILESIZE_CHECK(icon, 0x0840)
		iconSize = 0x0840;
		break;
	case 0x0002:
		FILESIZE_CHECK(icon, 0x0940)
		iconSize = 0x0940;
		break;
	case 0x0003:
		FILESIZE_CHECK(icon, 0x0A40)
		iconSize = 0x0A40;
		break;
	case 0x0103:
		FILESIZE_CHECK(icon, 0x23C0)
		iconSize = 0x23C0;
		break;
	}

	fileStream.seekg(-2, std::ios::cur);

	romCheckBounds(rom, romOffset, iconSize);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), iconSize);
	fileStream.close();

	iconOffset = romOffset;

	romOffset += iconSize;
	romOffset = alignAddress(romOffset, 512);

	std::cout << DINFO << "Adding NitroROM filesystem\n";

	nfsAddAndLink(rom, fatOffset, rootDir, rootPath, romOffset);

	std::cout << DINFO << "Adding RSA signature " << rsaPath << '\n';

	rsaSize = fs::file_size(rsaPath);
	
	if (rsaSize != 0x88)
	{
		std::cout << DERROR << "Invalid RSA signature size: Expected 136 bytes, got " << rsaSize << '\n';
		return -1;
	}
	
	fileStream.open(rsaPath, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(rsa)
	romCheckBounds(rom, romOffset, rsaSize);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), rsaSize);
	fileStream.close();

	std::cout << DINFO << "Done building ROM\n";
	std::cout << DINFO << "Fixing ROM header\n";

	unsigned* urom = reinterpret_cast<unsigned*>(rom.data());
	urom[8] = arm9Offset;
	urom[11] = arm9Size;
	urom[12] = arm7Offset;
	urom[15] = arm7Size;
	urom[16] = fntOffset;
	urom[17] = fntSize;
	urom[18] = fatOffset;
	urom[19] = fatSize;
	urom[20] = ovt9Size ? ovt9Offset : 0;
	urom[21] = ovt9Size;
	urom[22] = ovt7Size ? ovt7Offset : 0;
	urom[23] = ovt7Size;
	urom[26] = iconOffset;
	urom[32] = romOffset;
	urom[1024] = urom[32];

	if (rules["arm9_entry"].type == AVALUE)
		urom[9] = std::stoul(rules["arm9_entry"].val, nullptr, 16);

	if (rules["arm9_load"].type == AVALUE)
		urom[10] = std::stoul(rules["arm9_load"].val, nullptr, 16);

	if (rules["arm7_entry"].type == AVALUE)
		urom[13] = std::stoul(rules["arm7_entry"].val, nullptr, 16);

	if (rules["arm7_load"].type == AVALUE)
		urom[14] = std::stoul(rules["arm7_load"].val, nullptr, 16);

	rom[20] = static_cast<unsigned char>(std::log2(rom.size() >> 17));
	
	unsigned short* srom = reinterpret_cast<unsigned short*>(rom.data());
	srom[175] = crc16(rom.data(), 350);

	std::cout << DINFO << "Writing " << ndsOutputPath << '\n';

	std::ofstream outputStream(ndsOutputPath, std::ios::binary | std::ios::out);
	
	if (!outputStream.is_open())
	{
		std::cout << DERROR << "Failed to create output file " << ndsOutputPath << '\n';
		return -1;
	}

	outputStream.write(reinterpret_cast<const char*>(rom.data()), rom.size());
	outputStream.close();

	std::cout << DINFO << "Successfully written NDS image " << ndsOutputPath.filename() << '\n';

	return 0;
}
