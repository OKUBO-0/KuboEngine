# KuboRuntime development SDK

Controller follow-up (2026-10-13): strict `.animfsm` Graph/Text/Runtime and typed
AnimatorController assets, held GUID/revision loading, Object3D GUID assignment,
Scene authored controllerGuid and Inspector selection are implemented. Renderer
admits up to four ordered override/additive layers with subtree bone masks and
instance weights. Each layer owns its clock/state/blend; Scene speed multiplies
state speed; state loop owns looping. Stop/Restart reset the current state clock,
not parameters/entry; explicit rebind returns to entry. Failed normal admission
preserves the prior owner; GPU publication exceptions are not universal rollback.
Lua `scene.controller_state(id, layer?)` reads admitted current state or nil; scoped
`controller_set_float(id,name,value)`, `controller_set_bool(id,name,value)` and
`controller_trigger(id,name)` share the existing fixed Playback FIFO/64 hook budget/
256 pending budget and paired Audio/World publication. True means queued; actual
unknown-name/type/owner rejection happens at Scene consumption. Failed hooks discard
their requests, without undoing earlier healthy hooks. Snapshot identity includes
authored settings+GUID, not a component generation or asset-content revision token.
Export validator format2 checks Controller source+metadata in source and copied
candidate. Model clip compatibility remains actual runtime admission.
Object/Scene/request/reader layouts changed: use freshly matched Debug/Release
archives and compiled header closure, never replacement headers over older SDKs.
Earlier full CPU/shipped proofs concern their recorded snapshots, not this new
layered ABI. Targeted CPU and actual Editor/Player layer/Lua proofs exist; fresh
full156 and SDK consumer/package verification remain pending. IK, Inspector layer
controls, hot reload/generation, physical UI and overall phases remain pending.
Older entries below are historical capability checkpoints.

Force-state workspace follow-up (2026-10-13): Object3D/EntityScene expose
ForceControllerState and GetControllerStateNames for actual admitted layers.
Scene Inspector has runtime layer state/time/progress/normalized/weight and
force/restart controls. They do not edit Controller assets or Scene Undo/Redo.
Lua `scene.controller_play_state(id, state, blendSeconds?, layer?)` queues explicit
state reentry; defaults are .2 seconds/layer0, finite seconds0..60, integer layer0..3,
UTF-8 state1..128 bytes. It shares the existing hook/queue/Audio+World contracts.
True means queued; missing actual graph state/layer/owner can reject at consumption.
Same-hook reads still observe the prior actual owner. Parameters/triggers/weights
and authored settings remain unchanged; the Animator playing flag is not changed.
Use animator_play separately when necessary. New matching SDK/portable consumer
and full/distribution verification remain pending for this force-enabled source;
the preceding layered-1791863483518 packages were verified before force was added.
Do not overlay these declarations over their older archives.

Indexed Lua layer queries also expose `controller_time`, `controller_progress`
(unwrapped exit progress), `controller_normalized_time` (loop fraction or clamped
completion) and `controller_layer_weight`, each accepting `(id, layer?)`.
Omitted/nil layer means0; explicit indices must be integers0..3. Missing/stale
actual owners or absent graph layers return nil. `controller_set_layer_weight`
requires `(id, layer, weight)` with finite weight0..1 and uses the same request
FIFO/budgets/failed-hook isolation. True admits a request, not a predicted pose or
successful consumption; reads in that hook still observe the actual previous
owner. Layered frame preparation validates private clocks/poses/hierarchy/palette
before committing; this is not a guarantee against all GPU/device exceptions.

