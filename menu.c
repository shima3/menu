/*
  menu.c
  2022.6.27 by Kazuyuki Shima

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
  - セリカ式 キーコード
  http://www.kis-lab.com/serikashiki/man/ncurses_keycode.html
  - Ncurses 入門
  http://linuxmag.osdn.jp/Japanese/March2002/article233.shtml
  - man pages section 3: Curses Library Functions
  https://docs.oracle.com/cd/E86824_01/html/E54767/copywin-3curses.html
  - JIS X 0202:1998 (ISO/IEC 2022:1994)
  https://kikakurui.com/x0/X0202-1998-01.html
  - 2.7 対応している制御コード一覧 # エスケープシーケンス一覧
  https://kmiya-culti.github.io/RLogin/ctrlcode.html
  - 対応制御シーケンス
  https://ttssh2.osdn.jp/manual/4/ja/about/ctrlseq.html
  
  Bugs:
  - Docker内では、エスケープを入力すると次の文字を入力するまでブロックする。
  - コンソール内でエディタなどを起動すると表示が崩れる。
  - 恐らく、cursesライブラリのバグと考えられる。
  -- 背景色を変更したウィンドウが右端にあると左側のウィンドウの空行も同じ背景色になる。
  -- 画面の左右でウィンドウが異なる場合、全角文字の左半分がウインドウの右端のカラムに来たとき、
  --- 右側に別のウィンドウがあれば、右半分がはみ出る。
  --- 画面の右端であれば、次の行の左端から表示される。
  -- カーソルが左端にあるとき、バックスペースを出力しても１つ上の行に移動しない。

  Naming convention:
  - 型名 upper camel case
  - 定数 upper snake case
  - その他 lower camel case
*/
#include <stdio.h>
#define __USE_XOPEN2KXSI
#define __USE_XOPEN_EXTENDED
#define __USE_XOPEN2K
#include <stdlib.h>
// #include <ncurses.h>
#include <ncursesw/ncurses.h>
#include <locale.h>
#include <unistd.h>
#define __USE_BSD
#define __USE_MISC
#include <termios.h>
#include <sys/ioctl.h>	/* 44BSD requires this too */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>
#include <stdarg.h>

#define MENU_MAX_WIDTH 100
/* defined in termios.h
  #define CTRL(CH) ((CH)&0x1F)
*/

// #define DEBUG

#ifdef DEBUG
#define debug_printf(fmt, ...) debug_fprintFLf(stderr, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define STDERR_LOG debug_fprintFLf(stderr, __FILE__, __LINE__, "trace\n")
#else
#define debug_printf(fmt, ...)
#define STDERR_LOG
#endif
// #define STDERR_LOG fprintf(stderr, "%s %d: trace\n", __FILE__, __LINE__)

void debug_fprintFLf(FILE *out, char file[ ], int line, char fmt[ ], ...){
  va_list ap;
  char buf[1024];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  va_end(ap);
  fprintf(stderr, "%s %d: %s", file, line, buf);
}

typedef struct{
  char *title;
  char *command;
  char shortcut;
} MenuItem;

int screenWidth=0, screenHeight=0;
WINDOW *menuPad, *menuWin=NULL, *consoleWin, *commandWin;
int consoleWidth=0, consoleHeight=0;
int menuWidth=0, menuHeight=0, menuColumn;
int commandWidth=0, commandHeight=1;
MenuItem menuItems[ ]={
  {"T メニュー先頭", "", 'T'},
  {"L ファイル一覧", "ls", 'L'},
  {" ファイル詳細一覧", "ls -l", 0},
  {"C 作業フォルダ変更", "cd <<フォルダ名>>", 'C'},
  {" ホーム", "cd ~", 0},
  {" 上のフォルダ", "cd ..", 0},
  {"M メニュー操作類", "", 0},
  {"[ モード切替", "", '['},
  {"J コマンド実行", "", 0},
  {"↓ 下に移動", "", 0},
  {"↑ 上に移動", "", 0},
  {"Q メニュー終了", "", 'Q'},
  {" 異常に長いコマンド", "abcdefghi1abcdefghi2abcdefghi3abcdefghi4abcdefghi5abcdefghi6abcdefghi7abcdefghi8abcdefghi9abcdefgh10", 0},
  {NULL}
};
int menuItemNumber, menuItemWidth, menuItemSelect=0;
int childDie=FALSE;
int masterFD, slaveFD;

