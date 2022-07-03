TARGETS=menu console

all: $(TARGETS)
	git commit -am compile

menu: menu.c
	cc menu.c -lncursesw -o menu
#	cc menu.c -lcurses -o menu

console: console.c
	cc console.c -lpthread -o console

up: export LC_ALL := C
up: all
	scp $(TARGETS) shima@sstu.edu.ipc.hiroshima-cu.ac.jp:bin
	git push
