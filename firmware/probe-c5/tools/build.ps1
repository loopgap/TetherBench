param([string]$IdfPath='D:\esp-idf\v6.1.1\esp-idf', [string]$ToolsPath='C:\Espressif',
      [string]$PythonEnv='C:\Espressif\tools\python\v6.1.1\venv', [switch]$Flash, [string]$Port='COM7')
$ErrorActionPreference='Stop'
$env:IDF_TOOLS_PATH=$ToolsPath
$env:IDF_PYTHON_ENV_PATH=$PythonEnv
$env:PYTHONUTF8='1'
. (Join-Path $IdfPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!(Test-Path -LiteralPath (Join-Path $project '.local/device_config.h'))) { throw 'Run tools/provision.py before building.' }
idf.py --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
idf.py -C $project -B (Join-Path $project 'build') -D IDF_TARGET=esp32c5 -D "SDKCONFIG=$project/.local/sdkconfig" build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Flash) {
    idf.py -C $project -B (Join-Path $project 'build') -D IDF_TARGET=esp32c5 -D "SDKCONFIG=$project/.local/sdkconfig" -p $Port flash
    exit $LASTEXITCODE
}
