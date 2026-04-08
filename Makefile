CXX ?= g++
TARGET := bin/simcom_cli

SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp,build/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -MMD -MP
LDFLAGS ?=
LDLIBS += -lreadline

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p $@

bin:
	mkdir -p $@

clean:
	rm -rf build $(TARGET)

run: $(TARGET)
	./$(TARGET)

-include $(DEPS)
