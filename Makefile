BUILDDIR := $(shell mkdir -p build)

all:	build/tc-server build/tc-client

build/tc-server:
	g++ src/tc-server.cpp -lncursesw -lpthread -o build/tc-server

build/tc-client:
	g++ src/tc-client.cpp -lncursesw -lpthread -o build/tc-client

clean:
	rm -fR build
