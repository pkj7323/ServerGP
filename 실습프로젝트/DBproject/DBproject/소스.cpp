// SQLBindCol_ref.cpp  
// compile with: odbc32.lib  
#include <windows.h>  
#include <stdio.h>  

#define UNICODE  
#include <sqlext.h>  

#define NAME_LEN 20  

/************************************************************************
/* HandleDiagnosticRecord : display error/warning information
/*
/* Parameters:
/*	hHandle
/*	hType
/*	RetCode
/*	ODBC handle
/*	Type of handle (SQL_HANDLE_STMT, SQL_HANDLE_ENV, SQL_HANDLE_DBC)
/*	Return code of failing command
/************************************************************************/
void DB_Error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
    SQLSMALLINT iRec = 0;
    SQLINTEGER  iError;
    WCHAR
        wszMessage[1000];
    WCHAR
        wszState[SQL_SQLSTATE_SIZE + 1];
    if (RetCode == SQL_INVALID_HANDLE) {
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }
    while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
        (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
        // Hide data truncated..
        if (wcsncmp(wszState, L"01004", 5)) {
            fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
        }
    }
}

int main() {
    SQLHENV henv;
    SQLHDBC hdbc;
    SQLHSTMT hstmt = 0;
    SQLRETURN retcode;

    // Allocate environment handle  
    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

    // Set the ODBC version environment attribute  
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
        retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

        // Allocate connection handle  
        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

            // Set login timeout to 5 seconds  
            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

                // Connect to data source  
                retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2026_GameServer_ODBC", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

                // Allocate statement handle  
                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

                    retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"EXEC select_highLevel 1", SQL_NTS);
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						SQLINTEGER uid;
						SQLINTEGER user_id;
                        SQLWCHAR user_name[NAME_LEN];
						SQLINTEGER user_level;

                        SQLLEN indicator;
                        retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &uid, 10, &indicator);
                        retcode = SQLBindCol(hstmt, 2, SQL_C_LONG, &user_id, 10, &indicator);
                        retcode = SQLBindCol(hstmt, 3, SQL_C_WCHAR, user_name, NAME_LEN, &indicator);
                        retcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &user_level, 10, &indicator);

                        // Fetch and print each row of data. On an error, display a message and exit.  
                        for (int i = 0; ; i++) {
                            retcode = SQLFetch(hstmt);
                            if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
                                DB_Error(hstmt, SQL_HANDLE_STMT, retcode);
                            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
                            {
                                //replace wprintf with printf
                                //%S with %ls
                                //warning C4477: 'wprintf' : format string '%S' requires an argument of type 'char *'
                                //but variadic argument 2 has type 'SQLWCHAR *'
                                //wprintf(L"%d: %S %S %S\n", i + 1, sCustID, szName, szPhone);  
								wprintf(L"%d: %d %d %s %d\n", i + 1, uid, user_id, user_name, user_level);
                            }
                            else
                                break;
                        }
                    }
                    else
                    {
	                    DB_Error(hstmt, SQL_HANDLE_STMT, retcode);
                    }
                    // Process data  
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                        SQLCancel(hstmt);
                        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                    }

                    SQLDisconnect(hdbc);
                }

                SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
            }
        }
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
}