void makeMenuPad(){
  int x, y, i;

  menuItemWidth=0;
  menuPad=newpad(1, MENU_MAX_WIDTH);
  for(menuItemNumber=0;
      menuItems[menuItemNumber].title!=NULL; ++menuItemNumber){
    wmove(menuPad, 0, 0);
    waddstr(menuPad, menuItems[menuItemNumber].title);
    getyx(menuPad, y, x);
    if(menuItemWidth<x) menuItemWidth=x;
  }
  delwin(menuPad);

  menuPad=newpad(menuItemNumber, menuItemWidth);
  // wbkgd(menuPad, COLOR_PAIR(1));
  for(i=0; i<menuItemNumber; ++i){
    wmove(menuPad, i, 0);
    waddstr(menuPad, menuItems[i].title);
  }

  debug_printf("menuItemNumber=%d\n", menuItemNumber);
  debug_printf("menuItemWidth=%d\n", menuItemWidth);
}

void calculateMenuSize( ){
  menuColumn=screenWidth/menuItemWidth;
  if(menuColumn<1) menuColumn=1;
  menuHeight=(menuItemNumber+menuColumn-1)/menuColumn;
  menuColumn=(menuItemNumber+menuHeight-1)/menuHeight;
  menuWidth=menuItemWidth*menuColumn;

  debug_printf("menuColumn=%d\n", menuColumn);
  debug_printf("menuWidth=%d\n", menuWidth);
  debug_printf("menuHeight=%d\n", menuHeight);
}

#define TRACE STDERR_LOG
void redrawMenu(){
  int i;

  TRACE;
  calculateMenuSize( );
  mvwin(menuWin, screenHeight-menuHeight, 0);
  wresize(menuWin, menuHeight, menuWidth);
  werase(menuWin);
  TRACE;
  /*
  for(i=0; i<menuColumn; ++i){
    fprintf(stderr, "i=%d\n", i);
    copywin(menuPad, menuWin, menuHeight*i, 0, 0, menuItemWidth*i,
            menuHeight-1, menuItemWidth*(i+1)-1, FALSE);
  }
  */
  for(i=0; i<menuItemNumber; ++i){
    copywin(menuPad, menuWin, i, 0, i%menuHeight, i/menuHeight*menuItemWidth,
            i%menuHeight, (i/menuHeight+1)*menuItemWidth-1, FALSE);
  }
  /* int copywin(WINDOW *srcwin, WINDOW *dstwin,
     int sminrow,int smincol, int dminrow, int dmincol,
     int dmaxrow, int dmaxcol, int overlay);
     
     The copywin() routine provides a finer granularity of control over the overlay() and overwrite() routines. Like in the prefresh() routine, a rectangle is specified in the destination window, (dminrow, dmincol) and (dmaxrow, dmaxcol), and the upper-left-corner coordinates of the source window, (sminrow, smincol). If the argument overlay is true, then copying is non-destructive, as in overlay(). */
  TRACE;
}
#undef TRACE

/*
void redrawChoice(){
  mvwin(choiceWin, choiceY, screenWidth-menuWidth);
  werase(choiceWin);
  wmove(choiceWin, 0, 0);
  waddstr(choiceWin, menuItems[choiceY].title);
  overwrite(choiceWin, stdscr);
}
*/

void reverseSelect(){
  wmove(menuWin, menuItemSelect%menuHeight,
        menuItemSelect/menuHeight*menuItemWidth);
  wattrset(menuWin, A_REVERSE);
  waddstr(menuWin, menuItems[menuItemSelect].title);
  wattrset(menuWin, 0);
}

