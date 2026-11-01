# 봇 PC로 옮길 파일을 bot_package.zip으로 묶는다. VC++ 런타임이 없는 PC에서도 돌도록 DLL을 같이 넣는다.
$root = Split-Path $PSScriptRoot -Parent
$dir = "$PSScriptRoot\bot_package"
New-Item -ItemType Directory -Force $dir | Out-Null

Copy-Item (Join-Path $root 'STRESS_TEST\x64\exp\bot\STRESS_TEST.exe') $dir -Force
Copy-Item "$PSScriptRoot\run_bot.ps1", "$PSScriptRoot\matrix_bot.ps1", "$PSScriptRoot\bot_run.py", "$PSScriptRoot\net_watch.py" $dir -Force
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item "$env:SystemRoot\System32\$dll" $dir -Force
}
Compress-Archive -Path "$dir\*" -DestinationPath "$PSScriptRoot\bot_package.zip" -Force
Get-ChildItem $dir | ForEach-Object { "$($_.Name) $($_.Length)" }
"zip: $PSScriptRoot\bot_package.zip"
