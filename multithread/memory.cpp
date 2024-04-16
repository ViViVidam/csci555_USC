#include <chrono>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#define MAX_MEM 20000000

unsigned int arr_size = 0;
unsigned int iterations = 1000;
int* numbers_forw = nullptr;
int* numbers_rev = nullptr;

void runBenchmark(int mode);
int main(int argc,char** argv){
    srand(time(0));
    arr_size = 1000000;//10000 + rand() % MAX_MEM;//1000,000
    numbers_forw = new int[arr_size];
    numbers_rev = new int[arr_size];

    if(argc!=3){
        std::cout << "specify num of threads, specify which to run (1:forward, 2:backward, 3:random, 4:all)" << std::endl;
        return 0;
    }

    int num_of_threads = atoi(argv[1]);
    int mode = atoi(argv[2]);
    auto threadpool = std::vector<std::thread>();
    for(auto i=0; i < num_of_threads; i++){
        auto t = std::thread(runBenchmark,mode);
        threadpool.push_back(std::move(t));
    }
    for(auto i = 0; i <threadpool.size(); i++){
        threadpool[i].join();
    }
    return 0;
}

void runBenchmark(int mode) {
    printf("Beginrn");

    unsigned int sum_forward = 0;
    unsigned int sum_reverse = 0;
    unsigned int sum_random = 0;

    std::chrono::duration<double> sum_forward_ex_time{};
    std::chrono::duration<double> sum_reverse_ex_time{};
    std::chrono::duration<double> sum_random_ex_time{};

    printf("Prepare first array.\n");
    for (unsigned int i = 0; i < arr_size; ++i) {
        numbers_forw[i] = rand();
    }

    printf("Prepare second array.\n");
    for (unsigned int i = 0; i < arr_size; ++i) {
        numbers_rev[i] = rand();
    }


    if (mode == 1 || mode == 4) { // Forward
        printf("Test forward access\n");
        auto start = std::chrono::high_resolution_clock::now();
        for (int x = 0; x < iterations; x++)
            for (unsigned int i = 0; i < arr_size; ++i) {
                sum_forward += (numbers_forw[i] + numbers_rev[i]) % INT32_MAX;
            }
        sum_forward_ex_time = std::chrono::high_resolution_clock::now() - start;
    }

    if (mode == 2 || mode == 4) { // Reverse access
        printf("Test reverse access\n");
        auto start = std::chrono::high_resolution_clock::now();
        for (int x = 0; x < iterations; x++)
            for (int i = arr_size - 1; i >= 0; --i) {
                sum_reverse += numbers_forw[i] + numbers_rev[i];
            }
        sum_reverse_ex_time = std::chrono::high_resolution_clock::now() - start;
    }

    if (mode == 3 || mode == 4) { // Random access
        printf("Test random access\n");
        auto p = rand() % arr_size;
        auto start = std::chrono::high_resolution_clock::now();
        for (int x = 0; x < iterations; x++)
            for (unsigned int i = 0; i < arr_size; ++i) {
                p = (numbers_forw[p] + numbers_rev[p]) % arr_size;
            }
        sum_random_ex_time = std::chrono::high_resolution_clock::now() - start;
    }
    printf("Forward sum result: %i, tooks: %.6f\n", sum_forward, sum_forward_ex_time.count());
    printf("Reverse sum result: %i, tooks: %.6f\n", sum_reverse, sum_reverse_ex_time.count());
    printf("Random sum result: %i, tooks: %.6f\n", sum_random, sum_random_ex_time.count());
    return;
}