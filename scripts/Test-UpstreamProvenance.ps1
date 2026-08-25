[CmdletBinding()]
param([switch]$SkipAncestry)

$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'test_upstream_provenance.py'))
if ($SkipAncestry) { $arguments += '--skip-ancestry' }
& python @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
