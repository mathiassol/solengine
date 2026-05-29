# AAA Engine Architecture: From SolEngine to Unreal-Scale Power

> **Scope**: This document covers the non-rendering architectural systems that give AAA engines (Unreal Engine 5, CryEngine) their power and scalability. Renderer architecture is excluded. The goal is to understand what systems to build to push SolEngine toward UE5-scale capability.

---

## Current SolEngine Architecture: Honest Assessment

Before looking outward, here is where SolEngine stands today:

| Area | Current State | Notes |
|---|---|---|
| Scene graph | Node/Node3D inheritance tree, `unique_ptr` ownership | Clean, godot-like, works well for small scenes |
| Entity model | Inheritance-based + Lua scripting per node | No ECS, no component composition |
| Physics | Jolt (`PhysicsWorld`, CharacterVirtual, StaticBody3D) | Good backend, limited query API exposure |
| Scripting | Lua + LuaBridge, single shared state | Lifecycle: on_ready/on_update/on_destroy |
| Asset loading | Path-driven, synchronous, no asset registry | No GUIDs, no DDC, no async |
| Memory | `std::unique_ptr`/`shared_ptr`, no custom allocators | Jolt has its own allocators; nothing else |
| Threading | Jolt's internal `JobSystemThreadPool` only | Main loop is entirely single-threaded |
| Input | GLFW polling on main thread | No action abstraction layer |
| Events | `signal.h` exists but barely used | No event bus, all calls are direct |
| Reflection | Minimal (`reflect.h`, `ComponentRegistry`) | Not driving serialization or tooling |
| Audio | **None** | — |
| Networking | **None** | — |
| Profiling | **None** | No stat hooks, no trace system |
| World streaming | **None** | One scene at a time, blocking swap |

**The engine's strength**: the scene graph + Lua scripting makes rapid iteration very Godot-like. The gap is everything that makes a world *big, alive, and concurrent*.

---

## 1. Entity Component System vs. Actor-Component Model

### The Core Problem

SolEngine's inheritance model hits a performance wall at scale. For 10,000 enemies, you have 10,000 separately heap-allocated node objects, each with its own vtable, at random memory addresses. Every tick chases pointers. CPU prefetcher gives up.

### UE5: Actor-Component (OOP, good for unique objects)

UE5's traditional model: everything is an `AActor`. Components hang off actors via `TArray<UActorComponent*>`.

```
AActor → TArray<UActorComponent*>
          ├── UStaticMeshComponent
          ├── UCharacterMovementComponent
          └── UCapsuleComponent
```

This is **Array of Structures (AoS)** — data is scattered across heap. Fine for 100 unique actors. Terrible for 50,000 NPCs.

### UE5: Mass Entity — True ECS (their answer for crowds)

Mass Entity is UE5's data-oriented simulation layer. The key concept is **archetypes**:

```cpp
// Entities with identical fragment composition → same archetype
// All instances of FMassTransformFragment for an archetype sit contiguously in memory (SoA)
struct FMassArchetypeCompositionDescriptor {
    FMassFragmentBitSet        Fragments;   // What data components exist
    FMassTagBitSet             Tags;        // Boolean flags (no data)
    FMassSharedFragmentBitSet  SharedFragments; // Data shared across all in archetype
};

// Entity data is in 128KB chunks, SoA layout within chunk:
struct FMassRawEntityInChunkData {
    uint8* const ChunkRawMemory = nullptr;  // Direct pointer into SoA chunk
    const int32  IndexWithinChunk = INDEX_NONE;
};
```

**Processors** are "systems" — they declare fragment requirements and run as parallel TaskGraph tasks:
```cpp
struct FMassProcessorExecutionOrder {
    FName ExecuteInGroup;
    TArray<FName> ExecuteBefore;  // Dependency ordering
    TArray<FName> ExecuteAfter;
};
```

Even query access is cache-optimized — requirements are sorted the same way as archetype fragment configs so memory access is sequential, not random.

**Result**: UE5's City Sample runs 50,000+ NPCs on Mass Entity. The traditional Actor model tops out around 10,000–100,000.

### CryEngine: IEntity/IEntityComponent

CryEngine's entity system is component-based but OOP (not ECS). Components are registered via GUID + Schematyc reflection:

