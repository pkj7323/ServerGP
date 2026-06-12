#include "DBManager.h"
#include <iostream>
#include <thread>

#pragma comment(lib, "odbc32.lib")

// Matches io_type enum values in main.cpp
constexpr int IO_TYPE_DB_PLAYER_AUTH = 4; // We will adjust this in main.cpp if needed, actually it's better to just pass the actual int values from main.cpp when initializing if they are not exposed, but since they are enums, we'll assume 4 is db_player_auth for now. Wait, I'll define an extern or something... actually I will ensure main.cpp explicitly maps these values or I'll just hardcode the enum indices assuming they match. Let's just use 100 for auth and 101 for save to avoid clashing with io_type in main.cpp! We can just extend io_type in main.cpp.

DBManager::DBManager() : m_henv(SQL_NULL_HENV), m_hdbc(SQL_NULL_HDBC) {
}

DBManager::~DBManager() {
	if (m_hdbc != SQL_NULL_HDBC) {
		SQLDisconnect(m_hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, m_hdbc);
	}
	if (m_henv != SQL_NULL_HENV) {
		SQLFreeHandle(SQL_HANDLE_ENV, m_henv);
	}
}

void DBManager::ReportDBError(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode) {
	SQLSMALLINT iRec = 0;
	SQLINTEGER  iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];

	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

void DBManager::Init(std::function<void(int, int, const DBTask&)> notifyCb) {
	m_notifyCb = notifyCb;

	SQLRETURN retcode;
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_henv);

	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(m_henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, m_henv, &m_hdbc);

			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(m_hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
				
				// Ensure DSN "2021180009_SGP" is correctly set up in ODBC Data Source Administrator
				retcode = SQLConnect(m_hdbc, (SQLWCHAR*)L"2021180009_SGP_Term", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
					ReportDBError(m_hdbc, SQL_HANDLE_DBC, retcode);
					std::cout << "[DB] MSSQL 서버 연결 실패. DB 기능을 사용할 수 없습니다." << std::endl;
					// exit(-1); // Do not exit, just let it fail gracefully if DB is not set up
				} else {
					std::cout << "[DB] MSSQL 서버에 성공적으로 연결되었습니다." << std::endl;
				}
			}
		}
	}

	// Register Handlers
	m_handlers[DBTaskType::LOGIN_AUTH] = [this](DBTask& task) { HandleLoginAuth(task); };
	m_handlers[DBTaskType::SAVE_USER_DATA] = [this](DBTask& task) { HandleSaveData(task); };
}

void DBManager::StartDBThread() {
	std::thread db_thread(&DBManager::DBWorkerThread, this);
	db_thread.detach();
}

void DBManager::PushTask(const DBTask& task) {
	m_task_queue.push(task);
}

