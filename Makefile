LDFLAGS = -lvulkan -ldl -lpthread -lglfw
CFLAGS = -ggdb -std=c++17

shapes: main.cpp debug.h debug.cpp
	g++ $(CFLAGS) -o shapes main.cpp debug.cpp $(LDFLAGS)

.PHONY: run clean

run: shapes
	./shapes

clean:
	rm -f shapes
