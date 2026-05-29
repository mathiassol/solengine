param(
    [Parameter(Mandatory)][string] $InstallDir,
    [string] $AppVersion = "0.1.0"
)

$editor  = Join-Path $InstallDir "sol_editor.exe"
$appName = "SolEngine"

if (-not (Test-Path $editor)) {
    Write-Host "  WARNING: sol_editor.exe not found at $editor - skipping shortcuts."
    exit 0
}

$wsh = New-Object -ComObject WScript.Shell

# Desktop shortcut
$desktop = [Environment]::GetFolderPath("Desktop")
$lnk = $wsh.CreateShortcut("$desktop\$appName.lnk")
$lnk.TargetPath       = $editor
$lnk.WorkingDirectory = $InstallDir
$lnk.Description      = "SolEngine - Open or create a project"
$lnk.Save()
Write-Host "  Created Desktop shortcut: $desktop\$appName.lnk"

# Start Menu shortcut
$startMenuDir = Join-Path ([Environment]::GetFolderPath("StartMenu")) "Programs\$appName"
if (-not (Test-Path $startMenuDir)) {
    New-Item -ItemType Directory -Path $startMenuDir | Out-Null
}
$lnk2 = $wsh.CreateShortcut("$startMenuDir\$appName.lnk")
$lnk2.TargetPath       = $editor
$lnk2.WorkingDirectory = $InstallDir
$lnk2.Description      = "SolEngine Editor"
$lnk2.Save()
Write-Host "  Created Start Menu entry:  $startMenuDir\$appName.lnk"

# Register in Apps & Features (HKCU - no admin needed)
$regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SolEngine"
if (-not (Test-Path $regPath)) {
    New-Item -Path $regPath -Force | Out-Null
}
Set-ItemProperty -Path $regPath -Name "DisplayName"     -Value $appName
Set-ItemProperty -Path $regPath -Name "DisplayVersion"  -Value $AppVersion
Set-ItemProperty -Path $regPath -Name "Publisher"       -Value "SolEngine"
Set-ItemProperty -Path $regPath -Name "InstallLocation" -Value $InstallDir
Set-ItemProperty -Path $regPath -Name "DisplayIcon"     -Value "$editor,0"
Set-ItemProperty -Path $regPath -Name "UninstallString" -Value "powershell -Command `"Remove-Item -Recurse -Force '$InstallDir'`""
Set-ItemProperty -Path $regPath -Name "NoModify"        -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "NoRepair"        -Value 1 -Type DWord
Write-Host "  Registered in Apps and Features (v$AppVersion)"
