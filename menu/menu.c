/*
  menu.c
  2022.6.27 by Kazuyuki Shima

  References:
  - cursesライブラリの超てきとー解説
  https://www.kushiro-ct.ac.jp/yanagawa/pl2b-2018/curses/about.html
  - setlocale() - ロケールの設定
  https://www.ibm.com/docs/ja/zos/2.3.0?topic=functions-setlocale-set-locale
  - ncurses.h ヘッダファイル
  /Library/Developer/CommandLineTools/SDKs/MacOSX12.3.sdk/usr/include/ncurses.h
  - NCURSES Programming HOWTO
  https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/init.html
  - セリカ式 ncurses
  http://www.kis-lab.com/serikashiki/man/ncurses.html
  - Ncurses 入門
  http://linuxmag.osdn.jp/Japanese/March2002/article233.shtml
  - man pages section 3: Curses Library Functions
  https://docs.oracle.com/cd/E86824_01/html/E54767/copywin-3curses.html
  
  Bugs:
  - 背景色を変更したウィンドウが右端にあると左側のウィンドウの空行も同じ背景色になる。
  恐らく、cursesライブラリのバグと考えられる。
  - 画面の左右でウィンドウが異なる場合、全角文字の左半分がウインドウの右端のカラムに来たとき、
  -- 右側に別のウィンドウがあれば、右半分がはみ出る。
  -- 画面の右端であれば、次の行の左端から表示される。
  恐らく、cursesライブラリのバグと考えられる。
  - コンソール内でエディタなどを起動した場合のため、コンソールは左上にしか配置できない。

  Design:
  - 画面を左右に分割し、左側はメニュー、右側はコンソールとする。
  - 左右の境目に1カラムの垂直な線を引く。
  - メニューモードとコンソールモードがある。
  - メニューモードで、ユーザはメニューからコマンドを選んで実行できる。
  -- メニューにある複数の行のうち１行が選択されている。
  -- 選択中の行は、スクリーンの右端まで反転表示される。
  -- ユーザが上下の矢印キーを押すと、選択中の行が変わる。
  -- ユーザがリターンキーを押すと、選択中の行に設定されたコマンドが実行される。
  -- メニューの左端に１つのショートカットキーが表示されている。
  -- ユーザがショートカットキーを押すと、その行に設定されたコマンドが実行される。
  - コンソールモードでは、ユーザはコンソールにコマンドを入力して実行できる。
  - メニューモードとコンソールモードのどちらでも
  -- コマンドの実行結果はコンソールに表示される。
  -- ユーザがエスケープキーを押すと、メニューモードとコンソールモードが切り替わる。
*/
#include <stdio.h>
#include <stdlib.h>
// #include <ncurses.h>
#include <ncursesw/ncurses.h>
#include <locale.h>
#include <unistd.h>

int screenWidth, screenHeight;
int menuWidth=19, menuHeight=0;
WINDOW *menuPad, *menuWin, *consoleWin, *choiceWin;
int menuPadX=0, menuPadY=0;
char *menuItems[ ]={
  "ESC コマンド入力",
  "RET 選択中の処理を実行",
  "↓ 下に移動",
  "↑ 上に移動",
  "PageDown fn+↓ 下に移動",
  "PageUp fn+↑ 上に移動",
  "Q 終了",
  NULL
};
int consoleWidth;
int choiceY=0;

void makeMenuPad(){
  int i;
  
  for(menuHeight=0; menuItems[menuHeight]!=NULL; ++menuHeight);
  menuPad=newpad(menuHeight, menuWidth);
  wbkgd(menuPad, COLOR_PAIR(1));
  for(i=0; i<menuHeight; ++i){
    wmove(menuPad, i, 0);
    waddstr(menuPad, menuItems[i]);
  }
}

