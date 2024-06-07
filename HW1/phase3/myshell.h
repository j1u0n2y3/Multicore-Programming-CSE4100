#include "csapp.h"
#include <errno.h>

#define MAXARGS 128
#define MAXHISTORY 4000
char history[MAXHISTORY][MAXARGS]; // history array
int history_top = 1;               // history top idx (same with cardinality)

typedef struct job_entry *job_link; // linked
typedef struct job_entry
{
    char cmdline[MAXLINE];
    int job_count;
    int job_id;    // job id
    pid_t pid;     // job pid
    int job_state; // job state -> -1:UNASSIGNED, 0:BACKGROUND, 1:FOREGROUND, 2:STOPPED, 3:DONE, 4:TERMINATED
    job_link link;
} Job_Entry;

job_link jobs_front = NULL;
job_link jobs_rear = NULL;
int job_count = 0;

sigset_t mask1, mask2, prev1;
volatile sig_atomic_t fg_flag = 0;   // if foreground job is terminated, fg_flag=1;
volatile sig_atomic_t pipe_flag = 0; // if pipe execution is terminated, pipe_flag=1;

/* function headers */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv);
int ExecPipe(char **p_command, int p_command_cnt);
void Exec(char *filename, char *const argv[], char *const envp[]);
void cmdprintingjobs();
void cmdkill(char **argv);
void cmdbg(char **argv);
void cmdfg(char **argv);
void cmdnormalize(char *cmdline);
void jobinit();
Job_Entry *jobadd(pid_t pid, char **argv, int job_state);
void jobdelete(int job_id);
int jobgetid(pid_t pid);
int jobgetnextid();
Job_Entry *jobget(pid_t pid);
void printreaped();
void siginit();
void SIGINThandler(int sig);
void SIGSTPhandler(int sig);
void SIGCHLDhandler(int sig);
void SIGCHLDpipehandler(int sig);

void cmdprintjobs()
{
    Job_Entry *ptr;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        Sio_puts("[");
        Sio_putl((long)(ptr->job_id));
        Sio_puts("]");

        switch (ptr->job_state)
        {
        case 2:
            Sio_puts(" STOPPED\t");
            break;
        case 0:
            Sio_puts(" RUNNING\t");
            break;
        case 1:
            Sio_puts(" RUNNING\t");
            break;
        case 3:
            Sio_puts(" DONE\t");
            break;
        case 4:
            Sio_puts(" TERMINATED\t");
            break;
        }

        Sio_puts(ptr->cmdline);
        if (ptr->job_state == 0)
            Sio_puts("&");
        Sio_puts("\t");
        Sio_putl((long)(ptr->pid));
        Sio_puts("\n");
    }

    for (ptr = jobs_front; ptr != NULL;)
    {
        Job_Entry *next_ptr = ptr->link;
        if (ptr->job_state == 3 || ptr->job_state == 4)
            jobdelete(ptr->job_id);
        ptr = next_ptr;
    }
}

void cmdhistory()
{ // history
    int i;
    for (i = 1; i < history_top; i++)
    {
        Sio_putl((long)i);
        Sio_puts("\t");
        Sio_puts(history[i]);
        Sio_puts("\n");
    }
}

void cmdkill(char **argv) // kill (fg process)
{
    Job_Entry *ptr;
    char *id = argv[1];
    pid_t pid = 0;

    if (id[0] != '%') // pid
    {
        pid_t temp_pid = atoi(id);
        for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
        {
            if (ptr->pid == temp_pid) // pid exist
            {
                pid = ptr->pid;
                break;
            }
        }
    }
    else // job_id (%)
    {
        int job_id = atoi(id + 1);
        for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
        {
            if (ptr->job_id == job_id) // job exist
            {
                pid = ptr->pid;
                break;
            }
        }
    }
    if (pid != 0) // exist
    {
        if (ptr->job_state == 2) // stopped
        {
            Sio_puts("[");
            Sio_putl((long)(ptr->job_id));
            Sio_puts("]");
            Sio_puts(" STOPPED	");
            Sio_putl((long)(ptr->pid));
            Sio_puts(" ");
            Sio_puts(ptr->cmdline);
            Sio_puts("\n");
        }
        kill(-pid, SIGCONT); // SIGCONT
        kill(-pid, SIGINT);  // SIGINT to fg processes
    }
    else
        Sio_puts("proccess id does not exist in job lists\n");
    return;
}

