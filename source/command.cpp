#include "command.h"

#include <iostream>
#include <vector>

int Command::run(char** argv) const
{
	bool separatorFound = false;
	const char* error = nullptr;

	std::vector<std::string_view> args;
	args.reserve(maxArgs);

	// std::vector<std::string_view> options;

	while (*argv)
	{
		std::string_view arg = *argv++;

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
			args.push_back(arg);
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
	std::cout << "neondst " << name;

	if (subName) std::cout << ' ' << subName;

	std::cout << ' ' << usage << '\n';
}

void Command::printError(const char* error) const
{
	std::cout << "error: " << error << "\nusage: ";

	printUsage();
}

int runCommand(std::string_view commandName, char** argv)
{
	const char* foundName = nullptr;

	for (const Command& command : commands)
	{
		if (command.name == commandName)
		{
			if (!command.subName)
				return command.run(argv);

			if (argv[0] && std::string_view{argv[0]} == command.subName)
				return command.run(argv + 1);

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
