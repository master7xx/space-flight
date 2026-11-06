param([Parameter(Mandatory=$true)][string]$Editor)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$id = [guid]::NewGuid().ToString('N')
$dir = Join-Path $root "Saved\LanguagePersistence_$id"
$project = Join-Path $root 'SpaceFlight.uproject'
New-Item -ItemType Directory -Path $dir | Out-Null
$process = $null
function Run-Phase([string]$phase) {
    $log = Join-Path $dir "$phase.log"
    $arguments = @("`"$project`"", '-game', '-windowed', '-RenderOffScreen', '-NoSound',
        "-SpaceSaveTestId=$id", "-UserDir=`"$dir`"", "-SpaceLanguageSaveCheck=$phase",
        '-ddc=InstalledNoZenLocalFallback', "-abslog=`"$log`"")
    $script:process = Start-Process -FilePath $Editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        $result = $null
        do {
            Start-Sleep -Milliseconds 200
            $script:process.Refresh()
            if (Test-Path -LiteralPath $log) { $result = Select-String -LiteralPath $log -Pattern "LANGUAGE_SETTING_CHECK phase=$phase language=1" | Select-Object -Last 1 }
        } until ($result -or $script:process.HasExited -or [DateTime]::UtcNow -ge $deadline)
        if (-not $result) { throw "Language $phase phase failed: $log" }
        Write-Host $result.Line
    } finally {
        $script:process.Refresh()
        if (-not $script:process.HasExited) { Stop-Process -Id $script:process.Id -Force; $script:process.WaitForExit() }
        $script:process = $null
    }
}
try {
    Run-Phase 'write'
    Run-Phase 'verify'
    Write-Host 'LANGUAGE_PERSISTENCE_OK isolated=1'
} finally {
    if ($process) { $process.Refresh(); if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() } }
    if (Test-Path -LiteralPath $dir) {
        $item = Get-Item -LiteralPath $dir -Force
        if ($item.FullName -ne $dir -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            @(Get-ChildItem -LiteralPath $dir -Recurse -Force -Attributes ReparsePoint).Count) { throw "Unsafe test directory: $dir" }
        Add-Type -AssemblyName Microsoft.VisualBasic
        for ($attempt=0; $attempt -lt 3; ++$attempt) {
            try { [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($dir, [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs, [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin); break }
            catch { if ($attempt -eq 2) { throw }; Start-Sleep -Milliseconds 500 }
        }
    }
}
