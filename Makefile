VERSION   := $(shell git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo unknown)
SOURCES   := source source/commands
BUILD     := build
BUILD_WIN := build/windows
OUTPUT    := neondst
INCLUDES  := $(SOURCES)
INCLUDE   := $(foreach dir,$(INCLUDES),-I$(dir))
CXX       := g++
CXX_WIN   := x86_64-w64-mingw32-g++
CXXFLAGS  := $(INCLUDE) -DNEONDST_VERSION=\"$(VERSION)\" -std=c++23 -Wall -Wextra -O3
LDFLAGS   := -static -static-libgcc -static-libstdc++
BINDIR    ?= /usr/local/bin

CPPFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES     := $(foreach file,$(CPPFILES:.cpp=.o),$(BUILD)/$(file))
OFILES_WIN := $(foreach file,$(CPPFILES:.cpp=.o),$(BUILD_WIN)/$(file))

.SUFFIXES:
.SECONDEXPANSION:
.PHONY: all clean install uninstall

$(OUTPUT): $(OFILES)
	@echo linking $(OUTPUT)
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(OUTPUT).exe: $(OFILES_WIN)
	@echo linking $(OUTPUT).exe
	@$(CXX_WIN) -o $@ $^ $(LDFLAGS)

all: $(OUTPUT) $(OUTPUT).exe

$(BUILD)/%.o: $(foreach dir,$(SOURCES),$$(wildcard $(dir)/%.cpp)) | $(BUILD)
	@echo compiling $<
	@$(CXX) -MMD -MP -MF $(BUILD)/$*.d $(CXXFLAGS) -c $< -o $@

$(BUILD_WIN)/%.o: $(foreach dir,$(SOURCES),$$(wildcard $(dir)/%.cpp)) | $(BUILD_WIN)
	@echo compiling $<
	@$(CXX_WIN) -MMD -MP -MF $(BUILD_WIN)/$*.d $(CXXFLAGS) -c $< -o $@

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

$(BUILD_WIN):
	@[ -d $@ ] || mkdir -p $@

clean:
	@echo clean...
	@rm -fr $(BUILD) $(OUTPUT) $(OUTPUT).exe

install: $(OUTPUT)
	@echo installing $(OUTPUT) to $(BINDIR)
	@install -Dm755 "$(OUTPUT)" "$(BINDIR)/$(OUTPUT)"

uninstall:
	@echo removing $(BINDIR)/$(OUTPUT)
	@rm -f "$(BINDIR)/$(OUTPUT)"

-include $(BUILD)/*.d