```cpp
enum class EEntityComponentFlags : uint32 {
    Singleton   = BIT(0),   // Only one per entity
    Transform   = BIT(2),   // Has transform
    ServerOnly  = BIT(12),  // Dedicated server only
    ClientOnly  = BIT(13),
};

// Component dependencies:
struct SEntityComponentRequirements {
    enum class EType : uint32 {
        Incompatibility,  // Can't coexist
        SoftDependency,   // Must init before
        HardDependency    // Must exist and init before
    };
};
```

### What SolEngine Should Do

- **Keep the Node scene graph** for unique gameplay objects (player, camera, important NPCs).
- **Add an ECS layer** (flecs or EnTT — EnTT is already a dependency) for high-count simulation objects: particles, projectiles, crowd agents, foliage interactions.
- **Hybrid model** is exactly what UE5 does. The scene graph and ECS coexist.

---

## 2. World / Level Streaming

### The Problem

One blocking scene swap (`flush_pending()` waits for renderer idle) means a travel time while the screen freezes. For open worlds this is unacceptable.

### UE5: World Partition (UE5.0+)

World Partition replaces sublevel workflows entirely. The world is stored as one giant map; actors are automatically partitioned into streaming cells at save time.

```
UWorldPartition
  └── UWorldPartitionRuntimeHash     (spatial cell index)
  └── UWorldPartitionStreamingPolicy (decides what to load/unload)
  └── UDataLayerManager              (logical grouping: quests, events)

Cell state machine:
  Unloaded → Loading (async) → Loaded → Activated (in-game)
             ↓
  Always-loaded actors bypass this (GameMode, PlayerStart)
```

**Data Layers** allow grouping actors independently of spatial cells. A "DLC_Quest_3" layer only loads when that quest activates, regardless of where the player is standing.

Old **`ULevelStreaming`** still works for hand-crafted sublevel arrangements and is simpler to implement first.

### CryEngine: Layer + Zone Streaming

CryEngine assigns every entity a `sLayerName` at edit time. When a layer is deactivated, all its entities despawn. **Streaming zones** (3D volumes) trigger layer load/unload as the camera enters/exits them. Coarser than UE5's cell system but simpler.

Both engines also implement **whole-object LOD**: beyond a radius, an NPC switches from full behavior trees to cheap point simulation, or disappears entirely.

### What SolEngine Should Do

1. **Background asset loading** — the prerequisite for everything else. Load scenes async on a worker thread; swap only when ready.
2. **Scene streaming zones** — trigger child scene loads/unloads based on player distance.
3. **Object LOD system** — disable expensive per-node behavior beyond a configurable radius.

---

## 3. Job System / Task Graph / Parallelism

### The Problem

SolEngine's main loop is entirely sequential: input → scene update → physics → render. All CPU cores except one are idle almost every frame. Jolt internally uses its thread pool but that's the only concurrency.

### UE5: TaskGraph

The TaskGraph is UE5's fundamental concurrency primitive. Every named thread (Game, Render, RHI, Audio) and every worker is a TaskGraph consumer:

```cpp
namespace ENamedThreads {
    enum Type {
        RHIThread,              // Dedicated GPU command submission
        AudioThread,            // Audio processing thread
        GameThread,             // Main update
        ActualRenderingThread,  // Render commands
        AnyThread = 0xff,       // Worker pool thread
        // Priority flags:
        HighThreadPriority     = 0x400,
        BackgroundThreadPriority = 0x800,
    };
}

// Every task is an atomic counter:
// When NumberOfPrerequistitesOutstanding hits 0, the task auto-fires.
class FBaseGraphTask {
    FThreadSafeCounter NumberOfPrerequistitesOutstanding;
    // Tasks ≤ 256 bytes use lock-free pool allocator
    enum { SMALL_TASK_SIZE = 256 };
};

// Returns a handle (future-like):
typedef TRefCountPtr<FGraphEvent> FGraphEventRef;
typedef TArray<FGraphEventRef, TInlineAllocator<4>> FGraphEventArray;
```

**How subsystems parallelize**:
- **Physics**: `FPhysicsScene::StartFrame()` kicks a TaskGraph task; `EndFrame()` waits for it. Physics runs entirely off the GameThread.
- **Animation**: `USkeletalMeshComponent::TickPose()` spawns a parallel evaluation task; result is applied back on GameThread at `FinalizeAnimationUpdate()`.
- **AI**: Behavior Tree evaluation can run on worker threads; EQS queries are async tasks.
- **Audio**: Dedicated named `AudioThread` with SPSC/MPSC queues.
- **Mass Processors**: Each processor dispatches as `FGraphEventRef DispatchProcessorTasks(...)` — takes prerequisites, returns event. Full dependency-based parallelism.

