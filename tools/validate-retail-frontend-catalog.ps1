[CmdletBinding()]
param(
    [string]$CatalogPath = (Join-Path $PSScriptRoot '..\assets\catalog\retail-frontend-screens.json'),
    [string]$GeneratedPath = (Join-Path $PSScriptRoot '..\src\frontend\retail_ui_catalog.generated.cpp'),
    [string]$SourceRoot,
    [switch]$SkipSourceAudit
)

$ErrorActionPreference = 'Stop'

function Assert-CatalogCondition {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition) {
        throw "Retail frontend catalog validation failed: $Message"
    }
}

$resolvedCatalogPath = (Resolve-Path -LiteralPath $CatalogPath).Path
$catalog = Get-Content -Raw -LiteralPath $resolvedCatalogPath | ConvertFrom-Json
$resolvedGeneratedPath = (Resolve-Path -LiteralPath $GeneratedPath).Path
$expectedSourceHash = (Get-FileHash -LiteralPath $resolvedCatalogPath -Algorithm SHA256).Hash
$generatedHeader = Get-Content -LiteralPath $resolvedGeneratedPath -TotalCount 2
Assert-CatalogCondition ($generatedHeader.Count -eq 2) 'generated native catalog header is missing.'
Assert-CatalogCondition ($generatedHeader[1] -eq "// Source SHA-256: $expectedSourceHash") (
    'generated native catalog is stale; run tools/generate-retail-ui-catalog.py.'
)

Assert-CatalogCondition ($catalog.schemaVersion -eq 1) 'schemaVersion must be 1.'
Assert-CatalogCondition ($null -ne $catalog.referenceCanvas) 'referenceCanvas is required.'
Assert-CatalogCondition ($catalog.referenceCanvas.width -eq 800) 'reference canvas width must be 800.'
Assert-CatalogCondition ($catalog.referenceCanvas.height -eq 600) 'reference canvas height must be 600.'
Assert-CatalogCondition ($null -ne $catalog.shellContract) 'shellContract is required.'
Assert-CatalogCondition ($null -ne $catalog.cursor) 'cursor is required.'
Assert-CatalogCondition ($catalog.screens.Count -gt 0) 'screens must not be empty.'
Assert-CatalogCondition ($catalog.frontendClasses.Count -gt 0) 'frontendClasses must not be empty.'
Assert-CatalogCondition ($catalog.widgetTypes.Count -gt 0) 'widgetTypes must not be empty.'

$requiredScreenFields = @(
    'id',
    'class',
    'kind',
    'route',
    'source',
    'line',
    'reachability',
    'routeTriggers',
    'controls',
    'subviews',
    'states',
    'assets'
)

foreach ($screen in $catalog.screens) {
    foreach ($field in $requiredScreenFields) {
        $property = $screen.PSObject.Properties[$field]
        Assert-CatalogCondition ($null -ne $property) "screen '$($screen.id)' is missing '$field'."
    }

    Assert-CatalogCondition (-not [string]::IsNullOrWhiteSpace($screen.id)) 'screen id must not be blank.'
    Assert-CatalogCondition (-not [string]::IsNullOrWhiteSpace($screen.route)) "screen '$($screen.id)' has a blank route."
    Assert-CatalogCondition ($screen.line -gt 0) "screen '$($screen.id)' has an invalid source line."
}

$duplicateScreenIds = @(
    $catalog.screens |
        Group-Object -Property id |
        Where-Object Count -gt 1
)
$duplicateRoutes = @(
    $catalog.screens |
        Group-Object -Property route |
        Where-Object Count -gt 1
)
$duplicateFrontendClasses = @(
    $catalog.frontendClasses |
        Group-Object -Property class, source |
        Where-Object Count -gt 1
)

Assert-CatalogCondition ($duplicateScreenIds.Count -eq 0) 'screen ids must be unique.'
Assert-CatalogCondition ($duplicateRoutes.Count -eq 0) 'screen routes must be unique.'
Assert-CatalogCondition ($duplicateFrontendClasses.Count -eq 0) 'frontend class/source pairs must be unique.'

$screenIds = @{}
foreach ($screen in $catalog.screens) {
    $screenIds[$screen.id] = $true
}

foreach ($screen in $catalog.screens) {
    if ($screen.PSObject.Properties['parentScreen']) {
        Assert-CatalogCondition ($screenIds.ContainsKey($screen.parentScreen)) (
            "screen '$($screen.id)' references unknown parentScreen '$($screen.parentScreen)'."
        )
    }
}

if (-not $SkipSourceAudit) {
    if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
        $SourceRoot = $catalog.sourceRoot
    }

    $resolvedSourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
    $frontendRoot = Join-Path $resolvedSourceRoot 'aoslib\scenes\frontend'
    Assert-CatalogCondition (Test-Path -LiteralPath $frontendRoot -PathType Container) (
        "frontend source root does not exist: $frontendRoot"
    )

    $recoveredClasses = @()
    foreach ($file in Get-ChildItem -LiteralPath $frontendRoot -Filter '*.py' -File) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file.FullName) {
            $lineNumber += 1
            if ($line -match '^class\s+([A-Za-z_][A-Za-z0-9_]*)') {
                $relativeSource = 'aoslib/scenes/frontend/' + $file.Name
                $recoveredClasses += [pscustomobject]@{
                    Class = $Matches[1]
                    Source = $relativeSource
                    Line = $lineNumber
                    Key = "$($Matches[1])|$relativeSource"
                }
            }
        }
    }

    $catalogClassKeys = @{}
    foreach ($entry in $catalog.frontendClasses) {
        $key = "$($entry.class)|$($entry.source)"
        $catalogClassKeys[$key] = $entry
    }

    $recoveredClassKeys = @{}
    foreach ($entry in $recoveredClasses) {
        $recoveredClassKeys[$entry.Key] = $entry
    }

    $missingClasses = @(
        $recoveredClasses |
            Where-Object { -not $catalogClassKeys.ContainsKey($_.Key) }
    )
    $staleClasses = @(
        $catalog.frontendClasses |
            Where-Object { -not $recoveredClassKeys.ContainsKey("$($_.class)|$($_.source)") }
    )

    Assert-CatalogCondition ($missingClasses.Count -eq 0) (
        'catalog is missing recovered classes: ' + (($missingClasses | ForEach-Object Key) -join ', ')
    )
    Assert-CatalogCondition ($staleClasses.Count -eq 0) (
        'catalog has classes absent from the source tree: ' + (($staleClasses | ForEach-Object { "$($_.class)|$($_.source)" }) -join ', ')
    )

    foreach ($screen in $catalog.screens) {
        $sourcePath = Join-Path $resolvedSourceRoot ($screen.source -replace '/', '\')
        Assert-CatalogCondition (Test-Path -LiteralPath $sourcePath -PathType Leaf) (
            "screen '$($screen.id)' source does not exist: $sourcePath"
        )
    }
}

[pscustomobject]@{
    Catalog = $resolvedCatalogPath
    GeneratedCatalog = $resolvedGeneratedPath
    SchemaVersion = $catalog.schemaVersion
    Screens = $catalog.screens.Count
    FrontendClasses = $catalog.frontendClasses.Count
    WidgetTypes = $catalog.widgetTypes.Count
    ListRowTypes = $catalog.listRowTypes.Count
    SourceAudit = -not $SkipSourceAudit
} | Format-List
