#include <chrono>
#include <iostream>
#define abs(x) (((x)>0)?(x):-(x))

int abs2(int x)
{
	int y = x >> 31;
	return (y ^ x) - y;
}

constexpr int T_SIZE = 100000000;
short rand_arr[T_SIZE];
int main()
{
	for (int i = 0; i < T_SIZE; ++i) rand_arr[i] = rand() - 16384;
	int64_t sum = 0;
	auto start_t = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < T_SIZE; ++i) sum += abs(rand_arr[i]);
	auto du = std::chrono::high_resolution_clock::now() - start_t;
	std::cout << "[abs] Time " << duration_cast<std::chrono::milliseconds>(du).count() << " ms\n";
	std::cout << "Result : " << sum << std::endl;
	sum = 0;
	start_t = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < T_SIZE; ++i) sum += abs2(rand_arr[i]);
	du = std::chrono::high_resolution_clock::now() - start_t;
	std::cout << "[abs2] Time " << duration_cast<std::chrono::milliseconds>(du).count() << " ms\n";
	std::cout << "Result : " << sum << std::endl;

}
