#include "command.h"
#include "common.h"

#include <iostream>
#include <vector>

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
	std::cout << "neondst " << name << ' ' << usage << '\n';
}

void Command::printError(const char* error) const
{
	std::cout << ERROR << error << "\nusage: ";

	printUsage();
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