void DBManager::DBWorkerThread() {
	while (true) {
		DBTask task;
		if (m_task_queue.try_pop(task)) {
			
			auto it = m_handlers.find(task.type);
			if (it != m_handlers.end()) {
				it->second(task); // Execute handler
			} else {
				std::cout << "[DB] 등록되지 않은 DB 작업 타입입니다: " << static_cast<int>(task.type) << std::endl;
			}

			if (m_notifyCb) {
				m_notifyCb(task.session_id, task.ioType, task);
			}
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}

void DBManager::HandleLoginAuth(DBTask& task) {
	if (!std::holds_alternative<std::string>(task.data)) return;
	
	std::string user_id = std::get<std::string>(task.data);
	std::wstring w_uid(user_id.begin(), user_id.end());

	SQLHSTMT check_hstmt = SQL_NULL_HSTMT;
	SQLRETURN retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &check_hstmt);
	if (retcode != SQL_SUCCESS) {
		ReportDBError(check_hstmt, SQL_HANDLE_STMT, retcode);
		return;
	}

	std::wstring query = L"EXEC check_auth '" + w_uid + L"'";
	retcode = SQLExecDirect(check_hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
		ReportDBError(check_hstmt, SQL_HANDLE_STMT, retcode);
		SQLFreeHandle(SQL_HANDLE_STMT, check_hstmt);
		return;
	}

	SQLINTEGER count = 0;
	SQLLEN indicator;
	SQLBindCol(check_hstmt, 1, SQL_C_LONG, &count, sizeof(SQLINTEGER), &indicator);
	retcode = SQLFetch(check_hstmt);
	
	player_data result;
	result.success = (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) && count > 0;

	SQLCloseCursor(check_hstmt);
	SQLFreeHandle(SQL_HANDLE_STMT, check_hstmt);

	if (result.success) {
		SQLHSTMT hstmt = SQL_NULL_HSTMT;
		retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
		if (retcode == SQL_SUCCESS) {
			query = L"EXEC select_user_data '" + w_uid + L"'";
			retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
			
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				retcode = SQLFetch(hstmt);
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					SQLLEN ind;
					char   tmp_uid [MAX_NAME_LEN + 1] = {};
					char   tmp_name[MAX_NAME_LEN + 1] = {};
					SQLINTEGER tmp_x = 0, tmp_y = 0;
					SQLINTEGER tmp_hp = 0, tmp_maxhp = 0;
					SQLINTEGER tmp_level = 0, tmp_exp = 0;
					SQLINTEGER tmp_head = 0, tmp_chest = 0, tmp_legs = 0, tmp_boots = 0;
					SQLINTEGER tmp_weapon = 0;
					SQLINTEGER tmp_gold = 0;

					SQLGetData(hstmt, 1, SQL_C_CHAR,  tmp_uid,  sizeof(tmp_uid),  &ind);
					SQLGetData(hstmt, 2, SQL_C_CHAR,  tmp_name, sizeof(tmp_name), &ind);
					SQLGetData(hstmt, 3, SQL_C_SLONG, &tmp_x,   sizeof(tmp_x),    &ind);
					SQLGetData(hstmt, 4, SQL_C_SLONG, &tmp_y,   sizeof(tmp_y),    &ind);
					SQLGetData(hstmt, 5, SQL_C_SLONG, &tmp_hp,  sizeof(tmp_hp),   &ind);
					SQLGetData(hstmt, 6, SQL_C_SLONG, &tmp_maxhp, sizeof(tmp_maxhp), &ind);
					SQLGetData(hstmt, 7, SQL_C_SLONG, &tmp_level, sizeof(tmp_level), &ind);
					SQLGetData(hstmt, 8, SQL_C_SLONG, &tmp_exp,   sizeof(tmp_exp),   &ind);
					SQLGetData(hstmt, 9, SQL_C_SLONG, &tmp_head, sizeof(tmp_head), &ind);
					SQLGetData(hstmt, 10, SQL_C_SLONG, &tmp_chest, sizeof(tmp_chest), &ind);
					SQLGetData(hstmt, 11, SQL_C_SLONG, &tmp_legs, sizeof(tmp_legs), &ind);
					SQLGetData(hstmt, 12, SQL_C_SLONG, &tmp_boots, sizeof(tmp_boots), &ind);
					SQLGetData(hstmt, 13, SQL_C_SLONG, &tmp_weapon, sizeof(tmp_weapon), &ind);
					SQLGetData(hstmt, 14, SQL_C_SLONG, &tmp_gold, sizeof(tmp_gold), &ind);

					auto rtrim = [](char* str) {
						int len = static_cast<int>(strnlen_s(str, MAX_NAME_LEN));
						while (len > 0 && str[len - 1] == ' ') {
							str[len - 1] = '\0';
							len--;
						}
					};
					rtrim(tmp_uid);
					rtrim(tmp_name);

					strncpy_s(result.user_id, sizeof(result.user_id), tmp_uid, _TRUNCATE);
					strncpy_s(result.user_name, sizeof(result.user_name), tmp_name, _TRUNCATE);
					result.x = static_cast<int16_t>(tmp_x);
					result.y = static_cast<int16_t>(tmp_y);
					result.hp = static_cast<int>(tmp_hp);
					result.max_hp = static_cast<int>(tmp_maxhp);
					result.level = static_cast<int>(tmp_level);
					result.exp = static_cast<int>(tmp_exp);
					result.head_tier = static_cast<int>(tmp_head);
					result.chest_tier = static_cast<int>(tmp_chest);
					result.legs_tier = static_cast<int>(tmp_legs);
					result.boots_tier = static_cast<int>(tmp_boots);
					result.weapon_tier = static_cast<int>(tmp_weapon);
					result.gold = static_cast<int>(tmp_gold);
				}
			} else {
				ReportDBError(hstmt, SQL_HANDLE_STMT, retcode);
			}
			SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

			// 인벤토리 불러오기
			if (result.success) {
				SQLHSTMT inv_hstmt = SQL_NULL_HSTMT;
				retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &inv_hstmt);
				if (retcode == SQL_SUCCESS) {
					query = L"EXEC select_inventory_data '" + w_uid + L"'";
					retcode = SQLExecDirect(inv_hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
					
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						while (true) {
							retcode = SQLFetch(inv_hstmt);
							if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) break;
							
							SQLINTEGER tmp_item_id = 0, tmp_count = 0;
							SQLLEN ind;
							SQLGetData(inv_hstmt, 1, SQL_C_SLONG, &tmp_item_id, sizeof(tmp_item_id), &ind);
							SQLGetData(inv_hstmt, 2, SQL_C_SLONG, &tmp_count, sizeof(tmp_count), &ind);
							result.inventory[static_cast<int>(tmp_item_id)] = static_cast<int>(tmp_count);
						}
					}
					SQLFreeHandle(SQL_HANDLE_STMT, inv_hstmt);
				}
			}
		}
	}
	
	result.id = task.session_id;
	task.data = result;
}

