#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>

#define MAX 100

typedef struct process {
    pid_t pid;
    int status;  // 0: running, 1: ready, 2: terminated
    char process_name[MAX];
    time_t start_time;
    time_t end_time;
    int priority;  // 1 (low) to 4 (high)
} process;

typedef struct hnode {
    char command[MAX];
    pid_t pid;
    time_t start_time;
    time_t end_time;
    int status;
    struct hnode* next;
} hnode;

process process_table[MAX];
hnode* history_head = NULL;
int process_count = 0;
int NCPU, TSLICE;

void submit_job(const char* command, int priority);
void scheduler();
void handle_sigint(int sig);
void add_history(const char* command, pid_t pid, time_t start, time_t end, int status);
void display_history_details();
void remove_terminated_processes();
void sort_by_priority();
double get_e_slice(int priority);

void display_history_details() {
    printf("\nCommand History and Statistics:\n");
    hnode* temp = history_head;
    int count = 1;

    while (temp) {
        double t_time = difftime(temp->end_time, temp->start_time);  // End - Start
        double c_time = (t_time < (TSLICE / 1000.0)) ? (TSLICE / 1000.0) : t_time;
        double w_time = t_time - c_time;  // Adjusted wait time

        if (w_time < 0) w_time = 0; 

        printf("[%d] %s (PID: %d, Status: %s)\n", 
               count++, temp->command, temp->pid,
               temp->status == 0 ? "Success" : "Failed");
        printf("Completion Time: %.2f seconds, Wait Time: %.2f seconds\n", 
               c_time, w_time);

        temp = temp->next;
    }
}

char** parse_command(const char* command) {
    char** args = malloc(MAX * sizeof(char*));
    char* temp_command = strdup(command);
    char* token;
    int i = 0;

    token = strtok(temp_command, " ");
    while (token != NULL) {
        args[i++] = strdup(token);
        token = strtok(NULL, " ");
    }
    args[i] = NULL; 

    free(temp_command);
    return args;
}

void remove_terminated_processes() {
    int new_count = 0;
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].status != 2) {  // Keep non-terminated processes (2 means terminated)
            process_table[new_count++] = process_table[i];
        }
    }
    process_count = new_count;
}

// Signal handler for Ctrl+C
void handle_sigint(int sig) {
    printf("\nTerminating\n");
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].status == 0 || process_table[i].status == 1) {
            kill(process_table[i].pid, SIGKILL);
            printf("Killed process: %s (PID: %d)\n", process_table[i].process_name, process_table[i].pid);
        }
    }
    display_history_details();
    exit(0);
}

void add_history(const char* command, pid_t pid, time_t start, time_t end, int status) {
    hnode* new_node = (hnode*)malloc(sizeof(hnode));
    if (!new_node) {
        perror("Failed to allocate memory for history node");
        return;
    }

    //ensuring a minimum of 1 TSLICE
    double actual_time = difftime(end, start);  // Actual time in seconds
    double c_time = (actual_time < (TSLICE / 1000.0)) ? (TSLICE / 1000.0) : actual_time;
    strncpy(new_node->command, command, MAX - 1);
    new_node->pid = pid;
    new_node->start_time = start;
    new_node->end_time = start + (time_t)(c_time);
    new_node->status = status;
    new_node->next = NULL;

    if (!history_head) {
        history_head = new_node;
    } 
    else {
        hnode* temp = history_head;
        while (temp->next) temp = temp->next;
        temp->next = new_node;
    }
}

void sort_by_priority() {
    for (int i = 0; i < process_count - 1; i++) {
        for (int j = i + 1; j < process_count; j++) {
            if (process_table[i].priority < process_table[j].priority) {
                process temp = process_table[i];
                process_table[i] = process_table[j];
                process_table[j] = temp;
            }
        }
    }
}

