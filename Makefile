CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2
TARGET = dist-sys
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC) node.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

debug: CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -g -O0
debug: $(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run debug
