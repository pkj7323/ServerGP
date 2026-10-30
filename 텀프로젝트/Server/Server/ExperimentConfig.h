#pragma once
// =============================================================================
// ExperimentConfig.h
// 포트폴리오 성능 비교용 컴파일 스위치와 카운터.
// 기본값(전부 0)이면 평소 서버와 동일하게 빌드된다.
// 빌드할 때 /D 로 값을 넘긴다 (예: CL=/DEXP_METRICS=1 /DEXP_LUA_MODE=1).
// =============================================================================

#include <atomic>
#include <chrono>

#ifndef EXP_METRICS
#define EXP_METRICS 0         // 1: 초당 CSV 기록, 고빈도 콘솔 출력 끄기
#endif
#ifndef EXP_SECTOR_BRUTE
#define EXP_SECTOR_BRUTE 0    // 1: 격자 없이 전체 객체를 선형 탐색
#endif
#ifndef EXP_SEND_PER_PACKET
#define EXP_SEND_PER_PACKET 0 // 1: 패킷마다 WSASend (전송 중 합치기 끄기)
#endif
#ifndef EXP_LUA_MODE
#define EXP_LUA_MODE 0        // 0: thread_local 풀, 1: NPC별 state+mutex, 2: 타입별 state+전역 락
#endif
#ifndef EXP_DB_SYNC
#define EXP_DB_SYNC 0         // 1: DB 작업을 워커 스레드에서 동기 실행 (스레드별 ODBC 연결)
#endif

struct ExpCounters {
	std::atomic<long long> wsasend_calls{ 0 };
	std::atomic<long long> send_packets{ 0 };
	std::atomic<long long> send_bytes{ 0 };
	std::atomic<long long> sector_queries{ 0 };
	std::atomic<long long> sector_ns{ 0 };
	std::atomic<long long> sector_returned{ 0 };
	std::atomic<long long> lua_calls{ 0 };
	std::atomic<long long> lua_ns{ 0 };
	std::atomic<long long> lua_wait_ns{ 0 };
	std::atomic<long long> lua_states{ 0 };
	std::atomic<long long> active_npcs{ 0 };
	std::atomic<long long> timer_events{ 0 };
	std::atomic<long long> timer_lag_us{ 0 };
	std::atomic<long long> db_tasks{ 0 };
	std::atomic<long long> db_ns{ 0 };
	std::atomic<long long> db_pending{ 0 };
};
inline ExpCounters g_exp;

#if EXP_METRICS
#define EXP_ADD(field, n) g_exp.field.fetch_add((n), std::memory_order_relaxed)
#define EXP_NOW() std::chrono::steady_clock::now()
#define EXP_ADD_ELAPSED_NS(field, start) \
	EXP_ADD(field, std::chrono::duration_cast<std::chrono::nanoseconds>(EXP_NOW() - (start)).count())
#else
#define EXP_ADD(field, n) ((void)0)
#define EXP_NOW() 0
#define EXP_ADD_ELAPSED_NS(field, start) ((void)(start))
#endif
