-- 성능 비교용 DB를 만든다. 원본 2021180009_SGP_Term을 복사만 하고 수정하지 않는다.
-- 실행: sqlcmd -S DESKTOP-PARK\SQLEXPRESS01 -E -C -i setup_bench_db.sql
SET NOCOUNT ON;

IF DB_ID('2021180009_SGP_Bench') IS NOT NULL
BEGIN
    ALTER DATABASE [2021180009_SGP_Bench] SET SINGLE_USER WITH ROLLBACK IMMEDIATE;
    DROP DATABASE [2021180009_SGP_Bench];
END;

BACKUP DATABASE [2021180009_SGP_Term]
    TO DISK = 'SGP_Bench_seed.bak' WITH COPY_ONLY, INIT, NOFORMAT;

DECLARE @data NVARCHAR(400) = CAST(SERVERPROPERTY('InstanceDefaultDataPath') AS NVARCHAR(400));
DECLARE @mdf NVARCHAR(500) = @data + '2021180009_SGP_Bench.mdf';
DECLARE @ldf NVARCHAR(500) = @data + '2021180009_SGP_Bench_log.ldf';

RESTORE DATABASE [2021180009_SGP_Bench] FROM DISK = 'SGP_Bench_seed.bak'
    WITH MOVE '2021180009_SGP_Term' TO @mdf,
         MOVE '2021180009_SGP_Term_log' TO @ldf;
GO

USE [2021180009_SGP_Bench];
SET NOCOUNT ON;

-- 복원본의 실제 데이터를 비우고 DBSIM_0 ~ DBSIM_4999 계정을 만든다. (자식 테이블부터 삭제)
DELETE FROM Inventory;
DELETE FROM UserQuests;
DELETE FROM UserSkills;
DELETE FROM Users;

;WITH n AS (
    SELECT TOP (5000) ROW_NUMBER() OVER (ORDER BY (SELECT NULL)) - 1 AS i
    FROM sys.all_objects a CROSS JOIN sys.all_objects b
)
SELECT i INTO #n FROM n;

INSERT INTO Users (user_id, user_name, pos_x, pos_y, gold, hp, max_hp, user_level, exp,
                   weapon_tier, head_tier, chest_tier, legs_tier, boots_tier)
SELECT CONCAT('DBSIM_', i), CONCAT('DBSIM_', i),
       100 + (i * 7919) % 1700, 100 + (i * 104729) % 1700,
       0, 100, 100, 1, 0, 1, 0, 0, 0, 0
FROM #n;

INSERT INTO UserQuests (user_id, quest_id, quest_stage, quest_progress)
SELECT CONCAT('DBSIM_', i), 1, 0, 0 FROM #n;

INSERT INTO UserSkills (user_id, unspent_sp, skills_mask)
SELECT CONCAT('DBSIM_', i), 0, 0 FROM #n;

-- 저장 때 인벤토리 Execute도 함께 돌도록 계정마다 아이템 3개를 넣는다.
INSERT INTO Inventory (user_id, item_id, item_count)
SELECT CONCAT('DBSIM_', n.i), item.id, 1
FROM #n AS n CROSS JOIN (VALUES (1), (2), (3)) AS item(id);

DROP TABLE #n;

SELECT (SELECT COUNT(*) FROM Users) AS users, (SELECT COUNT(*) FROM Inventory) AS inventory;
GO