void consoleOutput(){
  char buf[1024];
  int len=0, i=0, j, x, y, ch, n, x2, y2;
  int count=0;

  // buf[0]=13; // carriage return
  for(;;){
    if(i>=len){
      getyx(consoleWin, y, x);

#ifdef DEBUG
      wprintw(commandWin, "(%d %d %d)", x, y, ++count); overwrite(commandWin, stdscr); // for debug
#endif

      scrollok(consoleWin, FALSE);
      ch = winch(consoleWin);
      wattrset(consoleWin, COLOR_PAIR(1));
      waddch(consoleWin, ch);
      wmove(consoleWin, y, x);
      wattrset(consoleWin, 0);

      overwrite(consoleWin, stdscr);
      touchwin(stdscr);
      refresh( );

      waddch(consoleWin, ch);
      wmove(consoleWin, y, x);
      scrollok(consoleWin, TRUE);

      len=read(masterFD, buf, sizeof(buf));
      if(len<=0) break;
      i=0;
      // getyx(consoleWin, y, x);
      // ch = winch(consoleWin);
    }
    switch(buf[i]){
    case 0x0D: // '\r':
      getyx(consoleWin, y, x);
      if(buf[++i] == 0x0A){ // '\n'
        if(y >= consoleHeight-1){
            wscrl(consoleWin, 1);
            wmove(consoleWin, consoleHeight-1, 0);
        }else wmove(consoleWin, y+1, 0);
        ++i;
      }else wmove(consoleWin, y, 0);
      break;
    case 0x1B:
      switch(buf[i+1]){
      case ' ':
      case 0x26:
        j=i+3;
        break;
      case '[': // 0x5B CSI
        j=i+2;
        if(buf[j] == '>') ++j;
        n=0;
        for(; j<len; ++j){
          if(buf[j] < '0' || buf[j] > '9') break;
          n = 10*n+buf[j]-'0';
        }
        switch(buf[j]){
        case 'A':
          getyx(consoleWin, y, x);
          if(n == 0) n=1;
          y-=n;
          if(y < 0) y=0;
          wmove(consoleWin, y, x);
          break;
        case 'B':
          getyx(consoleWin, y, x);
          if(n == 0) n=1;
          y+=n;
          if(y >= consoleHeight){
            wscrl(consoleWin, y-(consoleHeight-1));
            y=consoleHeight-1;
          }
          wmove(consoleWin, y, x);
          break;
        case 'C': // 0x43
          getyx(consoleWin, y, x);
          if(n == 0) n=1;
          x+=n;
          y+=x/consoleWidth;
          if(y >= consoleHeight){
            wscrl(consoleWin, y-(consoleHeight-1));
            y=consoleHeight-1;
          }
          x%=consoleWidth;
          wmove(consoleWin, y, x);
          break;
        case 'D':
          getyx(consoleWin, y, x);
          if(n == 0) n=1;
          x-=n;
          x=-x;
          y-=x/consoleWidth;
          if(y < 0) y=0;
          x%=consoleWidth;
          if(x>0) x=consoleWidth-x;
          wmove(consoleWin, y, x);
          break;
        case 'K': // 0x4B
          getyx(consoleWin, y, x);
          // leaveok(consoleWin, FALSE);
          switch(n){
          case 1:
            wmove(consoleWin, y, 0);
            for(n = 0; n < x; ++n)
              waddch(consoleWin, ' ');
            break;
          case 2:
            /*
            wmove(consoleWin, y, 0);
            for(n = 0; n < consoleWidth; ++n)
              waddch(consoleWin, ' ');
            */
            wdeleteln(consoleWin);
            break;
          default:
            /*
            for(n = x; n < consoleWidth; ++n)
              waddch(consoleWin, ' ');
            */
            wclrtoeol(consoleWin);
          }
          // leaveok(consoleWin, TRUE);
          wmove(consoleWin, y, x);
          break;
        case 'P':
          getyx(consoleWin, y, x);
          // if(n == 0) n=1;
          do wdelch(consoleWin); while(--n>0);
          /*
          for(x2 = x+n; x2 < consoleWidth; ++x2){
            wmove(consoleWin, y, x2);
            ch = winch(consoleWin);
            wmove(consoleWin, y, x2-n);
            waddch(consoleWin, ch);
          }
          while(--n >= 0)
            waddch(consoleWin, ' ');
          */
          wmove(consoleWin, y, x);
          break;
        case ';':
          while(++j<len)
            if(buf[j] < '0' || buf[j] > '9') break;
        }
        ++j;
        break;
      case ']': // 0x5D OSC
        for(j=i+2; j<len; ++j){
          if(buf[j] == 0x07){
            ++j;
            break;
          }
        }
        break;
      default:
        j=i+4;
      }
      /*
      waddnstr(commandWin, buf+i, j-i); // エスケープシーケンス表示
      overwrite(commandWin, stdscr);
      refresh( );
      waddnstr(consoleWin, buf+i, j-i);
      if(write(STDOUT_FILENO, buf+i, j-i)<=0) break;
      fsync(STDOUT_FILENO);
      */
      // wprintw(consoleWin, "[%d]", j-i);
      // for(++i; i<j; ++i) waddch(consoleWin, buf[i]);
#ifdef DEBUG
      scrollok(commandWin, TRUE);
      while(i<j) wprintw(commandWin, " %02X", buf[i++]);
      overwrite(commandWin, stdscr);
      touchwin(stdscr);
      refresh( );
#endif
      i=j;
      break;
    case CTRL('G'):
      if(write(STDOUT_FILENO, buf+i, 1)<=0) break;
      fsync(STDOUT_FILENO);
      ++i;
      break;
      /*
    case CTRL('J'): // '\n'
      getyx(consoleWin, y, x);
      if(y<consoleHeight-1) wmove(consoleWin, y+1, x);
      else wscrl(consoleWin, 1);
      ++i;
      break;
    case CTRL('M'): // '\r'
      getyx(consoleWin, y, x);
      wmove(consoleWin, y, 0);
      ++i;
      break;
      */
    default:
      switch(buf[i]&0xF0){ // UTF-8 多バイト文字
      case 0xC0:
      case 0xD0:
        j=i+2;
        break;
      case 0xE0:
        j=i+3;
        break;
      case 0xF0:
        j=i+4;
        break;
      default:
#ifdef DEBUG
        if(buf[i]<0x20 || buf[i]==0x7F){
          scrollok(commandWin, TRUE);
          wprintw(commandWin, " %02X", buf[i]);
          overwrite(commandWin, stdscr);
          touchwin(stdscr);
          refresh( );
        }
#endif
        j=i+1;
      }
      getyx(consoleWin, y, x);
      /*
      if(buf[i]==0x08){
        if(x==0){
          x=consoleWidth-1;
          --y;
        }else --x;
        wmove(consoleWin, y, x);
        wdelch(consoleWin);
      }else{
        waddnstr(consoleWin, buf+i, j-i);
        getyx(consoleWin, y2, x2);
        if(x2>0 && y2-y==1)
          wmove(consoleWin, y2, x2-1);
      }
      */
        waddnstr(consoleWin, buf+i, j-i);
        getyx(consoleWin, y2, x2);
        if(x2>0 && y2-y==1)
          wmove(consoleWin, y2, x2-1);
      i=j;
      // wprintw(consoleWin, "(%02x)", buf[i++]&0xFF);
    }
  }
  /*
    if(read(fdm, buf, sizeof(buf))!=1) break;
    if(buf[0]==10) write(STDOUT_FILENO, "\r", 1);
    if(write(STDOUT_FILENO, buf, len)!=len) break;
    fsync(STDOUT_FILENO);
  */
  childDie=TRUE;
}

