
#include <iostream>
#include <chrono>

int main()
{
	const unsigned int N = 1024;
	unsigned int* h_A = new unsigned int[N * N];
	unsigned int* h_B = new unsigned int[N * N];
	unsigned int* h_C = new unsigned int[N * N];

	for (unsigned int i = 0; i < N * N; ++i) {
		h_A[i] = 1;
		h_B[i] = 1;
		h_C[i] = 0;
	}
	auto start = std::chrono::high_resolution_clock::now();
	for (unsigned int y = 0; y < N; ++y) {
		for (unsigned int x = 0; x < N; x++) {
			for (unsigned int i = 0; i < N; ++i)
				h_C[y * N + x] += h_A[i + y * N] * h_B[i * N + x];
		}
	}
	auto dur = std::chrono::high_resolution_clock::now() - start;
	std::cout << "cache miss case" << std::endl;
	std::cout << "Time " << std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
	std::cout << " ms " << h_C[0] << std::endl;

	auto start2 = std::chrono::high_resolution_clock::now();
	for (unsigned int y = 0; y < N; ++y)
		for (unsigned int x = 0; x < N; x++)
			for (unsigned int i = 0; i < N; ++i)
				h_C[y * N + i] += h_A[x + y * N] * h_B[x * N + i];
	auto dur2 = std::chrono::high_resolution_clock::now() - start2;
	std::cout << "cache hit case" << std::endl;
	std::cout << "Time " << std::chrono::duration_cast<std::chrono::milliseconds>(dur2).count();
	std::cout << " ms " << h_C[0] << std::endl;

	/*
	 * cache miss case
	 * Time 2873 ms 1024
	 * 
	 * cache hit case
	 * Time 836 ms 2048
	 */
}


