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
#include "blz.hpp"

namespace fs = std::filesystem;

static void checkFileSize(const fs::path& path, std::size_t size, std::size_t maxSize)
{
	if (size > maxSize)
	{
		std::stringstream s;
		s << "error: the size of " << path.native() << " exceeds " << maxSize << " bytes";
		throw std::length_error(s.view().data());
	}
}

struct OverlayEntry
{
	unsigned start;
	unsigned end;
	unsigned short fileID;
};

static void romCheckBounds(std::vector<unsigned char>& rom, unsigned requiredSize)
{
	if (oneGB < requiredSize)
		throw std::length_error("error: ROM trying to grow larger than 1 GB");

	if (rom.size() < requiredSize)
		rom.resize(requiredSize, 0xff);
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

static unsigned short fntFindNextFreeFileID(const NDSDirectory& dir)
{
	unsigned short fileFree = dir.firstFileID + dir.files.size();

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		fileFree = std::max(fileFree, fntFindNextFreeFileID(dir.dirs[i]));

	return fileFree;
}

static unsigned short fntFindNextFreeDirID(const NDSDirectory& dir)
{
	unsigned short dirFree = dir.directoryID + 1;
	
	for (unsigned i = 0; i < dir.dirs.size(); i++)
		dirFree = std::max(dirFree, fntFindNextFreeDirID(dir.dirs[i]));

	return dirFree;
}

static unsigned fntDirectoryIndex(NDSDirectory& parent, const std::string& dataDir)
{
	for (unsigned i = 0; i < parent.dirs.size(); i++)
		if (dataDir == parent.dirs[i].dirName)
			return i;

	return ~0u;
}

static bool fntAddNewFiles(
	NDSDirectory& ndsDir,
	const fs::path& dataDir,
	unsigned short& freeFileID,
	unsigned short& freeDirID,
	bool newDir
)
{
	bool modified = false;

	for (const fs::path& p : fs::directory_iterator(dataDir))
	{
		if (!fs::is_directory(p))
		{
			if (newDir) continue;

			throw std::runtime_error(
				"error: new file " + p.native()
				+ " is not in a new directory"
			);
		}

		unsigned i = fntDirectoryIndex(ndsDir, p.filename().string());

		if (i != ~0u) // if the directory already exists in the fnt
		{
			modified = fntAddNewFiles(ndsDir.dirs[i], p, freeFileID, freeDirID, newDir) || modified;

			continue;
		}

		NDSDirectory& dir = ndsDir.dirs.emplace_back();
		dir.firstFileID = freeFileID;
		dir.directoryID = freeDirID;
		dir.dirName = p.filename().string();
		
		for (const fs::path& sp : fs::directory_iterator(p))
		{
			if (fs::is_regular_file(sp))
			{
				std::cout << DINFO << "File " << sp << " obtained File ID ";
				std::cout << (dir.firstFileID + dir.files.size()) << '\n';
				dir.files.push_back(sp.filename().string());
			}
		}

		freeFileID += dir.files.size();
		freeDirID++;
		modified = true;	

		fntAddNewFiles(dir, p, freeFileID, freeDirID, true);
	}

	return modified;
}

static unsigned fntDirectoryCount(const NDSDirectory& dir)
{
	unsigned count = dir.dirs.size();

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		count += fntDirectoryCount(dir.dirs[i]);

	return count;
}

static unsigned fntByteCountFn(const NDSDirectory& dir)
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

static unsigned fntByteCountHeader(const NDSDirectory& root)
{
	return (fntDirectoryCount(root) + 1) * 8;
}

static unsigned fntWriteDirectory(const NDSDirectory& dir, unsigned char* fnt, unsigned offset, unsigned parentID)
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

static void fntRebuild(std::vector<unsigned char>& rom, unsigned fntOffset, const NDSDirectory& root, unsigned& size)
{
	unsigned fntHeaderSize = fntByteCountHeader(root);
	unsigned fntFnSize = fntByteCountFn(root);
	size = fntHeaderSize + fntFnSize;

	romCheckBounds(rom, fntOffset + size);
	fntWriteDirectory(root, &rom[fntOffset], fntHeaderSize, fntHeaderSize / 8);
}

static unsigned alignAddress(unsigned address, unsigned align)
{
	return ((address + align - 1) & ~(align - 1));
}

static void openInputFile(std::ifstream& fileStream, const fs::path& path)
{
	fileStream.open(path, std::ios::binary | std::ios::in);

	if (!fileStream.is_open())
		throw std::runtime_error("error: failed to open file " + path.native());
}

static fs::path findInputFile(const fs::path& path)
{
	const fs::path modifiedUncompressedPath = "modified" / ("uncompressed" / path);

	if (fs::is_regular_file(modifiedUncompressedPath))
		throw std::runtime_error("error: compression is only supported for overlay, not for " + path.native());

	const fs::path modifiedFinalPath = "modified" / ("final" / path);

	if (fs::is_regular_file(modifiedFinalPath))
		return modifiedFinalPath;

	const fs::path cleanRawPath = "clean" / ("raw" / path);

	if (fs::is_regular_file(cleanRawPath))
		return cleanRawPath;

	throw std::runtime_error("error: could not find file: " + path.native());
}

static void writeOverlay(
	std::vector<unsigned char>& rom,
	unsigned ovID,
	OverlayEntry& entry,
	const fs::path& dir,
	unsigned& romOffset
)
{
	const fs::path path = dir / (std::to_string(ovID) + ".bin");
	const fs::path uncompressedPath = "modified" / ("uncompressed" / path);
	const fs::path finalPath        = "modified" / ("final" / path);

	const bool uncompressedExists = fs::is_regular_file(uncompressedPath);
	const bool finalExists        = fs::is_regular_file(finalPath);

	unsigned size;
	std::ifstream fileStream;

	if (uncompressedExists
		&& (!finalExists || fs::last_write_time(finalPath) < fs::last_write_time(uncompressedPath)))
	{
		const unsigned uncompressedSize = fs::file_size(uncompressedPath);
		std::vector<unsigned char> uncompressedData(uncompressedSize);

		openInputFile(fileStream, uncompressedPath);
		fileStream.read(reinterpret_cast<char*>(uncompressedData.data()), uncompressedSize);

		const auto compressedData = BLZ::compress(uncompressedData);
		size = compressedData.size();

		std::ofstream compressedFile(finalPath, std::ios::binary | std::ios::out);

		if (!compressedFile.is_open())
			throw std::runtime_error("error: failed to open file " + finalPath.native());

		compressedFile.write(reinterpret_cast<const char*>(compressedData.data()), size);

		std::cout << DINFO << "Compressed " << uncompressedPath << " -> " << finalPath << '\n';
		std::cout << DINFO << "Replacing overlay " << ovID << " with " << finalPath << '\n';

		romCheckBounds(rom, romOffset + size);
		std::memcpy(&rom[romOffset], compressedData.data(), size);
	}
	else if (finalExists)
	{
		std::cout << DINFO << "Replacing overlay " << ovID << " with " << finalPath << '\n';

		size = fs::file_size(finalPath);
		romCheckBounds(rom, romOffset + size);
		openInputFile(fileStream, finalPath);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), size);
	}
	else
	{
		const fs::path cleanPath = "clean" / ("raw" / path);

		if (!fs::is_regular_file(cleanPath))
			throw std::runtime_error("error: could not find overlay file: " + path.native());

		size = fs::file_size(cleanPath);
		romCheckBounds(rom, romOffset + size);
		openInputFile(fileStream, cleanPath);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), size);
	}

	entry.start = romOffset;
	entry.end = romOffset + size;
	romOffset += size;
}

