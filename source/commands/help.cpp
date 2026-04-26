#ifdef _WIN32
#include <windows.h>
#undef ERROR
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "command.h"
#include "common.h"
#include <cstring>
#include <iostream>
#include <utility>

static int getTerminalWidth()
{
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
		return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
	struct winsize w;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0)
		return w.ws_col;
#endif

	return 80; // fallback
}

extern const std::size_t maxUsageWidth;

static bool isSeparator(char c)
{
	return c == ' ' || c == '\n' || c == '\0';
}

void Commands::help(std::string_view commandName)
{
	const std::size_t terminalWidth = std::min(getTerminalWidth(), 100);

	if (commandName.empty())
		std::cout << "usage: neondst <command> [<arguments...>]\n\n";

	const std::size_t indentLength = maxUsageWidth + 4;
	char indent[indentLength + 1];
	std::memset(indent, ' ', indentLength);
	indent[indentLength] = '\0';
	bool found = false;

	for (const Command& command : commands)
	{
		if (!commandName.empty())
		{
			if (commandName == command.name)
				found = true;
			else
				continue;
		}

		command.printUsage();

		std::size_t column;
		for (column = command.usageWidth; column < indentLength - 1; ++column)
			std::cout << ' ';

		bool newline = false;
		std::size_t wordStart = 0;
		char prevChar = '\0';

		for (std::size_t i = 0; ; ++i)
		{
			const char c = command.desc[i];
			const char d = prevChar;
			prevChar = c;

			if (!isSeparator(c))
			{
				if (isSeparator(d)) wordStart = i;
				continue;
			}

			if (isSeparator(d))
			{
				if (c == '\0') break;
				if (c == '\n') newline = true;
				continue;
			}

			std::string word {command.desc + wordStart, command.desc + i};
			column += word.size() + 1;

			for (char& c : word) // Convert non-breaking spaces
				if (c == '\xa0') c = ' ';

			if (column > terminalWidth && wordStart > 0 && !newline)
			{
				std::cout << '\n' << indent << word;
				column = indentLength + word.size();
			}
			else
			{
				if (!newline) std::cout << ' ';
				std::cout << word;
			}

			newline = (c == '\n');
			if (newline)
			{
				std::cout << '\n' << indent;
				column = indentLength;
			}

			if (c == '\0') break;
		}

		std::cout << "\n\n";
	}

	if (!commandName.empty() && !found)
		std::cout << ERROR "'" << commandName << "' is not a neondst command\n";
}
