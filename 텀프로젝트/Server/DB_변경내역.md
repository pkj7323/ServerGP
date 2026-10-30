# DB 호출부 변경 내역

## 2026-09-26 — SP 호출을 파라미터 바인딩으로 전환 (SQL 인젝션 수정)

- 커밋: `f1eeeb2 Update DBManager.cpp`
- 파일: `Server/DBManager.cpp`
- 확인: Release x64 빌드 경고 없음, 클라이언트 접속으로 로그인 → 저장 → 재접속 동작 확인

### 왜 고쳤나

저장 프로시저(SP) 9개를 호출할 때 인자를 **문자열에 이어 붙여** 쿼리를 만들고 있었다.

```cpp
std::wstring query = L"EXEC check_auth '" + w_uid + L"'";
SQLExecDirect(check_hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
```

`user_id`는 클라이언트가 보낸 값이다. `'`가 들어간 ID를 보내면 따옴표가 닫히고 뒤에 임의의 SQL을 이어 붙일 수 있다.

```
입력:  x'; DROP TABLE Users; --
쿼리:  EXEC check_auth 'x'; DROP TABLE Users; --'
```

SP를 쓰는 이유 중 하나가 보안인데, 호출부에서 문자열을 조립하면 SP를 우회해 그대로 뚫린다.
**SP + 파라미터 바인딩을 함께 써야 의미가 있다.**

### 무엇을 바꿨나

#### 1. 바인딩 헬퍼 2개 추가

```cpp
static SQLRETURN BindUserId(SQLHSTMT hstmt, SQLUSMALLINT idx, const std::wstring& uid, SQLLEN& ind) {
	ind = SQL_NTS;
	return SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
		50, 0, (SQLPOINTER)uid.c_str(), 0, &ind); // Users.user_id nvarchar(50)
}

static SQLRETURN BindInt(SQLHSTMT hstmt, SQLUSMALLINT idx, SQLINTEGER& value) {
	return SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
		0, 0, &value, 0, nullptr);
}
```

- `SQLBindParameter`는 쿼리의 `?` 자리에 **값을 데이터로만** 넘긴다. 입력에 `'`나 `;`가 섞여도 SQL 문법으로 해석되지 않으므로 쿼리 구조가 바뀌지 않는다.
- `SQL_C_WCHAR` / `SQL_WVARCHAR`: `w_uid`가 `std::wstring`이고 컬럼이 `nvarchar(50)`이라 와이드 문자열 타입으로 맞췄다.
- `ind = SQL_NTS`: 널 종료 문자열이니 길이는 드라이버가 알아서 재라는 뜻이다.

#### 2. 호출 9곳을 `{CALL sp(?, ...)}` 형태로 교체

`{CALL ...}`은 ODBC 표준 프로시저 호출 문법(이스케이프 시퀀스)이다. 드라이버가 DB에 맞는 호출로 바꿔준다.

| SP | 수정 전 | 수정 후 |
|---|---|---|
| check_auth | `EXEC check_auth '<uid>'` | `{CALL check_auth(?)}` |
| select_user_data | `EXEC select_user_data '<uid>'` | `{CALL select_user_data(?)}` |
| select_inventory_data | `EXEC select_inventory_data '<uid>'` | `{CALL select_inventory_data(?)}` |
| select_quest_data | `EXEC select_quest_data '<uid>', 1` | `{CALL select_quest_data(?, ?)}` |
| select_skill_data | `EXEC select_skill_data '<uid>'` | `{CALL select_skill_data(?)}` |
| update_user_data | `EXEC ... '<uid>', x, y, ...` (to_wstring 12번) | `{CALL update_user_data(?, ×13)}` |
| update_quest_data | `EXEC ... '<uid>', 1, stage, prog` | `{CALL update_quest_data(?, ?, ?, ?)}` |
| update_skill_data | `EXEC ... '<uid>', sp, mask` | `{CALL update_skill_data(?, ?, ?)}` |
| save_inventory_data | 아이템마다 `EXEC ...` 문자열 조립 | `{CALL save_inventory_data(?, ?, ?)}` Prepare 1회 |

