#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <map>
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
		s << "the size of " << path.native() << " exceeds " << maxSize << " bytes";
		throw std::length_error(s.view().data());
	}
}

struct OverlayEntry
{
	u32 start;
	u32 end;
	u16 fileID;
};

static void romCheckBounds(std::vector<u8>& rom, u32 requiredSize, u8 padding)
{
	if (oneGB < requiredSize)
		throw std::length_error("ROM trying to grow larger than 1 GB");

	if (rom.size() < requiredSize)
		rom.resize(requiredSize, padding);
}

NDSDirectory buildFntTree(u8* fnt, u32 dirID, u32 fntSize)
{
	NDSDirectory dir;
	u32 dirOffset = (dirID & 0xFFF) * 8;
	u32 subOffset = readU32(fnt + dirOffset);
	dir.firstFileID = readU16(fnt + dirOffset + 4);
	dir.directoryID = dirID;

	u32 relOffset = 0;
	u8 len = 0;
	std::string name;

	while (subOffset + relOffset < fntSize)
	{
		len = fnt[subOffset + relOffset];
		relOffset++;

		if (len == 0x80)
		{
			std::cout << WARNING "FNT identifier 0x80 detected (reserved), skipping dir node\n";
			break;
		}
		else if (len == 0)
			break;

		bool isSubdir = len & 0x80;
		len &= 0x7F;

		name = std::string(reinterpret_cast<const char*>(fnt + subOffset + relOffset), len);
		relOffset += len;

		if (isSubdir)
		{
			NDSDirectory subDir = buildFntTree(fnt, readU16(fnt + subOffset + relOffset), fntSize);
			subDir.dirName = name;
			dir.dirs.push_back(subDir);
			relOffset += 2;
		}
		else
			dir.files.push_back(name);
	}

	return dir;
}

static u16 fntFindNextFreeFileID(const NDSDirectory& dir)
{
	u16 fileFree = dir.firstFileID + dir.files.size();

	for (u32 i = 0; i < dir.dirs.size(); i++)
		fileFree = std::max(fileFree, fntFindNextFreeFileID(dir.dirs[i]));

	return fileFree;
}

static u16 fntFindNextFreeDirID(const NDSDirectory& dir)
{
	u16 dirFree = dir.directoryID + 1;
	
	for (u32 i = 0; i < dir.dirs.size(); i++)
		dirFree = std::max(dirFree, fntFindNextFreeDirID(dir.dirs[i]));

	return dirFree;
}

static u32 fntDirectoryIndex(NDSDirectory& parent, const std::string& dataDir)
{
	for (u32 i = 0; i < parent.dirs.size(); i++)
		if (dataDir == parent.dirs[i].dirName)
			return i;

	return ~0u;
}

static bool fntAddNewFiles(
	NDSDirectory& ndsDir,
	const fs::path& dataDir,
	u16& freeFileID,
	u16& freeDirID,
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
				"new file " + p.native()
				+ " is not in a new directory"
			);
		}

		u32 i = fntDirectoryIndex(ndsDir, p.filename().string());

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
				std::cout << "File " << sp << " obtained File ID ";
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

static u32 fntDirectoryCount(const NDSDirectory& dir)
{
	u32 count = dir.dirs.size();

	for (u32 i = 0; i < dir.dirs.size(); i++)
		count += fntDirectoryCount(dir.dirs[i]);

	return count;
}

static u32 fntByteCountFn(const NDSDirectory& dir)
{
	u32 bytes = 0;

	for (u32 i = 0; i < dir.files.size(); i++)
		bytes += dir.files[i].length() + 1;

	for (u32 i = 0; i < dir.dirs.size(); i++)
	{
		bytes += dir.dirs[i].dirName.length() + 3;
		bytes += fntByteCountFn(dir.dirs[i]);
	}

	bytes++;

	return bytes;
}

static u32 fntByteCountHeader(const NDSDirectory& root)
{
	return (fntDirectoryCount(root) + 1) * 8;
}

