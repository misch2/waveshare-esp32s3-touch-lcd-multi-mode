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
  # The parent commit still points at the previous release while an update is
  # being validated. Read the staged gitlink so this guard proves precisely
  # what the next parent commit would record.
  $gitlink = (& git -C $repositoryRoot ls-files --stage -- $component.path)
  $gitlinkMatch = [regex]::Match($gitlink, '^160000 ([0-9a-f]{40}) \d+\t')
  if ($LASTEXITCODE -ne 0 -or -not $gitlinkMatch.Success) {
    throw "Cannot read staged gitlink for $($component.path). Stage the submodule with git add first."
  }
  $stagedPin = $gitlinkMatch.Groups[1].Value
  if ($stagedPin -ne $component.forkPin) {
    throw "$($component.id): manifest forkPin does not match staged gitlink (expected $stagedPin)."
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
