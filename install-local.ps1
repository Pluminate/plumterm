# Installs PlumTerm for the current user on Windows. No administrator rights needed.
#
#   .\install-local.ps1                       Build from source, install to %LOCALAPPDATA%\Programs\PlumTerm
#   .\install-local.ps1 D:\Apps\PlumTerm      Install somewhere else
#   .\install-local.ps1 -QtDir C:\Qt\6.10.3\msvc2022_arm64
#   .\install-local.ps1 -BundleDir .\app      Install an already built copy; needs no build tools
#   .\install-local.ps1 -Uninstall            Remove the app, shortcut, PATH entry and folder menu
#
# Building needs the Visual Studio C++ Build Tools, Rust, CMake, Ninja and Qt 6. README.md lists the
# commands to install them.

[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\PlumTerm'),
    [string]$QtDir,
    [string]$BundleDir,
    [switch]$NoLaunch,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$BinDir = Join-Path $InstallDir 'bin'
$Shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'PlumTerm.lnk'
$MenuKeys = @('HKCU:\Software\Classes\Directory\shell\PlumTerm',
              'HKCU:\Software\Classes\Directory\Background\shell\PlumTerm')

function Step($message) { Write-Host "==> $message" }
function Fail($message) { Write-Host "error: $message" -ForegroundColor Red; exit 1 }

# Edits the registry value directly: [Environment]::SetEnvironmentVariable would save the PATH with
# entries like %USERPROFILE% permanently expanded.
function Set-UserPathEntry($dir, [bool]$present) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
    try {
        $current = $key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $entries = @($current -split ';' | Where-Object { $_ })
        if (($entries -contains $dir) -eq $present) { return }
        $entries = @($entries | Where-Object { $_ -ne $dir })
        if ($present) { $entries += $dir }
        $key.SetValue('Path', ($entries -join ';'), [Microsoft.Win32.RegistryValueKind]::ExpandString)
    } finally {
        $key.Close()
    }
    # Clearing an unused variable makes Windows tell running programs that the environment changed.
    [Environment]::SetEnvironmentVariable('PLUMTERM_PATH_REFRESH', $null, 'User')
}

function Assert-NotRunning {
    $exe = Join-Path $InstallDir 'plumterm.exe'
    $running = Get-Process plumterm -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
    if ($running) { Fail "PlumTerm is running from $InstallDir. Close it and try again." }
}

# Only ever delete a folder that holds our own executable, since -InstallDir can point anywhere.
function Remove-InstalledApp {
    if (-not (Test-Path $InstallDir)) { return }
    if (-not (Test-Path (Join-Path $InstallDir 'plumterm.exe'))) {
        Fail "$InstallDir exists but is not a PlumTerm install, so it was left alone. Choose another folder."
    }
    Remove-Item $InstallDir -Recurse -Force
}

