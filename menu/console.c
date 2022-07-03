/*
  - Using pseudo-terminals (pty) to control interactive programs
  http://www.rkoucha.fr/tech_corner/pty_pdip.html
  - 例解UNIX/Linuxプログラミング教室 システムコールを使いこなすための12講
  https://books.google.co.jp/books?id=JldaDwAAQBAJ&pg=PA428&lpg=PA428&dq=posix_openpt+grantpt&source=bl&ots=NDNGAPdkvc&sig=ACfU3U3Odcn9nQHhSfgQ6T3ILgJ79G_gMw&hl=ja&sa=X&ved=2ahUKEwjwrdOHmNb4AhWJBLcAHdBMC-sQ6AF6BAgZEAM#v=onepage&q=posix_openpt%20grantpt&f=false
  - C言語系 / 「デーモン君のソース探検」読書メモ / 12, script(1)
  https://www.glamenv-septzen.net/view/563
  - 
  https://www.yendor.com/programming/unix/apue/lib.svr4/ptyfork.c
  - How to reset a broken TTY?
  https://superuser.com/questions/640338/how-to-reset-a-broken-tty
 */
#include <stdio.h>
#define __USE_XOPEN_EXTENDED
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#define __USE_BSD
#include <termios.h>
#include <sys/ioctl.h>	/* 44BSD requires this too */

#define FALSE (0)
#define TRUE (!FALSE)

// int childin, childout;
int childDie=FALSE;
int fdm, fds;

void logChildIn(void){
  char buf[1024];
  int len, i;

  fprintf(stderr, "logChildIn 1\n");
  /* 最初の警告を無視するため
  for(;;){
    len=read(fdm, buf, sizeof(buf));
    if(len<=0) break;
    for(i=0; i<len; ++i)
      if(buf[i]=='$') break;
    if(buf[i]=='$'){
      write(1, buf, len);
      break;
    }
  }
  */
  for(;;){
    // len=read(childin, buf, sizeof(buf));
    len=read(fdm, buf, sizeof(buf));
    if(len<=0) break;
    // fprintf(stderr, "len=%d\n", len);
    write(STDOUT_FILENO, buf, len);
  }
  childDie=TRUE;
  fprintf(stderr, "logChildIn 2\r\n");
  fflush(stderr);
  // close(childin);
}

void loop(){
  char buf[1024];
  int len;
  int ch;

  fprintf(stderr, "loop 1\r\n"); fflush(stderr);
  for(;;){
    ch=getchar();
    if(ch==EOF) break;
    write(fdm, &ch, 1);
    if(ch==12){
      fsync(fdm);
      printf("\033c");
    }
    /*
    len=read(STDIN_FILENO, buf, sizeof(buf));
    if(len<=0) break;
    if(write(fdm, buf, len)!=len) break;
    fsync(fdm);
    */
  }
  childDie=TRUE;
  fprintf(stderr, "loop 2\r\n"); fflush(stderr);
}

int main(){
  int pid;
  char *argv2[]={
    //    "/usr/local/bin/bash",
    "/bin/bash",
    "-i",
    NULL
  };
  pthread_t thread;
  struct termios term, term2, stdout_term, stderr_term;
  char buf[1024];
  int len;
  struct winsize ws, ws2;

  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)!=-1)
    printf("(%d, %d)\n", ws.ws_col, ws.ws_row);  // (幅, 高さ)
  else perror("ioctl");

  // tcsetattr(fds, TCSANOW, &term2);
  // tcsetattr(fds, TCSAFLUSH, &term2);
  /*
  stderr_term.c_lflag |= OPOST;
  stderr_term.c_lflag |= ONLCR;
  stderr_term.c_lflag &= ~ONLRET;
  // tcsetattr(STDERR_FILENO, TCSAFLUSH, &stderr_term);
  */
  tcgetattr(STDIN_FILENO, &term);
  tcgetattr(STDOUT_FILENO, &stdout_term);
  tcgetattr(STDERR_FILENO, &stderr_term);
  term2=term;
  cfmakeraw(&term2);
  // term2.c_lflag &= ~ECHO; // rawのとき不要かも
  tcsetattr(STDIN_FILENO, TCSANOW, &term2);

  ws2=ws;
  ws2.ws_col=ws.ws_col>10? ws.ws_col-10: 0;
  ws2.ws_row=ws.ws_row>10? ws.ws_row-10: 0;
  if(ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws2)) // ウィンドウサイズを設定する。
    perror("ioctl");
  printf("\033c");
  printf("ws2=(%d, %d)\r\n", ws2.ws_col, ws2.ws_row);

  fdm=posix_openpt(O_RDWR); // 疑似端末を開く
  if(fdm<0){
    perror("posix_openpt");
    exit(1);
  }
  if(grantpt(fdm)!=0) // 疑似端末スレーブをアクセス可能にする
    perror("grantpt");
  if(unlockpt(fdm)!=0) // 疑似端末の内部的なロックを解除する。
    perror("unlockpt");
  pid=fork( );
  if(pid==0){ // child
    strcpy(buf, ptsname(fdm));
    printf("slave %s\r\n", buf);
    fds=open(buf, O_RDWR);
    if(fds<0){
      perror("Error: open slave PTY");
      exit(1);
    }
    close(fdm);
    /*
    if(ioctl(fds, TIOCSCTTY, (char *)0) < 0)
      perror("TIOCSCTTY error");
    if(tcsetpgrp(fds, getpid( ))) // bashが行うらしい
      perror("tcsetpgrp");
    */
    dup2(fds, STDIN_FILENO);
    dup2(fds, STDOUT_FILENO);
    dup2(fds, STDERR_FILENO);
    if(setsid( )<0) // 警告（Inappropriate ioctl for device）を避けるため
      perror("setsid");
    // if(ioctl(fds, TIOCSWINSZ, &ws2)==-1) // ウィンドウサイズを設定する。
    if(ioctl(fds, TIOCSWINSZ, &ws2)) // ウィンドウサイズを設定する。
      perror("ioctl");
    close(fds);
    // ioctl(STDIN_FILENO, TIOCSCTTY, 1);
    execvp(argv2[0], argv2);
    fprintf(stderr, "main 3\n");
    perror("exec error");
    return errno;
  }
  // parent
  fprintf(stderr, "main 1\r\n");
  // pthread_create(&thread, NULL, (void*(*)(void*))logChildIn, NULL);
  pthread_create(&thread, NULL, (void*(*)(void*))loop, NULL);

  for(;;){
    if(childDie) break;
    len=read(fdm, buf, sizeof(buf));
    if(childDie || len<=0) break;
    // fprintf(stderr, "len=%d\n", len);
    if(write(STDOUT_FILENO, buf, len)!=len) break;
    fsync(STDOUT_FILENO);
  }
  fprintf(stderr, "main 2\r\n"); fflush(stderr);

  /*
  stdout_term.c_lflag |= OPOST;
  stdout_term.c_lflag |= ONLCR;
  stdout_term.c_lflag &= ~ONLRET;
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &stdout_term);
  */

  waitpid(pid, NULL, 0);
  close(fdm);

  if(ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws)) // ウィンドウサイズを設定する。
    perror("ioctl");

  // term.c_lflag |= ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &term); // 端末の設定を元に戻す
  tcsetattr(STDOUT_FILENO, TCSANOW, &stdout_term);
  tcsetattr(STDERR_FILENO, TCSANOW, &stderr_term);

  printf("\033c"); // ANSI reset command

  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)!=-1)
    printf("(%d, %d)\n", ws.ws_col, ws.ws_row);  // (幅, 高さ)
  else perror("ioctl");

  return 0;
}
