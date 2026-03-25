#pragma once

#include <stdexcept>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <vector>
#include <limits>
#include <span>

namespace fs = std::filesystem;

template<class T>
std::remove_reference_t<T> parse(const char* sv) { return sv; };

template<class... Args>
void commandFuncWrapper(std::uintptr_t funcAddr, int argc, char** argv)
{
	auto f = reinterpret_cast<void(*)(Args...)>(funcAddr);
	static_assert(sizeof(f) == sizeof(std::uintptr_t));

	[&f, argc, argv]<int... i>(std::integer_sequence<int, i...>)
	{
		f((i < argc ? parse<Args>(argv[i]) : Args{})...);
	}
	(std::make_integer_sequence<int, sizeof...(Args)>{});
}

template<class Arg>
void commandFuncWrapper2(std::uintptr_t funcAddr, int argc, char** argv)
{
	std::vector<std::remove_const_t<Arg>> args;
	args.reserve(argc);
	while (*argv) args.emplace_back(*argv++);

	reinterpret_cast<void(*)(std::span<Arg>)>(funcAddr)(args);
}

struct Command
{
	std::uintptr_t funcAddr;
	void(&funcWrapper)(std::uintptr_t funcAddr, int argc, char** argv);
	const char* name;
	const char* subName;
	int minArgs, maxArgs;
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

	template<class Arg> Command(
		void (&func)(std::span<Arg>),
		const char* name,
		const char* subName,
		const char* usage,
		uint32_t numRequiredArgs = 0
	):
		funcAddr(reinterpret_cast<std::uintptr_t>(&func)),
		funcWrapper(commandFuncWrapper2<Arg>),
		name(name),
		subName(subName),
		minArgs(numRequiredArgs),
		maxArgs(std::numeric_limits<int>::max()),
		usage(usage)
	{}

	int run(int argc, char** argv) const;

	void printUsage() const;
	void printError(const char* error) const;
};

namespace Commands
{
	void init(const fs::path& cleanRomPath);
	void decompress(std::span<const fs::path> relativePaths);
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
	{Commands::init,       "init",       nullptr, "<clean ROM>", 1},
	{Commands::decompress, "decompress", nullptr, "<files...>", 1},
	{Commands::build,      "build",      nullptr, ""},
	{Commands::start,      "start",      nullptr, ""},
	{Commands::diff_save,  "diff",       "save",  ""},
	{Commands::diff_apply, "diff",       "apply", ""},
	{Commands::temp_save,  "temp",       "save",  "[<temporary ROM>]"},
	{Commands::temp_apply, "temp",       "apply", "[<temporary ROM>]"},
	{Commands::help,       "help",       nullptr, "[<command>]"},
	{Commands::version,    "version",    nullptr, ""}
};

int runCommand(std::string_view commandName, int argc, char** argv);
