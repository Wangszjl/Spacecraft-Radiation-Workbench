param([Parameter(Mandatory=$true)][string]$BuildDirectory,[Parameter(Mandatory=$true)][string]$Destination,[Parameter(Mandatory=$true)][string]$QtPrefix,[Parameter(Mandatory=$true)][string]$VtkPrefix,[Parameter(Mandatory=$true)][string]$Geant4Prefix,[Parameter(Mandatory=$true)][string]$DataRoot,[string]$ModelPython)
$ErrorActionPreference='Stop'
$taskProjectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
cmake "-DBUILD_DIR=$([IO.Path]::GetFullPath($BuildDirectory))" "-DDESTINATION=$([IO.Path]::GetFullPath($Destination))" "-DQT_PREFIX=$([IO.Path]::GetFullPath($QtPrefix))" "-DVTK_PREFIX=$([IO.Path]::GetFullPath($VtkPrefix))" "-DGEANT4_PREFIX=$([IO.Path]::GetFullPath($Geant4Prefix))" -P (Join-Path $taskProjectRoot 'cmake/StageRuntime.cmake')
if($LASTEXITCODE -ne 0){throw 'Runtime staging failed'}
$taskRuntime=@{schema='spacerad.runtime/1';worker='scdat_radiation_worker.exe';data_root=[IO.Path]::GetFullPath($DataRoot);library_directories=@('.')}
$taskRuntime | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'runtime.json') -Encoding utf8
$taskCliGuide=Join-Path $taskProjectRoot 'docs/CLI.md'
if(Test-Path -LiteralPath $taskCliGuide){Copy-Item -LiteralPath $taskCliGuide -Destination (Join-Path $Destination 'CLI.md')}
$taskResultsGuide=Join-Path $taskProjectRoot 'docs/RESULTS.md'
if(Test-Path -LiteralPath $taskResultsGuide){Copy-Item -LiteralPath $taskResultsGuide -Destination (Join-Path $Destination 'RESULTS.md')}
$taskScanGuide=Join-Path $taskProjectRoot 'docs/SHIELDING_SCAN.md'
if(Test-Path -LiteralPath $taskScanGuide){Copy-Item -LiteralPath $taskScanGuide -Destination (Join-Path $Destination 'SHIELDING_SCAN.md')}
$taskVoxelGuide=Join-Path $taskProjectRoot 'docs/VOXEL_SCORING.md'
if(Test-Path -LiteralPath $taskVoxelGuide){Copy-Item -LiteralPath $taskVoxelGuide -Destination (Join-Path $Destination 'VOXEL_SCORING.md')}
foreach($taskImportanceName in @('IMPORTANCE_SAMPLING.md','IMPORTANCE_REVIEW.md','SOURCE_SAMPLING_EDITOR.md','ENERGY_RESPONSE_STUDY.md','PARTITION_STUDY.md','NATIVE_PARTITION_STUDY.md','DEVICE_RESPONSE.md','TARGET_FLUENCE.md','ORBIT_DESKTOP.md')){
 $taskImportanceGuide=Join-Path (Join-Path $taskProjectRoot 'docs') $taskImportanceName
 if(Test-Path -LiteralPath $taskImportanceGuide){Copy-Item -LiteralPath $taskImportanceGuide -Destination (Join-Path $Destination $taskImportanceName)}
}

$taskDeviceExample=Join-Path $taskProjectRoot 'examples/device-response'
if(Test-Path -LiteralPath $taskDeviceExample){$taskExamples=Join-Path $Destination 'examples';New-Item -ItemType Directory -Force -Path $taskExamples | Out-Null;Copy-Item -LiteralPath $taskDeviceExample -Destination $taskExamples -Recurse}
if($ModelPython){
 $taskPythonPath=[IO.Path]::GetFullPath($ModelPython)
 if(-not (Test-Path -LiteralPath $taskPythonPath -PathType Leaf)){throw 'Configured model Python does not exist'}
 @{schema='spacerad.orbit-model-runtime/1';python=$taskPythonPath;tools_directory='model-tools';timeout_seconds=300} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'orbit-model-runtime.json') -Encoding utf8
}
