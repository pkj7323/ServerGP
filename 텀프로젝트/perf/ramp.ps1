# 한 PC에서 인원을 올려 가며 어디서 무너지는지 본다. 봇과 서버가 CPU를 나눠 쓰므로 측정값으로 쓰지 않는다.
param(
    [string[]]$Modes = @('base'),
    [int[]]$Counts = @(500, 1000, 2000, 4000),
    [int]$StableSeconds = 30
)

$root = Split-Path $PSScriptRoot -Parent
$out = "$PSScriptRoot\results_ramp"
New-Item -ItemType Directory -Force $out | Out-Null

foreach ($mode in $Modes) {
    foreach ($bots in $Counts) {
        $tag = "${mode}_n$bots"
        $env:EXP_SERVER_CSV = "$out\server_$tag.csv"
        $env:EXP_DB_CONN = 'DRIVER={ODBC Driver 18 for SQL Server};SERVER=DESKTOP-PARK\SQLEXPRESS01;DATABASE=2021180009_SGP_Bench;Trusted_Connection=Yes;TrustServerCertificate=Yes;'
        $server = Start-Process (Join-Path $root "Server\x64\exp\$mode\Server.exe") -WorkingDirectory (Join-Path $root 'Server\Server') `
            -PassThru -WindowStyle Minimized -RedirectStandardOutput "$out\server_$tag.log" -RedirectStandardError "$out\server_$tag.err"

        $deadline = (Get-Date).AddSeconds(90)
        while ((Get-Date) -lt $deadline -and -not $server.HasExited) {
            if (Get-NetTCPConnection -OwningProcess $server.Id -State Listen -ErrorAction SilentlyContinue) { break }
            Start-Sleep -Milliseconds 500
        }
        Start-Sleep -Seconds 12 # NPC 초기화가 끝날 때까지 기다린다

        # 봇 루프의 Sleep(1)이 약 15ms라 초당 65명 정도 접속한다.
        $seconds = [int]($bots / 60) + $StableSeconds
        & "$PSScriptRoot\run_bot.ps1" -Bots $bots -Tag $tag -Seconds $seconds -ConnectMs 1 -OutDir $out | Out-Null

        if ($server.HasExited) { "CRASH $tag exit=$($server.ExitCode)" }
        else { Stop-Process -Id $server.Id -Force; "ok    $tag" }
    }
}
python "$PSScriptRoot\summarize.py" $out --warmup 5 --tail 1