// Submit a new job with priority
void submit_job(const char* command, int priority) {
    if (process_count >= MAX) {
        fprintf(stderr, "limit reached\n");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        return;
    }
    if (pid == 0) {  // Child process
        char** args = parse_command(command);
        kill(getpid(), SIGSTOP);  // Stop the process initially
        execvp(args[0], args);  // Execute the command with arguments
        perror("Exec failed");

        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
        }
        free(args);
        exit(1);
    } 
    else {  // Parent process
        process_table[process_count].pid = pid;
        process_table[process_count].status = 1;  // Ready state
        strncpy(process_table[process_count].process_name, command, MAX - 1);
        process_table[process_count].start_time = time(NULL);
        process_table[process_count].priority = priority;
        process_count++;
        printf("Submitted process: %s (PID: %d) with priority %d\n", command, pid, priority);
    }
}

void scheduler() {
    while (process_count > 0) { 
        sort_by_priority(); //sorting processes by prioirty

        int running_jobs = 0;

        // running upto ncpu processes
        for (int i = 0; i < process_count && running_jobs < NCPU; i++) {
            if (process_table[i].status == 1) {  // ready to run
                double e_slice = get_e_slice(process_table[i].priority);
                printf("Resuming process: %s (PID: %d) with priority %d and time slice %.2f ms\n", 
                       process_table[i].process_name, process_table[i].pid, process_table[i].priority, e_slice);

                kill(process_table[i].pid, SIGCONT);  // Resume the process
                usleep((int)(e_slice * 1000));

                //if the process has terminated
                int status;
                pid_t result = waitpid(process_table[i].pid, &status, WNOHANG);
                if (result == process_table[i].pid) {  // If terminated
                    printf("Process %s (PID: %d) terminated.\n", 
                           process_table[i].process_name, process_table[i].pid);
                    process_table[i].status = 2;  // Marking as terminated
                    process_table[i].end_time = time(NULL);  // Updating end time
                    add_history(process_table[i].process_name, process_table[i].pid,
                                process_table[i].start_time, process_table[i].end_time,
                                WIFEXITED(status) ? 0 : 1);
                } 
                else {
                    // If not terminated, pausing the process
                    printf("Paused process: %s (PID: %d)\n", 
                           process_table[i].process_name, process_table[i].pid);
                    kill(process_table[i].pid, SIGSTOP);
                }

                running_jobs++; 
            }
        }
        remove_terminated_processes();
    }
}
//Higher priority processes get bigger time slice
double get_e_slice(int priority) {
    switch (priority) {
        case 1: return 0.5 * TSLICE;
        case 2: return 0.75 * TSLICE;
        case 3: return 1.0 * TSLICE;
        case 4: return 1.25 * TSLICE;
        default: return TSLICE;
    }
}

void simple_shell() {
    char str[MAX];
    while (1) {
        printf("\ndhruv&varun$ ");
        fgets(str, sizeof(str), stdin);
        str[strcspn(str, "\n")] = '\0';

        char* token = strtok(str, ";");
        while (token != NULL) {
            while (*token == ' ') token++;

            if (strncmp(token, "submit ", 7) == 0) {
                char* job = token + 7;
                int priority = 1;  // Default priority
                char* space = strrchr(job, ' ');
                if (space && isdigit(*(space + 1))) {
                    priority = atoi(space + 1);
                    *space = '\0';  // Removing the priority part from the command
                }
                submit_job(job, priority);
            } 
            else if (strcmp(token, "exit") == 0) {
                display_history_details();
                return;
            } 
            else if (strcmp(token, "history") == 0) {
                display_history_details();
            } 
            else {
                printf("Unknown command: %s\n", token);
            }
            token = strtok(NULL, ";");
        }

        if (process_count > 0) scheduler();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <NCPU> <TSLICE(ms)>\n", argv[0]);
        exit(1);
    }

    NCPU = atoi(argv[1]);
    TSLICE = atoi(argv[2]);

    signal(SIGINT, handle_sigint);

    printf("Simple Shell & Scheduler started with %d CPUs and %dms time slice.\n", NCPU, TSLICE);

    simple_shell();
    return 0;
}
