#include <iostream>
#include <list>
#include <functional>
#include <stdlib.h>
#include <cstring>

int user_main(int argc, char **argv);

/* Demonstration on how to pass lambda as parameter.
 * "&&" means r-value reference. You may read about it online.
 */
void demonstration(std::function<void()> && lambda) {
  lambda();
}

// Thread Data Structure
struct ThreadData {
    int id;
    int start; //start for outer loop
    int end; //end for inner loop
    std::function<void(int)> singleLambda; //function for single loop lambda
    std::function<void(int, int)> doubleLambda; //finction for double loop lambda
    int innerStart; //start for inner loop
    int innerEnd; //end for inner loop
};

// Single-loop thread function
void* singleThreadFunction(void* arg) {
    auto* data = static_cast<ThreadData*>(arg);
    for (int i = data->start; i < data->end; ++i) {
        data->singleLambda(i);
    }
    return NULL;
}

// Double-loop thread function
void* doubleThreadFunction(void* arg) {
    auto* data = static_cast<ThreadData*>(arg);
    for (int i = data->start; i < data->end; ++i) {
        for (int j = data->innerStart; j < data->innerEnd; ++j) {
            data->doubleLambda(i, j);
        }
    }
    return NULL;
}

// Parallel for single-loop implementation
void parallel_for(int low, int high, std::function<void(int)> &&lambda, int numThreads) {
    if (numThreads <= 0) throw std::invalid_argument("threads cant be less than 1");
    
    auto startTime = std::chrono::high_resolution_clock::now(); //starting timer for calculation of execution time

    int range = high - low;
    int chunkSize = range / numThreads; //giving equivalent chunks to each thread
    int remainder = range % numThreads; //remainder will have to be given to one pf the threads to ensure entire range is 
    //being aptly distrbuted

    std::vector<pthread_t> threads(numThreads);
    std::vector<ThreadData> threadData(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        threadData[i] = {
            i,
            low + i * chunkSize,
            low + (i + 1) * chunkSize,
            lambda,
            NULL,
            0, //0 for start and end of inner loop because of single-loop
            0
        };

        if (i == numThreads - 1) {
            threadData[i].end += remainder; //Assigning remainder to the last thread
        }

        if (pthread_create(&threads[i], NULL, singleThreadFunction, (void *) &threadData[i]) != 0){
            printf("Couldn't create threads properly");
            exit(1);
        };
    }

    for (auto& thread : threads) {
        if (pthread_join(thread, NULL) != 0){
            printf("Error in joining threads");
        };
    }

    auto endTime = std::chrono::high_resolution_clock::now(); //ending timer
    std::chrono::duration<double> elapsed = endTime - startTime; //execution time arithmetic
    std::cout << "Execution Time (Single-loop): " << elapsed.count() << " seconds\n";
}

// Parallel for double-loop implementation
void parallel_for(int low1, int high1, int low2, int high2, std::function<void(int, int)> &&lambda, int numThreads) {
    if (numThreads <= 0) throw std::invalid_argument("Number of threads must be greater than 0");

    auto startTime = std::chrono::high_resolution_clock::now();

    int outerRange = high1 - low1;
    int chunkSize = outerRange / numThreads;
    int remainder = outerRange % numThreads;

    std::vector<pthread_t> threads(numThreads);
    std::vector<ThreadData> threadData(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        threadData[i] = {
            i,
            low1 + i * chunkSize,
            low1 + (i + 1) * chunkSize,
            NULL,
            lambda,
            low2,
            high2
        };

        if (i == numThreads - 1) {
            threadData[i].end += remainder; //Assigning remainder to the last thread
        }

        if (pthread_create(&threads[i], NULL, doubleThreadFunction, &threadData[i]) != 0){
            printf("Error in creating threads\n");
            exit(1);
        };
    }

    for (auto& thread : threads) {
        if (pthread_join(thread, NULL) != 0){
            printf("Error in joining threads");
            exit(1);
        };
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    std::cout << "Execution Time (Double-loop): " << elapsed.count() << " seconds\n";
}

int main(int argc, char **argv) {
  /* 
   * Declaration of a sample C++ lambda function
   * that captures variable 'x' by value and 'y'
   * by reference. Global variables are by default
   * captured by reference and are not to be supplied
   * in the capture list. Only local variables must be 
   * explicity captured if they are used inside lambda.
   */
  int x=5,y=1;
  // Declaring a lambda expression that accepts void type parameter
  auto /*name*/ lambda1 = /*capture list*/[/*by value*/ x, /*by reference*/ &y](void) {
    /* Any changes to 'x' will throw compilation error as x is captured by value */
    y = 5;
    std::cout<<"====== Welcome to Assignment-"<<y<<" of the CSE231(A) ======\n";
    /* you can have any number of statements inside this lambda body */
  };
  // Executing the lambda function
  demonstration(lambda1); // the value of x is still 5, but the value of y is now 5

  int rc = user_main(argc, argv);
 
  auto /*name*/ lambda2 = [/*nothing captured*/]() {
    std::cout<<"====== Hope you enjoyed CSE231(A) ======\n";
    /* you can have any number of statements inside this lambda body */
  };
  demonstration(lambda2);
  return rc;
}

#define main user_main