정수 인자(좌표·스탯·퀘스트·스킬)는 인젝션 위험이 없지만, 방식을 하나로 통일하려고 `std::to_wstring` 연결 대신 모두 바인딩했다.
`update_user_data`는 인자가 13개라 스탯을 배열에 담고 루프로 바인딩한다.

```cpp
SQLINTEGER stats[] = {
	pd.x, pd.y, pd.hp, pd.max_hp, pd.level, pd.exp,
	pd.head_tier, pd.chest_tier, pd.legs_tier, pd.boots_tier, pd.weapon_tier, pd.gold
};
BindUserId(hstmt, 1, w_uid, uid_ind);
for (SQLUSMALLINT i = 0; i < std::size(stats); ++i)
	BindInt(hstmt, static_cast<SQLUSMALLINT>(i + 2), stats[i]);
```

#### 3. 인벤토리 저장: 핸들 재사용 + Prepare 1회

수정 전에는 아이템 1종마다 핸들 할당 → 문자열 조립 → 실행 → 핸들 해제를 반복했다.

```cpp
SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &inv_hstmt);                                  // 핸들 1개
SQLPrepare(inv_hstmt, (SQLWCHAR*)L"{CALL save_inventory_data(?, ?, ?)}", SQL_NTS);    // 파싱 1회
SQLINTEGER item_id = 0, item_count = 0;
BindUserId(inv_hstmt, 1, w_uid, uid_ind);
BindInt(inv_hstmt, 2, item_id);
BindInt(inv_hstmt, 3, item_count);

for (const auto& pair : pd.inventory) {
	item_id = pair.first;            // 바인딩된 변수의 값만 교체
	item_count = pair.second;
	SQLExecute(inv_hstmt);
	SQLFreeStmt(inv_hstmt, SQL_CLOSE);   // 다음 Execute 전에 결과 커서 정리
}
SQLFreeHandle(SQL_HANDLE_STMT, inv_hstmt);
```

- `SQLBindParameter`는 값이 아니라 **변수의 주소**를 등록한다. 그래서 변수 값만 바꾸고 `SQLExecute`를 다시 부르면 새 값으로 실행된다.
- 핸들 할당·해제와 쿼리 파싱이 아이템 수 N회에서 1회로 줄었다.
- DB 왕복(Execute)은 여전히 N회다. 한 번에 보내려면 TVP(Table-Valued Parameter)가 필요하다 (아래 남은 과제 참고).

### 주의할 점 (코드 수정 시)

- **바인딩한 버퍼는 Execute가 끝날 때까지 살아 있어야 한다.** ODBC는 실행 시점에 등록된 주소를 읽는다. 그래서 `stats[]`, `quest_id`, `item_id` 같은 변수를 호출 블록 안의 지역 변수로 두었다. 임시 객체(`std::to_wstring(...)` 결과 등)를 바인딩하면 안 된다.
- `uid_ind`(길이 표시자)도 같은 이유로 Execute 전까지 유지해야 한다.
- 새 SP를 추가할 때도 `{CALL sp(?)}`와 `BindUserId`/`BindInt`를 쓰고, 문자열 연결은 쓰지 않는다.

### 남은 과제

- **한글 ID:** `std::wstring w_uid(uid.begin(), uid.end())`는 바이트 단위 확장이라 ASCII만 정상이다. `MultiByteToWideChar(CP_UTF8, ...)`로 변환해야 한다.
- **트랜잭션 없음:** 스탯·퀘스트·스킬·인벤토리 저장이 각각 따로 커밋된다. 중간에 실패하면 일부만 저장된 상태가 남는다. `SQL_ATTR_AUTOCOMMIT` OFF와 `SQLEndTran`으로 묶어야 한다.
- **인벤토리 왕복 N회:** TVP로 인벤토리 전체를 한 번에 넘기면 1회로 줄일 수 있다.