# Builds the app and assembles it in build\bundle, leaving any existing install untouched on failure.
function Build-Bundle {
    # ---- Tools --------------------------------------------------------------------------------

    # Ask the system, not this process: Git Bash on Windows on ARM is emulated x64, so the PowerShell
    # it starts reports PROCESSOR_ARCHITECTURE as AMD64.
    $arch = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment').PROCESSOR_ARCHITECTURE
    switch ($arch) {
        'ARM64' { $vcArch = 'arm64'; $vcComponent = 'Microsoft.VisualStudio.Component.VC.Tools.ARM64'; $qtPattern = 'msvc*_arm64' }
        'AMD64' { $vcArch = 'x64';   $vcComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'; $qtPattern = 'msvc*_64' }
        default { Fail "Unsupported architecture: $arch" }
    }
    Step "Detected Windows on $vcArch"

    # Tools installed moments ago are not on this session's PATH yet, so also read the saved PATH.
    $env:Path = @($env:Path, [Environment]::GetEnvironmentVariable('Path', 'User'),
                  [Environment]::GetEnvironmentVariable('Path', 'Machine'),
                  (Join-Path $env:USERPROFILE '.cargo\bin')) -join ';'

    $missing = @('cmake', 'ninja', 'cargo') | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
    if ($missing) { Fail "Missing tools: $($missing -join ', '). See the Windows section of README.md." }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires $vcComponent -property installationPath }
    if (-not $vsPath) { Fail "Visual Studio C++ tools for $vcArch not found. See the Windows section of README.md." }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'

    $qt = if ($QtDir) { $QtDir } else { $env:QT_DIR }
    if (-not $qt) {
        $qt = Get-ChildItem 'C:\Qt\6.*\*' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like $qtPattern -and (Test-Path (Join-Path $_.FullName 'bin\windeployqt.exe')) } |
            Sort-Object { [version]$_.Parent.Name } -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $qt -or -not (Test-Path (Join-Path $qt 'bin\windeployqt.exe'))) {
        Fail "Qt 6 for $vcArch not found in C:\Qt. Pass its folder with -QtDir (it holds bin\windeployqt.exe)."
    }
    Step "Using Qt from $qt"

    # ---- Build --------------------------------------------------------------------------------

    # Run the compiler environment script and bring its variables into this session.
    Step 'Loading the Visual Studio environment...'
    $vcEnv = & cmd.exe /c "`"$vcvars`" $vcArch >nul && set"
    if ($LASTEXITCODE -ne 0) { Fail 'vcvarsall.bat failed.' }
    foreach ($line in $vcEnv) {
        $name, $value = $line -split '=', 2
        if ($name -and $null -ne $value) { Set-Item "env:$name" $value }
    }

    $buildDir = Join-Path $Root 'build'
    Step 'Configuring & building PlumTerm...'
    & cmake -S $Root -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
    if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }
    & cmake --build $buildDir --target plumterm
    if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }

    # ---- Assemble -----------------------------------------------------------------------------

    $stage = Join-Path $buildDir 'bundle'
    Step 'Bundling Qt libraries with windeployqt...'
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory $stage, (Join-Path $stage 'bin') | Out-Null
    Copy-Item (Join-Path $buildDir 'plumterm.exe') $stage
    & (Join-Path $qt 'bin\windeployqt.exe') --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime (Join-Path $stage 'plumterm.exe')
    if ($LASTEXITCODE -ne 0) { Fail 'windeployqt failed.' }

    # Ship Microsoft's C++ runtime beside the app, so a PC without it can still run this copy.
    $crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir "$vcArch\Microsoft.VC*.CRT") -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($crt) {
        Copy-Item (Join-Path $crt.FullName '*.dll') $stage
    } else {
        Write-Warning 'The Visual C++ runtime was not found, so it is not bundled. Other PCs may need to install it.'
    }

    # The command goes in its own folder so PATH does not expose the app's Qt DLLs to other programs.
    Set-Content (Join-Path $stage 'bin\plumterm.cmd') -Encoding Ascii -Value "@start `"`" `"%~dp0..\plumterm.exe`" %*"
}

if ($Uninstall) {
    Assert-NotRunning
    Step "Removing PlumTerm from $InstallDir..."
    Remove-InstalledApp
    Remove-Item $Shortcut -Force -ErrorAction SilentlyContinue
    foreach ($key in $MenuKeys) { Remove-Item $key -Recurse -Force -ErrorAction SilentlyContinue }
    Set-UserPathEntry $BinDir $false
    Step 'PlumTerm removed. Your settings are still in %APPDATA%\PlumTerm.'
    exit 0
}

Assert-NotRunning

if ($BundleDir) {
    if (-not (Test-Path (Join-Path $BundleDir 'plumterm.exe'))) { Fail "$BundleDir does not hold plumterm.exe." }
    $stage = (Resolve-Path $BundleDir).Path
} else {
    Build-Bundle
    $stage = Join-Path $Root 'build\bundle'
}

# ---- Install ----------------------------------------------------------------------------------

Step "Installing to $InstallDir..."
Remove-InstalledApp
New-Item -ItemType Directory -Force (Split-Path $InstallDir) | Out-Null
Copy-Item $stage $InstallDir -Recurse

# ---- Integrate with Windows -------------------------------------------------------------------

$exe = Join-Path $InstallDir 'plumterm.exe'

Step 'Adding the Start Menu shortcut...'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($Shortcut)
$link.TargetPath = $exe
$link.WorkingDirectory = $env:USERPROFILE
$link.Description = 'PlumTerm terminal'
$link.Save()

Step 'Adding the plumterm command to your PATH...'
Set-UserPathEntry $BinDir $true

Step "Adding 'Open in PlumTerm' to the folder right-click menu..."
foreach ($entry in @(@($MenuKeys[0], '%1'), @($MenuKeys[1], '%V'))) {
    $key, $placeholder = $entry
    New-Item -Path "$key\command" -Force | Out-Null
    Set-ItemProperty -LiteralPath $key -Name '(default)' -Value 'Open in PlumTerm'
    Set-ItemProperty -LiteralPath $key -Name 'Icon' -Value "`"$exe`""
    Set-ItemProperty -LiteralPath "$key\command" -Name '(default)' -Value "`"$exe`" `"$placeholder`""
}

Step "Successfully installed to $InstallDir"
Write-Host "Find PlumTerm in the Start Menu, or open a new terminal and run: plumterm"

if (-not $NoLaunch) { Start-Process $exe -WorkingDirectory $env:USERPROFILE }