static u32 fntWriteDirectory(const NDSDirectory& dir, u8* fnt, u32 offset, u32 parentID)
{
	writeU32(fnt + (dir.directoryID & 0xfff)*8, offset);

	fnt[(dir.directoryID & 0xfff)*8 + 4] = dir.firstFileID & 0xff;
	fnt[(dir.directoryID & 0xfff)*8 + 5] = dir.firstFileID >> 8;
	fnt[(dir.directoryID & 0xfff)*8 + 6] = parentID & 0xff;
	fnt[(dir.directoryID & 0xfff)*8 + 7] = parentID >> 8;

	for (u32 i = 0; i < dir.files.size(); i++)
	{
		const std::string& filename = dir.files[i];

		fnt[offset] = filename.length();
		filename.copy(reinterpret_cast<char*>(&fnt[offset + 1]), filename.length());
		offset += filename.length() + 1;
	}

	for (u32 i = 0; i < dir.dirs.size(); i++)
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

	for (u32 i = 0; i < dir.dirs.size(); i++)
		offset = fntWriteDirectory(dir.dirs[i], fnt, offset, dir.directoryID);

	return offset;

}

static void fntRebuild(std::vector<u8>& rom, u32 fntOffset, const NDSDirectory& root, u32& size, u8 padding)
{
	u32 fntHeaderSize = fntByteCountHeader(root);
	u32 fntFnSize = fntByteCountFn(root);
	size = fntHeaderSize + fntFnSize;

	romCheckBounds(rom, fntOffset + size, padding);
	fntWriteDirectory(root, &rom[fntOffset], fntHeaderSize, fntHeaderSize / 8);
}

static u32 alignAddress(u32 address, u32 align)
{
	return ((address + align - 1) & ~(align - 1));
}

static void openInputFile(std::ifstream& fileStream, const fs::path& path)
{
	fileStream.open(path, std::ios::binary | std::ios::in);

	if (!fileStream.is_open())
		throw std::runtime_error("failed to open file " + path.native());
}

static fs::path findInputFile(const fs::path& path)
{
	const fs::path toBeCompressedPath = "modified" / ("to-be-compressed" / path);

	if (fs::is_regular_file(toBeCompressedPath))
		throw std::runtime_error("compression is only supported for overlay, not for " + path.native());

	const fs::path modifiedFinalPath = "modified" / ("final" / path);

	if (fs::is_regular_file(modifiedFinalPath))
		return modifiedFinalPath;

	const fs::path cleanRawPath = "clean" / ("raw" / path);

	if (fs::is_regular_file(cleanRawPath))
		return cleanRawPath;

	throw std::runtime_error("could not find file: " + path.native());
}

static void writeOverlay(
	std::vector<u8>& rom,
	u32 ovID,
	OverlayEntry& entry,
	const fs::path& dir,
	u32& romOffset,
	u32 ovtOffset,
	u8 padding
)
{
	const fs::path path = dir / (std::to_string(ovID) + ".bin");
	const fs::path toBeCompressedPath = "modified" / ("to-be-compressed" / path);
	const fs::path finalPath          = "modified" / ("final" / path);

	const bool toBeCompressedExists = fs::is_regular_file(toBeCompressedPath);
	const bool finalExists          = fs::is_regular_file(finalPath);

	u32 size;
	std::ifstream fileStream;
	bool clean = false;

	if (toBeCompressedExists
		&& (!finalExists || fs::last_write_time(finalPath) < fs::last_write_time(toBeCompressedPath)))
	{
		const u32 uncompressedSize = fs::file_size(toBeCompressedPath);
		std::vector<u8> uncompressedData(uncompressedSize);

		openInputFile(fileStream, toBeCompressedPath);
		fileStream.read(reinterpret_cast<char*>(uncompressedData.data()), uncompressedSize);

		std::cout << "Compressing " << toBeCompressedPath << " -> " << finalPath << "\n" WARNING;
		std::cout << "the compression feature is experimental; it may produce incorrect results\n";

		const auto compressedData = BLZ::compress(uncompressedData, padding);
		size = compressedData.size();

		fs::create_directories(finalPath.parent_path());
		std::ofstream compressedFile(finalPath, std::ios::binary | std::ios::out);

		if (!compressedFile.is_open())
			throw std::runtime_error("failed to open file " + finalPath.native());

		compressedFile.write(reinterpret_cast<const char*>(compressedData.data()), size);

		std::cout << "Replacing overlay " << ovID << " with " << finalPath << '\n';

		romCheckBounds(rom, romOffset + size, padding);
		std::memcpy(&rom[romOffset], compressedData.data(), size);
	}
	else if (finalExists)
	{
		std::cout << "Replacing overlay " << ovID << " with " << finalPath << '\n';

		size = fs::file_size(finalPath);
		romCheckBounds(rom, romOffset + size, padding);
		openInputFile(fileStream, finalPath);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), size);
	}
	else
	{
		clean = true;
		const fs::path cleanPath = "clean" / ("raw" / path);

		if (!fs::is_regular_file(cleanPath))
			throw std::runtime_error("could not find overlay file: " + path.native());

		size = fs::file_size(cleanPath);
		romCheckBounds(rom, romOffset + size, padding);
		openInputFile(fileStream, cleanPath);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), size);
	}

	if (!clean)
	{
		if (size >= 1 << 24)
			throw std::length_error("size of " + finalPath.native() + " exceeds 16 MB");

		// Adjust the compressed size of the overlay in the overlay table
		u8* p = rom.data() + ovtOffset + 0x20*ovID + 0x1c;

		p[0] = size       & 0xff;
		p[1] = size >>  8 & 0xff;
		p[2] = size >> 16 & 0xff;
	}

	entry.start = romOffset;
	entry.end = romOffset + size;
	romOffset += size;
}

