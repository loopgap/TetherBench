param([string]$IdfPath='D:\esp-idf\v6.1.1\esp-idf', [string]$ToolsPath='C:\Espressif',
      [string]$PythonEnv='C:\Espressif\tools\python\v6.1.1\venv', [string]$ExtraComponentDirs='',
      [ValidateSet('high', 'full')][string]$UsbPort='high')
$ErrorActionPreference='Stop'
$env:IDF_TOOLS_PATH=$ToolsPath; $env:IDF_PYTHON_ENV_PATH=$PythonEnv; $env:PYTHONUTF8='1'
. (Join-Path $IdfPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
idf.py --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$args = @('-C', $project, '-B', (Join-Path $project 'build'), '-D', 'IDF_TARGET=esp32p4', '-D', "TB_BRIDGE_USB_PORT=$UsbPort")
if ($ExtraComponentDirs.Trim()) { $args += @('-D', "EXTRA_COMPONENT_DIRS=$ExtraComponentDirs") }
idf.py @args build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
