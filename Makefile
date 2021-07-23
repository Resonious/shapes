LDFLAGS = -lvulkan -ldl -lpthread -lglfw
DEBUG_CFLAGS = -std=c++17 -DDEBUG -ggdb
RELEASE_CFLAGS = -std=c++17 -O2

shapes: main.cpp debug.h debug.cpp
	g++ $(DEBUG_CFLAGS) -o shapes main.cpp debug.cpp $(LDFLAGS)

.PHONY: run clean

run: shapes
	./shapes

clean:
	rm -f shapes