**UE5.1+ simplified API**:
```cpp
auto Task = UE::Tasks::Launch(TEXT("MyTask"), []{ /* work */ }, Prerequisites);
Task.Wait(); // or chain as prerequisite to next task
```

### CryEngine: CJobManager

CryEngine uses work-stealing queues:
- Each worker thread has a local deque; idle threads steal from other threads' tails.
- Jobs declare typed functors with parameters.
- Explicit separation: "blocking" jobs (large work items) vs "fiber-based" jobs (ultra-short, yield-capable).

### What SolEngine Should Do

1. **Implement a minimal job system** — even a simple thread pool with a `std::deque` of `std::function` tasks and atomic counters for dependencies would unlock massive wins.
2. **Move physics step off main thread** — Jolt already supports this; it just needs to be kicked on a thread and synced before rendering.
3. **Add a render thread** — submit draw commands from main thread to a command queue; render thread consumes and submits to Vulkan. This alone typically doubles GPU utilization.
4. **Async scene loading** — load scene JSON + assets on worker thread; publish to main thread when ready.

---

## 4. Memory Management

### UE5: FMalloc Abstraction + UObject GC

UE5 routes all allocations through a single global `FMalloc* GMalloc`:

```cpp
class FMalloc {
    virtual void*  Malloc(SIZE_T Count, uint32 Alignment) = 0;
    virtual void*  Realloc(void* Original, SIZE_T Count, uint32 Alignment) = 0;
    virtual void   Free(void* Original) = 0;
    virtual SIZE_T QuantizeSize(SIZE_T Count, uint32 Alignment); // Eliminate internal fragmentation
    virtual void   Trim(bool bTrimThreadCaches);
    virtual void   SetupTLSCachesOnCurrentThread(); // Per-thread TLS caches
};
```

**Key allocator implementations**:
- `FMallocBinned2`: 64 size classes, TLS caches per thread, OS-page-aligned pools. Default.
- `FMallocBinned3`: 64KB minimum OS allocation, console-optimized.
- `FMallocStomp`: Debug — guard pages before/after every allocation to catch overruns.
- `FMallocTBB`: Intel TBB scalable allocator.

**Per-frame stack allocations** (avoids heap):
```cpp
FMemMark Mark(FMemStack::Get());
TArray<FVector, TMemStackAllocator<>> Temp; // Freed automatically at scope exit
```

**UObject GC (mark-and-sweep)**:
1. **Mark phase**: Starting from GC roots, recursively marks all reachable `UObject`s. Each `UClass` has generated `AddReferencedObjects()` that enumerates `UPROPERTY` pointers.
2. **Sweep phase**: Iterates the global `GUObjectArray` (a contiguous index table), frees unmarked objects.
3. **Incremental GC** (UE5.3+): Spread across multiple frames to avoid GC stalls.
4. **Weak pointers**: `TWeakObjectPtr<T>` stores an index into `GUObjectArray`; valid check is an array lookup.

**Critical rule**: Non-`UPROPERTY` raw `UObject*` pointers are not scanned by GC. Dangling pointer = #1 UE crash cause.

### CryEngine: CryMemoryManager

- `CryMalloc/CryFree/CryRealloc` globally routed through the active allocator.
- Pool allocators for frequently allocated small objects (entities, physics contacts, audio events).
- Separate memory zones for render, physics, audio — useful for platform memory budgeting on consoles.
- No GC — raw reference counting on resources (`AddRef/Release`). Gameplay objects are manually lifetime-managed.

### What SolEngine Should Do

1. **Per-frame stack allocator** — a simple `FMemStack`-equivalent for temporary per-frame data. Zero-cost allocation, free at frame end.
2. **Object pools** for hot-path allocations — projectiles, particle instances, ECS chunk memory.
3. **Weak reference system** — `Node*` pointers dangle when nodes are destroyed; a handle system (index + generation counter) would prevent this.

---

## 5. Asset System / Content Pipeline

### UE5: UObject Serialization & Asset Registry

Every UE5 asset is a `UObject` inside a `UPackage`:

```
UPackage → FLinkerLoad
         → FExport[]  (objects defined in this package)
         → FImport[]  (references to other packages, resolved at load)
         → Bulk data  (mesh vertices, texture mips, etc.)
```

**Async loading flags**:
```cpp
enum ELoadFlags {
    LOAD_Async                = 0x00000001, // Use async loading path
    LOAD_DeferDependencyLoads = 0x00100000, // Don't load Blueprint deps yet
};
```