Object3D blend follow-up (2026-10-12): BlendToAnimationClip(name, seconds) admits
only present nonempty clips on the same skinned model and finite durations0..60.
Ordinary blends advance two real instance clocks and mix bind-relative local TRS;
interruption freezes the last sampled pose as a new source and resets target time.
Speed0 pauses; reset/clip assignment/model assignment cancel. Blend palettes bypass
the single-clip cache. GetAnimationJointTransform copies owned skeleton local TRS;
ordinary single-clip cache hits do not synchronize that skeleton, so this is NOT
a universal rendered-pose query. Layers/IK/controller/Lua authoring are not provided
by these low-level methods. Object3D layout changed: use new matching headers and
archives, never preceding clip-catalog binaries with replacement headers alone.

Loaded animation clip catalog follow-up (2026-10-12): Model and Object3D expose
GetAnimationClipNames(); EntityScene exposes the Entity-scoped overload, refusing
missing/inactive/mismatched/failed model owners. Names are valid authored UTF-8,
sorted case-sensitively with default first; empty channels/nonfinite or negative
durations are excluded. The list is read-only and does not import clips, advance
clocks, or validate cross-version ABI. Inspector selection uses Scene settings and
existing Undo/Redo. Use a newly rebuilt matched SDK; old archives do not define
these new nonvirtual methods. State machines/layers/blends/IK remain separate work.

Material dependency inspection follow-up (2026-10-12): glTF/GLB now admit
KHR_materials_specular and KHR_materials_ior in declared material payloads.
Scalar ranges, texture/image indices, declaration consistency and unknown/nested
extension refusal are checked; external images still use the shared bounded URI
and copied-root closure rules. This is dependency preservation, not PBR/IOR shader
fidelity or a general glTF schema validator. Other extensions remain unsupported.
Use rebuilt matching packages; earlier immutable validators still refuse these.

Animator actual-state workspace follow-up (2026-10-12): `scene.animator_state(id)`
reads the current Scene-owned model, not the command queue. It returns
`ready,playing,time`; unavailable/mismatched model/settings/inactive targets return
`false,false,nil`, and a missing Entity/Animator returns nil. IDs are strict.
A current host-owned hook and same-World scoped AnimatorStateReader are required;
missing/wrong/expired/module ownership refuses. Read-only Scene scopes may query.
Queued commands do not immediately change results; staged settings must match the
actual binding to report ready. Reader implementation performs no path resolution
or filesystem reads. Native reader/World lifetime and runtime-thread/no-reentry
ownership remain caller contracts; this is not a general dangling-pointer guard.
Hosts borrow a source with ScriptRuntime::AnimatorReadScope around Dispatch/Shutdown;
direct factories supply it to ScriptSceneView::Scope or WriteScope. Game/RuntimeGame
provide actual EntityScene sources. Query time is unavailable rather than zero when
not ready. Native lookup cost is outside the Lua instruction/wall-time budget.
ScriptSceneView/ScriptRuntime/EntityScene layouts changed: use a newly matched
state-query SDK, not the preceding149-header command SDK. Older statements below
about no state query describe their historical checkpoint. Current verification
and pending full145/application/distribution gates are in the Animator record.

Animator command workspace follow-up (2026-10-12): `scene.animator_play/pause/stop/restart(id)`
queue runtime commands from an active writable hook when the host opts in.
True means queued, not applied: the Scene later rejects missing/inactive targets,
missing clips or a changed exact clip/speed/loop/playOnAwake snapshot. Same-ID
remove/re-add with identical settings remains the same target; no component-generation
or retired-VM ownership guarantee is provided. Commands share the64 successful Scene
operation cap, with64 fixed requests per hook and256 pending requests per host.
Audio and command admission precede no-allocation World publication; unhandled hook
errors or either queue's refusal discard that hook's World/Audio/Animator candidates.
Earlier successful hooks remain committed. Main-thread/no-reentry ownership is required.
Direct hosts opt in with ScriptRuntime::SetAnimatorRequestsEnabled, drain
TakeAnimatorRequests, synchronize EntityScene models with zero delta, apply requests,
then perform the normal time step. Game/RuntimeGame implement this drain sequence.
Native fixed queue memory is outside the Lua VM allocator budget. No Lua runtime
clock/playing query is added. CreateLuaScriptFactory now has an eighth optional
Animator queue argument: use freshly matched Runtime/Lua libraries and headers,
never overlay them onto the older seven-argument SDK. The149-header command SDK
contains the code but predates this README follow-up; immutable published packages
are not rewritten. See docs/phase4-animator-integration-design.md for verification
and remaining full application/distribution gates. Older statements below are history.

