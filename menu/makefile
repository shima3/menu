.PHONY: all

TARGETS=menu console

all: $(TARGETS)
	git commit -am "Successful compilation" --untracked-files=no

menu: menu.c
	cc -std=c99 menu.c -lncursesw -o menu
#	cc menu.c -lcurses -o menu

console: console.c
	cc console.c -lpthread -o console

up: export LC_ALL := C
up:
	scp $(TARGETS) shima@sstu.edu.ipc.hiroshima-cu.ac.jp:bin
	git push