`TSoftObjectPtr<T>` stores a string path + lazily-loaded pointer — content doesn't load until explicitly requested.

**Derived Data Cache (DDC)**:
- Platform-specific cook artifacts (compressed textures, shader bytecode, navmesh data) stored keyed by `content hash + platform + cook version`.
- Local disk cache + network shared cache (for teams) + cloud cache.
- Avoids re-cooking unchanged assets. Distributed teams share DDC.

**IoStore** (UE5 runtime packaging):
- `.utoc`/`.ucas` containers with `FIoChunkId` addressing replacing legacy `.pak` files.
- Individual bulk data chunks (texture mips, mesh LODs) can be requested independently.
- `FIoDispatcher` coordinates async IO at the hardware level.

### CryEngine: CryPak Virtual Filesystem

`ICryPak` — all file access goes through `gEnv->pCryPak->FOpen()/FRead()/FClose()`, transparently handling loose files (dev) and packed `.pak` files (ship). Pak files support priorities for live hot-patching.

### What SolEngine Should Do

1. **Asset registry with GUIDs** — path-only asset references break on rename. GUID-based handles decouple reference from file location.
2. **Async asset loading** — kick a worker thread load, notify main thread on completion. Required for streaming.
3. **Asset dependency graph** — know what a scene depends on before loading it; preload dependencies.
4. **Hot reload** — scripts already have it; extend to meshes and textures.

---

## 6. Reflection / Metadata System

### Why Reflection is Foundational

Reflection is the **force multiplier** of a large engine. It enables:

| Uses Reflection | What it enables |
|---|---|
| Serialization | One save/load path handles all types automatically |
| Networking | Engine knows which fields to delta-compress and replicate |
| Blueprint/scripting | Dynamic dispatch without C++ knowledge |
| Editor property grids | Automatic UI generation for any type |
| GC | Knows which pointers to scan |
| Undo/redo | Diff reflected state before/after |

### UE5: UnrealHeaderTool (UHT)

UHT is a pre-compilation code generator. It parses `.h` files for macro annotations:

```cpp
UCLASS(BlueprintType)
class AMyActor : public AActor {
    GENERATED_BODY()  // ← UHT generates class registration + vtable

    UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite)
    float Health;     // ← Registered in UClass's FProperty chain

    UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
    void TakeDamage(float Amount); // ← RPC wrapper + Blueprint thunk generated
};
```

**What UHT generates**:
1. `StaticClass()` singleton factory for `UClass` lookup by name.
2. `GetLifetimeReplicatedProps()` stubs for replicated properties.
3. Blueprint thunks (`execFunctionName`) for every `UFUNCTION(BlueprintCallable)`.
4. `AddReferencedObjects()` for GC reference scanning.

**Type hierarchy optimization** — two implementations depending on build:
```cpp
#if UE_EDITOR
    USTRUCT_FAST_ISCHILDOF_IMPL = USTRUCT_ISCHILDOF_OUTERWALK  // Walk super chain — thread-safe for Blueprint reinstancing
#else
    USTRUCT_FAST_ISCHILDOF_IMPL = USTRUCT_ISCHILDOF_STRUCTARRAY // Pre-built array — 20-50ns vs 100ns+
#endif
```

### CryEngine: Schematyc

CryEngine uses runtime reflection (no code generation step). Properties are registered in `ReflectType()` virtual methods using type-safe descriptors keyed by `CryGUID`. Powers Schematyc's node-based visual scripting (their Blueprint equivalent).

### What SolEngine Should Do

SolEngine has `reflect.h` — expand it:

1. **Type-safe property descriptors** — each engine type declares its properties with names, types, and metadata flags.
2. **Serialization from reflection** — scene JSON serialization driven by reflection instead of hand-coded per-type `node_to_json`.
3. **Editor property grid from reflection** — inspector auto-generates UI from declared properties; adding a field in C++ automatically appears in editor.
4. **Script bindings from reflection** — instead of manually writing every LuaBridge binding, reflection generates bindings automatically.

---

## 7. Gameplay Framework

### UE5: Structured Hierarchy

```
UWorld
  └── AGameMode (server only — rules, match flow, victory conditions)
      └── AGameState (replicated → all clients — global game state)
          └── APlayerState[] (per-player replicated — score, ping)
  └── APlayerController (server + owning client — input, camera, HUD)
      └── APawn / ACharacter (possessed entity in world)
          └── UCharacterMovementComponent
          └── USkeletalMeshComponent
```