Run report source follow-up (2026-10-11): current Framework writes a best-effort, create-only `.kubo-run-error-*.log` into its captured project directory on caught Run operation/shutdown errors, before terminal propagation or persistent exit70. Reports retain both diagnostics, terminal flag and attempt count; controls/non-ASCII bytes use `\xHH` and each message is capped at1024 input bytes. The captured directory, not a changed cwd or replacement pathname, is the destination. Failed writes may leave a new partial file; an end marker identifies complete content. No overwrite/delete or fallback cwd is used. This is not hardware-crash coverage, whole-I/O deadline, power-loss/remote-volume guarantee, general OOM or job/MCP recovery. Existing published SDKs predate this feature; fresh matched archives/headers and native verification are required. See docs/phase3-run-reports.md for evidence and pending gates.

Lifecycle follow-up (2026-10-09): `Framework::Run` is single-use and refuses another active Run before shared-service acquisition. It retains operation/first shutdown errors, tries Finalize up to three times and propagates only after terminal base retirement. Paired errors use `Engine::Base::RunFailure::Operation()` / `Cleanup()`; `RunDiagnostics.h` reports both, including unknown non-standard native exceptions. Persistent refusal reports errors to stderr/debugger and exits70 without destructing a still-active host. The Starter delegates retry to Run and executes post-shutdown verification once. Direct Initialize/Finalize and other host APIs remain runtime-thread APIs, not a thread-safe multi-host contract. GPU fence event waits have a five-second remaining budget; stale notifications cannot renew that budget or prove completion. Timeout and device-removal sentinel results preserve pending ownership for Run's retry/fail-closed path. This is not GPU device recreation, a wall-clock bound on every driver call/whole shutdown, durable fatal reporting or automatic in-flight job/MCP recovery. Use newly matched headers/archives/Starter; do not overlay these onto an older SDK. Source/native failure probes and matching normal/package/distribution verification are recorded separately, not implied by this README.

Model-generation workspace follow-up: `scene.spawn_model(name,path[,parent[,guid]])` adds a Model Entity using the host's fixed project root and read-only asset registry. Missing parent returns nil; missing/wrong-type/invalid assets error and discard the hook. Omitted/nil/0 parent is root; GUID-only uses an empty path. Modern path identities are pinned, GUID paths follow identity without fallback, rooted/colon/parent escape/invalid UTF-8 and linked source ancestry are refused. Scalar TRS APIs configure the generated Entity (radians). Native hosts use `ScriptRuntime::SetAssetOwner(registry,absoluteRoot)`; registry must outlive Dispatch/Shutdown and cannot be modified concurrently. Owner replacement during dispatch is forbidden. Creation shares the64-Entity hook cap. This requires a new matched Model-generation SDK, not headers over the old component-clone libraries. Distribution verification is pending; primitive/Runtime Prefab factories remain absent. Statements below are older checkpoint history.

Runtime clone follow-up: Lua `scene.clone(source[,parent])` copies the selected subtree into fresh IDs; omitted/nil/0 parent is root, missing source/parent returns nil. Local TRS, typed components, asset references and Script properties are retained, Prefab links are stripped, and Script.started/RigidBody.grounded reset. New VM/render/Audio/Physics state is created by the hosts, not copied. Aggregate created Entities are capped at64 per hook (shared with empty spawn, no refund on deletion). Direct native hosts must reacquire every record by ID after structural publication. No Model-path spawning or Runtime Prefab factory is provided. Use a new matched component-clone SDK; preceding scene-name-utf8 distributions do not contain this API. Packaging/external verification is recorded separately, not implied by this text.

