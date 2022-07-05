.PHONY: all

TARGETS=menu console

all: $(TARGETS)
	git checkout -B compile
	git commit -am "Successful compilation" -uno
#	- git branch --quiet compile
#	git checkout compile
#	git commit --all "--message=Successful compilation" --untracked-files=no
#	git commit --all --allow-empty-message --untracked-files=no

menu: menu.c
	cc -std=c99 menu.c -lncursesw -lpthread -o menu
#	cc menu.c -lcurses -o menu

console: console.c
	cc console.c -lpthread -o console

merge:
	git checkout master
	git merge --squash compile
	git commit --all --untracked-files=no

up: export LC_ALL := C
up:
	scp $(TARGETS) shima@sstu.edu.ipc.hiroshima-cu.ac.jp:bin

push:
	git checkout master
	git push
