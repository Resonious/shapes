LDFLAGS = -lvulkan -ldl -lpthread
CFLAGS = -std=c++17

shapes: main.cpp
	g++ $(CFLAGS) -o shapes main.cpp $(LDFLAGS)
