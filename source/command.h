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

enum class SubcommandSet { none, saveApply };
enum class SaveApplySubCommand { save, apply };

template<class T>
std::remove_reference_t<T> parse(std::string_view sv) { return sv; };

template<>
inline SaveApplySubCommand parse<SaveApplySubCommand>(std::string_view sv)
{
	if (sv == "save")  return SaveApplySubCommand::save;
	if (sv == "apply") return SaveApplySubCommand::apply;

	throw CommandError("invalid subcommand");
};

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
	std::string_view name;
	uint32_t minArgs, maxArgs;
	SubcommandSet subcommandSet;
	const char* usage;

	template<class... Args> Command(
		void (&func)(Args...),
		const char* name,
		uint32_t numRequiredArgs,
		const char* usage,
		SubcommandSet subcommandSet = SubcommandSet::none
	):
		funcAddr(reinterpret_cast<std::uintptr_t>(&func)),
		funcWrapper(commandFuncWrapper<Args...>),
		name(name),
		minArgs(numRequiredArgs),
		maxArgs(sizeof...(Args)),
		subcommandSet(subcommandSet),
		usage(usage)
	{}

	int run(int argc, char** argv) const;

	void printUsage() const;
	void printError(const char* error) const;
};

namespace Commands
{
	void init(const fs::path& cleanRomPath, const fs::path& modifiedRomPath, const fs::path& bakDirPath);
	void build();
	void start();
	void diff(SaveApplySubCommand subcommand);
	void temp(SaveApplySubCommand subcommand, const fs::path& tempRomPath);
	void help(std::string_view command = "");
	void version();
}

inline const Command commands[] = {
	{Commands::init,    "init",    1, "<clean ROM> [<modified ROM>] [<bak directory>]"},
	{Commands::build,   "build",   0, ""},
	{Commands::start,   "start",   0, ""},
	{Commands::diff,    "diff",    1, "", SubcommandSet::saveApply},
	{Commands::temp,    "temp",    1, "", SubcommandSet::saveApply},
	{Commands::help,    "help",    0, "[<command>]"},
	{Commands::version, "version", 0, ""}
};

int runCommand(std::string_view commandName, int argc, char** argv);