void calculateConsoleSize( ){
  // consoleWidth=screenWidth-menuWidth;
  consoleWidth=screenWidth;
  consoleHeight=screenHeight-commandHeight-menuHeight;
}

#define TRACE STDERR_LOG
void redrawCommand( ){
  int len;
  // int x, y;

  TRACE;
  commandWidth = screenWidth;
  mvwin(commandWin, consoleHeight, 0);
  wresize(commandWin, commandHeight, commandWidth);
  werase(commandWin);
  wmove(commandWin, 0, 0);
  /*
    wattrset(commandWin, COLOR_PAIR(1));
    waddstr(commandWin, menuItems[choiceY].title);
    waddch(commandWin, '\n');
  */
  // wattrset(commandWin, 0);
  waddstr(commandWin, menuItems[menuItemSelect].command);

  TRACE;
  write(masterFD, "\x01\x0B", 2); // Ctrl+A Ctrl+K
  len=strlen(menuItems[menuItemSelect].command);
  write(masterFD, menuItems[menuItemSelect].command, len);
  write(masterFD, "\x01", 1); // Ctrl+A
  fsync(masterFD);

  /*
  scrollok(consoleWin, FALSE);
  getyx(consoleWin, y, x);
  waddstr(consoleWin, menuItems[menuItemSelect].command);
  overwrite(consoleWin, stdscr);
  touchwin(stdscr);
  refresh( );
  wmove(consoleWin, y, x);
  wclrtobot(consoleWin);
  scrollok(consoleWin, TRUE);
  */

  TRACE;
}
#undef TRACE

