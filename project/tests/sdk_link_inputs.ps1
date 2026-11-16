# Match input filenames, not MCP-like directory labels elsewhere in the command.
function Test-SdkRuntimeLinkInputs([string]$Command) {
    $runtimeToken='(?:^|[\s"\\/])KUBORUNTIME\.LIB(?=$|[\s"])'
    $editorToken='(?:^|[\s"\\/])(?:IMGUI\.LIB|[^\\/\s"]*MCP[^\\/\s"]*\.OBJ|GAME\.OBJ)(?=$|[\s"])'
    return $Command -match $runtimeToken -and $Command -notmatch $editorToken
}

# link.command describes requested inputs; link.read records files actually opened.
# This is lexical archive provenance, not a signature or physical-file identity check.
function Test-SdkResolvedArchiveInputs([string[]]$ReadLines,[string]$LibraryDirectory) {
    if([string]::IsNullOrWhiteSpace($LibraryDirectory) -or $LibraryDirectory -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+(?:[\\/]|$))'){return $false}
    try {
        $linkRequired=@('KuboRuntime.lib','KuboLua.lib','DirectXTex.lib','assimp.lib')
        $linkExpected=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($name in $linkRequired){[void]$linkExpected.Add([IO.Path]::GetFullPath((Join-Path $LibraryDirectory $name)))}
        $linkSeen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($record in $ReadLines){
            if([string]::IsNullOrWhiteSpace($record)){continue}
            $linkPath=$record.Trim();if($linkPath.StartsWith('^')){continue}
            $linkLeaf=[IO.Path]::GetFileName($linkPath)
            if($linkLeaf -in $linkRequired -or $linkLeaf -match '^assimp[^\\/]*\.lib$'){
                if($linkPath -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+(?:[\\/]|$))'){return $false}
                $linkFull=[IO.Path]::GetFullPath($linkPath)
                if(!$linkExpected.Contains($linkFull)){return $false}
                [void]$linkSeen.Add($linkFull)
            }
        }
        return $linkSeen.Count -eq $linkRequired.Count
    }catch{return $false}
}