Match state machine built-in:
```cpp
namespace MatchState {
    EnteringMap → WaitingToStart → InProgress → WaitingPostMatch → LeavingMap
                                    ↑
                              Your custom states go here
}
```

**GameplayAbilitySystem (GAS)**:
- `UAbilitySystemComponent` — attaches to any actor; manages active abilities, attribute sets, effects.
- `FGameplayAttribute` — a float (Health, Mana, Speed) stored in `UAttributeSet`, auto-replicated.
- `UGameplayEffect` — data asset applying modifiers. Can be instant, duration, or infinite.
- `FGameplayTag` — hierarchical string tags (`Combat.Attack.Melee`) used as conditions everywhere.
- **Built-in client prediction**: client predicts ability fires; server confirms or rolls back.

**Enhanced Input**:
- `UInputAction` — abstract input (Jump, Fire, Look) independent of hardware.
- `UInputMappingContext` — binds hardware to actions, with priority stacking and modifiers.
- Trigger types: Pressed, Released, Held, Chord (multi-key), Pulse (repeated).
- Multiple contexts active simultaneously (e.g., vehicle context overrides on-foot context).

### CryEngine: IGameFramework

```
IGameFramework (gEnv->pGameFramework)
  └── IActorSystem    (spawn/manage actors)
  └── IItemSystem     (inventory, weapons)
  └── IVehicleSystem
  └── IPlayerProfileManager
  └── IGameRulesSystem
```

Interface-based — games implement `IGame` and `IGameRules` to override behavior. More flexible but less structured than UE5's class hierarchy.

### What SolEngine Should Do

1. **Input action abstraction** — decouple "Jump" from `KEY_SPACE`. An `InputAction` maps hardware to semantic intent; context stacks override mappings.
2. **Game state object** — a scene-level data object for match state, player data, game rules.
3. **Attribute system** — data-driven floats (health, speed, damage) with modifiers. Far more powerful than hardcoding values in Lua.

---

## 8. Networking / Replication

### UE5: NetDriver Architecture

```
UNetDriver
  └── TArray<UNetConnection*> ClientConnections
  └── UNetConnection* ServerConnection
      └── TArray<UChannel*> OpenChannels
          ├── UControlChannel   (connection state, map changes)
          ├── UVoiceChannel
          └── UActorChannel[]   (one per replicated actor per connection)
```

**Property Replication**:
1. Every `UPROPERTY(Replicated)` is registered in `GetLifetimeReplicatedProps()`.
2. Each frame, `UActorChannel::ReplicateActor()` serializes replicated properties.
3. Delta compression: engine keeps a "shadow state" per actor per connection; only changed properties are sent.
4. `RepNotify`: `UPROPERTY(ReplicatedUsing=OnRep_Health)` fires a callback on the client when value changes.

**RPCs**:
- `UFUNCTION(Server, Reliable)` — client calls → server executes.
- `UFUNCTION(Client, Unreliable)` — server calls → owning client executes.
- `UFUNCTION(NetMulticast, Reliable)` — server calls → all clients execute.

**Actor Ownership**: `Owner` pointer determines RPC routing. `PlayerController → Pawn → Weapons` is an ownership chain; Server RPCs can only be called by the owning connection.

**Iris** (UE5.1+, opt-in rewrite):
- `FReplicationGraph` pre-filters which actors are relevant per connection via spatial hierarchy — eliminates O(actors × connections) relevancy check.
- Batch serialization: processes all dirty actors in a cache-friendly pass rather than one `ReplicateActor()` call per actor.

**Network-quantized types**:
```cpp
FVector_NetQuantize100 Location; // 1cm precision, smaller packets
```
This level of optimization is pervasive — every replicated field is carefully sized.

### CryEngine: Aspect-Based Replication

- Entity state divided into "aspects" (physics, animation, inventory) each with own sync priority and compression.
- Snapshot-based for high-frequency data (position/velocity); delta-per-property for low-frequency (health, inventory).
- `ISerialize` archives handle both save/load and network serialization with identical code paths.

### What SolEngine Should Do

Networking is a large addition. Prerequisites before starting:
1. **Reflection** (for auto-replication of declared properties)
2. **Entity handles** (stable IDs to reference objects across machines)
3. **Deterministic physics** (or accept client prediction + rollback)

First step when ready: integrate a library like **ENet** or **GameNetworkingSockets** for transport, then build property replication on top of the reflection system.

