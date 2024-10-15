#include "command.h"
#include <iostream>

void Commands::help(std::string_view commandName)
{
	if (commandName.empty())
	{
		std::cout << "usage: neondst <command> [<arguments...>]\ncommands:\n";

		for (const Command& command : commands)
		{
			if (command.subcommandSet == SubcommandSet::saveApply)
			{
				std::cout <<   "\tneondst " << command.name << " save " << command.usage;
				std::cout << "\n\tneondst " << command.name << " apply " << command.usage << '\n';
			}
			else
				std::cout << "\tneondst " << command.name << ' ' << command.usage << '\n';
		};
	}
	else
	{
		for (const Command& command : commands)
		{
			if (command.name == commandName)
			{
				command.printUsage();
				return;
			}
		}

		std::cout << "error: '" << commandName << "' is not a neondst command\n";

		Commands::help("help");
	}
}