Next workspace patch: spawn names additionally require well-formed UTF-8 (still 1–256 bytes, no NUL). Invalid sequences are rejected before native candidate allocation. The preceding `lua-scene-structure-20261008` distribution remains a verified earlier snapshot with byte-length/NUL-only spawn admission; it does not contain this correction. Build and verify a new matched SDK before using the updated Starter/headers with its archives.

Workspace follow-up (not yet a new verified SDK distribution): Lua now has `scene.spawn(name[,parent])` (new empty Entity ID, nil for a missing parent; omitted/nil/0 parent is root), `scene.destroy(id)` (single Entity, direct children become roots retaining local TRS) and `scene.destroy_subtree(id)` (explicit descendants). Delete returns true/false for success/missing. Spawn names are 1–256 bytes, no NUL. Scalar and structural commands share a 64-successful-call hook cap; at most 64 deleted Entities and 10000 World Entities. Reads see newly created/deleted IDs. Errors/quota/Audio refusal discard the hook's structural/scalar/Audio candidates without consuming live IDs. Structural hooks lazily copy the World, prepare before Audio, then exchange storage without moving owning records. All borrowed EntityRecord references are invalid after publication: direct factory hosts must reacquire by ID. Runtime retains final self state for self-deletion Destroy. Scalar-only hooks keep their fixed-array path. Component/Prefab spawning, World-space setters, byte/native wall-time budgets and concurrent writer protection remain absent. Old statements below about no spawn/destroy or scalar-only publication describe the preceding release. Use a newly built matched header/archive/Starter distribution once verified; do not overlay workspace headers onto the older safe-publication SDK.

Lua hooks have `scene` reads: find(name) (lowest matching ID), entity_count(), exists(id), name/parent/active(id), position/rotation/scale(id), velocity(id). Transform components are local, active is the Entity's own flag and velocity is an existing RigidBody's World-space linear velocity; missing valid IDs yield nil (exists yields false). A runtime-owned borrowed ScriptSceneView is active only during Dispatch/Shutdown; module-scope and expired direct-factory reads refuse. Self reads see the hook's candidate only while that Entity still exists. Native factory consumers may supply a shared view and a correctly scoped ScriptSceneView::Scope for reads or explicit WriteScope for mutations; neither may outlive the World. Changed public layouts/overloads require matched headers/archives. Searches are linear and Lua instruction quotas do not bound native query wall-time. Spawn/destroy and multi-World identity guarantees are not supplied by this increment.

`scene.set_position/translate/set_rotation/set_scale(id,x,y,z)`, `scene.set_active(id,boolean)` and `scene.set_velocity(id,x,y,z)` return true on success, false for missing valid targets (or a missing RigidBody for velocity). IDs are strict integer numbers; vectors are strict finite numeric floats and active is a strict boolean. Invalid arguments, overflow, missing write scope or more than 64 successful mutation calls per hook raise Lua errors. Repeated calls to the same ID count too. Inactive targets are writable. Self commands share the existing self candidate; other targets are staged in fixed storage. Reads, including composed World matrices, see these candidates during the hook. Unhandled hook error/quota or Audio admission refusal discards self/peer/Audio changes; VM internal state is not rolled back. Earlier successful hooks remain committed. Native factory hosts must use explicit WriteScope, keep view/World alive and prohibit reentry/concurrent World mutation between preparation, Audio admission and scalar publication. Ordinary Scope stays read-only even for a mutable World. No structural mutation or World-space setters are added. Native fixed batch memory is outside the Lua VM allocator budget. Use this snapshot's updated Starter and archives together.

