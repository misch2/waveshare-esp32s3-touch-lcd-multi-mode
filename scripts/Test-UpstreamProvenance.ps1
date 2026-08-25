[CmdletBinding()]
param(
  [switch]$SkipAncestry
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $repositoryRoot 'UPSTREAMS.json'
$manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json

if ($manifest.schemaVersion -ne 1 -or $null -eq $manifest.components) {
  throw 'UPSTREAMS.json must contain schemaVersion 1 and components.'
}

foreach ($component in $manifest.components) {
  $gitlink = (& git -C $repositoryRoot ls-tree HEAD -- $component.path)
  if ($LASTEXITCODE -ne 0 -or $gitlink -notmatch '^160000 commit ([0-9a-f]{40})\s') {
    throw "Cannot read gitlink for $($component.path)."
  }
  if ($Matches[1] -ne $component.forkPin) {
    throw "$($component.id): manifest forkPin does not match gitlink (expected $($Matches[1]))."
  }

  $checkedOut = (& git -C (Join-Path $repositoryRoot $component.path) rev-parse HEAD).Trim()
  if ($LASTEXITCODE -ne 0 -or $checkedOut -ne $component.forkPin) {
    throw "$($component.id): checked-out submodule does not match its pinned gitlink."
  }

  if (-not $SkipAncestry) {
    & git -C (Join-Path $repositoryRoot $component.path) cat-file -e "$($component.upstreamBase)^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "$($component.id): upstreamBase object is unavailable." }
    & git -C (Join-Path $repositoryRoot $component.path) merge-base --is-ancestor $component.upstreamBase $component.forkPin
    if ($LASTEXITCODE -ne 0) { throw "$($component.id): upstreamBase is not an ancestor of forkPin." }
  }
}

Write-Output 'Upstream provenance is valid'
