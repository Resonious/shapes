LDFLAGS = -lvulkan -ldl -lpthread -lglfw
CFLAGS = -ggdb -std=c++17

shapes: main.cpp debug.h debug.cpp
	g++ $(CFLAGS) -o shapes main.cpp debug.cpp $(LDFLAGS)

.PHONY: clean

clean:
	rm -f shapes