`scene.world_position(id)` and `scene.world_matrix(id)` now query composed World transforms. The matrix is a 16-element row-major Lua table, with translation at 13/14/15. Parent rotation/mirrored scaling/shear and the current hook's own TRS candidate (also for descendants) are retained. Broken ancestry, missing valid IDs and nonfinite matrices yield nil; finite zero scales are allowed. EntityWorld adds a read-only GetWorldMatrix(id, result, transformOverride) overload, preserving existing hierarchy links and failed output. Old two-argument calls remain. The Starter verifies the new native overload and Lua candidate World reads against this snapshot's archives; do not mix old headers/libraries. No World-space setter is provided.

ActionMap now supports mixed Keyboard/PadButton/PadAxis bindings. BindPadButton/BindPadAxis and the pad-frame Evaluate/Active overloads preserve separate source/code identities; old keyboard-only queries ignore pad bindings. Axis values contribute continuously while a configurable absolute-value threshold in (0,1] determines action button state. InputFrame/CaptureFrame deliver matching named actions to Lua/fixed hooks. Keyboard-only settings Save stays version1, pad settings use version2, both Load transactionally. The Editor has basic named-action add/replace/clear/save/load authoring. ActionMap::Binding layout changed again: this snapshot must not be mixed with previous headers/archives. The Starter verification includes mixed-domain calls against the prebuilt ActionMap implementation.

InputFrame includes the host-selected XInput port-0 pad snapshot. Lua hooks can read `input.pad_connected()`, `pad_connected_this_frame()`, `pad_disconnected_this_frame()`, `pad_down/pressed/released(input.pad_buttons.A)`, `pad_stick(right)` (two return values) and `pad_trigger(right)`; `right` is an optional boolean, default false. Button constants are stable IDs, not XInput masks. Foreground/host gating is neutral, disconnect releases held buttons once, and fixed hooks consume observed transitions through the bounded shared input FIFO. No four-controller selection or Lua vibration is claimed. InputFrame/ScriptRuntime/RuntimeGame layouts changed: use this snapshot's headers and archives together. The starter verification also exercises pad-only queue admission/consumption/gate reset through the prebuilt runtime; it does not prove physical device polling.

AssetRegistry now classifies `.prefab` as the appended `AssetType::Prefab` and persists its type name in import metadata/databases; existing enum values retain their numbering. Editor project-owned Prefab save/place/unlink is not a Runtime SDK authoring API. Runtime executes the Scene's baked components and retains links as authored data; it does not load/update source Prefabs automatically. Use this snapshot's headers/archives together, including AssetRegistry.h.

`EntityWorld::CreateEntity` now returns 0 without allocation on JSON-exact ID exhaustion or invalid allocation data; check its result before accessing a new Entity. Scene IDs are limited to 1..2^53-1 (parent 0 is root), matching MCP/Prefab. Existing/reserved IDs and the current allocator high-water are respected. Ordinary deleted IDs are not all persisted as tombstones on Scene reload. Use the matching snapshot's headers/archives for this behavior; general identity-consumer/performance auditing remains incomplete.

EntityRecord now includes root-only authored Prefab link data (source GUID, canonical baseline and local-to-Scene ID map). Use this snapshot's headers and archives together; earlier EntityRecord layouts are not ABI-compatible even with the same MSVC version. Runtime executes the baked live components and does not load a source Prefab file or perform Apply/Revert. Deleted mapped identities remain reserved on Scene reload. Editor creation/placement UI, propagation, nested Prefabs and explicit export baking remain incomplete.

Native exports now assemble and validate a sibling candidate with `package_publication.ps1` before directory publication, including `native-build.json` and the runtime SHA-256. Keep this snapshot's build/package/helper scripts together. New/empty output is supported without overwriting nonempty output; identity-bound recovery records retain interrupted stages/backups and recover already committed packages without recompilation. An interrupted uncommitted retry reports retained data and asks for inspection/retry. Existing empty-directory publication has a brief absent-name window on Windows, guarded by recovery; power-loss durability, hostile filesystem races and cooked distributions remain unverified.

