#include "config.h"

#include <iostream>
#include <fstream>

static u8 toU8(u32 val, const std::string& name)
{
	if (val < 0x100)
		return static_cast<u8>(val);

	throw std::invalid_argument('\'' + name + "' must be a hex value from 0 to ff");
}

Config::Config(const fs::path& path):
	romPath(path)
{
	const fs::path configPath = ".neondst";

	if (!fs::is_regular_file(configPath))
		return;

	std::ifstream configFile(configPath);

	if (!configFile.is_open())
		throw std::runtime_error("failed to open file " + configPath.native());

	while (configFile.good())
	{
		std::string line, first;
		std::stringstream s;
		std::getline(configFile, line);

		if (line.empty()) continue;

		s << line;
		s >> first;

		if (first.starts_with('#')) continue;

		auto sv = s.view();
		sv.remove_prefix(first.size());

		auto it = std::ranges::find_if_not(sv, static_cast<int(&)(int)>(std::isspace));
		sv.remove_prefix(std::distance(sv.begin(), it));

		if (first == "output")
		{
			if (path.empty())
				romPath = sv;

			continue;
		}

		u32 val;
		try
		{
			val = std::stoul(std::string(sv), nullptr, 16);
		}
		catch (const std::exception& e)
		{
			throw std::runtime_error("invalid value for '" + first + "'");
		}

		if (first == "ovt_repl_flag") ovtReplFlag = toU8(val, first);
		else if (first == "pad") padding = toU8(val, first);
		else if (first == "arm9_entry") arm9Entry = val;
		else if (first == "arm9_load" ) arm9Load  = val;
		else if (first == "arm7_entry") arm7Entry = val;
		else if (first == "arm7_load" ) arm7Load  = val;
		else throw std::invalid_argument("invalid configuration variable: " + first);
	}
}

void Config::print() const
{
	std::cout << std::hex;

	auto f = [](const char* s, auto val)
	{
		std::cout << '\t' << s;

		if (val == keep)
			std::cout << "keep\n";
		else
		{
			std::cout << std::setw(2*sizeof(val)) << std::setfill('0');
			std::cout << static_cast<u32>(val) << '\n';
		}
	};

	f("arm9_entry: ", arm9Entry);
	f("arm9_load:  ", arm9Load);
	f("arm7_entry: ", arm7Entry);
	f("arm7_load:  ", arm7Load);
	f("ovt_repl_flag: ", ovtReplFlag);

	std::cout << std::dec;
}