static void nfsAddAndLink(std::vector<unsigned char>& rom, unsigned fatOffset, const NDSDirectory& dir, const fs::path& p, unsigned& romOffset)
{
	unsigned short dirFileID = dir.firstFileID;

	for (unsigned i = 0; i < dir.files.size(); i++)
	{
		fs::path filePath = findInputFile(p / dir.files[i]);
		unsigned fileSize = fs::file_size(filePath);

		if (fileSize > oneGB)
		{
			std::cout << DWARNING << "File size of " << filePath << " with " << fileSize << " bytes exceeds 1GB, skipping\n";
			dirFileID++;
			continue;
		}

		std::ifstream fileStream;
		openInputFile(fileStream, filePath);

		romCheckBounds(rom, romOffset + fileSize);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fileSize);
		fileStream.close();

		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);
		fatPtr[dirFileID * 2] = romOffset;
		fatPtr[dirFileID * 2 + 1] = romOffset + fileSize;

		romOffset += fileSize;
		romOffset = alignAddress(romOffset, 4);
		dirFileID++;
	}

	for (unsigned i = 0; i < dir.dirs.size(); i++)
		nfsAddAndLink(rom, fatOffset, dir.dirs[i], p / dir.dirs[i].dirName, romOffset);
}

struct Config
{
	static constexpr unsigned keep = ~0u;

