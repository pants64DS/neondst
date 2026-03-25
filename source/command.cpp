#include "command.h"

#include <iostream>
#include <vector>

int Command::run(int argc, char** argv) const
{
	if (argc < minArgs)
	{
		printError("error: not enough positional arguments");
		return 1;
	}

	if (argc > maxArgs)
	{
		printError("error: too many positional arguments");
		return 1;
	}

	try
	{
		funcWrapper(funcAddr, argc, argv);
	}
	catch (const std::exception& ex)
	{
		printError(ex.what());

		return 1;
	}

	return 0;
}

void Command::printUsage() const
{
	std::cout << "neondst " << name;

	if (subName) std::cout << ' ' << subName;

	std::cout << ' ' << usage << '\n';
}

void Command::printError(const char* error) const
{
	std::cout << error << "\nusage: ";

	printUsage();
}

int runCommand(std::string_view commandName, int argc, char** argv)
{
	const char* foundName = nullptr;

	for (const Command& command : commands)
	{
		if (command.name == commandName)
		{
			if (!command.subName)
				return command.run(argc, argv);

			if (argv[0] && std::string_view{argv[0]} == command.subName)
				return command.run(argc - 1, argv + 1);

			foundName = command.name;
		}
	}

	if (foundName)
	{
		std::cout << "error: invalid subcommand\n";
		Commands::help(foundName);
	}
	else
	{
		std::cout << "error: '" << commandName << "' is not a neondst command\n";
		Commands::help();
	}

	return 1;
}
