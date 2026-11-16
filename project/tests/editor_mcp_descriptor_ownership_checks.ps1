[CmdletBinding()]
param([switch]$Candidate,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$ownerProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ownerFixture=[IO.Path]::GetFullPath((Join-Path $ownerProject ('..\generated\mcp-descriptor-ownership\'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $ownerFixture | Out-Null
Write-Output "MCP_OWNERSHIP_LOGS=$ownerFixture CANDIDATE=$Candidate"
if($Candidate){
 $ownerBody=Get-Content -LiteralPath (Join-Path $ownerProject 'engine\services\EditorMcpSession.h') -Raw
 $ownerAccess='descriptor_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ'
 $ownerClose='if (descriptor_ != INVALID_HANDLE_VALUE) { CloseHandle(descriptor_); descriptor_ = INVALID_HANDLE_VALUE; }'
 $ownerDelete='if (descriptorOwned_) { DeleteFileW(Descriptor(root_).c_str()); descriptorOwned_ = false; }'
 foreach($anchor in @($ownerAccess,$ownerClose,$ownerDelete)){if(([regex]::Matches($ownerBody,[regex]::Escape($anchor))).Count -ne 1){throw 'Candidate requires the unchanged baseline Start/Stop anchors'}}
 $ownerBody=$ownerBody.Replace('#include "McpCommandBridge.h"',"#include `"McpCommandBridge.h`"`n#include `"McpDescriptorRetirement.h`"")
 $ownerCapture='descriptorOwned_ = true; if (!descriptorRetirement_.Capture(path.parent_path(), descriptor_)) throw std::runtime_error("Cannot capture MCP descriptor ownership");'
 $ownerBody=$ownerBody.Replace('descriptorOwned_ = true;',$ownerCapture)
 $ownerRetire=@'
if (descriptor_ != INVALID_HANDLE_VALUE) {
            CloseHandle(descriptor_); descriptor_ = INVALID_HANDLE_VALUE;
            if (descriptorOwned_ && !descriptorRetirement_.Retire())
                OutputDebugStringA("KuboEngine cannot retire owned MCP descriptor; retained for inspection\n");
        }
        descriptorRetirement_.Reset();
'@
 $ownerBody=$ownerBody.Replace($ownerClose,$ownerRetire).Replace($ownerDelete,'descriptorOwned_ = false;')
 $ownerBody=$ownerBody.Replace('std::filesystem::path root_;',"McpDescriptorRetirement descriptorRetirement_;`n    std::filesystem::path root_;")
 $ownerHistory=(Join-Path $ownerProject 'engine\editor\EntityHistory.h').Replace('\','/')
 $ownerBody=$ownerBody.Replace('"../editor/EntityHistory.h"',('"'+$ownerHistory+'"'))
 [IO.File]::WriteAllText((Join-Path $ownerFixture 'candidate_EditorMcpSession.h'),$ownerBody,[Text.UTF8Encoding]::new($false))
}
& $MSBuild (Join-Path $PSScriptRoot 'editor_mcp_descriptor_ownership_checks.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 /p:VCToolsVersion=14.44.35207 "/p:OutDir=$ownerFixture\bin\" "/p:IntDir=$ownerFixture\obj\" "/p:McpOwnershipIncludeDirectory=$ownerFixture" "/p:McpOwnershipCandidate=$($Candidate.IsPresent.ToString().ToLowerInvariant())" /verbosity:minimal *> (Join-Path $ownerFixture 'build.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $ownerFixture 'build.log') -Tail 20;throw 'Ownership probe build failed'}
$ownerChild=$null
try {
 $ownerChild=Start-Process -FilePath (Join-Path $ownerFixture 'bin\McpDescriptorOwnershipChecks.exe') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $ownerFixture 'native.log') -RedirectStandardError (Join-Path $ownerFixture 'native.stderr.log')
 $ownerHandle=$ownerChild.Handle
 if(!$ownerChild.WaitForExit(10000)){throw 'Owned descriptor probe exceeded10s'}
 $ownerChild.WaitForExit();$ownerExit=$ownerChild.ExitCode
} finally {if($ownerChild){if(!$ownerChild.HasExited){Stop-Process -Id $ownerChild.Id;$ownerChild.WaitForExit()};$ownerChild.Dispose()}}
Get-Content -LiteralPath (Join-Path $ownerFixture 'native.log') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $ownerFixture 'native.stderr.log') -Encoding UTF8
Write-Output "MCP_OWNERSHIP_NATIVE_EXIT=$ownerExit CANDIDATE=$Candidate"
if($ownerExit -ne 0){throw 'Owned descriptor gate failed; retained evidence'}