void cmdbg(char **argv) // bg
{
    pid_t pid = 0;
    Job_Entry *ptr;
    Job_Entry *last_stopped; // stopped latest
    last_stopped = NULL;
    if (argv[1] == NULL) // job_id X
    {
        for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
        {
            if (ptr->job_state == 2) // find stopped job
                last_stopped = ptr;
        }
        if (last_stopped != NULL)
        {
            Sio_puts("[");
            Sio_putl((long)(last_stopped->job_id));
            Sio_puts("] ");
            Sio_putl((long)(last_stopped->pid));
            Sio_puts(" ");
            Sio_puts(last_stopped->cmdline);
            Sio_puts("&\n");
            last_stopped->job_state = 0;         // job_state > bg running
            kill(-(last_stopped->pid), SIGCONT); // SIGCONT
        }
        else
            Sio_puts("already running in background or being no such jobs\n");
    }
    else // job_id O
    {
        int job_id;
        if (argv[1][0] == '%')
            job_id = atoi(&argv[1][1]);
        else
            job_id = atoi(argv[1]);

        for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
        {
            if (ptr->job_id == job_id && ptr->job_state == 2) // find job
            {
                pid = ptr->pid;
                break;
            }
        }

        if (pid != 0)
        {
            Sio_puts("[");
            Sio_putl((long)(ptr->job_id));
            Sio_puts("] ");
            Sio_putl((long)(ptr->pid));
            Sio_puts(" ");
            Sio_puts(ptr->cmdline);
            Sio_puts("&\n");
            ptr->job_state = 0;  // job_state > bg running
            kill(-pid, SIGCONT); // SIGCONT
        }
        else
            Sio_puts("already running in background or being no such jobs\n");
    }
}

void cmdfg(char **argv) // fg
{
    pid_t pid = 0;
    sigset_t prev2;
    Sigemptyset(&prev2);
    Job_Entry *ptr;
    Job_Entry *last_stopped;    // stopped latest
    Job_Entry *last_background; // last background
    last_stopped = NULL;
    last_background = NULL;
    if (argv[1] == NULL) // job_id X
    {
        for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
        {
            if (ptr->job_state == 2) // find stopped job at job list
            {
                last_stopped = ptr;
            }
            if (ptr->job_state == 0)
            {
                last_background = ptr;
            }
        }
        if (last_stopped != NULL)
        {

            last_stopped->job_state = 1; // job_state > fg running
            Sio_puts(last_stopped->cmdline);
            Sio_puts("\n");
            kill(-(last_stopped->pid), SIGCONT); // bg process < SIGCONT
            tcsetpgrp(0, last_stopped->pid);
            while (!fg_flag)
                Sigsuspend(&prev2);
            fg_flag = 0;

            Signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(0, getpid());
            Signal(SIGTTOU, SIG_DFL);
        }
        else if (last_background != NULL)
        {

            last_background->job_state = 1; // job_state > fg running
            Sio_puts(last_background->cmdline);
            Sio_puts("\n");
            kill(-(last_background->pid), SIGCONT); // bg process < SIGCONT
            tcsetpgrp(0, last_background->pid);
            while (!fg_flag)
                Sigsuspend(&prev2);
            fg_flag = 0;
            Signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(0, getpid());
            Signal(SIGTTOU, SIG_DFL);
        }
        else
        {
            Sio_puts("jobs do not exist for fg.\n");
        }
    }
    else // job_id O
    {
        int job_id;
        if (argv[1][0] == '%')
            job_id = atoi(&argv[1][1]);
        else
            job_id = atoi(argv[1]);
        for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
        {
            if (ptr->job_id == job_id) // find job by job_id
            {
                pid = ptr->pid;
                break;
            }
        }
        if (pid != 0) // job exist
        {
            ptr->job_state = 1; // job_state > fg running
            Sio_puts(ptr->cmdline);
            Sio_puts("\n");
            kill(-pid, SIGCONT); // bg process < SIGCONT

            tcsetpgrp(0, ptr->pid);

            while (!fg_flag)
                Sigsuspend(&prev2);
            fg_flag = 0;
            Signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(0, getpid());
            Signal(SIGTTOU, SIG_DFL);
        }
        else
            Sio_puts("job does not exist in job lists\n");
    }
}

