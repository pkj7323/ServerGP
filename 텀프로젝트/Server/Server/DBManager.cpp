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

// 저장 프로시저 인자는 문자열에 이어 붙이지 않고 ? 자리표시자에 바인딩한다 (SQL 인젝션 방지).
// 바인딩한 버퍼는 SQLExecDirect/SQLExecute 가 끝날 때까지 살아 있어야 한다.
static SQLRETURN BindUserId(SQLHSTMT hstmt, SQLUSMALLINT idx, const std::wstring& uid, SQLLEN& ind) {
	ind = SQL_NTS;
	return SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
		50, 0, (SQLPOINTER)uid.c_str(), 0, &ind); // Users.user_id nvarchar(50)
}

static SQLRETURN BindInt(SQLHSTMT hstmt, SQLUSMALLINT idx, SQLINTEGER& value) {
	return SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
		0, 0, &value, 0, nullptr);
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
	m_db_thread_running = true;
	m_db_thread = std::thread(&DBManager::DBWorkerThread, this);
}

void DBManager::StopDBThread() {
	m_db_thread_running = false;
	if (m_db_thread.joinable()) {
		m_db_thread.join();
	}
}

void DBManager::PushTask(const DBTask& task) {
	m_task_queue.push(task);
}

void DBManager::DBWorkerThread() {
	while (m_db_thread_running) {
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

	// 서버 종료 시 큐에 남은 작업을 모두 처리 (Drain)
	DBTask task;
	while (m_task_queue.try_pop(task)) {
		auto it = m_handlers.find(task.type);
		if (it != m_handlers.end()) {
			it->second(task);
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

	SQLLEN uid_ind;
	BindUserId(check_hstmt, 1, w_uid, uid_ind);
	retcode = SQLExecDirect(check_hstmt, (SQLWCHAR*)L"{CALL check_auth(?)}", SQL_NTS);
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
			BindUserId(hstmt, 1, w_uid, uid_ind);
			retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"{CALL select_user_data(?)}", SQL_NTS);
			
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
					BindUserId(inv_hstmt, 1, w_uid, uid_ind);
					retcode = SQLExecDirect(inv_hstmt, (SQLWCHAR*)L"{CALL select_inventory_data(?)}", SQL_NTS);
					
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

				// Fetch quest data (quest_id = 1 for Main Quest)
				SQLHSTMT quest_hstmt = SQL_NULL_HSTMT;
				retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &quest_hstmt);
				if (retcode == SQL_SUCCESS) {
					SQLINTEGER quest_id = 1;
					BindUserId(quest_hstmt, 1, w_uid, uid_ind);
					BindInt(quest_hstmt, 2, quest_id);
					retcode = SQLExecDirect(quest_hstmt, (SQLWCHAR*)L"{CALL select_quest_data(?, ?)}", SQL_NTS);
					
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						retcode = SQLFetch(quest_hstmt);
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							SQLINTEGER tmp_qstage = 0, tmp_qprog = 0;
							SQLLEN ind;
							SQLGetData(quest_hstmt, 1, SQL_C_SLONG, &tmp_qstage, sizeof(tmp_qstage), &ind);
							SQLGetData(quest_hstmt, 2, SQL_C_SLONG, &tmp_qprog, sizeof(tmp_qprog), &ind);
							result.quest_stage = static_cast<int>(tmp_qstage);
							result.quest_progress = static_cast<int>(tmp_qprog);
						}
					} else {
						// If SP fails or doesn't exist, default to 0
						result.quest_stage = 0;
						result.quest_progress = 0;
					}
					SQLFreeHandle(SQL_HANDLE_STMT, quest_hstmt);
				}

				// Fetch skill data
				SQLHSTMT skill_hstmt = SQL_NULL_HSTMT;
				retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &skill_hstmt);
				if (retcode == SQL_SUCCESS) {
					BindUserId(skill_hstmt, 1, w_uid, uid_ind);
					retcode = SQLExecDirect(skill_hstmt, (SQLWCHAR*)L"{CALL select_skill_data(?)}", SQL_NTS);
					
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						retcode = SQLFetch(skill_hstmt);
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							SQLINTEGER tmp_sp = 0, tmp_mask = 0;
							SQLLEN ind;
							SQLGetData(skill_hstmt, 1, SQL_C_SLONG, &tmp_sp, sizeof(tmp_sp), &ind);
							SQLGetData(skill_hstmt, 2, SQL_C_SLONG, &tmp_mask, sizeof(tmp_mask), &ind);
							result.unspent_sp = static_cast<int>(tmp_sp);
							result.skills_mask = static_cast<int>(tmp_mask);
						}
					} else {
						result.unspent_sp = 0;
						result.skills_mask = 0;
					}
					SQLFreeHandle(SQL_HANDLE_STMT, skill_hstmt);
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
	SQLINTEGER stats[] = {
		pd.x, pd.y, pd.hp, pd.max_hp, pd.level, pd.exp,
		pd.head_tier, pd.chest_tier, pd.legs_tier, pd.boots_tier, pd.weapon_tier, pd.gold
	};
	SQLLEN uid_ind;
	BindUserId(hstmt, 1, w_uid, uid_ind);
	for (SQLUSMALLINT i = 0; i < std::size(stats); ++i) {
		BindInt(hstmt, static_cast<SQLUSMALLINT>(i + 2), stats[i]);
	}

	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"{CALL update_user_data(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}", SQL_NTS);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
		ReportDBError(hstmt, SQL_HANDLE_STMT, retcode);
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

	// Save quest data (quest_id = 1 for Main Quest)
	SQLHSTMT quest_hstmt = SQL_NULL_HSTMT;
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &quest_hstmt);
	if (retcode == SQL_SUCCESS) {
		SQLINTEGER quest_id = 1, stage = pd.quest_stage, progress = pd.quest_progress;
		BindUserId(quest_hstmt, 1, w_uid, uid_ind);
		BindInt(quest_hstmt, 2, quest_id);
		BindInt(quest_hstmt, 3, stage);
		BindInt(quest_hstmt, 4, progress);
		retcode = SQLExecDirect(quest_hstmt, (SQLWCHAR*)L"{CALL update_quest_data(?, ?, ?, ?)}", SQL_NTS);
		if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
			ReportDBError(quest_hstmt, SQL_HANDLE_STMT, retcode);
		}
		SQLFreeHandle(SQL_HANDLE_STMT, quest_hstmt);
	}

	// Save skill data
	SQLHSTMT skill_hstmt = SQL_NULL_HSTMT;
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &skill_hstmt);
	if (retcode == SQL_SUCCESS) {
		SQLINTEGER sp = pd.unspent_sp, mask = pd.skills_mask;
		BindUserId(skill_hstmt, 1, w_uid, uid_ind);
		BindInt(skill_hstmt, 2, sp);
		BindInt(skill_hstmt, 3, mask);
		retcode = SQLExecDirect(skill_hstmt, (SQLWCHAR*)L"{CALL update_skill_data(?, ?, ?)}", SQL_NTS);
		if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
			ReportDBError(skill_hstmt, SQL_HANDLE_STMT, retcode);
		}
		SQLFreeHandle(SQL_HANDLE_STMT, skill_hstmt);
	}

	// Save inventory: 한 번 Prepare 후 바인딩된 변수만 바꿔가며 아이템 수만큼 Execute
	if (!pd.inventory.empty()) {
		SQLHSTMT inv_hstmt = SQL_NULL_HSTMT;
		retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &inv_hstmt);
		if (retcode == SQL_SUCCESS) {
			retcode = SQLPrepare(inv_hstmt, (SQLWCHAR*)L"{CALL save_inventory_data(?, ?, ?)}", SQL_NTS);
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLINTEGER item_id = 0, item_count = 0;
				BindUserId(inv_hstmt, 1, w_uid, uid_ind);
				BindInt(inv_hstmt, 2, item_id);
				BindInt(inv_hstmt, 3, item_count);

				for (const auto& pair : pd.inventory) {
					item_id = pair.first;
					item_count = pair.second;
					retcode = SQLExecute(inv_hstmt);
					if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
						ReportDBError(inv_hstmt, SQL_HANDLE_STMT, retcode);
					}
					SQLFreeStmt(inv_hstmt, SQL_CLOSE); // 다음 Execute 전에 결과 커서 정리
				}
			} else {
				ReportDBError(inv_hstmt, SQL_HANDLE_STMT, retcode);
			}
			SQLFreeHandle(SQL_HANDLE_STMT, inv_hstmt);
		}
	}
}
