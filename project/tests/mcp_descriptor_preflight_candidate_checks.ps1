[CmdletBinding()]
param([string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$preflightProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$preflightLogs=[IO.Path]::GetFullPath((Join-Path $preflightProject ('..\generated\mcp-preflight-candidate\'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $preflightLogs | Out-Null
Write-Output "MCP_PREFLIGHT_CANDIDATE_LOGS=$preflightLogs"
$preflightBody=Get-Content -LiteralPath (Join-Path $preflightProject 'engine\services\McpStaleArchive.h') -Raw -Encoding UTF8
$preflightAnchor='!Shallow(bytes, size)'
if(([regex]::Matches($preflightBody,[regex]::Escape($preflightAnchor))).Count -ne 1){throw 'Private preflight anchor mismatch'}
$preflightBody=$preflightBody.Replace($preflightAnchor,'(!Shallow(bytes, size) || !McpDescriptorUniqueKeysCandidate::Unique(bytes, size))')
$preflightInclude=(Join-Path $PSScriptRoot 'mcp_descriptor_unique_keys_candidate.h').Replace('\','/')
$preflightBody=('#include "'+$preflightInclude+'"'+[Environment]::NewLine)+$preflightBody
[IO.File]::WriteAllText((Join-Path $preflightLogs 'mcp_stale_archive_unique_keys.h'),$preflightBody,[Text.UTF8Encoding]::new($false))
$preflightSession=Get-Content -LiteralPath (Join-Path $preflightProject 'engine\services\EditorMcpSession.h') -Raw -Encoding UTF8
$preflightSessionAnchor='#include "McpStaleArchive.h"'
if(([regex]::Matches($preflightSession,[regex]::Escape($preflightSessionAnchor))).Count -ne 1){throw 'Private session include anchor mismatch'}
$preflightSession=$preflightSession.Replace($preflightSessionAnchor,'#include "mcp_stale_archive_unique_keys.h"')
$preflightCallAnchor='const auto document = Value::parse(text);'
if(([regex]::Matches($preflightSession,[regex]::Escape($preflightCallAnchor))).Count -ne 1){throw 'Private Call preflight anchor mismatch'}
$preflightSession=$preflightSession.Replace($preflightCallAnchor,('if (!McpDescriptorUniqueKeysCandidate::Unique(text.data(), text.size())) throw std::runtime_error("Invalid Editor MCP descriptor");'+[Environment]::NewLine+'        '+$preflightCallAnchor))
[IO.File]::WriteAllText((Join-Path $preflightLogs 'candidate_EditorMcpSession.h'),$preflightSession,[Text.UTF8Encoding]::new($false))
$preflightRecovery=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'mcp_stale_archive_checks.cpp') -Raw -Encoding UTF8
$preflightRecoveryAnchors=@('#include "../engine/services/McpStaleArchive.h"','#include "../engine/services/EditorMcpSession.h"')
foreach($preflightRecoveryAnchor in $preflightRecoveryAnchors){if(([regex]::Matches($preflightRecovery,[regex]::Escape($preflightRecoveryAnchor))).Count -ne 1){throw 'Private recovery include anchor mismatch'}}
$preflightRecovery=$preflightRecovery.Replace($preflightRecoveryAnchors[0],'#include "mcp_stale_archive_unique_keys.h"').Replace($preflightRecoveryAnchors[1],'#include "candidate_EditorMcpSession.h"')
[IO.File]::WriteAllText((Join-Path $preflightLogs 'candidate_recovery_checks.cpp'),$preflightRecovery,[Text.UTF8Encoding]::new($false))
$preflightIpc=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'editor_mcp_session_checks.cpp') -Raw -Encoding UTF8
$preflightIpc=$preflightIpc.Replace('#include "../engine/services/EditorMcpSession.h"','#include "candidate_EditorMcpSession.h"').Replace('#include "../engine/editor/EntityDocumentState.h"','#include "engine/editor/EntityDocumentState.h"').Replace('#include "../engine/assets/AssetRegistry.h"','#include "engine/assets/AssetRegistry.h"')
$preflightIpcAnchor='auto invalid = descriptor; invalid["pid"] = 0.0;'
if(([regex]::Matches($preflightIpc,[regex]::Escape($preflightIpcAnchor))).Count -ne 1){throw 'Private IPC negative injection anchor mismatch'}
$preflightIpcCases=@'
const auto refuseRaw = [&](const std::string& raw) {
            const auto copiedPath = EditorMcpSession::Descriptor(descriptorCopyRoot);
            { std::ofstream copied(copiedPath, std::ios::binary); copied << raw; Require(bool(copied), "Raw fixture write failed"); }
            bool refused = false;
            try { EditorMcpSession::Call(descriptorCopyRoot, Value::parse(R"({"id":99,"method":"scene.create_entity","params":{"name":"Must not publish"}})")); }
            catch (const std::exception& failure) { refused = std::string(failure.what()) == "Invalid Editor MCP descriptor"; }
            std::ifstream retained(copiedPath, std::ios::binary);
            const std::string after{std::istreambuf_iterator<char>(retained), {}};
            Require(refused && after == raw && session.IsRunning() && world.Capture().dump() == ownerWorldBefore,
                "Ambiguous/deep metadata changed evidence or live World");
        };
        auto raw = descriptor.dump(); raw.pop_back();
        refuseRaw(raw + ",\"pid\":0}");
        refuseRaw(raw + ",\"p\\u0069d\":0}");
        refuseRaw(raw + ",\"process_creation_low\":0}");
        refuseRaw(raw + ",\"unknown\":[[[[[[[[0]]]]]]]]}");
        std::cout << "PASS: private Call rejects duplicate literal/escaped identity and depth before IPC; exact evidence/live World retained\n";
        
'@
$preflightIpc=$preflightIpc.Replace($preflightIpcAnchor,($preflightIpcCases+$preflightIpcAnchor))
[IO.File]::WriteAllText((Join-Path $preflightLogs 'candidate_ipc_checks.cpp'),$preflightIpc,[Text.UTF8Encoding]::new($false))
foreach($preflightMode in @('Keys','Archive','Recovery','IPC')) {
 & $MSBuild (Join-Path $PSScriptRoot 'mcp_descriptor_preflight_candidate_checks.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 /p:VCToolsVersion=14.44.35207 "/p:McpPreflightMode=$preflightMode" "/p:McpUniqueKeysDirectory=$preflightLogs" "/p:OutDir=$preflightLogs\$preflightMode\bin\" "/p:IntDir=$preflightLogs\$preflightMode\obj\" /verbosity:minimal *> (Join-Path $preflightLogs "$preflightMode-build.log")
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $preflightLogs "$preflightMode-build.log") -Tail 15;throw 'Private preflight build failed'}
 $preflightChild=$null
 try {
  $preflightChild=Start-Process -FilePath (Join-Path $preflightLogs "$preflightMode\bin\McpDescriptorPreflightCandidateChecks.exe") -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $preflightLogs "$preflightMode-native.log") -RedirectStandardError (Join-Path $preflightLogs "$preflightMode-native.stderr.log")
  $preflightHandle=$preflightChild.Handle
  if(!$preflightChild.WaitForExit(20000)){throw 'Owned private preflight exceeded20s'}
  $preflightChild.WaitForExit();$preflightExit=$preflightChild.ExitCode
 } finally {if($preflightChild){if(!$preflightChild.HasExited){Stop-Process -Id $preflightChild.Id;$preflightChild.WaitForExit()};$preflightChild.Dispose()}}
 Get-Content -LiteralPath (Join-Path $preflightLogs "$preflightMode-native.log") -Encoding UTF8
 Get-Content -LiteralPath (Join-Path $preflightLogs "$preflightMode-native.stderr.log") -Encoding UTF8
 Write-Output "MCP_PREFLIGHT_CANDIDATE_NATIVE_EXIT=$preflightExit MODE=$preflightMode"
 if($preflightExit -ne 0){throw 'Private preflight failed; evidence retained'}
}
