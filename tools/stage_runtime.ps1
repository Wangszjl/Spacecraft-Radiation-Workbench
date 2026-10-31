param([Parameter(Mandatory=$true)][string]$BuildDirectory,[Parameter(Mandatory=$true)][string]$Destination,[Parameter(Mandatory=$true)][string]$QtPrefix,[Parameter(Mandatory=$true)][string]$VtkPrefix,[Parameter(Mandatory=$true)][string]$Geant4Prefix,[Parameter(Mandatory=$true)][string]$DataRoot)
$ErrorActionPreference='Stop'
$taskProjectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
cmake "-DBUILD_DIR=$([IO.Path]::GetFullPath($BuildDirectory))" "-DDESTINATION=$([IO.Path]::GetFullPath($Destination))" "-DQT_PREFIX=$([IO.Path]::GetFullPath($QtPrefix))" "-DVTK_PREFIX=$([IO.Path]::GetFullPath($VtkPrefix))" "-DGEANT4_PREFIX=$([IO.Path]::GetFullPath($Geant4Prefix))" -P (Join-Path $taskProjectRoot 'cmake/StageRuntime.cmake')
if($LASTEXITCODE -ne 0){throw 'Runtime staging failed'}
$taskRuntime=@{schema='spacerad.runtime/1';worker='scdat_radiation_worker.exe';data_root=[IO.Path]::GetFullPath($DataRoot);library_directories=@('.')}
$taskRuntime | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'runtime.json') -Encoding utf8
$taskCliGuide=Join-Path $taskProjectRoot 'docs/CLI.md'
if(Test-Path -LiteralPath $taskCliGuide){Copy-Item -LiteralPath $taskCliGuide -Destination (Join-Path $Destination 'CLI.md')}