Job_Entry *jobadd(pid_t pid, char **argv, int job_state) // add job to job list and return
{
    Job_Entry *new_job = (Job_Entry *)malloc(sizeof(Job_Entry));

    char cmdline[MAXLINE] = "";
    for (int i = 0; argv[i] != NULL; i++) // make complete cmdline
    {
        strcat(cmdline, argv[i]);
        strcat(cmdline, " ");
    }

    int job_number = jobgetnextid();
    new_job->pid = pid;
    if (job_state == 0) // bg process
    {
        new_job->job_id = job_number;
        new_job->job_count = job_count++;
    }
    // no job id (instantly reaped after termination)
    else if (job_state == 1) // fg process
        new_job->job_id = -1;
    new_job->job_state = job_state;
    new_job->link = NULL;
    strcpy(new_job->cmdline, cmdline);

    /* adding to linked list */
    if (jobs_front == NULL)
    {
        jobs_front = new_job;
        jobs_rear = new_job;
    }
    else
    {
        jobs_rear->link = new_job;
        jobs_rear = new_job;
    }

    return new_job;
}

void jobdelete(int job_id) // delete job by job_id
{
    Job_Entry *ptr;
    Job_Entry *prev_ptr = jobs_front;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        if (ptr->job_id == job_id) // job exist
            break;
        prev_ptr = ptr;
    }
    if (ptr != NULL) // exist > unlist
    {
        if (ptr == jobs_front && ptr == jobs_rear) // #1
        {
            jobs_front = NULL;
            jobs_rear = NULL;
            free(ptr);
        }
        else if (ptr == jobs_front) // head
        {
            jobs_front = ptr->link;
            free(ptr);
        }
        else if (ptr == jobs_rear) // tail
        {
            jobs_rear = prev_ptr;
            jobs_rear->link = NULL;
            free(ptr);
        }
        else // else
        {
            prev_ptr->link = ptr->link;
            free(ptr);
        }
    }
    else // not exist
        Sio_puts("cannot find job id in jobs\n");
}

int jobgetid(pid_t pid) // return job_id of pid
{
    Job_Entry *ptr;
    int res = 0;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        if (ptr->pid == pid) // pid exists
            res = ptr->job_id;
        break;
    }
    return res;
}

int jobgetnextid() // get next job id
{
    Job_Entry *ptr;
    int job_number = 1;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        if (ptr->job_id >= job_number)
            job_number = ptr->job_id + 1;
    }
    return job_number;
}

Job_Entry *jobget(pid_t pid) // return job of pid
{
    Job_Entry *ptr;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        if (ptr->pid == pid) // pid exist
            return ptr;
    }
    return NULL; // not exist
}

void printreaped() // printing done or terminated bg processes
{
    Job_Entry *ptr;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        if (ptr->job_state == 3 || ptr->job_state == 4) // DONE or TERMINATED
        {
            Sio_puts("[");
            Sio_putl((long)(ptr->job_id));
            Sio_puts("] ");
            if (ptr->job_state == 3)
                Sio_puts(" DONE ");
            else
                Sio_puts(" TERMINATED ");
            Sio_putl((long)(ptr->pid));
            Sio_puts(" ");
            Sio_puts(ptr->cmdline);
            Sio_puts("\n");
        }
    }
    for (ptr = jobs_front; ptr != NULL;)
    {
        Job_Entry *next_ptr = ptr->link;
        if (ptr->job_state == 3 || ptr->job_state == 4) // deleting done or terminated process from job list
        {
            jobdelete(ptr->job_id);
        }
        ptr = next_ptr;
    }
}

void siginit()
{
    Sigfillset(&mask1);
    Sigemptyset(&mask2);
    Sigaddset(&mask2, SIGCHLD);
    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGCHLD, SIGCHLDhandler);
    Signal(SIGTSTP, SIGSTPhandler);
    Signal(SIGINT, SIGINThandler);
    pid_t pid = getpid();
    setpgid(pid, pid);
    tcsetpgrp(0, pid);
}

