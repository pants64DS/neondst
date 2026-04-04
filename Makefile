SOURCES  := source source/commands
BUILD    := build
OUTPUT   := neondst
INCLUDES := $(SOURCES)
INCLUDE  := $(foreach dir,$(INCLUDES),-I$(dir))
CXX      := g++
CXXFLAGS := $(INCLUDE) -std=c++23 -Wall -Wextra -O3
LDFLAGS  := -static-libgcc -static-libstdc++
BINDIR   ?= /usr/local/bin

CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES   := $(foreach file,$(CPPFILES:.cpp=.o),$(BUILD)/$(file))

.SUFFIXES:
.SECONDEXPANSION:
.PHONY: clean install uninstall

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

install: $(OUTPUT)
	@echo installing $(OUTPUT) to $(BINDIR)
	@install -Dm755 "$(OUTPUT)" "$(BINDIR)/$(OUTPUT)"

uninstall:
	@echo removing $(BINDIR)/$(OUTPUT)
	@rm -f "$(BINDIR)/$(OUTPUT)"

-include $(BUILD)/*.d