	fs::path outputPath;
	unsigned char ovtReplFlag = 0xff;
	unsigned arm9Entry = keep;
	unsigned arm9Load  = keep;
	unsigned arm7Entry = keep;
	unsigned arm7Load  = keep;

	Config(const fs::path& path):
		outputPath(path)
	{
		const fs::path configPath = ".neondst";

		if (!fs::is_regular_file(configPath))
			return;

		std::ifstream configFile(configPath);

		if (!configFile.is_open())
			throw std::runtime_error("error: failed to open file " + configPath.native());

		while (configFile.good())
		{
			std::string line, first;
			std::stringstream s;
			std::getline(configFile, line);

			if (line.empty()) continue;

			s << line;
			s >> first;

			if (first.starts_with('#')) continue;

			auto sv = s.view();
			sv.remove_prefix(first.size());

			auto it = std::ranges::find_if_not(sv, static_cast<int(&)(int)>(std::isspace));
			sv.remove_prefix(std::distance(sv.begin(), it));

			if (first == "output")
			{
				if (path.empty())
					outputPath = sv;

				continue;
			}

			unsigned val;
			try
			{
				val = std::stoul(std::string(sv), nullptr, 16);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error("invalid value for '" + first + "'");
			}

			if (first == "ovt_repl_flag") ovtReplFlag = static_cast<unsigned char>(val);
			else if (first == "arm9_entry") arm9Entry = val;
			else if (first == "arm9_load" ) arm9Load  = val;
			else if (first == "arm7_entry") arm7Entry = val;
			else if (first == "arm7_load" ) arm7Load  = val;
		}
	}

	void print() const
	{
		std::cout << "Building with the following configuration:\n";
		std::cout << std::hex;

		auto f = [](const char* s, auto val)
		{
			std::cout << '\t' << s;

			if (val == keep)
				std::cout << "keep\n";
			else
			{
				std::cout << std::setw(2*sizeof(val)) << std::setfill('0');
				std::cout << static_cast<unsigned>(val) << '\n';
			}
		};

		f("arm9_entry: ", arm9Entry);
		f("arm9_load:  ", arm9Load);
		f("arm7_entry: ", arm7Entry);
		f("arm7_load:  ", arm7Load);
		f("ovt_repl_flag: ", ovtReplFlag);

		std::cout << std::dec;
	}
};