---

## 9. AI Systems

### UE5: Navigation System

Tile-based navmesh (Recast/Detour). Each tile is rebuilt independently when geometry changes:

```cpp
class UNavigationSystemBase : public UObject {
    // World Partition integration — nav tiles stream with geometry:
    virtual void AddNavigationDataChunk(ANavigationDataChunkActor& DataChunkActor) {}
    virtual void RemoveNavigationDataChunk(ANavigationDataChunkActor& DataChunkActor) {}
};

// Nav lock prevents mid-frame rebuilds:
class FNavigationLockContext {
    FNavigationLockContext(ENavigationLockReason::Type Reason, bool bApplyLock = true);
    ~FNavigationLockContext(); // RAII unlock
};
```

**Nav Invokers**: For open worlds, only tiles within a `UNavInvokerComponent` radius are built — prevents generating navmesh for the entire map.

**Multiple agent support**: Different navmesh instances per agent capsule size. A 40cm NPC queries a different mesh than a 200cm vehicle.

**Behavior Trees**:
- Composites: Selector (try in order), Sequence (all must succeed), SimpleParallel.
- Decorators: Conditions — abort subtree if condition fails (`EBTFlowAbortMode`).
- Services: Background tickers on composites — update target every N seconds.
- Blackboard: Shared key-value store for the BT (`FBlackboardKeySelector`).

**EQS (Environment Query System)**: Generates candidate locations, scores them (distance, visibility, dot product to threat), returns best. Used for tactical positioning, cover finding.

**StateTree** (UE5.2+):
- Hierarchical state machine — lighter than BT for simple state logic.
- Evaluators, conditions, and tasks are plain structs, not UObjects — cache-friendly.
- Can run on worker threads. Designed for Mass Entity crowd agents.

### CryEngine: MNM Navigation + BST

**Modular Navigation Mesh (MNM)** — tile-based with hierarchical pathfinding (tile-level then within-tile).

**Behavior Selection Trees (BST)** use "signal receivers" and "goalpipes" (priority queues of behavioral goals). Less visual than BTs but powerful for scripted AI sequences.

### What SolEngine Should Do

1. **Navmesh generation** (Recast is MIT-licensed, used by UE5 and many others) — drop-in tile-based pathfinding.
2. **Simple behavior tree** — composites + tasks + blackboard. The tree structure is the same regardless of the backing pathfinder.
3. **Perception system** — sight/sound cone queries feeding the blackboard.

---

## 10. Animation System

### UE5: Parallel Anim Evaluation

```cpp
// Animation runs on a worker thread:
struct FParallelEvaluationData {
    FBlendedHeapCurve& OutCurve;    // Animation curve values
    FCompactPose&      OutPose;     // Bone transforms (compact = only needed bones)
    UE::Anim::FHeapAttributeContainer& OutAttributes; // Custom float/vector channels
};
```

`FCompactPose` stores only bones referenced by active nodes — a running character with no upper-body blend only evaluates lower-body bones.

**Key systems**:
- `UAnimMontage`: Override partial poses (upper/lower body slots), blend in/out.
- `FAnimNotify` / `FAnimNotifyState`: Fire events at specific frames — VFX, sounds, gameplay triggers.
- **Control Rig**: Node-based IK and procedural animation. Foot IK, look-at, hand placement on surfaces. Runs *after* AnimGraph.
- **Animation Sharing**: Share animation instances across hundreds of identical NPCs.

### CryEngine: CryAnimation + Mannequin

- 6 LOD levels per character geometry.
- `CA_SkipSkelRecreation` flag for hot-reload performance.
- Characters skip evaluation if `CS_FLAG_RENDER_NODE_VISIBLE` is not set — no animation cost for off-screen characters.

**Mannequin** — action-based controller on top of CryAnimation:
- Actions are typed objects (sprint, aim, jump) with scopes (full-body, upper-body, face).
- `FragmentID + TagState` pairs define what plays for a given context.
- Procedural callbacks inject IK modifications.

### What SolEngine Should Do

1. **Skeletal mesh + bone hierarchy** — prerequisite for animation.
2. **Blend tree** — blend between animation clips by weight.
3. **AnimNotify** — frame-accurate event callbacks from animation to game code.
4. **Evaluate off-thread** — kick anim evaluation as a job, sync before render.

---

## 11. Audio

### UE5: MetaSound