void DBManager::HandleSaveData(DBTask& task) {
	if (!std::holds_alternative<player_data>(task.data)) return;
	
	player_data pd = std::get<player_data>(task.data);
	
	SQLHSTMT hstmt = SQL_NULL_HSTMT;
	SQLRETURN retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
	if (retcode != SQL_SUCCESS) {
		ReportDBError(hstmt, SQL_HANDLE_STMT, retcode);
		return;
	}

	std::string uid(pd.user_id);
	std::wstring w_uid(uid.begin(), uid.end());
	
	// update_user_data expects: user_id, x, y, hp, max_hp, level, exp, head_tier, chest_tier, legs_tier, boots_tier, weapon_tier, gold
	std::wstring query = L"EXEC update_user_data '" + w_uid + L"', " 
						+ std::to_wstring(pd.x) + L", " 
						+ std::to_wstring(pd.y) + L", "
						+ std::to_wstring(pd.hp) + L", "
						+ std::to_wstring(pd.max_hp) + L", "
						+ std::to_wstring(pd.level) + L", "
						+ std::to_wstring(pd.exp) + L", "
						+ std::to_wstring(pd.head_tier) + L", "
						+ std::to_wstring(pd.chest_tier) + L", "
						+ std::to_wstring(pd.legs_tier) + L", "
						+ std::to_wstring(pd.boots_tier) + L", "
						+ std::to_wstring(pd.weapon_tier) + L", "
						+ std::to_wstring(pd.gold);

	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
		ReportDBError(hstmt, SQL_HANDLE_STMT, retcode);
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

	// Save inventory
	for (const auto& pair : pd.inventory) {
		SQLHSTMT inv_hstmt = SQL_NULL_HSTMT;
		retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &inv_hstmt);
		if (retcode == SQL_SUCCESS) {
			std::wstring inv_query = L"EXEC save_inventory_data '" + w_uid + L"', "
									+ std::to_wstring(pair.first) + L", "
									+ std::to_wstring(pair.second);
			retcode = SQLExecDirect(inv_hstmt, (SQLWCHAR*)inv_query.c_str(), SQL_NTS);
			if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
				ReportDBError(inv_hstmt, SQL_HANDLE_STMT, retcode);
			}
			SQLFreeHandle(SQL_HANDLE_STMT, inv_hstmt);
		}
	}
}
