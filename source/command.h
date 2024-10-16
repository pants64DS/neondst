#pragma once

#include <stdexcept>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cstdint>

struct CommandError : std::logic_error
{
	using std::logic_error::logic_error;
};

namespace fs = std::filesystem;

template<class T>
std::remove_reference_t<T> parse(std::string_view sv) { return sv; };

template<class... Args>
void commandFuncWrapper(std::uintptr_t funcAddr, std::string_view* arr)
{
	auto f = reinterpret_cast<void(*)(Args...)>(funcAddr);
	static_assert(sizeof(f) == sizeof(std::uintptr_t));

	[&f, arr]<std::size_t... i>(std::index_sequence<i...>)
	{
		f(parse<Args>(arr[i])...);
	}
	(std::make_index_sequence<sizeof...(Args)>{});
}

struct Command
{
	std::uintptr_t funcAddr;
	void(&funcWrapper)(std::uintptr_t funcAddr, std::string_view* arr);
	const char* name;
	const char* subName;
	uint32_t minArgs, maxArgs;
	const char* usage;

	template<class... Args> Command(
		void (&func)(Args...),
		const char* name,
		const char* subName,
		const char* usage,
		uint32_t numRequiredArgs = 0
	):
		funcAddr(reinterpret_cast<std::uintptr_t>(&func)),
		funcWrapper(commandFuncWrapper<Args...>),
		name(name),
		subName(subName),
		minArgs(numRequiredArgs),
		maxArgs(sizeof...(Args)),
		usage(usage)
	{}

	int run(char** argv) const;

	void printUsage() const;
	void printError(const char* error) const;
};

namespace Commands
{
	void init(const fs::path& cleanRomPath, const fs::path& modifiedRomPath, const fs::path& bakDirPath);
	void build();
	void start();
	void diff_save();
	void diff_apply();
	void temp_save(const fs::path& tempRomPath);
	void temp_apply(const fs::path& tempRomPath);
	void help(std::string_view command = "");
	void version();
}

inline const Command commands[] = {
	{Commands::init,       "init",    nullptr, "<clean ROM> [<modified ROM>] [<bak directory>]", 1},
	{Commands::build,      "build",   nullptr, ""},
	{Commands::start,      "start",   nullptr, ""},
	{Commands::diff_save,  "diff",    "save",  ""},
	{Commands::diff_apply, "diff",    "apply", ""},
	{Commands::temp_save,  "temp",    "save",  "[<temporary ROM>]"},
	{Commands::temp_apply, "temp",    "apply", "[<temporary ROM>]"},
	{Commands::help,       "help",    nullptr, "[<command>]"},
	{Commands::version,    "version", nullptr, ""}
};

int runCommand(std::string_view commandName, char** argv);
