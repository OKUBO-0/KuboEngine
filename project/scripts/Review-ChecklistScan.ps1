param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [int]$MaxFilesPerFolder = 8,
    [int]$MaxFunctionLines = 40,
    [int]$MaxClassLines = 300
)

$ErrorActionPreference = 'Stop'

$excludedDirectoryNames = @(
    '.git',
    '.vs',
    'externals',
    'generated',
    'Debug',
    'Release',
    'x64'
)

$basicTypes = @(
    'bool',
    'char',
    'signed char',
    'unsigned char',
    'short',
    'unsigned short',
    'int',
    'unsigned int',
    'long',
    'unsigned long',
    'long long',
    'unsigned long long',
    'float',
    'double',
    'size_t',
    'uint8_t',
    'uint16_t',
    'uint32_t',
    'uint64_t',
    'int8_t',
    'int16_t',
    'int32_t',
    'int64_t'
)

function Test-IsExcludedPath {
    param([string]$Path)

    $relativePath = Get-RelativePath $Path
    if ($relativePath -eq '.') {
        return $false
    }

    $parts = $relativePath -split '[\\/]'
    foreach ($part in $parts) {
        if ($excludedDirectoryNames -contains $part) {
            return $true
        }
    }

    return $false
}

function Get-ProjectFiles {
    param([string[]]$Extensions)

    Get-ChildItem -Path $Root -Recurse -File |
        Where-Object {
            -not (Test-IsExcludedPath $_.FullName) -and
            ($Extensions -contains $_.Extension.ToLowerInvariant())
        }
}

function Get-EngineFiles {
    param([string[]]$Extensions)

    $engineRoot = Join-Path $Root 'engine'
    if (-not (Test-Path $engineRoot)) {
        return @()
    }

    Get-ChildItem -Path $engineRoot -Recurse -File |
        Where-Object { $Extensions -contains $_.Extension.ToLowerInvariant() }
}

