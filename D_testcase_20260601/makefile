# Compiler and flags
CXX = g++
CXXFLAGS = -O3 -std=c++11 -Wall

# Target executable name
TARGET = cadd0016

# Source files and object files
SRCS = main.cpp parser.cpp timing_engine.cpp optimizer.cpp
OBJS = $(SRCS:.cpp=.o)

# Default rule
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

# Compile .cpp source files to .o object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean