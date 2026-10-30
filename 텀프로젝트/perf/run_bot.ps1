# 봇 PC에서 실행한다. 서버를 먼저 띄운 뒤 시작한다.
# 예: .\run_bot.ps1 -Server 192.168.0.10 -Bots 1000 -Tag base_n1000_r1 -Seconds 240
param(
    [string]$Server = '127.0.0.1',
    [Parameter(Mandatory)][int]$Bots,
    [Parameter(Mandatory)][string]$Tag,
    [int]$Seconds = 240,
    [int]$Seed = 1,
    [int]$ConnectMs = 30,
    [int]$DbRate = 0,
    [string]$Exe = '',
    [string]$OutDir = ''
)

# Windows PowerShell 5.1은 param 기본값에서 $PSScriptRoot가 비어 있으므로 여기서 정한다.
if (-not $OutDir) { $OutDir = "$PSScriptRoot\results" }
if (-not $Exe) {
    # 봇 PC로 옮긴 패키지에서는 같은 폴더의 exe를 쓴다.
    $Exe = "$PSScriptRoot\STRESS_TEST.exe"
    if (-not (Test-Path $Exe)) { $Exe = "$PSScriptRoot\..\STRESS_TEST\x64\exp\bot\STRESS_TEST.exe" }
}
if (-not (Test-Path $Exe)) { throw "봇 빌드 없음: $Exe" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

$env:EXP_SERVER_IP = $Server
$env:EXP_BOTS = $Bots
$env:EXP_DURATION = $Seconds
$env:EXP_SEED = $Seed
$env:EXP_CONNECT_MS = $ConnectMs
$env:EXP_DB_RATE = $DbRate
$env:EXP_BOT_CSV = Join-Path $OutDir "bot_$Tag.csv"

$p = Start-Process (Resolve-Path $Exe) -PassThru
"bot pid=$($p.Id) bots=$Bots db_rate=$DbRate tag=$Tag"
$p.WaitForExit()
"done: $env:EXP_BOT_CSV"