function Get-RelativePath {
    param([string]$Path)

    $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $targetPath = [System.IO.Path]::GetFullPath($Path)
    $rootUri = [System.Uri]::new($rootPath)
    $targetUri = [System.Uri]::new($targetPath)
    $relativeUri = $rootUri.MakeRelativeUri($targetUri)
    [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

function Convert-ToSummary {
    param(
        [string]$Name,
        [object[]]$Items,
        [string]$OkMessage,
        [string]$FailMessage
    )

    if ($Items.Count -eq 0) {
        [pscustomobject]@{
            Check = $Name
            Result = 'OK'
            Details = $OkMessage
        }
    } else {
        [pscustomobject]@{
            Check = $Name
            Result = 'NG'
            Details = "$FailMessage ($($Items.Count))"
        }
    }
}

function Find-DirectoriesOverLimit {
    $directories = Get-ChildItem -Path $Root -Recurse -Directory |
        Where-Object { -not (Test-IsExcludedPath $_.FullName) }

    foreach ($directory in $directories) {
        $count = @(
            Get-ChildItem -LiteralPath $directory.FullName -File |
                Where-Object { -not (Test-IsExcludedPath $_.FullName) }
        ).Count

        if ($count -gt $MaxFilesPerFolder) {
            [pscustomobject]@{
                Path = Get-RelativePath $directory.FullName
                Count = $count
            }
        }
    }
}

function Find-ForbiddenPlatforms {
    $projectFiles = @(
        Get-Item (Join-Path $Root 'KuboEngine.sln') -ErrorAction SilentlyContinue
        Get-Item (Join-Path $Root 'KuboEngine.vcxproj') -ErrorAction SilentlyContinue
    ) | Where-Object { $_ -ne $null }
    $pattern = '(Debug|Release)\|(?:Win32|ARM64)|Platform\)(?:==|''\))?[^''"]*(?:Win32|ARM64)|<Platform>(?:Win32|ARM64)</Platform>'

    foreach ($file in $projectFiles) {
        $matches = Select-String -Path $file.FullName -Pattern $pattern
        foreach ($match in $matches) {
            [pscustomobject]@{
                Path = Get-RelativePath $file.FullName
                Line = $match.LineNumber
                Text = $match.Line.Trim()
            }
        }
    }
}

function Find-UsingNamespace {
    $files = Get-EngineFiles @('.h', '.hpp', '.cpp')
    foreach ($file in $files) {
        $matches = Select-String -Path $file.FullName -Pattern '^\s*using\s+namespace\s+'
        foreach ($match in $matches) {
            [pscustomobject]@{
                Path = Get-RelativePath $file.FullName
                Line = $match.LineNumber
                Text = $match.Line.Trim()
            }
        }
    }
}

function Find-BasicConstReference {
    $files = Get-EngineFiles @('.h', '.hpp', '.cpp')
    $typePattern = ($basicTypes | ForEach-Object { [regex]::Escape($_) }) -join '|'
    $pattern = "\bconst\s+($typePattern)\s*&"

    foreach ($file in $files) {
        $matches = Select-String -Path $file.FullName -Pattern $pattern
        foreach ($match in $matches) {
            [pscustomobject]@{
                Path = Get-RelativePath $file.FullName
                Line = $match.LineNumber
                Text = $match.Line.Trim()
            }
        }
    }
}

function Find-RawNewDelete {
    $files = Get-EngineFiles @('.h', '.hpp', '.cpp')
    $pattern = '\b(new|delete)\b'

    foreach ($file in $files) {
        $lineNumber = 0
        foreach ($line in Get-Content -Path $file.FullName) {
            $lineNumber++
            $trimmed = $line.Trim()
            if ($trimmed.StartsWith('//')) {
                continue
            }
            if ($trimmed -match 'operator\s+(new|delete)') {
                continue
            }
            if ($trimmed -match '=\s*delete\s*;') {
                continue
            }
            if ($trimmed -match '\bnew\b|\bdelete\b') {
                [pscustomobject]@{
                    Path = Get-RelativePath $file.FullName
                    Line = $lineNumber
                    Text = $trimmed
                }
            }
        }
    }
}

function Find-LongMemberFunctions {
    $files = Get-EngineFiles @('.cpp')
    $signaturePattern = '^\s*(?:[\w:<>,~*&\s]+\s+)?[\w:<>~]+::[\w:<>~]+\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?$'
    $sameLinePattern = '^\s*(?:[\w:<>,~*&\s]+\s+)?[\w:<>~]+::[\w:<>~]+\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{'

    foreach ($file in $files) {
        $lines = Get-Content -Path $file.FullName
        for ($index = 0; $index -lt $lines.Count; $index++) {
            $line = $lines[$index]
            $isFunctionStart = $line -match $sameLinePattern
            $braceLine = $index

            if (-not $isFunctionStart -and $line -match $signaturePattern -and ($index + 1) -lt $lines.Count -and $lines[$index + 1].Trim() -eq '{') {
                $isFunctionStart = $true
                $braceLine = $index + 1
            }

            if (-not $isFunctionStart) {
                continue
            }

            $depth = 0
            $started = $false
            for ($end = $braceLine; $end -lt $lines.Count; $end++) {
                $chars = $lines[$end].ToCharArray()
                foreach ($char in $chars) {
                    if ($char -eq '{') {
                        $depth++
                        $started = $true
                    } elseif ($char -eq '}') {
                        $depth--
                    }
                }

                if ($started -and $depth -eq 0) {
                    $lineCount = $end - $index + 1
                    if ($lineCount -gt $MaxFunctionLines) {
                        [pscustomobject]@{
                            Path = Get-RelativePath $file.FullName
                            Line = $index + 1
                            Count = $lineCount
                            Signature = $line.Trim()
                        }
                    }
                    $index = $end
                    break
                }
            }
        }
    }
}

function Find-LongClassDefinitions {
    $files = Get-EngineFiles @('.h', '.hpp')
    $startPattern = '^\s*(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\b'

    foreach ($file in $files) {
        $lines = Get-Content -Path $file.FullName
        for ($index = 0; $index -lt $lines.Count; $index++) {
            if ($lines[$index] -notmatch $startPattern) {
                continue
            }

            $name = $Matches[2]
            $depth = 0
            $started = $false
            for ($end = $index; $end -lt $lines.Count; $end++) {
                $chars = $lines[$end].ToCharArray()
                foreach ($char in $chars) {
                    if ($char -eq '{') {
                        $depth++
                        $started = $true
                    } elseif ($char -eq '}') {
                        $depth--
                    }
                }

                if ($started -and $depth -eq 0 -and $lines[$end] -match '};') {
                    $lineCount = $end - $index + 1
                    if ($lineCount -gt $MaxClassLines) {
                        [pscustomobject]@{
                            Path = Get-RelativePath $file.FullName
                            Line = $index + 1
                            Count = $lineCount
                            Name = $name
                        }
                    }
                    $index = $end
                    break
                }
            }
        }
    }
}

$directoryIssues = @(Find-DirectoriesOverLimit)
$platformIssues = @(Find-ForbiddenPlatforms)
$usingNamespaceIssues = @(Find-UsingNamespace)
$basicConstRefIssues = @(Find-BasicConstReference)
$rawNewDeleteIssues = @(Find-RawNewDelete)
$longFunctionIssues = @(Find-LongMemberFunctions)
$longClassIssues = @(Find-LongClassDefinitions)

$summaries = @(
    Convert-ToSummary 'Folders with too many direct files' $directoryIssues 'No folder exceeds the configured direct-file limit.' 'Folders exceed the configured direct-file limit'
    Convert-ToSummary 'Unsupported Visual Studio platforms' $platformIssues 'No Win32 or ARM64 platform entries were found in project files.' 'Unsupported platform entries were found'
    Convert-ToSummary 'using namespace in engine' $usingNamespaceIssues 'No using namespace directives were found under engine.' 'using namespace directives were found under engine'
    Convert-ToSummary 'Basic type const references' $basicConstRefIssues 'No basic type const-reference parameters were found under engine.' 'Basic type const-reference parameters were found'
    Convert-ToSummary 'Raw new/delete in engine' $rawNewDeleteIssues 'No raw new/delete use was found under engine.' 'Raw new/delete use was found under engine'
    Convert-ToSummary 'Member functions over line limit' $longFunctionIssues 'No member function exceeds the configured line limit.' 'Member functions exceed the configured line limit'
    Convert-ToSummary 'Class definitions over line limit' $longClassIssues 'No class definition exceeds the configured line limit.' 'Class definitions exceed the configured line limit'
)

Write-Host 'Review checklist scan'
Write-Host "Root: $Root"
Write-Host ''
$summaries | Format-Table -AutoSize

$issueSets = @(
    @{ Name = 'Folders with too many direct files'; Items = $directoryIssues },
    @{ Name = 'Unsupported Visual Studio platforms'; Items = $platformIssues },
    @{ Name = 'using namespace in engine'; Items = $usingNamespaceIssues },
    @{ Name = 'Basic type const references'; Items = $basicConstRefIssues },
    @{ Name = 'Raw new/delete in engine'; Items = $rawNewDeleteIssues },
    @{ Name = 'Member functions over line limit'; Items = $longFunctionIssues },
    @{ Name = 'Class definitions over line limit'; Items = $longClassIssues }
)

foreach ($issueSet in $issueSets) {
    if ($issueSet.Items.Count -eq 0) {
        continue
    }

    Write-Host ''
    Write-Host "[$($issueSet.Name)]"
    $issueSet.Items | Format-Table -AutoSize
}

$totalIssues = (
    $directoryIssues.Count +
    $platformIssues.Count +
    $usingNamespaceIssues.Count +
    $basicConstRefIssues.Count +
    $rawNewDeleteIssues.Count +
    $longFunctionIssues.Count +
    $longClassIssues.Count
)

if ($totalIssues -gt 0) {
    exit 1
}

exit 0
