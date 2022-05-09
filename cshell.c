#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXLINE 1024
#define MAXJOBS 1024

typedef struct process process_t;

/*TODO:
- add signal mask for rmLL
*/

struct process
{
    char *command;
    char **argv;
    char *path;
    pid_t pid;
    char* pid_char;
    char* job_char;
    int status;
    process_t *next;
    int job_num;
    bool fg;
    bool stopped;
};

int counter = 0;

char **environ;

process_t *head = NULL;

volatile sig_atomic_t stt = 0;
volatile sig_atomic_t stt2 = 0;

void rmLL(pid_t rem) { 
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    process_t *temp = head;
        if (temp->pid == rem) {
            //printf("rmed\n");
            temp->fg = false;
            head = temp->next;
            //printf("[%d] (%d)  killed1  %s\n", (temp)->job_num,(temp)->pid,(temp)->command);
            //kill(rem, SIGKILL);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }

        while (temp->next != NULL)
        {
            if ((temp->next)->pid == rem)
            {
                //printf("rmed\n");
                temp->fg = false;
                process_t* tfree = temp->next;
                temp->next = temp->next->next;
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                return;
                //printf("[%d] (%d)  killed2  %s\n", (temp->next)->job_num,(temp->next)->pid,(temp->next)->command);
                //kill(rem, SIGKILL);
            }
            temp = temp->next;
        }
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

void handle_sigchld(int sig)
{
    // handle child termination (finished task, or killed)
    //printf("enter sigchld handler\n");

    if(stt == 1) {
        stt = 0;
        return;
    }
    if(stt2 == 1) {
        stt2 = 0;
        return;
    }
    while (1)
    {

        pid_t rem = waitpid(-1, NULL, WNOHANG); //keep reaping, anything
        if (rem <= 0)
        {
            //printf("prement\n");
            break;
        }
        rmLL(rem);
    }

    // while (waitpid(-1, NULL, WNOHANG|WUNTRACED) > 0) {}
}

void handle_sigtstp(int sig)
{
    // ctrl+z to fg process sends task to background and suspends it

    //find fg task, suspend it(SIGSTOP? SIGTSTP?), put it in the background
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    process_t* temp = head;
    while(temp != NULL) {
        //printf("%d, %d\n", temp->pid, temp->fg);
        if(temp->fg) {

            write(STDOUT_FILENO, "[", 1);
            write(STDOUT_FILENO, temp->job_char, strlen(temp->job_char));
            write(STDOUT_FILENO, "] (", 3);
            write(STDOUT_FILENO, temp->pid_char, strlen(temp->pid_char));
            write(STDOUT_FILENO, ")  suspended  ", 14);
            write(STDOUT_FILENO, temp->command, strlen(temp->command));
            write(STDOUT_FILENO, "\n", 1);



            stt = 1;
            temp->stopped = true;
            temp->fg = false;
            kill((pid_t) temp->pid, SIGSTOP);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;

        }
        temp = temp->next;
    }

    sigprocmask(SIG_BLOCK, &mask, NULL);
}

void handle_sigint(int sig) {

    process_t* temp = head;

    while(temp != NULL) {
        //printf("%d, %d\n", temp->pid, temp->fg);
        if(temp->fg) {

            write(STDOUT_FILENO, "[", 1);
            write(STDOUT_FILENO, temp->job_char, strlen(temp->job_char));
            write(STDOUT_FILENO, "] (", 3);
            write(STDOUT_FILENO, temp->pid_char, strlen(temp->pid_char));
            write(STDOUT_FILENO, ")  killed  ", 11);
            write(STDOUT_FILENO, temp->command, strlen(temp->command));
            write(STDOUT_FILENO, "\n", 1);
            kill((pid_t) temp->pid, SIGKILL);

            temp -> fg = false;
            break;
        }
        temp = temp->next;
    }


    //if foreground task, kill it
    //otherwise don't do anything
}

void handle_sigquit(int sig)
{
       //if no foreground task, exit(0)
    //if there is, kill it


    process_t* temp = head;

    while(temp != NULL) {
        if(temp->fg) {
            kill(temp->pid, SIGQUIT);//???
            write(STDOUT_FILENO, "[", 1);
            write(STDOUT_FILENO, temp->job_char, strlen(temp->job_char));
            write(STDOUT_FILENO, "] (", 3);
            write(STDOUT_FILENO, temp->pid_char, strlen(temp->pid_char));
            write(STDOUT_FILENO, ")  killed  ", 11);
            write(STDOUT_FILENO, temp->command, strlen(temp->command));
            write(STDOUT_FILENO, "\n", 1);
            kill((pid_t) temp->pid, SIGTERM);
            temp -> fg = false;
            return;
        }
        temp = temp->next;
    }
    exit(0);
}

void install_signal_handlers()
{
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa2;
    sa2.sa_handler = handle_sigint;
    sa2.sa_flags = SA_RESTART;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGINT, &sa2, NULL);


    struct sigaction sa3;
    sa3.sa_handler = handle_sigquit;
    sa3.sa_flags = SA_RESTART;
    sigemptyset(&sa3.sa_mask);
    sigaction(SIGQUIT, &sa3, NULL);

    struct sigaction sa4;
    sa4.sa_handler = handle_sigtstp;
    sa4.sa_flags = SA_RESTART;
    sigemptyset(&sa4.sa_mask);
    sigaction(SIGTSTP, &sa4, NULL);

}

void spawn(const char **toks, bool bg)
{ // bg is true iff command ended with &


    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);


    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        
    }

    pid_t childpid = fork();

    if (childpid == 0)
    {
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        setpgid(0,0);
        if (execvp((toks[0]), (char * const *) toks) != 0)
        {
            printf("ERROR: cannot run %s\n", toks[0]);
            exit(-1);
        }
    }
    else
    {
        if (bg)
        {

            counter++;

            printf("[%d], (%d)  %s\n", counter, childpid, toks[0]);

            process_t *new_proc = (process_t *)malloc(sizeof(process_t));
            char *newtoks = (char *)malloc(sizeof(char) * strlen(toks[0]));
            strcpy(newtoks, toks[0]);
            new_proc->command = newtoks;
            new_proc->pid = childpid;
            new_proc->next = NULL;
            new_proc->job_num = counter;
            new_proc->fg = false;
            new_proc->job_char = (char*) malloc(30);
            new_proc->pid_char = (char*) malloc(30);
            sprintf(new_proc->job_char, "%d", new_proc->job_num);
            sprintf(new_proc->pid_char, "%d", new_proc->pid);

            process_t *temptr = head;

            if (temptr == NULL)
            {
                head = new_proc;
            }
            else
            {
                while (temptr->next != NULL)
                {
                    temptr = temptr->next;
                }
                temptr->next = new_proc;
            }
        }
        else
        {
            counter++;

            //printf("[%d], (%d)  %s\n", counter, childpid, toks[0]);

            process_t *new_proc = (process_t *)malloc(sizeof(process_t));
            char *newtoks = (char *)malloc(sizeof(char) * strlen(toks[0]));
            strcpy(newtoks, toks[0]);
            new_proc->command = newtoks;
            new_proc->pid = childpid;
            new_proc->next = NULL;
            new_proc->job_num = counter;
            new_proc->fg = true;
            new_proc->job_char = (char*) malloc(30);
            new_proc->pid_char = (char*) malloc(30);
            sprintf(new_proc->job_char, "%d", new_proc->job_num);
            sprintf(new_proc->pid_char, "%d", new_proc->pid);
            process_t *temptr = head;

            if (temptr == NULL)
            {
                head = new_proc;
            }
            else
            {
                while (temptr->next != NULL)
                {
                    temptr = temptr->next;
                }
                temptr->next = new_proc;
            }
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            bool ef = false;
            while (1)
            {
                if(new_proc->stopped) break;
                process_t* temp = head;
                while(temp!=NULL) {
                    if(temp->pid == childpid) {
                        ef = true;
                        break;
                    }
                    temp = temp->next;
                }
                if(!ef) {break;}
                ef = false;
                sleep(0.01); //added
            } // suspend parent thread, wait for child
        }

        sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }
}

