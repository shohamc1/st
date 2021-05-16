LDLIBS += `pkg-config --libs x11`
CFLAGS += -std=c99 -Wall -Wextra `pkg-config --cflags x11`

install:
	make build
	make run
build:
	mkdir -p out
	gcc -o out/st st.c -L/usr/X11/lib -lX11 -lstdc++
run:
	./out/st
clean:
	rm -rf ./out