void redrawMenu(){
  int i;

  werase(menuWin);
  /*
  wmove(menuWin, 0, 0);
  for(int i=0; menuItems[i]!=NULL; ++i){
    waddchstr(menuWin, menuItems[i]);
    waddch(menuWin, '\n');
  }
  prefresh(menuPad, 0, 0, 0, 0, screenHeight-1, menuWidth-1);
  */
  /* int copywin(WINDOW *srcwin, WINDOW *dstwin, int sminrow,
     int smincol, int dminrow, int dmincol,
     int dmaxrow, int dmaxcol, int overlay);
     
     The copywin() routine provides a finer granularity of control over the overlay() and overwrite() routines. Like in the prefresh() routine, a rectangle is specified in the destination window, (dminrow, dmincol) and (dmaxrow, dmaxcol), and the upper-left-corner coordinates of the source window, (sminrow, smincol). If the argument overlay is true, then copying is non-destructive, as in overlay(). */
  // overlay(menuPad, menuWin);
  // overwrite(menuPad, menuWin);
  for(i=0; i<screenHeight; ++i){
    wmove(menuWin, i, menuWidth/2);
    waddch(menuWin, 9);
    // waddstr(menuWin, "");
  }

  int height=menuHeight-menuPadY;
  if(height>screenHeight) height=screenHeight;
  copywin(menuPad, menuWin, menuPadY, menuPadX, 0, 0, height-1, menuWidth-1, FALSE);
  // copywin(menuPad, menuWin, 0, 0, 0, 0, screenHeight, menuWidth, TRUE);
}

void redrawChoice(){
  mvwin(choiceWin, choiceY, screenWidth-menuWidth);
  werase(choiceWin);
  wmove(choiceWin, 0, 0);
  waddstr(choiceWin, menuItems[choiceY]);
  overwrite(choiceWin, stdscr);
}

