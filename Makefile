# Makefile for gb_emu

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

SRCS := main.cpp
OBJS := $(SRCS:.cpp=.o)

DEPS := bus.h core.h types.h

TARGET := gb_emu

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)
