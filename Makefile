CXX = g++
CXXFLAGS = -std=gnu++20 -O2 -pthread -Wall -Wextra

SRC = src/main.cpp
TARGET = btree_demo

$(TARGET): $(SRC) src/btree.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
