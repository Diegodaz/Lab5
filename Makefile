CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS = -pthread

.PHONY: all
all: pi matriz_vector lista barreras

pi matriz_vector lista barreras: %: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)
