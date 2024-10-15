#include "command.h"

#include <iostream>
#include <vector>

int Command::run(int argc, char** argv) const
{
	bool separatorFound = false;
	const char* error = nullptr;

	std::vector<std::string_view> args;
	args.reserve(maxArgs);

	// std::vector<std::string_view> options;

	for (int i = 2; i < argc; ++i)
	{
		std::string_view arg = argv[i];

		if (!separatorFound && arg.starts_with("--"))
		{
			if (arg.size() == 2)
				separatorFound = true;
			else
			{
				// Handle options
			}
		}
		else
		{
			args.push_back(arg);
		}
	}

	if (args.size() < minArgs)
		error = "not enough positional arguments";

	if (args.size() > maxArgs)
		error = "too many positional arguments";

	if (error)
	{
		printError(error);

		return 1;
	}

	if (args.size() < maxArgs)
		args.resize(maxArgs);

	try
	{
		funcWrapper(funcAddr, args.data());
	}
	catch (const CommandError& ex)
	{
		printError(ex.what());

		return 1;
	}

	return 0;
}

void Command::printUsage() const
{
	if (subcommandSet == SubcommandSet::saveApply)
	{
		std::cout <<   "usage: neondst " << name << " save " << usage;
		std::cout << "\n   or: neondst " << name << " apply " << usage << '\n';
	}
	else
		std::cout << "usage: neondst " << name << ' ' << usage << '\n';;
}

void Command::printError(const char* error) const
{
	std::cout << "error: " << error << '\n';

	printUsage();
}

int runCommand(std::string_view commandName, int argc, char** argv)
{
	for (const Command& command : commands)
		if (command.name == commandName)
			return command.run(argc, argv);

	std::cout << "error: '" << commandName << "' is not a neondst command\n";
	Commands::help();

	return 1;
}