void pack(const fs::path& outputPath)
{
	Config config(outputPath);
	config.print();

	if (config.outputPath.empty())
		throw std::invalid_argument("error: no output file given");

	std::vector<unsigned char> rom;
	NDSDirectory rootDir;

	std::ifstream fileStream;
	std::map<unsigned, OverlayEntry> ov7Entries;
	std::map<unsigned, OverlayEntry> ov9Entries;

	unsigned short freeOvFileID = 0;
	unsigned short freeFileID = 0;
	unsigned ovt9Offset, ovt7Offset, arm9Offset, arm7Offset, fntOffset, iconOffset, fatOffset;
	unsigned romHeaderSize, fntSize, ovt7Size, ovt9Size, fatSize, arm7Size, arm9Size, romOffset, iconSize, rsaSize;

	fs::path romHeaderPath = findInputFile("header.bin");
	fs::path fntPath       = findInputFile("fnt.bin");
	fs::path ovt7Path      = findInputFile("arm7ovt.bin");
	fs::path ovt9Path      = findInputFile("arm9ovt.bin");
	fs::path arm7Path      = findInputFile("arm7.bin");
	fs::path arm9Path      = findInputFile("arm9.bin");
	fs::path iconPath      = findInputFile("banner.bin");
	fs::path rsaPath       = findInputFile("rsasig.bin");

	std::cout << DINFO << "Reading ROM header\n";

	romHeaderSize = fs::file_size(romHeaderPath);

	if (romHeaderSize != 0x200 && romHeaderSize != 0x4000)
		throw std::length_error("invalid size of ROM header: must be 0x200 or 0x4000");

	openInputFile(fileStream, romHeaderPath);

	auto romHeader = std::make_unique<unsigned char[]>(0x4000);
	fileStream.read(reinterpret_cast<char*>(romHeader.get()), romHeaderSize);
	fileStream.close();

	if (romHeaderSize == 0x200)
		std::fill(romHeader.get() + 0x200, romHeader.get() + 0x4000, 0);

	if (romHeader[20] > 13)
		throw std::length_error("error: final ROM size in header exceeds 1 GB\n");

	rom.resize(0x20000 << romHeader[20], 0xFF);
	romOffset = 0;
	std::memcpy(rom.data(), romHeader.get(), 0x4000);
	romOffset += 0x4000;

	std::cout << DINFO << "Adding ARM9 binary " << arm9Path << '\n';

	arm9Size = fs::file_size(arm9Path);
	checkFileSize(arm9Path, arm9Size, 0x3bfe00);

	openInputFile(fileStream, arm9Path);
	romCheckBounds(rom, romOffset + arm9Size);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm9Size);
	fileStream.close();

	arm9Offset = romOffset;
	romOffset += arm9Size;
	romOffset = std::max(0x8000U, romOffset);

	std::cout << DINFO << "Adding ARM9 overlay table " << ovt9Path << '\n';

	ovt9Size = fs::file_size(ovt9Path);
	checkFileSize(ovt9Path, ovt9Size, oneGB);

	if (ovt9Size % 0x20)
	{
		throw std::length_error(
			"invalid ARM9 overlay table: " + ovt9Path.native()
			+ " (each entry must be 0x20 bytes)"
		);
	}

	if (ovt9Size)
		romOffset = alignAddress(romOffset, 16);
	else
		romOffset = alignAddress(romOffset, 4);

	romCheckBounds(rom, romOffset + 4);
	ovt9Offset = romOffset;

	if (ovt9Size)
	{
		openInputFile(fileStream, ovt9Path);
		romCheckBounds(rom, romOffset + ovt9Size);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt9Offset]), ovt9Size);
		fileStream.close();

		for (unsigned i = 0; i < ovt9Size / 32; i++)
		{
			OverlayEntry e = { 0, 0, -1 };

			if (rom[ovt9Offset + i * 32 + 31] != config.ovtReplFlag)
			{
				unsigned short fid = *reinterpret_cast<unsigned short*>(&rom[ovt9Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;
			}
			
			ov9Entries[*reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32])] = e;
		}
	}

	romOffset += ovt9Size;

	std::cout << DINFO << "Adding ARM9 overlay files\n";

	for (auto& e : ov9Entries)
		writeOverlay(rom, e.first, e.second, "overlay9", romOffset);

	romOffset = alignAddress(romOffset, 512);

	std::cout << DINFO << "Adding ARM7 binary " << arm7Path << '\n';

	arm7Size = fs::file_size(arm7Path);
	checkFileSize(arm7Path, arm7Size, 0x3bfe00);

	openInputFile(fileStream, arm7Path);
	romCheckBounds(rom, romOffset + arm7Size);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm7Size);
	fileStream.close();

	arm7Offset = romOffset;
	romOffset += arm7Size;
	romOffset = alignAddress(romOffset, 4);

	std::cout << DINFO << "Adding ARM7 overlay table " << ovt7Path << '\n';

	ovt7Size = fs::file_size(ovt7Path);
	checkFileSize(ovt7Path, ovt7Size, oneGB);

	if (ovt7Size % 0x20)
	{
		throw std::length_error(
			"invalid ARM7 overlay table: " + ovt7Path.native()
			+ " (each entry must be 0x20 bytes)"
		);
	}

	if (ovt7Size)
		romOffset = alignAddress(romOffset, 16);
	else
		romOffset = alignAddress(romOffset, 4);

	romCheckBounds(rom, romOffset + 4);
	ovt7Offset = romOffset;

	if (ovt7Size)
	{
		openInputFile(fileStream, ovt7Path);
		romCheckBounds(rom, romOffset + ovt7Size);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt7Offset]), ovt7Size);
		fileStream.close();

		for (unsigned i = 0; i < ovt7Size / 32; i++)
		{
			OverlayEntry e = { 0, 0, -1 };

			if (rom[ovt7Offset + i * 32 + 31] != config.ovtReplFlag)
			{
				unsigned short fid = *reinterpret_cast<unsigned short*>(&rom[ovt7Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;
			}

			ov7Entries[*reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32])] = e;
		}
	}

	romOffset += ovt7Size;

	std::cout << DINFO << "Adding ARM7 overlay files\n";

	for (auto& e : ov7Entries)
		writeOverlay(rom, e.first, e.second, "overlay7", romOffset);

	romOffset = alignAddress(romOffset, 4);

	std::cout << DINFO << "Reading FNT " << fntPath << '\n';

	fntSize = fs::file_size(fntPath);
	checkFileSize(fntPath, fntSize, oneGB);

	openInputFile(fileStream, fntPath);
	romCheckBounds(rom, romOffset + fntSize);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fntSize);
	fileStream.close();

	std::cout << DINFO << "Extracting FNT directory tree\n";

	rootDir = buildFntTree(&rom[romOffset], 0xF000, fntSize);
	freeFileID = std::max(freeOvFileID, fntFindNextFreeFileID(rootDir));

	std::cout << DINFO << "Assigning file IDs to new overlays\n";

	for (unsigned i = 0; i < ovt9Size / 32; i++)
	{
		if (rom[ovt9Offset + i * 32 + 31] == config.ovtReplFlag)
		{
			unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
			rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
			rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
			rom[ovt9Offset + i * 32 + 26] = 0;
			rom[ovt9Offset + i * 32 + 27] = 0;
			rom[ovt9Offset + i * 32 + 31] = 3;
			ov9Entries[ovID].fileID = freeFileID;
			std::cout << DINFO << "ARM9 overlay " << ovID << " obtained file ID " << freeFileID << '\n';
			freeFileID++;
		}
	}

	for (unsigned i = 0; i < ovt7Size / 32; i++)
	{
		if (rom[ovt7Offset + i * 32 + 31] == config.ovtReplFlag)
		{
			unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
			rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
			rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
			rom[ovt7Offset + i * 32 + 26] = 0;
			rom[ovt7Offset + i * 32 + 27] = 0;
			rom[ovt7Offset + i * 32 + 31] = 3;
			ov7Entries[ovID].fileID = freeFileID;
			std::cout << DINFO << "ARM7 overlay " << ovID << " obtained file ID " << freeFileID << '\n';
			freeFileID++;
		}
	}

	unsigned short freeDirID = fntFindNextFreeDirID(rootDir);

	if (const fs::path rootPath = fs::path("modified")/"final"/"root";
		fs::is_directory(rootPath)
		&& fntAddNewFiles(rootDir, rootPath, freeFileID, freeDirID, false))
	{
		std::cout << DINFO << "Rebuilding FNT\n";

		fntRebuild(rom, romOffset, rootDir, fntSize);
	}
	else
		std::cout << DINFO << "Keeping the original FNT\n";

	fntOffset = romOffset;
	romOffset += fntSize;
	romOffset = alignAddress(romOffset, 4);

	std::cout << DINFO << "Allocating FAT\n";

	fatSize = freeFileID * 8;
	romCheckBounds(rom, romOffset + fatSize);

	fatOffset = romOffset;
	std::memset(&rom[fatOffset], 0x00, fatSize);

	romOffset += fatSize;
	romOffset = alignAddress(romOffset, 512);

	std::cout << DINFO << "Linking overlays to FAT\n";

	for (const auto& ov : ov9Entries)
	{
		const OverlayEntry& ov9e = ov.second;
		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);

		fatPtr[ov9e.fileID * 2] = ov9e.start;
		fatPtr[ov9e.fileID * 2 + 1] = ov9e.end;
	}

	for (const auto& ov : ov7Entries)
	{
		const OverlayEntry& ov7e = ov.second;
		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);

		fatPtr[ov7e.fileID * 2] = ov7e.start;
		fatPtr[ov7e.fileID * 2 + 1] = ov7e.end;
	}

	std::cout << DINFO << "Adding icon / title " << iconPath << '\n';

	iconSize = fs::file_size(iconPath);

	openInputFile(fileStream, iconPath);

	unsigned short version;
	fileStream.read(reinterpret_cast<char*>(&version), 2);

	switch (version)
	{
	default:
		std::cout << DWARNING << "Invalid icon / title ID, defaulting to 0x840\n";
		[[fallthrough]];
	case 0x0001:
		checkFileSize(iconPath, iconSize, 0x0840);
		iconSize = 0x0840;
		break;
	case 0x0002:
		checkFileSize(iconPath, iconSize, 0x0940);
		iconSize = 0x0940;
		break;
	case 0x0003:
		checkFileSize(iconPath, iconSize, 0x0A40);
		iconSize = 0x0A40;
		break;
	case 0x0103:
		checkFileSize(iconPath, iconSize, 0x23C0);
		iconSize = 0x23C0;
		break;
	}

	fileStream.seekg(-2, std::ios::cur);

	romCheckBounds(rom, romOffset + iconSize);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), iconSize);
	fileStream.close();

	iconOffset = romOffset;

	romOffset += iconSize;
	romOffset = alignAddress(romOffset, 512);

	std::cout << DINFO << "Adding NitroROM filesystem\n";

	nfsAddAndLink(rom, fatOffset, rootDir, "root", romOffset);

	std::cout << DINFO << "Adding RSA signature " << rsaPath << '\n';

	rsaSize = fs::file_size(rsaPath);
	
	if (rsaSize != 0x88)
	{
		std::stringstream s;
		s << "invalid RSA signature size: expected 136 bytes, got ";
		s << rsaSize;

		throw std::length_error(s.view().data());
	}
	
	openInputFile(fileStream, rsaPath);
	romCheckBounds(rom, romOffset + rsaSize);
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

	if (config.arm9Entry != Config::keep) urom[ 9] = config.arm9Entry;
	if (config.arm9Load  != Config::keep) urom[10] = config.arm9Load;
	if (config.arm7Entry != Config::keep) urom[13] = config.arm7Entry;
	if (config.arm7Load  != Config::keep) urom[14] = config.arm7Load;

	rom[20] = static_cast<unsigned char>(std::log2(rom.size() >> 17));
	
	unsigned short* srom = reinterpret_cast<unsigned short*>(rom.data());
	srom[175] = crc16(rom.data(), 350);

	std::cout << DINFO << "Writing " << config.outputPath << '\n';

	std::ofstream outputStream(config.outputPath, std::ios::binary | std::ios::out);
	
	if (!outputStream.is_open())
		throw std::runtime_error("error: failed to create file " + config.outputPath.native());

	outputStream.write(reinterpret_cast<const char*>(rom.data()), rom.size());
	outputStream.close();

	std::cout << DINFO << "Successfully written NDS image " << config.outputPath << '\n';
}