void writeCommand(char command[ ]){
  int len;
  
  len=strlen(command);
  if(len>0){
    write(masterFD, command, len);
    write(masterFD, "\n", 1);
    fsync(masterFD);
  }
}

#define TRACE STDERR_LOG
int menuMode(){
  int ch, i, x, y, shortcut;
  char buf[1024];
  MenuItem *item;

  for(;;){
    TRACE;
    // write(fdm, "\r", 1); fsync(fdm);
    getmaxyx(stdscr, screenHeight, screenWidth); // スクリーンサイズを取得する。

    TRACE;

    redrawMenu( );

    TRACE;
    reverseSelect( );

    TRACE;
    overwrite(menuWin, stdscr);

    TRACE;

    calculateConsoleSize( );
    wresize(consoleWin, consoleHeight, consoleWidth);
    overwrite(consoleWin, stdscr);

    TRACE;

    redrawCommand( );
    overwrite(commandWin, stdscr);

    TRACE;
    
    // redrawChoice();
    touchwin(stdscr);
    refresh( );
    ch=getch( ); // キーボードから文字を入力する。

    TRACE;

    switch(ch){
    case KEY_UP:
      if(menuItemSelect > 0)
        --menuItemSelect;
      break;
    case KEY_DOWN:
      if(menuItemSelect < menuItemNumber-1)
        ++menuItemSelect;
      break;
    case KEY_LEFT:
      if(menuItemSelect >= menuHeight)
        menuItemSelect -= menuHeight;
      break;
    case KEY_RIGHT:
      if(menuItemSelect < menuItemNumber-menuHeight)
        menuItemSelect += menuHeight;
      break;
    case '\n':

      TRACE;

      item=&menuItems[menuItemSelect];
      switch(item->shortcut){
      case 'Q':
        return FALSE;
      case '[':
        return TRUE;
      }
      writeCommand(item->command);
      break;
      /*
    case 0x1B: // escape key
      return TRUE;
      */
    default:

      TRACE;

      for(i=0; i<menuItemNumber; ++i){
        shortcut=menuItems[i].shortcut;
        if(shortcut!=0){
          if(CTRL(shortcut) == ch){
            switch(shortcut){
            case 'Q':
              return FALSE;
            case '[':
              return TRUE;
            }
            writeCommand(menuItems[i].command);
            break;
          }
          if(toupper(shortcut) == toupper(ch)){
            menuItemSelect=i;
            break;
          }
        }
      }
    }
  }
}
#undef TRACE