static void nfsAddAndLink(
	std::vector<u8>& rom,
	u32 fatOffset,
	const NDSDirectory& dir,
	const fs::path& p,
	u32& romOffset,
	u8 padding
)
{
	u16 dirFileID = dir.firstFileID;

	for (u32 i = 0; i < dir.files.size(); i++)
	{
		fs::path filePath = findInputFile(p / dir.files[i]);
		u32 fileSize = fs::file_size(filePath);

		if (fileSize > oneGB)
		{
			std::cout << WARNING "File size of " << filePath << " with " << fileSize << " bytes exceeds 1 GB, skipping\n";
			dirFileID++;
			continue;
		}

		std::ifstream fileStream;
		openInputFile(fileStream, filePath);

		romCheckBounds(rom, romOffset + fileSize, padding);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fileSize);
		fileStream.close();

		u8* p = rom.data() + fatOffset + dirFileID*8;
		writeU32(p, romOffset);
		writeU32(p + 4, romOffset + fileSize);

		romOffset += fileSize;
		romOffset = alignAddress(romOffset, 4);
		dirFileID++;
	}

	for (u32 i = 0; i < dir.dirs.size(); i++)
		nfsAddAndLink(rom, fatOffset, dir.dirs[i], p / dir.dirs[i].dirName, romOffset, padding);
}

static u8 toU8(u32 val, const std::string& name)
{
	if (val < 0x100)
		return static_cast<u8>(val);

	throw std::invalid_argument('\'' + name + "' must be a hex value from 0 to ff");
}

struct Config
{
	static constexpr u32 keep = ~0u;
	static constexpr s16 noPadding = -1;

	fs::path outputPath;
	u8 ovtReplFlag = 0xff;
	s16 padding = noPadding;
	u32 arm9Entry = keep;
	u32 arm9Load  = keep;
	u32 arm7Entry = keep;
	u32 arm7Load  = keep;