MetaSound is a **programmable DSP graph** (UE5.0+):
- Nodes are strongly-typed DSP processors (`FMetaSoundOscillatorNode`, `FMetaSoundDelayNode`).
- Graphs compile to `FMetaSoundFrontendDocument` at cook time.
- Runtime: `FMetasoundGeneratorHandle` drives per-voice sample generation on `AudioThread`.
- A single MetaSound asset defines both sound logic *and* spatialization parameters.
- Supports fully procedural audio — engine pitch tracking vehicle RPM via a curve, not a sample.

**Sound Cues** (simpler legacy path): Node-based mixer (random, switch, modulator) wrapping `USoundWave` assets. More accessible.

### CryEngine: CryAudio (ATL)

**Audio Translation Layer** — all game audio code calls abstract IDs:
```cpp
// Hashed control IDs (compile-time CRC32):
static constexpr ControlId LoseFocusTriggerId = StringToId("lose_focus");
static constexpr ControlId MuteAllTriggerId   = StringToId("mute_all");

// Game code:
gEnv->pAudioSystem->ExecuteTrigger("gun_fire");
// ↑ Mapped by ATL to current middleware (Wwise, FMOD, SDL) — zero game code changes to swap
```

**Key insight**: The ATL abstraction means audio middleware can be swapped (dev uses SDL, ship uses Wwise) without touching any game code.

### What SolEngine Should Do

1. **SoLoud or miniaudio integration** — both are small, permissive-license C libraries. SoLoud has positioning, mixing, and streaming support.
2. **Audio abstraction layer** — reference sounds by string IDs, not file paths. ATL-style mapping from game events to audio assets.
3. **Spatial audio** — distance attenuation, occlusion queries using the physics raycaster.

---

## 12. Profiling / Debugging Infrastructure

### The Problem

As systems grow, "it's slow" becomes undebuggable without instrumentation. Retrofitting profiling hooks into a complex engine is extremely painful — build them in early.

### UE5: Stat System

Stats are declared inline across all engine code:
```cpp
DECLARE_STATS_GROUP(TEXT("Task Graph Tasks"), STATGROUP_TaskGraphTasks, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ParallelFor"),     STAT_ParallelFor,     STATGROUP_TaskGraphTasks, CORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ParallelForTask"), STAT_ParallelForTask, STATGROUP_TaskGraphTasks, CORE_API);

// Mass Entity:
DECLARE_STATS_GROUP(TEXT("Mass"), STATGROUP_Mass, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Mass Total Frame Time"), STAT_Mass_Total, STATGROUP_Mass, MASSENTITY_API);
```

**Stat types**:
- `DECLARE_CYCLE_STAT`: Wall-clock scope timer, accumulated per frame.
- `DECLARE_DWORD_COUNTER_STAT`: Integer counter (entity count, draw calls).
- `DECLARE_MEMORY_STAT`: Allocation tracking per category.
- `DECLARE_FLOAT_ACCUMULATOR_STAT`: Summable float per frame.

**Unreal Insights** (separate profiling tool, trace socket):
- **CPU Trace**: Per-thread task/scope timeline. Sees TaskGraph tasks, named threads, AI ticks.
- **GPU Trace**: GPU timeline alongside CPU.
- **Memory Trace**: Allocation callstacks, heap snapshots, GC events.
- **Net Trace**: Packet timeline, bandwidth per actor channel.
- **Mass Debug**: Archetype composition, processor timings, entity counts.

**GC tracing** (non-shipping only):
```cpp
#define ENABLE_GC_HISTORY (!UE_BUILD_SHIPPING)
// Stores history of GC runs: what was collected, what held the last reference
```

### CryEngine: CryPerfHUD + CVar System

- **CryPerfHUD**: Real-time overlay — entity counts, physics ms, AI ms, render thread ms.
- **CVar system**: Every subsystem exposes live-tunable console variables (`e_AIUpdateRadius`, `p_gravity_z`).
- **IStatoscope**: Timeline profiler writing CSV for offline analysis.
- **`IRenderAuxGeom`**: AI agents draw their nav intent, paths, attention targets, physics overlaps directly in viewport. Enabled with `ai_debugDraw 1`.

### What SolEngine Should Do

1. **Scope timer macros** (`SCOPE_STAT("Physics")`) — cheap rdtsc/steady_clock measurement, accumulates per frame.
2. **Debug overlay** — ImGui already exists; add a "stats" panel showing frame time breakdown by system.
3. **Console variable system** — every tunable constant (physics substeps, culling distances, LOD ranges) exposed as a live-tweakable CVar.
4. **Debug draw layer** — `debug_draw_line`, `debug_draw_sphere`, `debug_draw_text` routed to a pass that renders after scene. Invaluable for AI, physics, and camera debugging.

