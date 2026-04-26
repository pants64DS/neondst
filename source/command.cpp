#include "command.h"
#include "common.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <ranges>

int Command::run(int argc, char** argv) const
{
	if (argc < minArgs)
	{
		printError("not enough positional arguments");
		return 1;
	}

	if (argc > maxArgs)
	{
		printError("too many positional arguments");
		return 1;
	}

	try
	{
		funcWrapper(funcAddr, argc, argv);
	}
	catch (const std::exception& ex)
	{
		std::cout << ERROR << ex.what() << '\n';

		return 1;
	}

	return 0;
}

void Command::printUsage() const
{
	std::cout << "neondst " << name << ' ' << usage;
}

void Command::printError(const char* error) const
{
	std::cout << ERROR << error << "\nusage: ";

	printUsage();
	std::cout << '\n';
}

int runCommand(std::string_view commandName, int argc, char** argv)
{
	for (const Command& command : commands)
		if (command.name == commandName)
			return command.run(argc, argv);

	std::cout << ERROR "'" << commandName << "' is not a neondst command\n";
	Commands::help();

	return 1;
}

constexpr const Command commands[] =
{
	{
		Commands::init, "init", "<clean ROM>", 1,
		"Initializes a new neondst project in the current directory. "
		"Files from the  clean  ROM are   extracted to  clean/raw and "
		"other   relevant directories  are created."
	},
	{
		Commands::build, "build", "[<output ROM>]", 0,
		"Builds the ROM from the files in the source directories, "
		"which are prioritized in this order:"
		"\n\xa0\xa0\xa0\xa0" "1.\xa0" "modified/final"
		"\n\xa0\xa0\xa0\xa0" "2.\xa0" "modified/to-be-compressed"
		"\n\xa0\xa0\xa0\xa0" "3.\xa0" "modified/base"
		"\n\xa0\xa0\xa0\xa0" "4.\xa0" "clean/raw"
	},
	{
		Commands::apply, "apply", "[<input ROM>]", 0,
		"Applies changes from the ROM to modified/base. "
		"Files in modified/to-be-compressed or modified/final are skipped. "
		"If modified/to-be-compressed exists, it will be deleted."
	},
	{
		Commands::status, "status", "[<ROM>]", 0,
		"Enumerates files that differ between the ROM and the source "
		"directories, including ones that only exist in one or the other. "
		"This effectively allows the user to check if anything will get "
		"overwritten or deleted when running 'neondst\xa0"
		"build' or 'neondst\xa0" "apply'."
	},
	{
		Commands::decompress, "decompress", "<files...>", 1,
		"Decompresses files from clean/raw to clean/decompressed. "
		"File paths should be relative to clean/raw. "
		"At the moment, this is only supported for overlays and the "
		"ARM9 binary (arm9.bin)."
	},
	{
		Commands::help, "help", "[<command>]", 0,
		"Shows this information."
	},
	{
		Commands::version, "version", "", 0,
		"Shows version information."
	}
};

extern constinit const std::size_t maxUsageWidth
	= std::ranges::max_element(commands, {}, &Command::usageWidth)->usageWidth;