void SIGINThandler(int sig) // fg kill
{
    pid_t pid = 0;
    Job_Entry *ptr;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link) // fg job
    {
        if (ptr->job_state == 1)
        {
            pid = ptr->pid;
            break;
        }
    }
    if (pid != 0)
    {
        kill(-pid, SIGINT); // sigint to fg job
        tcsetpgrp(STDERR_FILENO, getpgrp());
    }
    return;
}

void SIGSTPhandler(int sig)
{
    pid_t pid = 0;
    Job_Entry *ptr;
    for (ptr = jobs_front; ptr != NULL; ptr = ptr->link)
    {
        if (ptr->job_state == 1) // fg job
        {
            pid = ptr->pid;
            break;
        }
    }
    if (pid != 0)
    {
        kill(-pid, SIGTSTP); // sigstp to fg job
        tcsetpgrp(STDERR_FILENO, getpgrp());
    }
    return;
}

void SIGCHLDhandler(int sig)
{
    int olderrno = errno;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) // get status about DONE | TERMINATED process
    {
        if (WIFSTOPPED(status)) // TERMINATED
        {
            Job_Entry *job = jobget(pid);

            if (job->job_state == 1) // fg
            {
                if (job->job_id == -1) // fg at first ( > set id -1 )
                    job->job_id = jobgetnextid();
                job->job_count = job_count++;
                Sio_puts("[");
                Sio_putl((long)(job->job_id));
                Sio_puts("] STOPPED	");
                Sio_putl((long)(job->pid));
                Sio_puts(" ");
                Sio_puts(job->cmdline);
                Sio_puts("\n");
                job->job_state = 2; // STOPPED
                fg_flag = 1;        // flag for waiting parent process
            }
            else                    // bg
                job->job_state = 2; // STOPPED
        }

        else if (WIFEXITED(status)) // DONE > same process
        {
            Job_Entry *job = jobget(pid);
            if (job->job_state == 1)
            {
                fg_flag = 1;
                jobdelete(job->job_id);
            }
            else
            {
                job->job_state = 3; // jobs cmd > DONE
            }
        }

        else if (WIFSIGNALED(status)) // terminated by signals
        {
            Job_Entry *job = jobget(pid);

            if (job->job_state == 1)
            {
                fg_flag = 1;
                jobdelete(job->job_id);
            }
            else
                job->job_state = 4;
        }
    }
    errno = olderrno;
}

void SIGCHLDpipehandler(int sig)
{
    int olderrno = errno;
    sigset_t mask1, prev2;
    pid_t pid;
    int status;
    Sigfillset(&mask1); // signal control
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        Sigprocmask(SIG_BLOCK, &mask1, &prev2); // signal control
        if (WIFEXITED(status) || WIFSIGNALED(status))
            pipe_flag = 1; // flag for waiting parent process (pipe)
        Sigprocmask(SIG_SETMASK, &prev2, NULL);
    }
    errno = olderrno;
}

void Exec(char *filename, char *const argv[], char *const envp[])
{
    if (execve(filename, argv, environ) < 0)
    {
        strcpy(filename, "/usr/bin/"); // if cmd does not exist in /bin/
        strcat(filename, argv[0]);
        if (execve(filename, argv, environ) < 0)
        {
            Sio_puts(argv[0]);
            Sio_puts(": Command not found.\n");
            exit(0);
        }
    }
}

int ExecPipe(char **p_command, int p_command_cnt)
{
    int i;
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int fdpipe[MAXARGS][2]; // input 0 output 1
    pid_t pid;
    int status;
    sigset_t prev2;
    Sigemptyset(&prev2);
    Signal(SIGCHLD, SIGCHLDpipehandler);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGINT, SIG_DFL);            // SIGTSTP & SIGINT : default
    for (i = 0; i < p_command_cnt; i++) // a| b| c| d ... execution
    {
        int temp;
        char argv0[MAXLINE] = "/bin/";
        strcpy(buf, p_command[i]);
        temp = parseline(buf, argv);
        strcat(argv0, argv[0]); // parsing
        pipe(fdpipe[i]);        // pipe

        if ((pid = Fork()) == 0)
        {
            setpgid(0, getppid());
            if (i == 0) // a
            {
                close(fdpipe[i][0]);

                // STDOUT_FILENO >> fdpipe[i][1]
                dup2(fdpipe[i][1], STDOUT_FILENO);
                close(fdpipe[i][1]);
                Exec(argv0, argv, environ);
            }
            if (i == p_command_cnt - 1) // d
            {
                close(fdpipe[i - 1][1]);
                dup2(fdpipe[i - 1][0], STDIN_FILENO);
                close(fdpipe[i - 1][0]);

                Exec(argv0, argv, environ);
            }
            else // else
            {
                dup2(fdpipe[i - 1][0], STDIN_FILENO); // input pipe
                dup2(fdpipe[i][1], STDOUT_FILENO);    // output pipe
                close(fdpipe[i - 1][0]);
                close(fdpipe[i][1]);
                Exec(argv0, argv, environ);
            }
        }
        close(fdpipe[i][1]);
        while (!pipe_flag) // wait until the pipe command done
            Sigsuspend(&prev2);
        pipe_flag = 0;
    }
    exit(0);
}