---

## Cross-System Dependencies: The Dependency Graph

Understanding what needs to be built before what:

```
Reflection / Metadata
  ├── Serialization (auto-driven by declared properties)
  ├── Editor property grids (auto-generated from reflection)
  ├── Script bindings (auto-generated, not hand-written)
  └── Networking (knows what to replicate by declaration)

Job System
  ├── Async Asset Loading (kicks worker thread loads)
  ├── Physics off-thread (Jolt already supports; just needs the kick)
  ├── Animation off-thread (eval as job, sync before render)
  └── AI (behavior trees as async tasks)

Asset Registry + Async Loading
  ├── World Streaming (need async loads to stream cells)
  └── Hot Reload (watch file changes, reload in background)

ECS Layer
  ├── Crowd simulation (1000+ agents)
  ├── Projectile systems (10000+ bullets)
  └── Integrates with Job System (processors as parallel tasks)

Memory Pools
  ├── ECS chunk allocations (fixed-size 128KB slabs)
  ├── Per-frame stack (temporary data, reset each frame)
  └── Job system task objects (small, lock-free pool)
```

---

## Priority Roadmap for SolEngine

Based on the research above, here is the recommended build order. Each tier unlocks the next:

### Tier 0 — Infrastructure (Build These First)
These are invisible to users but everything else stands on them.

| System | Why Now | Estimated Impact |
|---|---|---|
| **Job system** (thread pool + dependency graph) | Every other system needs parallelism | Unlocks multi-core utilization |
| **Stat / profiling hooks** | Build before systems get complex | Makes all subsequent work debuggable |
| **CVar system** | All tunable constants as live variables | Massively speeds up iteration |
| **Debug draw layer** | Cheap to add, invaluable for all future systems | Visual debugging for everything |

### Tier 1 — Engine Core
| System | Why Now | Builds On |
|---|---|---|
| **Reflection expansion** | Drives editor, scripting, serialization | Nothing — this IS the foundation |
| **Entity handle system** (index + generation) | Prevent dangling `Node*` pointers | — |
| **Asset registry + GUIDs** | Decouple asset identity from file path | — |
| **Async asset loading** | Required for streaming | Job system |

### Tier 2 — Simulation Scale
| System | Why Now | Builds On |
|---|---|---|
| **ECS layer** (flecs or EnTT expansion) | Handle 10,000+ simulation objects | Job system |
| **Physics off-thread** | Jolt supports it; free performance | Job system |
| **Memory pools** | ECS chunks, per-frame stack | — |
| **World streaming** | Open worlds | Async loading |

### Tier 3 — Gameplay Systems
| System | Builds On |
|---|---|
| **Audio** (SoLoud/miniaudio) | Job system (audio thread), asset registry |
| **Navigation** (Recast) | Physics world (walkability), job system (tile builds) |
| **Animation system** | Job system (parallel eval), asset pipeline |
| **Input abstraction** (action layer) | — |
| **Gameplay attribute system** | Reflection |

### Tier 4 — Advanced / Multiplayer
| System | Builds On |
|---|---|
| **Networking** | Reflection, entity handles, deterministic physics |
| **AI behavior trees** | Navigation, job system |
| **World Partition** | World streaming, asset registry |

---

## Key Insight: Why Reflection Comes First

In UE5, everything ultimately depends on the reflection system:
- GC scans `UPROPERTY` chains.
- Networking serializes `Replicated` properties.
- Blueprint calls `UFUNCTION` thunks.
- The editor generates UI from `EditAnywhere` properties.
- Serialization saves/loads `UPROPERTY` values.

SolEngine's current `reflect.h` is a seed. Expanding it into a real property descriptor system — types with named fields, metadata flags, type-safe setters/getters — would make the entire subsequent roadmap far cheaper to implement.

Without reflection, every system is hand-coded: every new type needs its own JSON serializer, its own editor widget, its own Lua binding. With reflection, each new type declared correctly gets all of that for free.

---

*Research grounded in UE5 source mirrors (`chenyong2github/UnrealEngine`, `emacser0/UnrealRuntimeSources`, `windystrife/UnrealEngine_NVIDIAGameWorks`) and CryEngine headers (`MibuWolf/CryGame`). Current SolEngine analysis from source at `C:\Users\mathi\Desktop\SolEngine`.*
