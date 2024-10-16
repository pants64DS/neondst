#include "command.h"

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		Commands::help();

		return 1;
	}

	std::string_view commandName = argv[1];

	if (commandName == "--help" || commandName == "--version")
		commandName.remove_prefix(2);

	return runCommand(commandName, argv + 2);
}