void cmd_jobs(const char **toks)
{
    // printf("display jobs: %d \n", counter);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    process_t *temptr = head;
    while (temptr != NULL)
    {
        if(temptr->stopped) {
            printf("[%d] (%d)  suspended  %s\n", temptr->job_num, temptr->pid, temptr->command);
        }
        else {
            printf("[%d] (%d)  running  %s\n", temptr->job_num, temptr->pid, temptr->command);
        }
        temptr = temptr->next;
    }

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

void cmd_fg(const char **toks)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    if(toks[1] == NULL || toks[2] != NULL) {
        printf("Error: fg takes exactly one argument\n");
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        return;
    }

    pid_t proc = (pid_t) atoi(toks[1]);

    if(toks[1][0] != '%') { //parser
        char* endptr;
        strtol(toks[1], &endptr, 10);  //has garbage behind it
        if(*endptr != '\0') {
            printf("Error: bad argument for fg: %s\n", toks[1]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }

        if(strtol(toks[1], NULL, 10) <= 0) {
            printf("Error: no PID %ld\n", strtol(toks[1], NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }

        process_t* temp = head;
        if(temp == NULL) {
            printf("Error: no PID %ld\n", strtol(toks[1], NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        while(temp != NULL) {
            if(temp->pid == (pid_t) strtol(toks[1], NULL, 10)) {
                proc = (pid_t) strtol(toks[1], NULL, 10);
                break;
            }
            if(temp->next == NULL) {
                printf("Error: no PID %ld\n", strtol(toks[1], NULL, 10));
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                return;
            }
            temp = temp->next;
        }
        //printf("Error: no PID %d\n", strtol(toks[1], NULL, 10));
        

    }
    else {
        char* endptr;
        strtol(toks[1]+1, &endptr, 10);  //has garbage behind it
        if(*endptr != '\0') {
            printf("Error: bad argument for fg: %s\n", toks[1]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        process_t* temp = head;
        if(temp == NULL) {
            printf("Error: no job %%%d\n", (int) strtol(toks[1]+1, NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        while(temp != NULL) {
            if(temp->job_num == (pid_t) strtol(toks[1]+1, NULL, 10)) {
                proc = temp->pid;
                break;
            }
            if(temp->next == NULL) {
                printf("Error: no job %%%d\n",(pid_t) strtol(toks[1]+1, NULL, 10));
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                return;
            }
            temp = temp->next;
        }
    }

    process_t* temp = head;

    kill(proc, SIGCONT);
    while(temp != NULL) {

        if(temp->fg) {
            printf("There shoundt be a fg right now\n");
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            exit(-1);
        }
        if(temp->pid == proc) {
            temp->fg = true;
            temp->stopped = false;


        }
        temp = temp->next;
    }
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    int flag2 = 0;
    while(1) {
        process_t* temp2 = head;

        while(temp2 != NULL) {
            if(temp2 -> pid == proc) flag2 = 1;
            if(temp2->stopped) { sigprocmask(SIG_UNBLOCK, &mask, NULL); return;}
            temp2 = temp2->next;
        }
        if(flag2 == 0) { sigprocmask(SIG_UNBLOCK, &mask, NULL); return;}
        flag2 = 0;
        sleep(0.01); //shell waits while this new fg task still runs;
    }

    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    //wait(NULL);
   // printf("%d \n", waitpid(proc, NULL, 0)); //cannot waitpid, as it will reap my child instead of sigchld handler doing so


}

void cmd_bg(const char **toks)
{
    
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    
    if(toks[1] == NULL || toks[2] != NULL) {
        printf("Error: bg takes exactly one argument\n");
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        return;
    }

    pid_t proc = atoi(toks[1]);

    if(toks[1][0] != '%') { //parser
        char* endptr;
        strtol(toks[1], &endptr, 10);  //has garbage behind it
        if(*endptr != '\0') {
            printf("Error: bad argument for bg: %s\n", toks[1]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }

        if(strtol(toks[1], NULL, 10) <= 0) {
            printf("Error: no PID %d\n", strtol(toks[1], NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }

        process_t* temp = head;
        if(temp == NULL) {
            printf("Error: no PID %d\n", strtol(toks[1], NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        while(temp != NULL) {
            if(temp->pid == strtol(toks[1], NULL, 10)) {
                proc = strtol(toks[1], NULL, 10);
                temp->stopped = false;
                printf("[%d] (%d)  %s\n", temp->job_num, temp->pid, temp->command);
                break;
            }
            if(temp->next == NULL) {
                printf("Error: no PID %d\n", strtol(toks[1], NULL, 10));
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                return;
            }
            temp = temp->next;
        }
        //printf("Error: no PID %d\n", strtol(toks[1], NULL, 10));
    }
    else {
        char* endptr;
        strtol(toks[1]+1, &endptr, 10);  //has garbage behind it
        if(*endptr != '\0') {
            printf("Error: bad argument for bg: %s\n", toks[1]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        process_t* temp = head;
        if(temp == NULL) {
            printf("Error: no job %%%d\n", strtol(toks[1]+1, NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        while(temp != NULL) {
            if(temp->job_num == strtol(toks[1]+1, NULL, 10)) {
                proc = temp->pid;
                temp->stopped = false;
                printf("[%d] (%d)  %s\n", temp->job_num, temp->pid, temp->command);
                break;
            }
            if(temp->next == NULL) {
                printf("Error: no job %%%d\n", strtol(toks[1]+1, NULL, 10));
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                return;
            }
            temp = temp->next;
        }
    }
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    //stt2 = 1;
    //printf("true sigcont\n");
    stt2 = 1;
    kill(proc, SIGCONT);
    //printf("true fg pt2\n");

}


void cmd_slay(const char **toks)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);


    if(toks[1] == NULL || toks[2] != NULL) {
        printf("Error: slay takes exactly one argument\n");
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        return;
    }
    if(toks[1][0] != '%') {

        char* endptr;
        strtol(toks[1], &endptr, 10);  //has garbage behind it
        if(*endptr != '\0') {
            printf("Error: bad argument for slay: %s\n", toks[1]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        
        if(strtol(toks[1], NULL, 10) <= 0) {
            printf("Error: no PID %d\n",(pid_t)strtol(toks[1], NULL, 10));
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }

        process_t* temp = head;
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        while(temp!=NULL) {
            if(temp->pid == (pid_t)strtol(toks[1], NULL, 10)) {
                printf("[%d] (%d)  killed  %s\n", temp->job_num, temp->pid, temp->command); 
                break;
            }
            if(temp->next == NULL) {
                printf("Error: no PID %d\n", (pid_t) strtol(toks[1], NULL, 10));
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                return;
            }
            temp=temp->next;
        }
        kill((pid_t)atoi(toks[1]), SIGKILL);
        //kill((pid_t)atoi(toks[1]), SIGCHLD);
    }
    else {
        char* endptr;
        strtol(toks[1]+1, &endptr, 10);  //has garbage behind it
        if(*endptr != '\0') {
            printf("Error: bad argument for slay: %s\n", toks[1]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            return;
        }
        process_t* temp = head;
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        while(temp != NULL) {
            if(temp->job_num == (pid_t)strtol(toks[1]+1, NULL, 10)) {
                printf("[%d] (%d)  killed  %s\n", temp->job_num, temp->pid, temp->command);
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                kill(temp->pid, SIGKILL);
                
                return;
            }
            temp = temp->next;
        }
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        printf("Error: no job %d\n",(pid_t) strtol(toks[1]+1, NULL, 10));
    }
    
}

int atoi_helper(char* input) {
    int i = 1;
    int len = strlen(input);
    int res = 0;
   
    int fac = 1;
    while(len > 0) {
        fac*=10;
        len--;
    }
    fac/=100;

    while(input[i] != '\0') {
        res += (input[i]-'0')*fac;
        fac/=10;
        i++;
    }

    return res;
}

void cmd_quit(const char **toks)
{
    if (toks[1] != NULL)
    {
        fprintf(stderr, "ERROR: quit takes no arguments\n");
    }
    else
    {
        exit(0);
    }
}

void eval(const char **toks, bool bg)
{ // bg is true iff command ended with &
    assert(toks);
    if (*toks == NULL)
        return;
    if (strcmp(toks[0], "quit") == 0)
    {
        cmd_quit(toks);
    }
    else if (strcmp(toks[0], "jobs") == 0)
    {
        cmd_jobs(toks);
    }
    else if (strcmp(toks[0], "slay") == 0)
    {
        cmd_slay(toks);
    }
    else if(strcmp(toks[0], "fg") == 0) {
        cmd_fg(toks);
    }
    else if(strcmp(toks[0], "bg") == 0) {
        cmd_bg(toks);
    }
    else
    {
        spawn(toks, bg);
    }
}

void parse_and_eval(char *s)
{
    assert(s);
    const char *toks[MAXLINE + 1];

    while (*s != '\0')
    {
        bool end = false;
        bool bg = false;
        int t = 0;

        while (*s != '\0' && !end)
        {
            while (*s == ' ' || *s == '\t' || *s == '\n')
                ++s;
            if (*s != '&' && *s != ';' && *s != '\0')
                toks[t++] = s;
            while (strchr("&; \t\n", *s) == NULL)
                ++s;
            switch (*s)
            {
            case '&':
                bg = true;
                end = true;
                break;
            case ';':
                end = true;
                break;
            }
            if (*s)
                *s++ = '\0';
        }
        toks[t] = NULL;
        eval(toks, bg);
    }
}

void prompt()
{
    printf("crash> ");
    fflush(stdout);
}
int repl()
{
    char *line = NULL;
    size_t len = 0;
    while (prompt(), getline(&line, &len, stdin) != -1)
    {
        parse_and_eval(line);
    }

    if (line != NULL)
        free(line);
    if (ferror(stdin))
    {
        perror("ERROR");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    install_signal_handlers();
    return repl();
}