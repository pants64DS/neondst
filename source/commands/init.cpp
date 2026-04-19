#include "command.h"
#include <fstream>

static const fs::path cleanRawPath = fs::path("clean") / "raw";

struct InitExtractor : Extractor
{
	using Extractor::Extractor;

	virtual void writeFile(const fs::path& shortPath, const void* data, std::size_t size) override
	{
		const fs::path path = cleanRawPath / shortPath;

		if (fs::exists(path))
			throw std::runtime_error("file " + path.string() + " already exists");

		std::ofstream outFile(path, std::ios::binary | std::ios::out);

		if (!outFile.is_open())
			throw std::runtime_error("failed to create file " + path.string());

		if (!outFile.write(static_cast<const char*>(data), size))
			throw std::runtime_error("failed to write file " + path.string());
	}

	virtual void writeDir(const fs::path& shortPath) override
	{
		const fs::path path = cleanRawPath / shortPath;

		if (fs::is_directory(path))
			throw std::runtime_error("directory " + path.string() + " already exists");

		fs::create_directory(path);
	}
};

void Commands::init(const fs::path& cleanRomPath)
{
	const fs::path clean = "clean";

	fs::remove_all(clean);
	fs::create_directory(clean);

	InitExtractor(cleanRomPath).extract();

	fs::create_directory(clean / "decompressed");

	const fs::path modified = "modified";

	fs::create_directories(modified / "to-be-compressed");
	fs::create_directory(modified / "final");
}
