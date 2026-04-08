#include "command.h"
#include <fstream>

static const fs::path cleanRawPath = fs::path("clean") / "raw";

struct InitExtractor : Extractor
{
	virtual void writeFile(const fs::path& shortPath, const void* data, std::size_t size) override
	{
		const fs::path path = cleanRawPath / shortPath;

		if (fs::exists(path))
			throw std::runtime_error("file " + path.native() + " already exists");

		std::ofstream outFile(path, std::ios::binary | std::ios::out);

		if (!outFile.is_open())
			throw std::runtime_error("failed to create file " + path.native());

		outFile.write(static_cast<const char*>(data), size);
	}

	virtual void writeDir(const fs::path& shortPath) override
	{
		const fs::path path = cleanRawPath / shortPath;

		if (fs::is_directory(path))
			throw "directory " + path.native() + " already exists";

		fs::create_directory(path);
	}
};

void Commands::init(const fs::path& cleanRomPath)
{
	const fs::path clean = "clean";

	fs::remove_all(clean);
	fs::create_directory(clean);

	InitExtractor{}.extract(cleanRomPath);

	fs::create_directory(clean / "decompressed");

	const fs::path modified = "modified";

	fs::create_directories(modified / "to-be-compressed");
	fs::create_directory(modified / "final");
}
