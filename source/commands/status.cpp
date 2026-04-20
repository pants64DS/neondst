#include "command.h"
#include "config.h"

#include <unordered_set>
#include <iostream>

const fs::path cleanRaw               = fs::path("clean") / "raw";
const fs::path modifiedBase           = fs::path("modified") / "base";
const fs::path modifiedFinal          = fs::path("modified") / "final";
const fs::path modifiedToBeCompressed = fs::path("modified") / "to-be-compressed";

struct StatusExtractor : Extractor
{
	using Extractor::Extractor;
	std::unordered_set<fs::path> paths;
	std::vector<fs::path> diffPaths;
	std::vector<fs::path> romOnlyPaths;

	virtual void writeFile(const fs::path& shortPath, const void* data, std::size_t size)
	{
		paths.insert(shortPath);

		if (fileExistsAndEquals(modifiedFinal / shortPath, data, size))
			return;

		fs::path modifiedBasePath = modifiedBase / shortPath;

		if (fs::is_regular_file(modifiedBasePath))
		{
			if (!fileEquals(modifiedBasePath, data, size))
				diffPaths.push_back(shortPath);
		}
		else if (!fs::is_regular_file(cleanRaw / shortPath))
			romOnlyPaths.push_back(shortPath);
	}

	virtual void writeDir(const fs::path& shortPath)
	{
		paths.insert(shortPath);

		if (fs::is_directory(modifiedFinal / shortPath)) return;
		if (fs::is_directory(modifiedBase / shortPath)) return;
		if (fs::is_directory(cleanRaw / shortPath)) return;

		romOnlyPaths.push_back(shortPath);
	}
};

void Commands::status(const fs::path& romPath)
{
	Config config(romPath);

	StatusExtractor status {config.romPath};
	status.extract();

	bool changes = false;
	std::vector<fs::path> sourceOnlyPaths;

	for (const fs::path& rootPath : {modifiedBase, modifiedToBeCompressed, modifiedFinal})
	{
		if (!fs::is_directory(rootPath))
			continue;

		for (const fs::directory_entry& dirEntry : fs::recursive_directory_iterator(rootPath))
		{
			const fs::path& p = dirEntry.path();

			auto q = relative(p, rootPath);

			if (!status.paths.contains(q))
				sourceOnlyPaths.push_back(p);
		}
	}

	if (!sourceOnlyPaths.empty())
	{
		changes = true;
		std::cout << "Files in " << fs::path("modified") << " not in " << config.romPath << ":\n";

		for (const fs::path& p : sourceOnlyPaths)
			std::cout << "\t\x1b[0;32m" << p.string() << "\x1b[0m\n";

		std::cout << '\n';
	}

	if (!status.romOnlyPaths.empty())
	{
		changes = true;
		std::cout << "Files in " << config.romPath << " not in the source directories:\n";

		for (const auto& p : status.romOnlyPaths)
			std::cout << "\t\x1b[0;31m" << p.string() << "\x1b[0m\n";

		std::cout << '\n';
	}

	if (!status.diffPaths.empty())
	{
		changes = true;
		std::cout << "Files that differ between " << config.romPath << " and the source directories:\n";

		for (const auto& p : status.diffPaths)
			std::cout << "\t\x1b[0;33m" << p.string() << "\x1b[0m\n";

		std::cout << '\n';
	}

	if (!changes) std::cout << "No changes\n";
}
