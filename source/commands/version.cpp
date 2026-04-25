#include "command.h"
#include <iostream>

const char logo[] = R"(
                                                        __
                                                       / /              __
    ________    ______   ________   ________    ______/ /  ______   ___/ /___
    \  ___  |  / ____ \  \  ____ \  \  ___  |  / ____  /  / _____\ /__  ____/
    / /  / /  / /___/ /  / /   / /  / /  / /  / /   / /  / /___      / /
   / /  / /  / ______/  / /   / /  / /  / /  / /   / /   \____ \    / /
  / /  / /  / /_____   / /___/ /  / /  / /  / /___/ /  ______/ /   / /____
 /_/  /_/   \______/   \______/  /_/  /_/   \______/   \______/    \_____/

)";

constexpr const char versionString[] = NEONDST_VERSION;

void Commands::version()
{
	if constexpr (versionString[0] == 'v')
		std::cout << "neondst version " << versionString + 1;
	else
		std::cout << "neondst @ " << versionString;

	for (int row = 0, column = 0; char c : logo)
	{
		if (column < 33 || (column == 33 && row != 8))
		{
			if (
				(row >= 8               ) ||
				(row == 7 && column > 8 ) ||
				(row == 6 && column > 14) ||
				(row == 5 && column > 30)
			)
				std::cout << "\x1b[1;94m";
			else
				std::cout << "\x1b[1;96m";
		}
		else
			std::cout << "\x1b[1;97m";

		std::cout << c;

		if (c == '\n')
		{
			++row;
			column = 0;
		}
		else
			++column;
	}

	std::cout << "\x1b[0m";
}
