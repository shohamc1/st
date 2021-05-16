# st - Stupid Terminal

My attempt at creating a X11 terminal emulator.
[Inspired by this blog post.](https://www.uninformativ.de/blog/postings/2018-02-24/0/POSTING-en.html)

## How to run

### Install Prerequisites

To build the project, you will need the X11 development headers.

Arch: `# pacman -S libx11`

Debian: `# apt install libx11`

### Build and Run

These commands should build and run the terminal emulator.

```bash
git clone https://www.github.com/shohamc1/st.git
cd st
make clean install
```

To only build: `make build`
To only run: `make run`
Clean files: `make clean`
