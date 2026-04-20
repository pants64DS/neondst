#include "command.h"
#include "config.h"

#include <iostream>
#include <fstream>
#include <cstring>

bool fileExistsAndEquals(const fs::path& path, const void* data, std::size_t size)
{
	return fs::is_regular_file(path) && fileEquals(path, data, size);
}

bool fileEquals(const fs::path& path, const void* data, std::size_t size)
{
	if (fs::file_size(path) != size) return false;

	std::ifstream file(path, std::ios::in | std::ios::binary);

	if (!file.is_open())
		throw std::runtime_error("failed to open file " + path.string());

	auto buffer = std::make_unique<char[]>(size);

	if (!file.read(buffer.get(), size))
		throw std::runtime_error("failed to read file " + path.string());

	return std::memcmp(buffer.get(), data, size) == 0;
}

struct ApplyExtractor : Extractor
{
	fs::path tempPath;

	ApplyExtractor(const fs::path& romPath, const fs::path& tempPath):
		Extractor(romPath),
		tempPath(tempPath)
	{}

	virtual void writeFile(const fs::path& path, const void* data, std::size_t size) override
	{
		const fs::path toBeCompressedPath = "modified" / ("to-be-compressed" / path);
		const fs::path finalPath          = "modified" / ("final" / path);

		const bool toBeCompressedExists = fs::is_regular_file(toBeCompressedPath);
		const bool finalExists          = fs::is_regular_file(finalPath);

		const fs::path cleanRawPath = "clean" / ("raw" / path);
		const fs::path cleanDecompressedPath = "clean" / ("decompressed" / path);

		if (!toBeCompressedExists && !finalExists)
		{
			if (fileExistsAndEquals(cleanRawPath, data, size) || fileExistsAndEquals(cleanDecompressedPath, data, size))
				return;

			const fs::path tempFilePath = tempPath / path;
			fs::create_directories(tempFilePath.parent_path());

			std::ofstream outFile(tempFilePath, std::ios::binary | std::ios::out);

			if (!outFile.is_open())
				throw std::runtime_error("failed to create file " + tempFilePath.string());

			if (!outFile.write(static_cast<const char*>(data), size))
				throw std::runtime_error("failed to write file " + tempFilePath.string());

			return;
		}

		const auto& lastBuiltPath = toBeCompressedExists ? toBeCompressedPath : finalPath;
		std::ifstream lastBuiltFile(lastBuiltPath, std::ios::in | std::ios::binary);

		if (!lastBuiltFile.is_open())
			throw std::runtime_error("failed to open file " + lastBuiltPath.string());

		std::vector<u8> lastBuiltFileData(fs::file_size(lastBuiltPath));

		if (!lastBuiltFile.read(reinterpret_cast<char*>(lastBuiltFileData.data()), lastBuiltFileData.size()))
			throw std::runtime_error("failed to read file " + lastBuiltPath.string());

		lastBuiltFile.close();

		if (lastBuiltFileData.size() != size || std::memcmp(data, lastBuiltFileData.data(), size) != 0)
		{
			const fs::path modifiedBasePath = "modified" / ("base" / path);

			std::cout << WARNING << lastBuiltPath;
			std::cout << " differs from the corresponding file in " << romPath;
			std::cout << " but the changes will not be applied to " << modifiedBasePath;
			std::cout << " (unimplemented)\n";
		}

		const fs::path baseFilePath = "modified" / ("base" / path);

		if (fs::is_regular_file(baseFilePath))
		{
			const fs::path tempFilePath = tempPath / path;
			fs::create_directories(tempFilePath.parent_path());
			fs::copy_file(baseFilePath, tempFilePath);
		}

		/*
		const fs::path* baseFilePath = nullptr;
		bool compressed = false;

		if (fs::is_regular_file(modifiedBasePath))
			baseFilePath = &modifiedBasePath;
		else if (fs::is_regular_file(cleanDecompressedPath))
			baseFilePath = &cleanDecompressedPath;
		else if (fs::is_regular_file(cleanRawPath))
		{
			throw std::runtime_error("not implemented (1)");
			// check if compressed ...
		}
		else
		{
			throw std::runtime_error("not implemented (2)");
		}

		std::ifstream baseFile(*baseFilePath, std::ios::in | std::ios::binary);

		if (!baseFile.is_open())
			throw std::runtime_error("failed to open file " + baseFilePath->string());

		std::vector<u8> baseFileData(fs::file_size(*baseFilePath));
		baseFile.read(reinterpret_cast<char*>(baseFileData.data()), baseFileData.size());

		if (compressed)
		{
			throw std::runtime_error("not implemented (3)");
		}
		*/

		/*
		TODO:
			- compare data with lastBuiltFileData
			- apply differences to baseFileData
			- store the result in tempPath / path
		*/
	}

	virtual void writeDir(const fs::path&) override {}
};

void Commands::apply(const fs::path& romPath)
{
	Config config(romPath);

	fs::path tempPath = fs::path("modified") / "temp-";
	tempPath += config.romPath.stem();
	fs::remove_all(tempPath);

	try
	{
		ApplyExtractor{config.romPath, tempPath}.extract();
	}
	catch (const std::exception& ex)
	{
		fs::remove_all(tempPath);
		throw;
	}

	const fs::path modifiedBasePath = fs::path("modified") / "base";
	fs::remove_all(modifiedBasePath);

	if (fs::is_directory(tempPath))
		fs::rename(tempPath, modifiedBasePath);

	fs::remove_all(fs::path("modified") / "to-be-comressed");
}
