#pragma once

#include <variant>
#include <string>
#include <unordered_map>
#include <functional>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <concurrent_queue.h>
#include <thread>

#include "Protocol.h"

// Define player_data structure to hold data from/to the DB
struct player_data {
	int16_t x;
	int16_t y;
	int id;
	bool success;
	char user_id[MAX_NAME_LEN];
	char user_name[MAX_NAME_LEN];
	int head_tier;
	int chest_tier;
	int legs_tier;
	int boots_tier;
	int weapon_tier;
	int hp;
	int max_hp;
	int level;
	int exp;
	int gold;
	int quest_stage;
	int quest_progress;
	int unspent_sp;
	int skills_mask;
	std::unordered_map<int, int> inventory;
};

// Define DB Task Types
enum class DBTaskType : uint32_t {
	DB_TASK_ERROR = 0,
	LOGIN_AUTH = 1,
	SAVE_USER_DATA = 2,
};

// The task data can be either empty, a string (for login requests), or player_data (for save requests/login results)
using DBTaskData = std::variant<std::monostate, std::string, player_data>;

struct DBTask {
	int ioType; // Corresponds to the io_type enum in main.cpp
	DBTaskType type;
	int32_t session_id;
	DBTaskData data;
};

class DBManager {
public:
	// Singleton access
	static DBManager& Instance() {
		static DBManager instance;
		return instance;
	}

	// Initialize ODBC and register handlers
	void Init(std::function<void(int, int, const DBTask&)> notifyCb);
	
	// Spawns the background DB thread
	void StartDBThread();
	
	// Stops and joins the DB thread
	void StopDBThread();
	
	// Push a task to the DB queue from the worker threads
	void PushTask(const DBTask& task);

private:
	DBManager();
	~DBManager();

	void DBWorkerThread();
	void HandleLoginAuth(DBTask& task);
	void HandleSaveData(DBTask& task);

	void ReportDBError(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);

private:
	std::thread m_db_thread;
	std::atomic<bool> m_db_thread_running{ false };
	std::function<void(int, int, const DBTask&)> m_notifyCb;
	SQLHENV m_henv;
	SQLHDBC m_hdbc;
	concurrency::concurrent_queue<DBTask> m_task_queue;
	std::unordered_map<DBTaskType, std::function<void(DBTask&)>> m_handlers;
};
