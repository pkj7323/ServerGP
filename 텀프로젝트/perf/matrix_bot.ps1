# 봇 PC에서 실행한다. 서버가 뜨기를 기다렸다가 조건을 순서대로 돌린다. 서버 PC의 matrix_server.ps1과 인자를 맞춘다.
# 예: .\matrix_bot.ps1 -Server 172.30.1.10 -Modes base,sector_brute -Counts 500,1000,2000,4000
param(
    [Parameter(Mandatory)][string]$Server,
    [Parameter(Mandatory)][string[]]$Modes,
    [Parameter(Mandatory)][string[]]$Counts,
    [string[]]$Reps = @('1'),
    [int]$WarmupSeconds = 60,
    [int]$StableSeconds = 180,
    [int]$InitSeconds = 20,
    [int]$DbRate = 0,
    [int]$Adaptive = 0,
    [int]$StopGain = 0,
    [string]$OutDir = ''
)

if (-not $OutDir) { $OutDir = "$PSScriptRoot\results" }
# powershell -File로 실행하면 1,2 같은 목록이 문자열 하나로 들어오므로 직접 나눈다.
$Modes = @($Modes -split ',')
$Counts = @($Counts -split ',' | ForEach-Object { [int]$_ })
$Reps = @($Reps -split ',' | ForEach-Object { [int]$_ })

function Test-Port {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $wait = $client.BeginConnect($Server, 3900, $null, $null)
        return $wait.AsyncWaitHandle.WaitOne(1000) -and $client.Connected
    }
    catch { return $false }
    finally { $client.Close() }
}

foreach ($mode in $Modes) {
    foreach ($bots in $Counts) {
        foreach ($rep in $Reps) {
            $tag = "${mode}_n${bots}_r$rep"
            "$(Get-Date -Format HH:mm:ss) wait server for $tag"
            while (-not (Test-Port)) { Start-Sleep -Seconds 1 }
            Start-Sleep -Seconds $InitSeconds # NPC 초기화가 끝날 때까지 기다린다

            # 봇 접속 속도가 초당 65명 정도이므로 접속 시간을 더한다. 끝부분 10초는 집계에서 뺀다.
            $seconds = [int]($bots / 60) + $WarmupSeconds + $StableSeconds + 10
            & "$PSScriptRoot\run_bot.ps1" -Server $Server -Bots $bots -Tag $tag -Seconds $seconds -ConnectMs 1 -DbRate $DbRate -Adaptive $Adaptive -StopGain $StopGain -OutDir $OutDir

            # 서버 PC가 이 서버를 내릴 때까지 기다려야 다음 조건이 이전 서버에 붙지 않는다.
            while (Test-Port) { Start-Sleep -Seconds 1 }
        }
    }
}
"finished. results: $OutDir"