`EntityWorld::SetParent(id, parent, true, error)` preserves the current World matrix when it can be stably represented as local TRS under the new parent. Root parent is 0. Singular/shear/degenerate/overflow results are refused without publishing parent or Transform; the two-argument overload retains local TRS. This is not arbitrary shear authoring or bitwise matrix preservation. Use the new headers and runtime archives together for the added overload.

Physics now uses a step-local spatial BVH broad phase. Position corrections refit all child shapes owned by the moved Bodies and requery later pairs in the existing storage order. This reduces separated AABB work; dense contacts can still be quadratic and this is not CCD, a stack solver or a frame-time guarantee.

This is a local Windows x64 Debug/Release C++20 SDK snapshot, not a stable ABI or a publicly cleared distribution. It contains prebuilt KuboRuntime/DirectXTex/Lua/Assimp archives, the headers observed in both runtime builds, properties, resources/DXC DLLs and a native-game starter. Engine `.cpp` implementation and Editor/MCP/Launcher executables are not included. Some compiled-runtime headers still expose legacy/editor helper types; those are not supported SDK authoring APIs.

ColliderComponent includes optional Scene friction/restitution surface coefficients (both default zero). Use this snapshot's headers and runtime archives together: older snapshots have a different component layout and do not implement the new response. Matching MSVC alone does not make mixed engine snapshots ABI-compatible. The starter's `--sdk-verify-materials` switch is an additional fixture-only eight-frame material test, not general game validation.

Physics uses parent/self-composed World AABBs, World velocity/gravity and candidate parent-local position publication. Body-less child colliders share the nearest explicit RigidBody; nested Bodies have independent World poses during a Step. Inactive ancestors skip simulation. Collider half extents remain local; rotated/sheared boxes are conservatively enclosed, not exact OBB contacts. Dynamic Bodies without their own Collider can move freely or own child shapes. Singular parent branches are diagnosed; an unrepresentable local publication retains the Physics Step. RuntimeGame/PhysicsWorld layout also changed, so never mix this snapshot with earlier archives. `--sdk-verify-hierarchy` additionally verifies the id 4/5 fixture, not arbitrary game correctness.

Use compatible MSVC v143 (the package records the exact compiler version), Windows SDK and MSBuild. You do not need to open Visual Studio, but C++ compilation requires its Build Tools/toolchain. Lua/generic Scene use in the portable engine remains no-build. Release archives contain MSVC LTCG objects and must be linked with a compatible toolset, not an arbitrary later compiler. Match configuration, `/MTd` versus `/MT`, `KUBO_RUNTIME_ONLY`, Unicode and C++20; changing class-layout/CRT macros can break the ABI.

Build `examples/Starter/Starter.vcxproj` with MSBuild, `/m:1 /p:Configuration=Debug` (or Release) `/p:Platform=x64`. Run its `bin/<Configuration>/Starter.exe --project "C:\Projects\Game\Game.kubo"` from any working directory. The template derives RuntimeGame and lets native code add Update/Draw/Initialize/Finalize overrides. Keep the derived overrides' base lifecycle calls where needed. Pass the project root, asset root, Game extent and Startup Scene before Run. Only generic EntityWorld Scenes are the authored path demonstrated by this starter. In a packaged game it discovers an executable-relative manifest when `--project` is omitted, even after executable renaming.

For an external project copy the starter's project/source, pass `KuboSdkRoot` pointing to this SDK (with a trailing separator), or import KuboRuntime.props after Microsoft.Cpp.props. The properties supply headers/libraries/definitions and copy executable-owned resources/DXC DLLs/notices to OutDir. Only your C++ sources compile. Keep your project manifest/assets separate; SDK resources are engine-owned, not gameplay assets. `--sdk-verify` is a starter test mode that verifies the exported keep-World API on an isolated mirrored/rotated hierarchy, runs eight frames and requests native close, not a normal game feature.

