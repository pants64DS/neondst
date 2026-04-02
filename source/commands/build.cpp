#include "command.h"
#include <iostream>

int pack(const fs::path& outputPath);

void Commands::build(const fs::path& outputPath)
{
	pack(outputPath);
}
