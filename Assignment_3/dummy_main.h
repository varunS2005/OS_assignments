#ifndef DUMMY_MAIN_H
#define DUMMY_MAIN_H

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int dummy_main(int argc, char **argv);

int main(int argc, char **argv) {
    time_t start_time = time(NULL);
    printf("Process %s started at %s", argv[0], ctime(&start_time));
    int ret = dummy_main(argc, argv);
    time_t end_time = time(NULL);
    printf("Process %s exited with status %d at %s", argv[0], ret, ctime(&end_time));
    return ret;
}

#define main dummy_main

#endif  // DUMMY_MAIN_H
