param([string]$IdfPath='D:\esp-idf\v6.1.1\esp-idf', [string]$ToolsPath='C:\Espressif',
      [string]$PythonEnv='C:\Espressif\tools\python\v6.1.1\venv', [string]$ExtraComponentDirs='')
$ErrorActionPreference='Stop'
$env:IDF_TOOLS_PATH=$ToolsPath; $env:IDF_PYTHON_ENV_PATH=$PythonEnv; $env:PYTHONUTF8='1'
. (Join-Path $IdfPath 'export.ps1')
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$args = @('-C', $project, '-B', (Join-Path $project 'build'), '-D', 'IDF_TARGET=esp32p4')
if ($ExtraComponentDirs.Trim()) { $args += @('-D', "EXTRA_COMPONENT_DIRS=$ExtraComponentDirs") }
idf.py @args build
