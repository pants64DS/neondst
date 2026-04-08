#pragma once

#include "common.h"

struct Config
{
	static constexpr u32 keep = ~0u;
	static constexpr s16 noPadding = -1;

	fs::path romPath;
	u8 ovtReplFlag = 0xff;
	s16 padding = noPadding;
	u32 arm9Entry = keep;
	u32 arm9Load  = keep;
	u32 arm7Entry = keep;
	u32 arm7Load  = keep;

	Config(const fs::path& path);
	void print() const;
};
