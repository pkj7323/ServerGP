# 서버 PC에서 실행한다. 모드 하나를 지정 시간만큼 돌리고 CSV를 남긴 뒤 종료한다.
# 예: .\run_server.ps1 -Mode base -Tag base_n1000_r1 -Seconds 260
param(
    [Parameter(Mandatory)][string]$Mode,
    [Parameter(Mandatory)][string]$Tag,
    [int]$Seconds = 260,
    [string]$OutDir = ''
)

# Windows PowerShell 5.1은 param 기본값에서 $PSScriptRoot가 비어 있으므로 여기서 정한다.
if (-not $OutDir) { $OutDir = "$PSScriptRoot\results" }
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "Server\x64\exp\$Mode\Server.exe"
if (-not (Test-Path $exe)) { throw "빌드 없음: $exe" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

$env:EXP_SERVER_CSV = Join-Path $OutDir "server_$Tag.csv"
$env:EXP_DB_CONN = 'DRIVER={ODBC Driver 18 for SQL Server};SERVER=DESKTOP-PARK\SQLEXPRESS01;DATABASE=2021180009_SGP_Bench;Trusted_Connection=Yes;TrustServerCertificate=Yes;'

# Data, ai 폴더를 상대 경로로 읽으므로 소스 폴더를 작업 폴더로 쓴다.
$p = Start-Process $exe -WorkingDirectory (Join-Path $root 'Server\Server') -PassThru -WindowStyle Minimized `
    -RedirectStandardOutput (Join-Path $OutDir "server_$Tag.log") -RedirectStandardError (Join-Path $OutDir "server_$Tag.err")
"server pid=$($p.Id) mode=$Mode tag=$Tag"
if ($p.WaitForExit($Seconds * 1000)) { throw "서버가 먼저 종료됨 (exit=$($p.ExitCode)). 로그: server_$Tag.err" }
Stop-Process -Id $p.Id -Force
"done: $env:EXP_SERVER_CSV"