void consoleMode( ){
  int buf[1];

  redrawMenu( );
  overwrite(menuWin, stdscr);
  touchwin(stdscr);
  refresh( );
  for(;;){
    buf[0]=getch( );
    switch(buf[0]){
    case 0x1B:
      return;
    case CTRL('L'):
      werase(consoleWin);
      overwrite(consoleWin, stdscr);
      touchwin(stdscr);
      refresh( );
      break;
    case KEY_UP:
      buf[0]=CTRL('P');
      write(masterFD, buf, 1);
      fsync(masterFD);
      break;
    case KEY_DOWN:
      buf[0]=CTRL('N');
      write(masterFD, buf, 1);
      fsync(masterFD);
      break;
    case KEY_LEFT:
      buf[0]=CTRL('B');
      write(masterFD, buf, 1);
      fsync(masterFD);
      break;
    case KEY_RIGHT:
      buf[0]=CTRL('F');
      write(masterFD, buf, 1);
      fsync(masterFD);
      break;
    case KEY_BACKSPACE:
      buf[0]=CTRL('H');
      write(masterFD, buf, 1);
      fsync(masterFD);
      break;
    default:
      /* for debug
      wmove(commandWin, 0, 0);
      wprintw(commandWin, "(%d)", buf[0]);
      overwrite(commandWin, stdscr);
      touchwin(stdscr);
      refresh( );
      */
      write(masterFD, buf, 1);
      fsync(masterFD);
    }
  }
}

