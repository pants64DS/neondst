#include "command.h"
#include "config.h"

#include <iostream>
#include <fstream>
#include <cstring>

static bool fileEquals(const fs::path& path, const void* data, std::size_t size)
{
	if (!fs::is_regular_file(path)) return false;
	if (fs::file_size(path) != size) return false;

	std::ifstream file(path, std::ios::in | std::ios::binary);

	if (!file.is_open())
		throw std::runtime_error("failed to open file " + path.native());

	auto buffer = std::make_unique<char[]>(size);

	if (!file.read(buffer.get(), size))
		throw std::runtime_error("failed to read file " + path.native());

	return std::memcmp(buffer.get(), data, size) == 0;
}

struct ApplyExtractor : Extractor
{
	fs::path tempPath;
	ApplyExtractor(const fs::path& path) : tempPath(path) {}

	virtual void writeFile(const fs::path& shortPath, const void* data, std::size_t size) override
	{
		if (fileEquals("clean" / ("raw" / shortPath), data, size))
			return;

		const fs::path modifiedFilePath = tempPath / shortPath;
		fs::create_directories(modifiedFilePath.parent_path());

		std::ofstream outFile(modifiedFilePath, std::ios::binary | std::ios::out);

		if (!outFile.is_open())
			throw std::runtime_error("failed to create file " + modifiedFilePath.native());

		outFile.write(static_cast<const char*>(data), size);
	}

	virtual void writeDir(const fs::path&) override {}
};

void Commands::apply(const fs::path& romPath)
{
	Config config(romPath);

	fs::path tempPath = fs::path("modified") / "temp-";
	tempPath += romPath.stem();
	fs::remove_all(tempPath);

	try
	{
		ApplyExtractor{tempPath}.extract(config.romPath);
	}
	catch (const std::exception& ex)
	{
		fs::remove_all(tempPath);
		throw ex;
	}

	const fs::path modifiedFinalPath = fs::path("modified") / "final";
	fs::remove_all(modifiedFinalPath);
	fs::rename(tempPath, modifiedFinalPath);
	fs::remove_all(fs::path("modified") / "to-be-comressed");
}
