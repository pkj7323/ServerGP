#include <iostream>
#include <chrono>
#include <thread>
using namespace std;
using namespace chrono;
int main()
{
	volatile long long tmp = 0;
	auto start = high_resolution_clock::now();
	for (int j = 0; j < 10000000; ++j) {
		tmp += j;
		this_thread::yield();
	}
	auto duration = high_resolution_clock::now() - start;
	cout << "Time " << duration_cast<milliseconds>(duration).count();
	cout << " ms\n";
	cout << "RESULT " << tmp << endl;

	/*시스템 콜이 있을때
	 *Time 904 ms
	RESULT 49999995000000*/

	/*시스템콜이 없을때
	 *Time 13 ms
	RESULT 49999995000000*/
}
