#include "common.h"
#include "command.h"
#include "blz.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>

namespace fs = std::filesystem;

static const fs::path rawPath = fs::path{"clean"} / "raw";
static const fs::path decompressedPath = fs::path{"clean"} / "decompressed";
static const fs::path arm9Path = rawPath / "arm9.bin";

void Commands::decompress(std::span<const fs::path> relativePaths)
{
	for (auto& relativePath : relativePaths)
	{
		const fs::path inputPath = rawPath / relativePath;
		const fs::path outputPath = decompressedPath / relativePath;
		const fs::path parentPath = inputPath.parent_path();
		const bool isArm9Bin = fs::equivalent(inputPath, arm9Path);

		if (!isArm9Bin && fs::equivalent(parentPath, rawPath))
		{
			std::cout << WARNING << inputPath << " should not be compressed (skipped)\n";
			continue;
		}

		std::cout << "Decompressing " << inputPath;
		std::cout << " -> " << outputPath << '\n';

		std::ifstream inputFile(inputPath, std::ios::in | std::ios::binary);

		if (!inputFile.is_open())
			throw std::runtime_error("failed to open file " + inputPath.native());

		auto compressedSize = fs::file_size(inputPath);
		std::vector<u8> buffer;
		buffer.reserve(compressedSize << 1);
		buffer.resize(compressedSize);

		if (!inputFile.read(reinterpret_cast<char*>(buffer.data()), compressedSize))
			throw std::runtime_error("failed to read file " + inputPath.native());

		if (isArm9Bin)
		{
			if (compressedSize < 0xaf0)
				throw std::runtime_error("invalid arm9.bin");

			const u32 compressedPartEnd = readU32(buffer.data() + 0xaec);

			if (compressedSize != compressedPartEnd - 0x02004000)
				throw std::runtime_error("invalid arm9.bin");

			BLZ::uncompressInplace(buffer);
			std::memset(buffer.data() + 0xaec, 0, 4);
		}
		else if (fs::equivalent(parentPath, rawPath / "overlay9"))
			BLZ::uncompressInplace(buffer);
		else
			throw std::runtime_error("decompression of regular files not implemented yet");

		fs::create_directories(outputPath.parent_path());
		std::ofstream outputFile(outputPath, std::ios::binary | std::ios::out);

		if (!outputFile.is_open())
			throw std::runtime_error("failed to create file " + outputPath.native());

		if (!outputFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size()))
			throw std::runtime_error("failed to write file " + outputPath.native());
	}

	std::cout << "Done\n";
}
