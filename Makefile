SOURCES  := source
BUILD    := build
OUTPUT   := neondst
INCLUDES := $(SOURCES)
INCLUDE  := $(foreach dir,$(INCLUDES),-I$(dir))
CXX      := g++
CXXFLAGS := $(INCLUDE) -std=c++23 -Wall -Wextra -Wno-narrowing -O3
LDFLAGS  := -static-libgcc -static-libstdc++

CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES   := $(foreach file,$(CPPFILES:.cpp=.o),$(BUILD)/$(file))

.SUFFIXES:
.SECONDEXPANSION:
.PHONY: clean

$(OUTPUT): $(OFILES)
	@echo linking $(OUTPUT)
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: $(foreach dir,$(SOURCES),$$(wildcard $(dir)/%.cpp)) | $(BUILD)
	@echo compiling $<
	@$(CXX) -MMD -MP -MF $(BUILD)/$*.d $(CXXFLAGS) -c $< -o $@

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

clean:
	@echo clean...
	@rm -fr $(BUILD) $(OUTPUT)

-include $(BUILD)/*.d