void cmdnormalize(char *cmdline)
{
    int length = strlen(cmdline);
    char buf[MAXLINE];
    int bufidx = 0;
    cmdline[length - 1] = ' '; /* Replace trailing '\n' with space */
    int i;
    for (i = 0; i < length; i++)
    {
        if (cmdline[i] == '\t' || cmdline[i] == '|' || cmdline[i] == '&') // pipe of bg sign
        {
            if (cmdline[i] != '\t')
                buf[bufidx++] = ' ';
            buf[bufidx++] = cmdline[i];
            if (cmdline[i] != '\t')
                buf[bufidx++] = ' ';
        }
        else
            buf[bufidx++] = cmdline[i];
    }
    buf[bufidx] = '\0';
    strcpy(cmdline, buf);
}

void eval(char *cmdline)
{

    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line(for argv) */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */
    int pipe_flag = 0;   // If there is a pipe | -> pipe_flag=1;
    char buf2[MAXLINE];  /* new buffer to check pipe command line*/
    int i;
    int idx = 0;    // index for history command
    Job_Entry *job; //
    sigset_t prev2;
    Sigemptyset(&prev2);
    cmdnormalize(cmdline); // cmdline normalization >> parsing by ' '

    /*check for history, !!, !#*/
    if (cmdline[0] == '!')
    {
        if (cmdline[1] == '!')
        {
            strcpy(cmdline, history[history_top - 1]);
            Sio_puts(cmdline);
            Sio_puts("\n");
        }
        else
        { // consumption : there are only two options -> {!!, !#}
            for (int k = 1; cmdline[k] != '\0'; k++)
            {
                idx *= 10;
                idx += ((int)cmdline[k]) - '0';
            }
            if (idx < 0)
                idx = 1;
            else
                idx = idx / 10 + 2; // check #
            if (idx >= history_top)
                ;
            else
            {
                strcpy(cmdline, history[idx]);
                strcpy(history[history_top], cmdline);
                history_top++;
            }
            Sio_puts(cmdline);
            Sio_puts("\n");
        }
    }
    else
    {
        strcpy(history[history_top], cmdline);
        history_top++;
    } // make history command replacing cmdline by conditions / record them in history array

    strcpy(buf, cmdline);
    bg = parseline(buf, argv); // parsing & return (bg or fg)

    if (argv[0] == NULL)
        return; /* Ignore empty lines */
    if (!builtin_command(argv))
    { // quit -> exit(0), & -> ignore, other -> run
        char argv0[MAXLINE] = "/bin/";
        strcat(argv0, argv[0]); // "/bin/(cmd)"

        /* pipe action */
        char *p_command[MAXARGS]; // pipe com
        int p_command_cnt = 0;    // # of pipe com
        int p_start = 0;          // starting idx of pipe com
        strcpy(buf2, cmdline);    // buffer for pipe com

        if (!bg)
            job = jobadd(-1, argv, 1); // ADD the foreground Child to the job list
        else
            job = jobadd(-1, argv, 0); // ADD the background Child to the job list

        if ((pid = Fork()) == 0)
        {
            /*if SIGCHLD occurs before adding job, jobs can be deleted before execution.*/
            Signal(SIGTTOU, SIG_DFL);
            Signal(SIGTTIN, SIG_DFL);
            if (job->pid > 0)
                setpgid(0, job->pid);
            else
            {
                job->pid = getpid();
                setpgid(0, job->pid);
            }

            /* pipe check */
            for (i = 0; i < strlen(cmdline); i++)
            {
                if (buf2[i] == '|')
                {
                    /* pipe action */
                    buf2[i] = '\0';
                    p_command[p_command_cnt++] = &buf2[p_start]; // before |
                    p_start = i + 1;
                    pipe_flag = 1;
                }
            }
            if (pipe_flag == 1) // O
            {
                p_command[p_command_cnt++] = &buf2[p_start]; // | after
                ExecPipe(p_command, p_command_cnt);
            }
            else // X
            {
                Exec(argv0, argv, environ);
            }
            exit(0);
        }
        /* Parent waits for foreground job to terminate */
        else
        {
            if (job->pid > 0)
                setpgid(pid, job->pid);
            else
            {
                job->pid = pid;
                setpgid(pid, job->pid);
            }
            if (!bg) // foreground, reaping if forked child is terminated
            {
                tcsetpgrp(0, pid);
                int status;
                while (!fg_flag)
                    Sigsuspend(&prev2);
                fg_flag = 0;

                Signal(SIGTTOU, SIG_IGN);
                tcsetpgrp(0, getpid());
                Signal(SIGTTOU, SIG_DFL);
            }
            else // background, managed by Job structure
            {
                Sio_puts("[");
                Sio_putl((long)(job->job_id));
                Sio_puts("] ");
                Sio_putl((long)(job->pid));
                Sio_puts("\n");
            }
        }
    }
    return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv)
{
    if (!strcmp(argv[0], "exit"))
        exit(0);
    if (!strcmp(argv[0], "&")) /* Ignore singleton & */
        return 1;

    if (!strcmp(argv[0], "jobs")) // printing jobs
    {
        cmdprintjobs();
        return 1;
    }
    if (!strcmp(argv[0], "bg")) // background
    {
        cmdbg(argv);
        return 1;
    }
    if (!strcmp(argv[0], "fg")) // foreground
    {
        cmdfg(argv);
        return 1;
    }
    if (!strcmp(argv[0], "kill")) // kill
    {
        cmdkill(argv);
        return 1;
    }
    if (!strcmp(argv[0], "history"))
    { // printing history
        cmdhistory();
        return 1;
    }
    if (!strcmp(argv[0], "cd")) // cd ~
    {
        if (argv[1] == NULL) // cd
            chdir(getenv("HOME"));
        else if (argv[2] != NULL) // cd ~ ?
            Sio_puts("too many arguments\n");
        else if ((chdir(argv[1]) < 0)) // (executed in if condition if it is good form,) no file or directory
            Sio_puts("such file or directory does not exist\n");

        return 1;
    }

    return 0; /* Not a builtin command */
}
/* $end eval */

