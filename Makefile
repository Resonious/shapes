LDFLAGS = -lvulkan -ldl -lpthread -lglfw
DEBUG_CFLAGS = -std=c++17 -DDEBUG -ggdb
RELEASE_CFLAGS = -std=c++17 -O2

#=== C++ program ===#
shapes: main.cpp debug.h debug.cpp triangle.vert.h triangle.frag.h
	g++ $(DEBUG_CFLAGS) -o shapes main.cpp debug.cpp $(LDFLAGS)

#=== C headers of SPIR-V bytecode ===#
# NOTE: `xxd` comes from the `vim` package... haha
triangle.vert.h: triangle.vert.spv
	xxd -i triangle.vert.spv > triangle.vert.h

triangle.frag.h: triangle.frag.spv
	xxd -i triangle.frag.spv > triangle.frag.h

#=== GLSL shaders ===#
triangle.vert.spv: triangle.vert
	glslc triangle.vert -o triangle.vert.spv

triangle.frag.spv: triangle.frag
	glslc triangle.frag -o triangle.frag.spv

#=== Tasks ===#
.PHONY: run debug clean

run: shapes
	./shapes
debug: shapes
	gdb ./shapes
clean:
	rm -f shapes triangle.*.h triangle.*.spv
