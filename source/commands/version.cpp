#include "command.h"
#include <iostream>

const char logo[] = R"(

    ________    ______   ________    __      _   ______       ________
    \  ___  |  / ____ \  \  ____ \  |  \    | | |  ___ \_    /  ____  \
    / /  / /  / /___/ /  / /   / /  | \ \   | | | |   \_ \  |  /    \_/       __
   / /  / /  / ______/  / /   / /   | |\ \  | | | |     | | |  \_____     ___/ /___
  / /  / /  / /_____   / /___/ /    | | \ \ | | | |     | |  \_____  \   /__  ____/
 /_/  /_/   \______/   \______/     | |  \ \| | | |    _| |  _     \  \    / /
                                    | |   \ \ | | |___/ _/  | \____/  /   / /____
                                    |_|    \__| |______/    \________/    \_____/

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
		if (column < 35)
		{
			if (
				(row >= 7               ) ||
				(row == 6 && column > 8 ) ||
				(row == 5 && column > 14) ||
				(row == 4 && column > 30)
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
