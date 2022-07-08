.PHONY: all

TARGETS=menu console

all: $(TARGETS)
	date > .git/commit.msg
	git status --short --untracked-files=no >> .git/commit.msg
	if [ -s .git/commit.msg ]; then git commit --all --file=.git/commit.msg --untracked-files=no --quiet; fi
#	git commit -am "Successful compilation" -uno
#	git checkout -B compile
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
	bash ./merge.sh

up: export LC_ALL := C
up:
	scp $(TARGETS) shima@sstu.edu.ipc.hiroshima-cu.ac.jp:bin
