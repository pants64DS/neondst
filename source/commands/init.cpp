#include "command.h"

void extract(const fs::path& ndsInputPath, const fs::path& fsOutputPath);

void Commands::init(const fs::path& cleanRomPath)
{
	const fs::path clean = "clean";

	extract(cleanRomPath, clean / "raw");

	fs::create_directory(clean / "decompressed");
}
