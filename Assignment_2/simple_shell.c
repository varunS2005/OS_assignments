#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    char* command; //Command
    pid_t pid; // Process ID of command's execution
    char start_time[20]; // Time when command was started
    long duration; //How long command took to execute (in ms)
} details;

details history[100]; //For upto 100 history. We can extend to store more.
int h_count = 0;


//we took elements from time.h module
void get_cur_time(char* a, int len) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now); //format - year-month-day hour-minute-second
    if (strftime(a, len, "%Y-%m-%d %H:%M:%S", t) == 0) {
        fprintf(stderr, "Messed up format\n");
    }
}

void input_user_command(char* str) {
    char* check = fgets(str, 1024, stdin); //we set max character limit 1024
    if (check == NULL) {
        fprintf(stderr, "Couldn't read input\n");
        str[0] = '\0'; // setting back to empty string
        return;
    }
    if (strlen(str) > 0) {
        str[strcspn(str, "\n")] = '\0'; // removing any newlines
        if (strchr(str, '\\') != NULL || strchr(str, '\"') != NULL) {
            fprintf(stderr, "Characters restricted by our assignment found!\n");
            str[0] = '\0'; // setting back to empty string
        }
    }
}

void parse_user_command(char* str, char** arg) {
    char* start = strtok(str, " "); //getting commands seperated by a space
    int i = 0;
    while (start != NULL && i < 99) {
        arg[i] = start;
        i++;
        start = strtok(NULL, " "); //rest of the token
    }
    if (i == 99) {
        fprintf(stderr, "too many arguments, we can allocate more space in code\n");
    }
    arg[i] = NULL; //to tell where list of arguments is ending
}

void add_history(char* str, pid_t pid, struct timeval start, long duration) {
    if (h_count < 100) {
        history[h_count].command = strdup(str); //storing command into history
        if (history[h_count].command == NULL) {
            fprintf(stderr, "couldn't put it into history\n");
            return;
        }
        history[h_count].pid = pid; //storing pid into history
        get_cur_time(history[h_count].start_time, sizeof(history[h_count].start_time)); 
        history[h_count].duration = duration;
        h_count++; //increasing h_count to input next command
    } else {
        fprintf(stderr, "Allocate more space for history in code\n");
    }
}

void display_history_details() {
    if (h_count == 0) {
        printf("No history\n");
        return;
    }
    for (int i = 0; i < h_count; i++) {
        printf("Command: %s\n", history[i].command);
        printf("PID: %d\n", history[i].pid);
        printf("Initiation: %s\n", history[i].start_time);
        printf("Time (in ms): %ld\n\n", history[i].duration);
    }
}

void launch(char* str) {
    char* commands[100];
    int cmd_count = 0;

    char* t = strtok(str, "|");
    while (t != NULL && cmd_count < 100) {
        commands[cmd_count++] = t;
        t = strtok(NULL, "|");
    }
    commands[cmd_count] = NULL;

    int pipe_fd[cmd_count - 1][2]; //to store file descriptors
    bool is_background[cmd_count];

    struct timeval start, end;

    for (int i = 0; i < cmd_count; i++) {
        int len = strlen(commands[i]);
        if (len > 0 && commands[i][len - 1] == '&') {
            is_background[i] = true;
            commands[i][len - 1] = '\0';  // we know the command is a background command and we dont need a background symbol for it.
        } 
        else {
            is_background[i] = false;
        }

        //creating the pipes, not checking for the last one
        if (i < cmd_count - 1) {
            if (pipe(pipe_fd[i]) == -1) { // Error checking for creation of pipes
                perror("Pipe failed");
                return;
            }
        }

        gettimeofday(&start, NULL);

        pid_t pid = fork();
        if (pid == 0) { // Child process
        //We will only enter here if pipes are present
            if (i > 0) { 
                dup2(pipe_fd[i - 1][0], STDIN_FILENO); //reading from previous pipe and sending it as inout to current process
                close(pipe_fd[i - 1][0]);
                close(pipe_fd[i - 1][1]); //closing both ends of the pipe
            }

            if (i < cmd_count - 1) {
                dup2(pipe_fd[i][1], STDOUT_FILENO); //write end of current pipe is sent to output of current process
                close(pipe_fd[i][0]);
                close(pipe_fd[i][1]); //close both ends of the pipe
            }

            char* arg[100];
            parse_user_command(commands[i], arg);

            if (execvp(arg[0], arg) == -1) {
                perror("Execution failed");
                exit(EXIT_FAILURE);
            }
        }
        else if (pid < 0) {
            perror("Fork failed");
            return;
        }

        if (i > 0) { //closing pipes of previous command for parent process
            close(pipe_fd[i - 1][0]);
            close(pipe_fd[i - 1][1]);
        }


        if (!is_background[i]) {
            waitpid(pid, NULL, 0);
            gettimeofday(&end, NULL);
            long duration = ((end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000); //tv_sec gives us time in seconds and tv_usec gives it in microseconds
            add_history(commands[i], pid, start, duration);
        } else {
            printf("Process %d running in background\n", pid);
            add_history(commands[i], pid, start, 0); //Adding with 0 duration since it's in the background
        }
    }
}




int main() {
    char str[1024];
    do {
        printf("\ndhruv&varun ~ $ "); //our command prompt
        input_user_command(str);

        if (strlen(str) > 0) {
            if (strcmp(str, "exit") == 0) {
                display_history_details();
                break;
            }
            if (strcmp(str, "history") == 0) {
                display_history_details();
                continue;
            }
            launch(str);
        }
    } 
    while (true);

    for (int i = 0; i < h_count; i++) {
        free(history[i].command);
    }

    return 0;
}