For one-command native build/export use `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_native_game.ps1 -ProjectFile <Game.kubo> -NativeProject <Game.vcxproj> -SdkDirectory <this-sdk> -OutputDirectory <new-empty-export> -Configuration Release`. The bundled driver discovers MSBuild with vswhere (or accepts `-MSBuild`), verifies SDK hashes, builds your trusted application project into an isolated temporary stage and exports its executable through the bundled packager. Source ProjectReferences are refused; build errors do not start export. Temporary native build products are removed on normal cleanup; force-stopped builds can leave stages. Game assembly occurs in a retained sibling candidate, not directly in the public output; see the staged-publication limits above. The command executes your MSBuild project/targets and is not a sandbox for untrusted projects. The separate portable engine offers Hub `New C++ Project (SDK)` and Editor `File > Export Game` native mode with local settings/conflict checks and explicit cancellation; SDK selection/management and manual/clean-machine UI QA are still incomplete.

`sdk-package.json` records format/configurations/toolchain and file hashes. Verify it when relocating the SDK. Original library/resource/license provenance remains subject to a full redistribution audit; retained header license comments and available Lua/ImGui notices are not proof that every dependency's redistribution obligations are complete. No clean-machine, cross-toolchain, public ABI/version, installer/signing or production SDK guarantee is made.

## Startup asset reference checks

### Window-close ownership for custom hosts

`Framework::ConfigureWindowCloseRequestPolicy(bool)` is a protected, pre-initialization hook. It prepares the existing owned WinApp before native window creation. The default Runtime/Player policy remains native close destruction. A custom host opting into deferred close must consume `winApp->ConsumeCloseRequest()` and route it through its own confirmation/end-request flow; deferral alone does not stop the Run loop. Calls after Framework initialization starts are rejected. The Editor configures deferral before creation so a startup close request survives to its document-action handler. This does not cancel initialization work or suppress initialization errors, and is not a public ABI/version compatibility guarantee.

The export tool checks startup Model paths and Model/Script GUID sources before assembly and again inside the copied candidate. Missing/wrong-type/non-copied references prevent publication. Path-only Script registrations may identify native callbacks rather than files, so their missing-file validation remains separate pending an explicit native registration contract.

The bundled `tools/KuboAssetValidator.exe` reuses the Editor's read-only OBJ/MTL/glTF/GLB dependency inspection for reachable material/Texture/external-buffer files. Keep it with this snapshot's scripts. Missing/non-copied dependencies or unsupported inspected syntax prevent publication; no sidecars/import caches are generated. The source tree is bounded to 65536 entries and existing identity metadata must be readable. GLB v2 containers have a 512 MiB container/8 MiB JSON bound; binary payloads are skipped while container/chunk structure, implicit buffer extent/zero padding, JSON version and external URI references are checked. FBX graphs, extended MTL/glTF features, import success, Lua syntax and full Scene validation/cooking remain unverified. Native build still requires matching Build Tools; this prebuilt validation tool does not.

Animator authored-settings follow-up (2026-10-11): `scene.animator(id)` returns
`clip,speed,loop,playOnAwake` or nil for a missing Entity/component.
`scene.set_animator(id,clip,speed,loop,playOnAwake)` creates/updates settings;
`scene.remove_animator(id)` removes the component. Mutations require an active
host-owned writable hook, return false for missing valid targets, and share the
64-successful-operation cap. Strict UTF-8 nonempty clip (256 bytes max), finite
nonnegative float-range speed and strict booleans are required. Invalid arguments
raise Lua errors. Settings use private World promotion, are immediately queryable
and cloneable inside the hook, and publish with scalar/structural changes only
after hook/Audio admission succeeds. Actual clip availability is checked later by
EntityScene; saved missing names are not silently replaced. No Lua runtime
Play/Pause/Stop/Restart/query-clock API is added yet. This requires a newly built
matched Runtime SDK, not the preceding148-header Animator-render-only SDK libraries.
`examples/lua_api/Animator.lua` is an annotation-only supplement, not a loadable
game Script or a verified full language-server integration. Current broader SDK
publication/external consumer gates are documented in the Animator integration record.

