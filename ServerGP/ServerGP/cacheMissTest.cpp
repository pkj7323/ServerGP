#include <chrono>
#include <iostream>

constexpr int CACHE_LINE_SIZE = 64;
int main() {
	for (int i = 0; i < 20; ++i) {
		const int size = 1024 << i;
		char* a = (char*)malloc(size);
		unsigned int index = 0;
		int tmp = 0;
		auto start = std::chrono::high_resolution_clock::now();
		for (int j = 0; j < 100000000; ++j) {
			tmp += a[index % size];
			index += CACHE_LINE_SIZE * 11;
		}
		auto dur = std::chrono::high_resolution_clock::now() - start;
		std::cout << "Size : " << size / 1024 << "K,  ";
		std::cout << "Time " << duration_cast<std::chrono::milliseconds>(dur).count();
		std::cout << " ms " << tmp << std::endl;
	}

		/*Size: 1K, Time 151 ms 500000000
		Size : 2K, Time 153 ms 465625000
		Size : 4K, Time 150 ms 100000000
		Size : 8K, Time 147 ms 137500000
		Size : 16K, Time 146 ms 31250000
		Size : 32K, Time 150 ms 15625040
		Size : 64K, Time 150 ms 6250064
		Size : 128K, Time 156 ms 8593808
		Size : 256K, Time 170 ms 1953200
		Size : 512K, Time 280 ms 976640
		Size : 1024K, Time 220 ms 0
		Size : 2048K, Time 209 ms 0
		Size : 4096K, Time 213 ms 0
		Size : 8192K, Time 404 ms 0
		Size : 16384K, Time 672 ms 0
		Size : 32768K, Time 823 ms 0
		Size : 65536K, Time 723 ms 0
		Size : 131072K, Time 775 ms 0
		Size : 262144K, Time 790 ms 0
		Size : 524288K, Time 937 ms 0*/

	// L1 캐시: 384kb
	// L2 캐시: 1.5mb
	// L3 캐시: 12.0mb

}
