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

