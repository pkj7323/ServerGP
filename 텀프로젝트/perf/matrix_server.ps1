# 서버 PC에서 실행한다. 조건마다 서버를 새로 띄우고, 봇이 접속했다 나가면 다음 조건으로 넘어간다.
# 봇 PC의 matrix_bot.ps1에 같은 Modes, Counts, Reps를 준다.
# 예: .\matrix_server.ps1 -Modes base,sector_brute -Counts 500,1000,2000,4000
param(
    [Parameter(Mandatory)][string[]]$Modes,
    [Parameter(Mandatory)][string[]]$Counts,
    [string[]]$Reps = @('1'),
    [int]$MaxWaitBotSeconds = 600,
    [int]$MaxRunSeconds = 900,
    [string]$OutDir = ''
)

if (-not $OutDir) { $OutDir = "$PSScriptRoot\results" }
# powershell -File로 실행하면 1,2 같은 목록이 문자열 하나로 들어오므로 직접 나눈다.
$Modes = @($Modes -split ',')
$Counts = @($Counts -split ',' | ForEach-Object { [int]$_ })
$Reps = @($Reps -split ',' | ForEach-Object { [int]$_ })
New-Item -ItemType Directory -Force $OutDir | Out-Null
$root = Split-Path $PSScriptRoot -Parent

function Get-BotCount {
    @(Get-NetTCPConnection -LocalPort 3900 -State Established -ErrorAction SilentlyContinue).Count
}

foreach ($mode in $Modes) {
    foreach ($bots in $Counts) {
        foreach ($rep in $Reps) {
            $tag = "${mode}_n${bots}_r$rep"
            $env:EXP_SERVER_CSV = Join-Path $OutDir "server_$tag.csv"
            $env:EXP_DB_CONN = 'DRIVER={ODBC Driver 18 for SQL Server};SERVER=DESKTOP-PARK\SQLEXPRESS01;DATABASE=2021180009_SGP_Bench;Trusted_Connection=Yes;TrustServerCertificate=Yes;'
            $server = Start-Process (Join-Path $root "Server\x64\exp\$mode\Server.exe") -WorkingDirectory (Join-Path $root 'Server\Server') `
                -PassThru -WindowStyle Minimized -RedirectStandardOutput (Join-Path $OutDir "server_$tag.log") -RedirectStandardError (Join-Path $OutDir "server_$tag.err")
            "$(Get-Date -Format HH:mm:ss) start $tag pid=$($server.Id)"

            # 봇 PC의 포트 확인 접속은 금방 끊기므로, 봇이 일정 수 이상 붙어야 시작으로 본다.
            $need = [Math]::Min(10, $bots)
            $deadline = (Get-Date).AddSeconds($MaxWaitBotSeconds)
            while ((Get-Date) -lt $deadline -and -not $server.HasExited -and (Get-BotCount) -lt $need) { Start-Sleep -Seconds 1 }
            $started = (Get-BotCount) -ge $need

            $deadline = (Get-Date).AddSeconds($MaxRunSeconds)
            $empty = 0
            while ($started -and (Get-Date) -lt $deadline -and -not $server.HasExited -and $empty -lt 3) {
                if ((Get-BotCount) -eq 0) { $empty++ } else { $empty = 0 }
                Start-Sleep -Seconds 1
            }

            if ($server.HasExited) { "$(Get-Date -Format HH:mm:ss) CRASH $tag exit=$($server.ExitCode)" }
            else {
                Stop-Process -Id $server.Id -Force
                $server.WaitForExit()
                if (-not $started) { "$(Get-Date -Format HH:mm:ss) NO_BOTS $tag" }
                elseif ($empty -lt 3) { "$(Get-Date -Format HH:mm:ss) TIMEOUT $tag" }
                else { "$(Get-Date -Format HH:mm:ss) ok $tag" }
            }
            Start-Sleep -Seconds 10 # 봇 PC가 포트가 닫힌 것을 보고 다음 조건으로 넘어갈 시간을 준다
        }
    }
}
"finished. results: $OutDir"