int main(int argc, char *argv[ ]){
  int ch=0, x=0, y=0;
  char str[1024];
  short screenForeground, screenBackground;

  // setlocale(LC_ALL, "ja_JP.UTF-8"); // ロケールをja_JP.UTF-8に設定する。
  setlocale(LC_ALL, ""); // 環境変数に従ってロケールを設定する。
  // printf("%s\n", setlocale(LC_ALL, NULL)); // 現在のロケールを確認する。
  
  initscr( ); // スクリーンを初期化する。
  // curs_set(FALSE); // 物理カーソルを見えなくする。
  raw( ); // Ctrl+C や Ctrl+Z などもキー入力するよう設定する。
  cbreak( ); // 入力バッファを使用しないモードに設定する。
  // echo( ); // キー入力された文字を表示するモードに設定する。
  noecho( ); // キー入力された文字を表示しないモードに設定する。
  keypad(stdscr, TRUE); // カーソルキーを有効にする。
  /*
    カーソルキーのマクロ名: KEY_UP，KEY_DOWN，KEY_LEFT，KEY_RIGHT
    KEY_BACKSPACE	backspace key
    KEY_F(n)		Value of function key n
    KEY_ENTER		enter/send key
  */

  start_color( ); // カラーを有効にする。
  pair_content(0, &screenForeground, &screenBackground);
  init_pair(1, screenBackground, screenForeground);
  init_pair(2, screenForeground, screenBackground);
  /*
    bkgd(COLOR_PAIR(0));
  */
  use_default_colors( ); // 端末のデフォルトの配色を利用する。

  getmaxyx(stdscr, screenHeight, screenWidth); // スクリーンサイズを取得する。
  // ch=inch( ); // スクリーン上のカーソル位置にある文字を読み取る。
  // getstr(str); // キーボードから文字列を入力する。
  // instr(str); //  スクリーン上のカーソル位置にある文字列を読み取る。
  // nodelay(stdscr, TRUE); // 非ブロッキングモードに設定する。入力なし=ERR=-1
  // timeout(10); // 入力の待ち時間を10msに設定する。
  // timeout(0); // 非ブロッキングモードに設定する。
  // timeout(-1); // ブロッキングモードに設定する。
  // scrollok(menu, TRUE); // winをスクロールできるように設定する。

  makeMenuPad();
  
  /* WINDOW *subwin(WINDOW *orig, int lines, int cols, int y, int x);
     lines行，cols列の新しいウィンドウを指定したウィンドウのy行，x列目に作成します．
     x, y は画面stdscrの絶対座標 */
  // menuHeight=screenHeight;
  // menuWin=subwin(stdscr, screenHeight, menuWidth, 0, 0);
  menuWin=subwin(stdscr, screenHeight, menuWidth, 0, screenWidth-menuWidth);
  // menu=newwin(menuHeight, menuWidth, 0, screenWidth-menuWidth-1);
  if(menuWin==NULL){
    fprintf(stderr, "Failed to create a menu window.\n");
    exit(1);
  }
  wbkgd(menuWin, COLOR_PAIR(1));
  // wvline(menuframe, 0, menuHeight);
  // wcolor_set(menu, 1, NULL);
  // wattrset(menu, COLOR_PAIR(0) | A_REVERSE);
  // werase(menu);
  // wprintw(menuWin, "ESCで終了\n");
  // wclear(menu);
  // wbkgd(stdscr, COLOR_PAIR(0));
  // wrefresh(menu);

  /* WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int y, int x);
     nlines行，ncols列の新しいウィンドウをスクリーンのy行，x列目に作成する。
     指定したウィンドウと文字列バッファを共有する。*/
  // consoleWidth=screenWidth-menuWidth-1;
  consoleWidth=screenWidth-menuWidth;
  // consoleWin=newwin(screenHeight, consoleWidth, 0, menuWidth+1); // ウィンドウを作成する 。
  consoleWin=newwin(screenHeight, consoleWidth, 0, 0); // ウィンドウを作成する 。
  // menu=subwin(stdscr, 10, 20, 10, 10); // ウィンドウを作成する。
  // log=newwin(screenHeight, screenWidth, 0, 0); // ウィンドウを作成する。
  if(consoleWin==NULL){
    fprintf(stderr, "Failed to create a console window.\n");
    exit(1);
  }
  // wbkgd(log, COLOR_PAIR(0));
  scrollok(consoleWin, TRUE); // スクロールできるように設定する。
  // wprintw(logWin, "ログ\n");

  // choiceWin=newwin(1, screenWidth, choiceY, 0);
  choiceWin=newwin(1, menuWidth, choiceY, 0);
  if(choiceWin==NULL){
    fprintf(stderr, "Failed to create a choice window.\n");
    exit(1);
  }
  // wbkgd(choiceWin, COLOR_PAIR(1));
  wbkgd(choiceWin, COLOR_PAIR(2));
  
  // menuframe=newwin(menuHeight+1, menuWidth+1, 0, screenWidth-menuWidth-1); // ウィンドウを作成する。
  // wcolor_set(menuframe, COLOR_PAIR(1), NULL);
  // wcolor_set(menuframe, 1, NULL);
  // box(menu, 0, 0); // ウィンドウに枠を作成する。
  // box(menu, '|', '-');
  /*
  wbkgd(menuframe, COLOR_PAIR(1));
  werase(menuframe);
  wvline(menuframe, 0, menuHeight);
  wmove(menuframe, menuHeight, 0);
  waddch(menuframe, ACS_LLCORNER);
  whline(menuframe, 0, menuWidth);
  */

  for(;;){
    getmaxyx(stdscr, screenHeight, screenWidth); // スクリーンサイズを取得する。
    wresize(menuWin, screenHeight, menuWidth); // ウィンドウのサイズを変更する。
    consoleWidth=screenWidth-menuWidth-1;
    wresize(consoleWin, screenHeight, consoleWidth);
    // if(menuHeight<screenHeight) redrawMenu();
    redrawMenu();
    overwrite(consoleWin, stdscr);
    /*
    move(0, menuWidth);
    vline(0, screenHeight);
    */
    redrawChoice();
    // move(choiceY, 0);
    // wrefresh(menu);
    // overlay(menu, menuframe);
    // overwrite(menu, menuframe);
    // overwrite(log, stdscr);
    // wrefresh(menuframe);
    // overwrite(menuframe, stdscr);
    // overwrite(menu, stdscr);
    // overwrite(log, stdscr);
    // wrefresh(log); // 端末を再描画します。
    // wrefresh(menu);
    // wnoutrefresh(menu);
    // wnoutrefresh(log);
    // doupdate( );
    // wrefresh(menu);
    // move(y, x); // カーソルをy行x列に移動する。
    refresh( );
    ch=getch( ); // キーボードから文字を入力する。
    // getyx(log, y, x); // カーソルの座標を取得する。
    // getyx(stdscr, y, x); // カーソルの座標を取得する。
    wprintw(consoleWin, "ch=%d, x=%d, y=%d, w=%d, h=%d\n", ch, x, y, screenWidth, screenHeight); // curses版のprintf
    touchwin(stdscr);
    if(ch == 'Q') break;
    switch(ch){
    case KEY_UP:
      if(choiceY>0) --choiceY;
      break;
    case KEY_DOWN:
      if(choiceY<menuHeight-1) ++choiceY;
      break;
      /*
    case KEY_LEFT:
      if(menuPadX>0) --menuPadX;
      break;
    case KEY_RIGHT:
      if(menuPadX<screenWidth-1) ++menuPadX;
      break;
      */
    }
  }
  endwin( ); // 端末制御を終了する。
  printf("%d x %d\n", screenWidth, screenHeight);
  printf("前景 %d, 背景 %d\n", screenForeground, screenBackground);
  printf("menuWin=%lx\n", (long)menuWin);
  return 0;
}
