#include "command.h"
#include <iostream>

void Commands::help(std::string_view commandName)
{
	if (commandName.empty())
	{
		std::cout << "usage: neondst <command> [<arguments...>]\ncommands:\n";

		for (const Command& command : commands)
		{
			std::cout << '\t';

			command.printUsage();
		}

		return;
	}

	bool found = false;

	for (const Command& command : commands)
	{
		if (command.name == commandName)
		{
			std::cout << (found ? "   or: " : "usage: ");
			found = true;

			command.printUsage();
		}
	}

	if (!found)
	{
		std::cout << "error: '" << commandName << "' is not a neondst command\n";

		Commands::help("help");
	}
}
