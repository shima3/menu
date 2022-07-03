TARGETS=menu console

all: $(TARGETS)

menu: menu.c
	cc menu.c -lncursesw -o menu
#	cc menu.c -lcurses -o menu

console: console.c
	cc console.c -lpthread -o console

up: all
	LC_ALL=C scp $(TARGETS) shima@sstu.edu.ipc.hiroshima-cu.ac.jp:bin
