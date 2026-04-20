#include "command.h"

#ifdef _WIN32
#include <windows.h>
#include <iostream>
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

	if (hOut == INVALID_HANDLE_VALUE)
	{
		std::cout << "GetStdHandle returned with error 0x";
		std::cout << std::hex << GetLastError() << '\n';
		return 1;
	}

	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode))
	{
		std::cout << "GetConsoleMode returned with error 0x";
		std::cout << std::hex << GetLastError() << '\n';
		return 1;
	}

	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode))
	{
		std::cout << "SetConsoleMode returned with error 0x";
		std::cout << std::hex << GetLastError() << '\n';
		return 1;
	}
#endif

	if (argc < 2)
	{
		Commands::help();

		return 1;
	}

	std::string_view commandName = argv[1];

	if (commandName == "--help" || commandName == "--version")
		commandName.remove_prefix(2);

	return runCommand(commandName, argc - 2, argv + 2);
}