int parseline(char *buf, char **argv)
{
    char *delim; /* Points to first space delimiter */
    int argc;    /* Number of args */
    int bg;      /* Background job? */

    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    while ((delim = strchr(buf, ' ')))
    {
        argv[argc++] = buf;

        char *temp = buf;
        char *start = buf;  // starting quote
        char *end = buf;    // ending quote
        int quote_flag = 0; // if there is quoting, quote_flag=1

        /*quote processing*/
        while (*temp && (*temp != ' ')) // searching
        {
            if (*temp == '\'' || *temp == '\"') // starting ' or "
            {
                start = temp;
                temp++;
                end = strchr(temp, *temp); // ending ' or "
                temp = end;
                delim = strchr(temp, ' '); // next space
                quote_flag = 1;
                break;
            }
            temp++;
        }
        *delim = '\0';

        if (quote_flag == 1)
        {
            temp = buf;
            while (temp <= delim)
            {
                if (buf > delim)
                {
                    *temp = ' ';
                    temp++;
                }
                else if (buf != start && buf != end) // buf is not quotation mark
                {
                    *temp = *buf;
                    temp++;
                }
                buf++;
            }
        }

        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* Ignore spaces */
            buf++;
    }
    argv[argc] = NULL;

    if (argc == 0) /* Ignore blank line */
        return 1;

    /* Should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0)
        argv[--argc] = NULL;

    return bg;
}
/* $end parseline */
