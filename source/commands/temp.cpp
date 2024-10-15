#include "command.h"
#include <iostream>

void Commands::temp(SaveApplySubCommand subcommand, const fs::path& tempRomPath)
{
	(void)subcommand;
	(void)tempRomPath;

	std::cout << "temp not implemented yet :(\n";
}