#define TRACE STDERR_LOG
int main(int argc, char *argv[ ]){
  int x=0, y=0, curX=0, curY=0;
  // char buf[1024];
  short foreground, background;
  struct winsize ws0, ws;
  struct termios term, term0stdin, term0stdout, term0stderr;
  char *argv2[]={
    //    "/usr/local/bin/bash",
    "/bin/bash",
    "-i",
    NULL
  };
  int pid;
  pthread_t thread;

  // 端末の状態を保存する。
  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws0)!=-1)
    printf("(%d, %d)\n", ws0.ws_col, ws0.ws_row);  // (幅, 高さ)
  else perror("ioctl");
  tcgetattr(STDIN_FILENO, &term0stdin);
  tcgetattr(STDOUT_FILENO, &term0stdout);
  tcgetattr(STDERR_FILENO, &term0stderr);

  TRACE;

  // setlocale(LC_ALL, "ja_JP.UTF-8"); // ロケールをja_JP.UTF-8に設定する。
  setlocale(LC_ALL, ""); // 環境変数に従ってロケールを設定する。
  // printf("%s\n", setlocale(LC_ALL, NULL)); // 現在のロケールを確認する。

  initscr( ); // スクリーンを初期化する。
  // curs_set(TRUE); // カーソルを表示するモードに設定する。
  curs_set(FALSE); // カーソルを表示しないモードに設定する。
  // getmaxyx(stdscr, screenHeight, screenWidth); // スクリーンサイズを取得する。
  // ch=inch( ); // スクリーン上のカーソル位置にある文字を読み取る。
  // getstr(str); // キーボードから文字列を入力する。
  // instr(str); //  スクリーン上のカーソル位置にある文字列を読み取る。
  // nodelay(stdscr, TRUE); // 非ブロッキングモードに設定する。入力なし=ERR=-1
  // timeout(10); // 入力の待ち時間を10msに設定する。
  // timeout(0); // 非ブロッキングモードに設定する。
  // timeout(-1); // ブロッキングモードに設定する。
  // scrollok(menu, TRUE); // winをスクロールできるように設定する。
  getmaxyx(stdscr, screenHeight, screenWidth); // スクリーンサイズを取得する。

  TRACE;
  
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
  // leaveok(stdscr, TRUE); // 論理カーソルが物理カーソルの位置に移動する。
  // leaveok(stdscr, FALSE); // 物理カーソルの位置を元に戻す。

  TRACE;

  start_color( ); // カラーを有効にする。
  pair_content(0, &foreground, &background);
  init_pair(1, background, foreground);
  init_pair(2, foreground, background);
  use_default_colors( ); // 端末のデフォルトの配色を利用する。
  /* bkgd(COLOR_PAIR(0)); */

  TRACE;

  makeMenuPad( );
  calculateMenuSize( );
  menuWin = subwin(stdscr, menuHeight, menuWidth, screenHeight-menuHeight, 0);
  if(menuWin == NULL){
    fprintf(stderr, "Failed to create a menu window.\n");
    exit(1);
  }

  TRACE;

  calculateConsoleSize( );
  consoleWin=newwin(consoleHeight, consoleWidth, 0, 0); // ウィンドウを作成する 。
  if(consoleWin == NULL){
    fprintf(stderr, "Failed to create a console window.\n");
    exit(1);
  }
  scrollok(consoleWin, TRUE); // スクロールできるように設定する。

  TRACE;

  // commandWidth=screenWidth-menuWidth;
  commandWidth=screenWidth;
  commandWin=subwin(stdscr, commandHeight, commandWidth, consoleHeight, 0);
  if(commandWin==NULL){
    fprintf(stderr, "Failed to create a command window.\n");
    exit(1);
  }
  // scrollok(commandWin, TRUE); // スクロールできるように設定する。
  wbkgd(commandWin, COLOR_PAIR(1));

  TRACE;

  // 端末の状態を変更する。
  /*
  ws=ws0;
  ws.ws_col=consoleWidth;
  ws.ws_row=consoleHeight;
  if(ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws)) // ウィンドウサイズを設定する。
    perror("ioctl");
  cfmakeraw(&term);
  tcsetattr(STDIN_FILENO, TCSANOW, &term);
  printf("\033c"); // ANSI reset command
  */
  /*
  term=term0stdout;
  term.c_oflag &= ~OPOST;
  // term.c_oflag |= OPOST;
  // term.c_oflag |= ONLCR;
  tcsetattr(STDOUT_FILENO, TCSANOW, &term);
  */

  masterFD=posix_openpt(O_RDWR); // 疑似端末を開く
  if(masterFD<0){
    perror("posix_openpt");
    exit(1);
  }
  if(grantpt(masterFD)!=0) // 疑似端末スレーブをアクセス可能にする
    perror("grantpt");
  if(unlockpt(masterFD)!=0) // 疑似端末の内部的なロックを解除する。
    perror("unlockpt");
  /*
  term=term0stdout;
  term.c_lflag &= ~ECHO; // エコーしない。
  tcsetattr(fdm, TCSANOW, &term);
  */

  TRACE;

  pid=fork( );
  if(pid==0){ // child
    slaveFD=open(ptsname(masterFD), O_RDWR);
    close(masterFD);
    if(slaveFD<0){
      perror("Error: open slave PTY");
      exit(1);
    }
    if(setsid( )<0) // 警告（Inappropriate ioctl for device）を避けるため
      perror("setsid");
    /*
    ws=ws0;
    ws.ws_col=consoleWidth;
    ws.ws_row=consoleHeight;
    if(ioctl(slaveFD, TIOCSWINSZ, &ws)==-1) // ウィンドウサイズを設定する。
      perror("ioctl");
    */
    // tcsetattr(fds, TCSANOW, &term);
    dup2(slaveFD, STDIN_FILENO);
    dup2(slaveFD, STDOUT_FILENO);
    dup2(slaveFD, STDERR_FILENO);
    close(slaveFD);
    // ioctl(STDIN_FILENO, TIOCSCTTY, 1);
    // setenv("PS1", "\r\n$ ", 1);

    ws=ws0;
    ws.ws_col=consoleWidth;
    ws.ws_row=consoleHeight;
    if(ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws)==-1) // ウィンドウサイズを設定する。
      perror("ioctl");

    execvp(argv2[0], argv2);
    perror("execvp");
    return errno;
  }
  // parent
  pthread_create(&thread, NULL, (void*(*)(void*))consoleOutput, NULL);
  sleep(2);

  /* WINDOW *subwin(WINDOW *orig, int lines, int cols, int y, int x);
     lines行，cols列の新しいウィンドウを指定したウィンドウのy行，x列目に作成します．
     x, y は画面stdscrの絶対座標 */
  // menuHeight=screenHeight;
  // menuWin=subwin(stdscr, screenHeight, menuWidth, 0, 0);
  // printf("%d %d %d %d\n", screenHeight, menuWidth, screenWidth, menuWidth);
  // menuWin=subwin(stdscr, screenHeight, menuWidth, 0, screenWidth-menuWidth);
  // menu=newwin(menuHeight, menuWidth, 0, screenWidth-menuWidth-1);
  // leaveok(menuWin, TRUE); // 物理カーソルの位置を元に戻さない。
  // leaveok(menuWin, FALSE); // 論理カーソルを物理カーソルの位置に戻す。
  // wbkgd(menuWin, COLOR_PAIR(1));
  // wvline(menuframe, 0, menuHeight);
  // wcolor_set(menu, 1, NULL);
  // wattrset(menu, COLOR_PAIR(0) | A_REVERSE);
  // werase(menu);
  // wprintw(menuWin, "ESCで終了\n");
  // wclear(menu);
  // wbkgd(stdscr, COLOR_PAIR(0));
  // wrefresh(menu);

  // wbkgd(consoleWin, 0);
  /* WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int y, int x);
     nlines行，ncols列の新しいウィンドウをスクリーンのy行，x列目に作成する。
     指定したウィンドウと文字列バッファを共有する。*/
  // consoleWidth=screenWidth-menuWidth-1;
  // consoleWin=newwin(screenHeight, consoleWidth, 0, menuWidth+1); // ウィンドウを作成する 。
  // menu=subwin(stdscr, 10, 20, 10, 10); // ウィンドウを作成する。
  // log=newwin(screenHeight, screenWidth, 0, 0); // ウィンドウを作成する。
  // leaveok(consoleWin, TRUE);
  // leaveok(consoleWin, FALSE); // 物理カーソルが論理カーソルの位置に戻る。
  // wbkgd(consoleWin, COLOR_PAIR(2));
  // wprintw(logWin, "ログ\n");

  // leaveok(commandWin, TRUE); // 物理カーソルの位置を元に戻す。
  // leaveok(commandWin, FALSE); // 物理カーソルの位置を元に戻す。

  // choiceWin=newwin(1, screenWidth, choiceY, 0);
  // choiceWin=newwin(1, menuWidth, choiceY, 0);
  /*
  choiceWin=newwin(1, menuWidth, choiceY, 0);
  if(choiceWin==NULL){
    fprintf(stderr, "Failed to create a choice window.\n");
    exit(1);
  }
  wbkgd(choiceWin, COLOR_PAIR(2));
  */
  // wbkgd(choiceWin, COLOR_PAIR(1));
  // leaveok(choiceWin, TRUE); //  論理カーソルは物理カーソルの位置になる。
  // leaveok(choiceWin, FALSE); // 物理カーソルの位置を元に戻す。

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

  // write(fdm, "top\n", 4); fsync(fdm);
  // strcpy(buf, "nano makefile\n"); write(fdm, buf, strlen(buf)); fsync(fdm);
  // strcpy(buf, "export PS1=\"$  \"\n"); write(fdm, buf, strlen(buf));
  // strcpy(buf, "echo $PS1\n"); write(fdm, buf, strlen(buf));
  // fsync(fdm);

  TRACE;

  while(menuMode( )) consoleMode( );

  TRACE;

  tcsetattr(STDOUT_FILENO, TCSANOW, &term0stdin);
  tcsetattr(STDOUT_FILENO, TCSANOW, &term0stdout);
  tcsetattr(STDERR_FILENO, TCSANOW, &term0stderr);
  if(ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws0)) // ウィンドウサイズを設定する。
    perror("ioctl");
  printf("\033c"); // ANSI reset command
  endwin( ); // 端末制御を終了する。
  /*
  printf("%d x %d\n", screenWidth, screenHeight);
  printf("前景 %d, 背景 %d\n", screenForeground, screenBackground);
  printf("menuWin=%lx\n", (long)menuWin);
  */
  // printf("(%d, %d)\n", ws0.ws_col, ws0.ws_row);  // (幅, 高さ)
  debug_printf("menuItemNumber=%d\n", menuItemNumber);
  debug_printf("menuItemWidth=%d\n", menuItemWidth);
  debug_printf("screenWidth=%d\n", screenWidth);
  debug_printf("screenHeight=%d\n", screenHeight);
  debug_printf("menuColumn=%d\n", menuColumn);
  return 0;
}
#undef TRACE
