# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17

# Libraries to link
LIBS = -ldpp

# Target executable name
TARGET = bot

# Source files
SRC = main.cpp

# Default rule to build the target
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LIBS)

# Clean rule to remove generated files
clean:
	rm -f $(TARGET)
