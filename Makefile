
# Makefile for mid.cpp (steam prototype)
# Usage: just run `make`

CXX = g++
CXXFLAGS = -Wall -std=c++17 -Iinclude

SRC = mid.cpp glad.c
OBJ = $(SRC:.cpp=.o)
TARGET = steam.exe

LIBS = -lglfw3 -lopengl32 -lgdi32

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET) *.o