	Config(const fs::path& path):
		outputPath(path)
	{
		const fs::path configPath = ".neondst";

		if (!fs::is_regular_file(configPath))
			return;

		std::ifstream configFile(configPath);

		if (!configFile.is_open())
			throw std::runtime_error("failed to open file " + configPath.native());

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

			u32 val;
			try
			{
				val = std::stoul(std::string(sv), nullptr, 16);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error("invalid value for '" + first + "'");
			}

			if (first == "ovt_repl_flag") ovtReplFlag = toU8(val, first);
			else if (first == "pad") padding = toU8(val, first);
			else if (first == "arm9_entry") arm9Entry = val;
			else if (first == "arm9_load" ) arm9Load  = val;
			else if (first == "arm7_entry") arm7Entry = val;
			else if (first == "arm7_load" ) arm7Load  = val;
			else throw std::invalid_argument("invalid configuration variable: " + first);
		}
	}

	void print() const
	{
		std::cout << "Building ROM with the following configuration:\n";
		std::cout << std::hex;

		auto f = [](const char* s, auto val)
		{
			std::cout << '\t' << s;

			if (val == keep)
				std::cout << "keep\n";
			else
			{
				std::cout << std::setw(2*sizeof(val)) << std::setfill('0');
				std::cout << static_cast<u32>(val) << '\n';
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
		throw std::invalid_argument("no output file given");

	NDSDirectory rootDir;

	std::ifstream fileStream;
	std::map<u32, OverlayEntry> ov7Entries;
	std::map<u32, OverlayEntry> ov9Entries;

	u16 freeOvFileID = 0;
	u16 freeFileID = 0;
	u32 ovt9Offset, ovt7Offset, arm9Offset, arm7Offset, fntOffset, iconOffset, fatOffset;
	u32 romHeaderSize, fntSize, ovt7Size, ovt9Size, fatSize, arm7Size, arm9Size, romOffset, iconSize, rsaSize;

	fs::path romHeaderPath = findInputFile("header.bin");
	fs::path fntPath       = findInputFile("fnt.bin");
	fs::path ovt7Path      = findInputFile("arm7ovt.bin");
	fs::path ovt9Path      = findInputFile("arm9ovt.bin");
	fs::path arm7Path      = findInputFile("arm7.bin");
	fs::path arm9Path      = findInputFile("arm9.bin");
	fs::path iconPath      = findInputFile("banner.bin");
	fs::path rsaPath       = findInputFile("rsasig.bin");

	std::cout << "Reading ROM header\n";

	romHeaderSize = fs::file_size(romHeaderPath);

	if (romHeaderSize != 0x200 && romHeaderSize != 0x4000)
		throw std::length_error("invalid size of ROM header: must be 0x200 or 0x4000");

	openInputFile(fileStream, romHeaderPath);

	std::vector<u8> rom(0x4000);
	fileStream.read(reinterpret_cast<char*>(rom.data()), romHeaderSize);
	fileStream.close();

	rom.reserve(readU32(rom.data() + 0x80)*3 >> 1);

	if (romHeaderSize == 0x200)
		std::fill(rom.data() + 0x200, rom.data() + 0x4000, 0);

	romOffset = 0;
	romOffset += 0x4000;

	std::cout << "Adding ARM9 binary " << arm9Path << '\n';

	arm9Size = fs::file_size(arm9Path);
	checkFileSize(arm9Path, arm9Size, 0x3bfe00);

	openInputFile(fileStream, arm9Path);
	romCheckBounds(rom, romOffset + arm9Size, config.padding);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm9Size);
	fileStream.close();

	arm9Offset = romOffset;
	romOffset += arm9Size;
	romOffset = std::max(0x8000U, romOffset);

	std::cout << "Adding ARM9 overlay table " << ovt9Path << '\n';

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

	romCheckBounds(rom, romOffset + 4, config.padding);
	ovt9Offset = romOffset;

	if (ovt9Size)
	{
		openInputFile(fileStream, ovt9Path);
		romCheckBounds(rom, romOffset + ovt9Size, config.padding);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt9Offset]), ovt9Size);
		fileStream.close();

		for (u32 i = 0; i < ovt9Size / 32; i++)
		{
			OverlayEntry e = { 0, 0, 0xffff };

			if (rom[ovt9Offset + i * 32 + 31] != config.ovtReplFlag)
			{
				u16 fid = readU16(&rom[ovt9Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;
			}
			
			ov9Entries[readU32(&rom[ovt9Offset + i * 32])] = e;
		}
	}

	romOffset += ovt9Size;

	std::cout << "Adding ARM9 overlay files\n";

	for (auto& e : ov9Entries)
		writeOverlay(rom, e.first, e.second, "overlay9", romOffset, ovt9Offset, config.padding);

	romOffset = alignAddress(romOffset, 512);

	std::cout << "Adding ARM7 binary " << arm7Path << '\n';

	arm7Size = fs::file_size(arm7Path);
	checkFileSize(arm7Path, arm7Size, 0x3bfe00);

	openInputFile(fileStream, arm7Path);
	romCheckBounds(rom, romOffset + arm7Size, config.padding);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm7Size);
	fileStream.close();

	arm7Offset = romOffset;
	romOffset += arm7Size;
	romOffset = alignAddress(romOffset, 4);

	std::cout << "Adding ARM7 overlay table " << ovt7Path << '\n';

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

	romCheckBounds(rom, romOffset + 4, config.padding);
	ovt7Offset = romOffset;

	if (ovt7Size)
	{
		openInputFile(fileStream, ovt7Path);
		romCheckBounds(rom, romOffset + ovt7Size, config.padding);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt7Offset]), ovt7Size);
		fileStream.close();

		for (u32 i = 0; i < ovt7Size / 32; i++)
		{
			OverlayEntry e = { 0, 0, 0xffff };

			if (rom[ovt7Offset + i * 32 + 31] != config.ovtReplFlag)
			{
				u16 fid = readU16(&rom[ovt7Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;
			}

			ov7Entries[readU32(&rom[ovt7Offset + i * 32])] = e;
		}
	}

	romOffset += ovt7Size;

	std::cout << "Adding ARM7 overlay files\n";

	for (auto& e : ov7Entries)
		writeOverlay(rom, e.first, e.second, "overlay7", romOffset, ovt7Offset, config.padding);

	romOffset = alignAddress(romOffset, 4);

	std::cout << "Reading FNT " << fntPath << '\n';

	fntSize = fs::file_size(fntPath);
	checkFileSize(fntPath, fntSize, oneGB);

	openInputFile(fileStream, fntPath);
	romCheckBounds(rom, romOffset + fntSize, config.padding);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fntSize);
	fileStream.close();

	std::cout << "Extracting FNT directory tree\n";

	rootDir = buildFntTree(&rom[romOffset], 0xF000, fntSize);
	freeFileID = std::max(freeOvFileID, fntFindNextFreeFileID(rootDir));

	std::cout << "Assigning file IDs to new overlays\n";

	for (u32 i = 0; i < ovt9Size / 32; i++)
	{
		if (rom[ovt9Offset + i * 32 + 31] == config.ovtReplFlag)
		{
			u32 ovID = readU32(&rom[ovt9Offset + i * 32]);
			rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
			rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
			rom[ovt9Offset + i * 32 + 26] = 0;
			rom[ovt9Offset + i * 32 + 27] = 0;
			rom[ovt9Offset + i * 32 + 31] = 3;
			ov9Entries[ovID].fileID = freeFileID;
			std::cout << "ARM9 overlay " << ovID << " obtained file ID " << freeFileID << '\n';
			freeFileID++;
		}
	}

	for (u32 i = 0; i < ovt7Size / 32; i++)
	{
		if (rom[ovt7Offset + i * 32 + 31] == config.ovtReplFlag)
		{
			u32 ovID = readU32(&rom[ovt7Offset + i * 32]);
			rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
			rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
			rom[ovt7Offset + i * 32 + 26] = 0;
			rom[ovt7Offset + i * 32 + 27] = 0;
			rom[ovt7Offset + i * 32 + 31] = 3;
			ov7Entries[ovID].fileID = freeFileID;
			std::cout << "ARM7 overlay " << ovID << " obtained file ID " << freeFileID << '\n';
			freeFileID++;
		}
	}

	u16 freeDirID = fntFindNextFreeDirID(rootDir);

	if (const fs::path rootPath = fs::path("modified")/"final"/"root";
		fs::is_directory(rootPath)
		&& fntAddNewFiles(rootDir, rootPath, freeFileID, freeDirID, false))
	{
		std::cout << "Rebuilding FNT\n";

		fntRebuild(rom, romOffset, rootDir, fntSize, config.padding);
	}
	else
		std::cout << "Keeping the original FNT\n";

	fntOffset = romOffset;
	romOffset += fntSize;
	romOffset = alignAddress(romOffset, 4);

	std::cout << "Allocating FAT\n";

	fatSize = freeFileID * 8;
	romCheckBounds(rom, romOffset + fatSize, config.padding);

	fatOffset = romOffset;
	std::memset(&rom[fatOffset], 0x00, fatSize);

	romOffset += fatSize;
	romOffset = alignAddress(romOffset, 512);

	std::cout << "Linking overlays to FAT\n";

	for (const auto& ov : ov9Entries)
	{
		const OverlayEntry& ov9e = ov.second;
		u8* fatPtr = rom.data() + fatOffset + ov9e.fileID*8;

		writeU32(fatPtr, ov9e.start);
		writeU32(fatPtr + 4, ov9e.end);
	}

	for (const auto& ov : ov7Entries)
	{
		const OverlayEntry& ov7e = ov.second;
		u8* fatPtr = rom.data() + fatOffset + ov7e.fileID*8;

		writeU32(fatPtr, ov7e.start);
		writeU32(fatPtr + 4, ov7e.end);
	}

	std::cout << "Adding icon / title " << iconPath << '\n';

	iconSize = fs::file_size(iconPath);

	openInputFile(fileStream, iconPath);

	u16 version;
	fileStream.read(reinterpret_cast<char*>(&version), 2);

	switch (version)
	{
	default:
		std::cout << WARNING "Invalid icon / title ID, defaulting to 0x840\n";
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

	romCheckBounds(rom, romOffset + iconSize, config.padding);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), iconSize);
	fileStream.close();

	iconOffset = romOffset;

	romOffset += iconSize;
	romOffset = alignAddress(romOffset, 512);

	std::cout << "Adding NitroROM filesystem\n";

	nfsAddAndLink(rom, fatOffset, rootDir, "root", romOffset, config.padding);

	std::cout << "Adding RSA signature " << rsaPath << '\n';

	rsaSize = fs::file_size(rsaPath);
	
	if (rsaSize != 0x88)
	{
		std::stringstream s;
		s << "invalid RSA signature size: expected 136 bytes, got ";
		s << rsaSize;

		throw std::length_error(s.view().data());
	}
	
	openInputFile(fileStream, rsaPath);
	romCheckBounds(rom, romOffset + rsaSize, config.padding);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), rsaSize);
	fileStream.close();

	std::cout << "Done building ROM\n";
	std::cout << "Fixing ROM header\n";

	writeU32(rom.data() +   0x20, arm9Offset);
	writeU32(rom.data() +   0x2c, arm9Size);
	writeU32(rom.data() +   0x30, arm7Offset);
	writeU32(rom.data() +   0x3c, arm7Size);
	writeU32(rom.data() +   0x40, fntOffset);
	writeU32(rom.data() +   0x44, fntSize);
	writeU32(rom.data() +   0x48, fatOffset);
	writeU32(rom.data() +   0x4c, fatSize);
	writeU32(rom.data() +   0x50, ovt9Size ? ovt9Offset : 0);
	writeU32(rom.data() +   0x54, ovt9Size);
	writeU32(rom.data() +   0x58, ovt7Size ? ovt7Offset : 0);
	writeU32(rom.data() +   0x5c, ovt7Size);
	writeU32(rom.data() +   0x68, iconOffset);
	writeU32(rom.data() +   0x80, romOffset);
	writeU32(rom.data() + 0x1000, romOffset);

	if (config.arm9Entry != Config::keep) writeU32(rom.data() + 0x24, config.arm9Entry);
	if (config.arm9Load  != Config::keep) writeU32(rom.data() + 0x28, config.arm9Load);
	if (config.arm7Entry != Config::keep) writeU32(rom.data() + 0x34, config.arm7Entry);
	if (config.arm7Load  != Config::keep) writeU32(rom.data() + 0x38, config.arm7Load);

	rom[20] = std::max(std::bit_width(rom.size() - 1) - 17, 0);

	std::cout << "ROM device capacity: 0x" << std::hex << (0x20000 << rom[20]);
	std::cout << " bytes\nUsed ROM space: 0x" << rom.size();
	std::cout << " bytes\n" << std::dec;

	const u16 crc = crc16(rom.data(), 0x15e);
	rom[0x15e] = crc & 0xff;
	rom[0x15f] = crc >> 8;

	std::cout << "Writing " << config.outputPath << '\n';

	std::ofstream outputStream(config.outputPath, std::ios::binary | std::ios::out);
	
	if (!outputStream.is_open())
		throw std::runtime_error("failed to create file " + config.outputPath.native());

	outputStream.write(reinterpret_cast<const char*>(rom.data()), rom.size());

	if (config.padding != Config::noPadding)
	{
		const auto size = (0x20000 << rom[20]) - rom.size();
		rom.clear();
		rom.resize(size, config.padding);

		outputStream.write(reinterpret_cast<const char*>(rom.data()), rom.size());
	}

	outputStream.close();

	std::cout << "Successfully written NDS image " << config.outputPath << '\n';
}
