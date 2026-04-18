#include "command.h"
#include "config.h"

#include <iostream>

struct StatusExtractor : Extractor
{
	using Extractor::Extractor;
	std::vector<fs::path> diffPaths;
	std::vector<fs::path> romOnlyPaths;

	virtual void writeFile(const fs::path& shortPath, const void* data, std::size_t size)
	{
		if (fileExistsAndEquals("modified" / ("final" / shortPath), data, size))
			return;

		fs::path modifiedBasePath = "modified" / ("base" / shortPath);

		if (fs::is_regular_file(modifiedBasePath))
		{
			if (!fileEquals(modifiedBasePath, data, size))
				diffPaths.push_back(shortPath);
		}
		else if (!fs::is_regular_file("clean" / ("raw" / shortPath)))
			romOnlyPaths.push_back(shortPath);
	}

	virtual void writeDir(const fs::path& shortPath)
	{

	}
};

void Commands::status(const fs::path& romPath)
{
	Config config(romPath);

	StatusExtractor status {config.romPath};
	status.extract();

	if (status.romOnlyPaths.empty() && status.diffPaths.empty())
	{
		std::cout << "No changes\n";
		return;
	}

	if (!status.romOnlyPaths.empty())
	{
		std::cout << "Files in " << config.romPath << " not in the source directories:\n";

		for (const auto& p : status.romOnlyPaths)
			std::cout << '\t' << p << '\n';
	}

	if (!status.diffPaths.empty())
	{
		std::cout << "Files that differ between " << config.romPath << " and the source directories:\n";

		for (const auto& p : status.diffPaths)
			std::cout << '\t' << p << '\n';
	}

	// TODO: show files that are only in the source directories
}
