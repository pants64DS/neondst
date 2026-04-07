#include "command.h"

void extract(const fs::path& ndsInputPath, const fs::path& fsOutputPath);

void Commands::init(const fs::path& cleanRomPath)
{
	const fs::path clean = "clean";
	const fs::path modified = "modified";

	extract(cleanRomPath, clean / "raw");

	fs::create_directory(clean / "decompressed");
	fs::create_directories(modified / "to-be-compressed");
	fs::create_directory(modified / "final");
}
