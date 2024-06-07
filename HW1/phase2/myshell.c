/* $begin shellmain */
#include "myshell.h"

int main()
{
    char cmdline[MAXLINE];
    siginit();

    while (1)
    {
        printf("20211584: myshell> ");
        fgets(cmdline, MAXLINE, stdin);

        eval(cmdline);
        printreaped(); //print reaped child
        fflush(stdout);
    }
}

