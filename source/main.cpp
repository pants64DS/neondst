#include <iostream>
#include <cstring>

#include <filesystem>
namespace fs = std::filesystem;

int extract(const fs::path& ndsInputPath, const fs::path& fsOutputPath);
int pack(const fs::path& ndsInputPath, const fs::path& fsOutputPath);

const char* help = R"(Usage: neondst [option(s)] [file(s)]
Exactly one of the following switches must be given:
  -e, --extract            Extract a ROM
  -p, --pack               Pack a ROM    
)";

template<std::size_t n>
static bool anyOf(const char* str, const char* const (&strs)[n])
{
	for (const char* s : strs)
		if (std::strcmp(str, s) == 0)
			return true;

	return false;
}

int main(int argc, char** argv)
{
	if (argc != 4)
	{
		std::cout << "Wrong number of arguments\n";
		std::cout << help;
		return 1;
	}

	if (anyOf(argv[1], {"-e", "--extract"}))
		return extract(argv[2], argv[3]);

	if (anyOf(argv[1], {"-p", "--pack"}))
		return pack(argv[2], argv[3]);

	std::cout << help;
	return 1;
}
