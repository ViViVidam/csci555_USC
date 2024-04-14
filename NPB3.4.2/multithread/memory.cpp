#include <chrono>
#include <stdio.h>
#include <string>

int main() {

	printf("Beginrn");
	unsigned int arr_size = 100000;
	unsigned int iterations = 90000;

	unsigned int numbers_forw[arr_size];
	unsigned int numbers_rev[arr_size];

	unsigned int sum_forward = 0;
	unsigned int sum_reverse = 0;
	unsigned int sum_random = 0;

	std::chrono::duration<double> sum_forward_ex_time;
	std::chrono::duration<double> sum_reverse_ex_time;
	std::chrono::duration<double> sum_random_ex_time;

	printf("Prepare first array.\n");
	for (unsigned int i = 0; i < arr_size; ++i) {
		numbers_forw[i] = rand();
	}

	printf("Prepare second array.\n");
	for (unsigned int i = 0; i < arr_size; ++i) {
		numbers_rev[i] = rand();
	}

	{ // Forward
		printf("Test forward access\n");
		auto start = std::chrono::high_resolution_clock::now();
		for(int x = 0; x < iterations; x++)
			for (unsigned int i = 0; i < arr_size; ++i) {
				sum_forward += numbers_forw[i] + numbers_rev[i];
			}
		sum_forward_ex_time = std::chrono::high_resolution_clock::now() - start;
	}

	{ // Reverse access
		printf("Test reverse access\n");
		auto start = std::chrono::high_resolution_clock::now();
		for(int x = 0; x < iterations; x++)
			for (int i = arr_size - 1; i >= 0; --i) {
				sum_reverse += numbers_forw[i] + numbers_rev[i];
			}
		sum_reverse_ex_time = std::chrono::high_resolution_clock::now() - start;
	}

	{ // Random access
		printf("Test random access\n");
		auto start = std::chrono::high_resolution_clock::now();
		for(int x = 0; x < iterations; x++)
			for (unsigned int i = 0; i < arr_size; ++i) {
				const int p(rand() % arr_size);
				sum_random += numbers_forw[p] + numbers_rev[p];
			}
		sum_random_ex_time = std::chrono::high_resolution_clock::now() - start;
	}

	printf("Forward sum result: %i, tooks: %.6f\n", sum_forward, sum_forward_ex_time.count());
	printf("Reverse sum result: %i, tooks: %.6f\n", sum_reverse, sum_reverse_ex_time.count());
	printf("Random sum result: %i, tooks: %.6f\n", sum_random, sum_random_ex_time.count());

	return 0;
}