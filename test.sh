make && rm -rf extracted packed_rom.nds && echo ------------ && \
./neondst -e clean_rom.nds extracted
./neondst -p test_build_rules.txt packed_rom.nds
