# Firedancer Plugin System

## Goal

Add support for third-party plugins to Firedancer. A plugin is a self-contained module (a submodule or folder under `plugin/`) that defines a topology—one or more tiles and the links between them—which is merged into the main Firedancer topology at startup. Plugin tiles run alongside core tiles inside the same process, sharing the same IPC infrastructure.

Plugin support targets the full Firedancer client only. Frankendancer (`fdctl`) will not run plugins. The refactor may touch shared code (e.g., `fd_topob_*`, `fd_topo_t`) and must not break Frankendancer.

Plugins use the same imperative `fd_topob_*` API that core firedancer uses to construct its topology. A lightweight manifest file declares which links are exposed across plugin boundaries and their versions, enabling compile-time and startup-time compatibility checks without adding a new topology description layer.

## Design Principles

- **Minimal boilerplate.** A read-only plugin that taps a few links should be achievable in under 200 lines of C.

- **Composability.** A plugin declares what it needs; the build system and runtime wire it up. Plugins may depend on links produced by other plugins.

- **Safety by default.** Plugins are sandboxed with seccomp. A plugin crash does not take down the validator unless it is explicitly marked as critical.

## Non-Goals

- **Hot-reload.** Plugins cannot be loaded, unloaded, or updated without rebuilding and restarting the validator. All plugin code is statically linked at build time. Dynamic plugin loading is explicitly out of scope.

## Concrete Next Steps

The work breaks down into an MVP for C plugins. Ordered by dependency:

| # | Task | Depends On |
|---|------|------------|
| 1 | ✅ done — Implement `connect` / `connect_many` / `publish` / `subscribe` API with version and optional params | — |
| 2 | ✅ done — Refactor `fd_topob_tile`: remove `cpu_idx`, add `kind_id`, replace positional booleans with `ulong flags` bitmask (`FD_TOPOB_TILE_FLOATING`, `FD_TOPOB_TILE_CRITICAL`, `FD_TOPOB_TILE_USES_ID_KEYSWITCH`, etc.); defer CPU assignment to `fd_topob_auto_layout()` | — |
| 3 | ✅ done — Add `namespace` field to `fd_topo_t`; implement two-pass dispatch with namespace prefix enforcement | 1, 2 |
| 4 | ✅ done — Core link manifest (`manifest.toml`) for plugin-facing links | — |
| 5 | ✅ done — Extend build system to discover & build plugins; `Plugin.mk` handles symbol exposure and registration | — |
| 6 | ✅ done — Generate `fd_plugin_registry.h` for `TILES[]` and plugin topo function table | 5 |
| 7 | ✅ done — Unified dynamic config: copy full TOML config pod into `topo->config`; implement `fd_topo_cfg.h` wrappers; change `scratch_footprint` signature to `(topo, tile)`. Core tile migration to read from `topo->config` is future work. | — |
| 8 | ✅ done — Plugin config: `[plugins.*]` namespace available in `topo->config`; plugin tiles read directly via `fd_pod_query_*` | 3, 7 |
| 9 | ✅ done — Build-time manifest validation (namespace collisions, version checks) | 4, 5 |
| 10 | 🔲 not started — Startup-time manifest validation (code-declared version cross-checked against manifest; link name, MTU match) | 1, 3, 4 |

---

## Phase 1: Imperative Plugin Topology

### 1a. Connection API

The `fd_topob` builder provides four high-level connection APIs that replace manual `fd_topob_link()` + `fd_topob_tile_out()` + `fd_topob_tile_in()` sequences. `connect` and `connect_many` are **idempotent** and create links with both a producer and consumer. `publish` creates a link with a producer but no consumer — used when a tile exposes a link for others (plugins or heterogeneous core consumers) to subscribe to later. `subscribe` is used when connecting to a link whose parameters aren't known (e.g., a plugin consuming a core link) — it is **deferred** and resolved by the framework across two passes over each plugin's topology function.

All link-creating functions (`connect`, `connect_many`, `publish`) accept a `version` string (semver, e.g. `"3.0.0"`). For core-internal links that are not exposed to plugins, pass `NULL`. For plugin-facing links, the version is recorded in the topology and validated against manifests at build time and startup time. `subscribe` also takes a version — the consumer's expected version — which is checked against the producer's declared version.

#### `connect` / `connect_many` — Full Connections

Create link(s) and wire both producer and consumer in one call. Idempotent on the link name — if the link already exists, the producer/depth/mtu/burst/link_wksp parameters are ignored and only the consumer side is wired.

```c
/* 1:1 — One producer instance to one consumer instance.
   First call: creates 1 link, wires producer:0 out, consumer:0 in.
   Subsequent calls (link exists): just wires consumer:0 in.
   version may be NULL for core-internal links. */
void
fd_topob_connect( fd_topo_t * topo,
    char const * producer,      char const * consumer,
    char const * link_name,     char const * link_wksp,
    char const * version,
    ulong depth, ulong mtu, ulong burst,
    int reliable, int polled );

/* N:M — N producers to M consumers (full mesh).
   First call: creates N link instances, wires each producer:i out
   on link:i, each consumer:j in on all link:0..N-1.
   Subsequent calls (links exist): just wires consumer:0..M-1 in.
   Subsumes 1:N (producer_cnt=1) and N:1 (consumer_cnt=1).
   version may be NULL for core-internal links. */
void
fd_topob_connect_many( fd_topo_t * topo,
    char const * producer,      ulong producer_cnt,
    char const * consumer,      ulong consumer_cnt,
    char const * link_name,     char const * link_wksp,
    char const * version,
    ulong depth, ulong mtu, ulong burst,
    int reliable, int polled );
```

Both functions default to `"metric_in"` as the fseq workspace (consistent with every connection in the current codebase). `fd_topob_connect(...)` is equivalent to `fd_topob_connect_many(... , 1, ... , 1, ...)`.

#### `publish` — Producer-Only Link Creation

Create link(s) with a producer but no consumer. Used when a tile exposes a link that other tiles (plugins or heterogeneous core consumers) will `subscribe` to or `connect` to later. Idempotent — no-ops if the link already exists.

```c
/* Create 1 link instance per producer, wire each producer:i out
   on link:i, but do not wire any consumer.
   version may be NULL for core-internal links. */
void
fd_topob_publish( fd_topo_t * topo,
    char const * producer,      ulong producer_cnt,
    char const * link_name,     char const * link_wksp,
    char const * version,
    ulong depth, ulong mtu, ulong burst );
```

#### `subscribe` — Consumer-Only Connection

For consumers that don't know a link's parameters (depth, mtu, burst) — typically plugins connecting to core links or other plugins' links. The link must already exist by the time the subscribe is resolved (unless `optional` is set). The `version` is the consumer's expected version and is checked against the producer's declared version.

```c
/* Wire consumer(s) to an existing link without knowing its
   creation parameters.  Behavior depends on the framework's
   current pass (see Two-Pass Dispatch):
     Pass 1: silently skips if the link doesn't exist yet.
     Pass 2: FD_LOG_ERR if the link still doesn't exist,
             UNLESS optional (FD_TOPOB_OPTIONAL) — then
             silently skip on both passes.
   version is the consumer's expected semver — must be compatible
   with the producer's version (same major, minor <= producer).
   An optional subscribe allows the consumer to gracefully handle
   the non-existence of the link (e.g., run with reduced
   functionality). */
void
fd_topob_subscribe( fd_topo_t * topo,
    char const * consumer,      ulong consumer_cnt,
    char const * link_name,     char const * version,
    int reliable, int polled,   int optional );
```

#### Idempotency and Two-Pass Resolution

`connect`, `connect_many`, and `publish` are fully idempotent — if the link already exists, the creation parameters are ignored and only the consumer side (if any) is wired. All `fd_topob_*` creation calls (`wksp`, `tile`, `obj`) are similarly idempotent.

`subscribe` is **deferred**: it silently skips on the first pass if the link doesn't exist, then succeeds (or fails hard) on the second pass. If `optional` (`FD_TOPOB_OPTIONAL`) is set, subscribe silently skips on both passes if the link doesn't exist — the consumer handles the absence gracefully (e.g., by checking `tile->in_cnt` at runtime and running with reduced functionality). This allows plugins to mix tile creation, internal connections, and cross-boundary subscriptions in a single function:

```c
/* Core publishes the link (no consumer specified here) */
fd_topob_publish( topo, "replay", 1,
    "replay_out", "replay_out", "3.0.0",
    65536UL, sizeof(fd_replay_message_t), 1UL );

/* Core consumers connect (idempotent — link already exists, just wires consumer) */
fd_topob_connect( topo, "replay", "repair",
    "replay_out", "replay_out", "3.0.0",
    65536UL, sizeof(fd_replay_message_t), 1UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );

/* Plugin uses subscribe — doesn't need to know replay_out's params.
   Pass 1: link may not exist yet → silently skipped.
   Pass 2: link exists → consumer wired, version checked. */
fd_topob_subscribe( topo, "myplug", tile_cnt,
    "replay_out", "3.0.0", FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED,
    FD_TOPOB_REQUIRED );

/* Optional subscribe — consumer runs fine without this link.
   If the link doesn't exist on pass 2, silently skipped. */
fd_topob_subscribe( topo, "myplug", tile_cnt,
    "extra_data", "1.0.0", FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED,
    FD_TOPOB_OPTIONAL );
```

Note that inter-plugin dependencies are only satisfied **across passes**, not within a single pass. If plugin A creates a link and plugin B subscribes to it, B's subscribe may skip on pass 1 (if A hasn't run yet), but will resolve on pass 2 since A will have created the link during pass 1. Circular subscriptions (A subscribes to B's link and B subscribes to A's link) resolve correctly for wiring purposes, but the link graph must still be a DAG — `fd_topob_finish()` validates this to prevent circular backpressure.

#### Before / After

The current three-step pattern:

```c
/* N verify tiles → 1 dedup tile (before — 3N calls): */
FOR(verify_cnt) fd_topob_link(     topo, "verify_dedup", "verify_dedup", depth, mtu, 1UL );
FOR(verify_cnt) fd_topob_tile_out( topo, "verify", i,    "verify_dedup", i );
FOR(verify_cnt) fd_topob_tile_in(  topo, "dedup",  0UL,  "metric_in",
                                   "verify_dedup", i, FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
```

Becomes:

```c
/* N verify tiles → 1 dedup tile (after — 1 call): */
fd_topob_connect_many( topo,
    "verify", verify_cnt, "dedup", 1,
    "verify_dedup", "verify_dedup", NULL,
    depth, mtu, 1UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
```

Heterogeneous fan-out — `publish` creates the link, then `connect` adds consumers (idempotent — link already exists, just wires consumer side):

```c
/* replay_out: 1 producer, many heterogeneous consumers.
   publish creates the link; connect calls add consumers. */
fd_topob_publish( topo, "replay", 1,
    "replay_out", "replay_out", "3.0.0",
    65536UL, sizeof(fd_replay_message_t), 1UL );
fd_topob_connect( topo, "replay", "repair",
    "replay_out", "replay_out", "3.0.0",
    65536UL, sizeof(fd_replay_message_t), 1UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
fd_topob_connect( topo, "replay", "tower",
    "replay_out", "replay_out", "3.0.0",
    65536UL, sizeof(fd_replay_message_t), 1UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
fd_topob_connect( topo, "replay", "gui",
    "replay_out", "replay_out", "3.0.0",
    65536UL, sizeof(fd_replay_message_t), 1UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
/* Plugins subscribe to the same link — resolved on pass 2 */
```

More examples from the core topology:

```c
/* 1:N — pack fans out to all execle tiles */
fd_topob_connect_many( topo,
    "pack", 1, "execle", execle_cnt,
    "pack_execle", "pack_execle", NULL,
    65536UL, USHORT_MAX, 1UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );

/* N:M — all net tiles to all quic tiles (full mesh) */
fd_topob_connect_many( topo,
    "net", net_cnt, "quic", quic_cnt,
    "net_quic", "net_quic", NULL,
    ingress_buf_sz, FD_NET_MTU, 1UL,
    FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED );

/* N shred tiles → multiple heterogeneous consumers.
   publish creates the links; connect_many adds each consumer group. */
fd_topob_publish( topo, "shred", shred_cnt,
    "shred_out", "shred_out", "1.0.0",
    shred_depth, sizeof(fd_shred_message_t), 3UL );
fd_topob_connect_many( topo,
    "shred", shred_cnt, "repair", 1,
    "shred_out", "shred_out", "1.0.0",
    shred_depth, sizeof(fd_shred_message_t), 3UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
fd_topob_connect_many( topo,
    "shred", shred_cnt, "tower", 1,
    "shred_out", "shred_out", "1.0.0",
    shred_depth, sizeof(fd_shred_message_t), 3UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
fd_topob_connect_many( topo,
    "shred", shred_cnt, "gui", 1,
    "shred_out", "shred_out", "1.0.0",
    shred_depth, sizeof(fd_shred_message_t), 3UL,
    FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );
```

For edge cases not covered by the convenience API — bidirectional sign protocol, non-zero starting kind_ids, per-consumer reliability differences — the raw `fd_topob_link()` / `fd_topob_tile_in()` / `fd_topob_tile_out()` primitives remain available.

### 1b. Plugin Topology Functions

Each plugin exports a single topology function. The framework calls it **twice** (see Two-Pass Dispatch). `connect` / `connect_many` / `publish` and all `fd_topob_*` creation calls are idempotent and no-op on the second pass. `subscribe` skips gracefully on pass 1 if the link doesn't exist (or on both passes if `FD_TOPOB_OPTIONAL`), then wires the consumer on pass 2.

**CPU assignment is not part of topology construction.** The `cpu_idx` parameter is removed from `fd_topob_tile` entirely — it does not appear in the signature. CPU-to-tile assignment is handled later by `fd_topob_auto_layout()`, which runs after all topology functions (core + plugins) have completed. This separation is cleaner even for core — it eliminates the fragile `tile_to_cpu[ topo->tile_cnt ]` indexing pattern and ensures auto-layout has a complete view of all tiles before making placement decisions. This change applies to core topology construction as well, not just plugins.

**Tile attributes replace boolean flags.** The old `fd_topob_tile` signature used a `cpu_idx` and positional booleans (`is_agave`, `uses_id_keyswitch`, `uses_av_keyswitch`). The refactored signature removes `cpu_idx`, adds a `kind_id` parameter, and replaces the positional booleans with a single `ulong flags` bitmask. The `is_agave` boolean becomes `FD_TOPOB_TILE_IS_AGAVE` (bit 4), and the keyswitch booleans become `FD_TOPOB_TILE_USES_ID_KEYSWITCH` (bit 6) and `FD_TOPOB_TILE_USES_AV_KEYSWITCH` (bit 7):

```c
fd_topo_tile_t *
fd_topob_tile( fd_topo_t *  topo,
               char const * tile_name,
               ulong        kind_id,
               char const * tile_wksp,
               char const * metrics_wksp,
               ulong        flags );

/* Flag constants */
#define FD_TOPOB_TILE_FLOATING        (1UL<<0) /* not pinned to a core             */
#define FD_TOPOB_TILE_STARTUP         (1UL<<1) /* runs only during startup         */
#define FD_TOPOB_TILE_POST_START      (1UL<<2) /* starts after main tiles          */
#define FD_TOPOB_TILE_CRITICAL        (1UL<<3) /* hyperthread twin bad             */
#define FD_TOPOB_TILE_IS_AGAVE        (1UL<<4) /* Agave/JVM tile                   */
#define FD_TOPOB_TILE_ALLOW_SHUTDOWN  (1UL<<5) /* tile can be shut down gracefully */
#define FD_TOPOB_TILE_USES_ID_KEYSWITCH (1UL<<6) /* uses identity keyswitch       */
#define FD_TOPOB_TILE_USES_AV_KEYSWITCH (1UL<<7) /* uses authority voter keyswitch */
```

Plugin tiles default to `flags=0` (non-critical, non-floating, no graceful shutdown). The call is idempotent, keyed on `(tile_name, kind_id)` — if a tile with the same name and `kind_id` already exists, the function returns the existing tile.

```c
/* fd_myplugin_topo.c */
#include "../../src/disco/topo/fd_topob.h"

void
fd_myplugin_topo( fd_topo_t * topo ) {
  ulong tile_cnt = fd_pod_query_ulong( topo->config,
      "plugins.myplugin.tile_count", 1UL );

  /* Create tiles — idempotent, no-ops on pass 2.
     flags=0 → default (non-critical, non-floating).
     fd_topob_auto_layout() assigns CPUs after all topology
     functions have run. */
  fd_topob_wksp( topo, "myplug" );
  for( ulong i=0UL; i<tile_cnt; i++ )
    fd_topob_tile( topo, "myplug", i, "myplug", "metric_in", 0 );

  /* Publish a link for other plugins or core tiles to subscribe to.
     Idempotent — no-ops on pass 2. */
  fd_topob_publish( topo, "myplug", tile_cnt,
      "myplug_out", "myplug_out", "1.0.0",
      128UL, 1024UL, 1UL );

  /* Subscribe to core's replay_out — required, fails on pass 2
     if the link doesn't exist. */
  fd_topob_subscribe( topo, "myplug", tile_cnt,
      "replay_out", "3.0.0", FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED,
      FD_TOPOB_REQUIRED );
}
```

The plugin topology function has full access to the `fd_topob_*` API — conditionals, loops, dynamic tile counts from config, shared objects via `fd_topob_tile_uses()`, etc. All creation calls are idempotent, so the function is safe to call twice.

### 1c. Plugin Namespace and Registration

Plugins do not need a dedicated registration struct. Each plugin exports a plain topology function (e.g., `void fd_myplugin_topo( fd_topo_t * topo )`). The build system discovers plugins via `Plugin.mk` declarations and generates a registry that pairs each plugin's namespace name with its function pointer.

**Namespace field on `fd_topo_t`.** `fd_topo_t` gains a `namespace` field:

```c
/* In fd_topo.h */
char namespace[ 7 ]; /* Plugin namespace; set by framework during
                         plugin dispatch.  "" (empty) during core
                         topology construction — namespace enforcement
                         is skipped when namespace[0] is '\0'. */
```

During plugin dispatch, the framework sets `topo->namespace` to the plugin's name before calling its topology function. The `fd_topob_*` functions use this field to enforce namespace prefixes — any tile or link created while `namespace` is set to a plugin name must have a name starting with that prefix. During core topology construction, `namespace` is `""` (empty string, zeroed by `fd_topob_new`) and namespace enforcement is not applied.

**Generated registry.** `Plugin.mk` handles symbol exposure. The build system generates a registry from each plugin's `PLUGIN_NAME` and topology function name (`PLUGIN_TOPO_FN`, defaulting to `fd_<PLUGIN_NAME>_topo`):

The `fd_plugin_topo_fn` typedef and `fd_plugin_entry_t` struct are declared in `fd_topob.h` (the core header). The generated header only instantiates the `PLUGIN_TOPOS[]` array:

```c
/* generated/fd_plugin_registry.h — do not edit */
extern void fd_myplugin_topo( fd_topo_t * topo );
extern void fd_slotlog_topo( fd_topo_t * topo );

fd_plugin_entry_t PLUGIN_TOPOS[] = {
  { "myplugin", fd_myplugin_topo },
  { "slotlog",  fd_slotlog_topo },
  { NULL, NULL },
};
```

Plugin authors never define a registration struct — they just export their topology function and their `Plugin.mk` declares `PLUGIN_NAME`.

### 1d. Two-Pass Dispatch

At the end of `fd_topo_initialize()`, after core topology construction but before `fd_topob_finish()`, the framework dispatches enabled plugins via `fd_topob_plugin_dispatch()`. This standalone function (in `fd_topob.c`) calls each enabled plugin's topology function **twice**:

```c
/* Core topology is already built using connect / connect_many / publish.
   topo->config already contains the full parsed TOML config pod
   (copied at the top of fd_topo_initialize via fd_memcpy).
   Now integrate plugins. */

/* CPU assignment happens before plugin dispatch.  auto_layout
   sees every core tile and assigns CPUs. */
fd_topob_auto_layout( topo, 0 );

/* Filter to only enabled plugins */
fd_plugin_entry_t enabled[ plugin_cnt+1 ];
ulong cnt = 0UL;
for( ulong i=0UL; PLUGIN_TOPOS[i].ns; i++ ) {
  char key[ 64 ];
  FD_TEST( fd_cstr_printf_check( key, sizeof(key), NULL,
      "plugins.%s.enabled", PLUGIN_TOPOS[i].ns ) );
  if( fd_pod_query_int( topo->config, key, 0 ) )
    enabled[ cnt++ ] = PLUGIN_TOPOS[i];
}
enabled[ cnt ].ns = NULL;
enabled[ cnt ].fn = NULL;
if( cnt ) fd_topob_plugin_dispatch( topo, enabled );

fd_topob_finish( topo, CALLBACKS );
```

`fd_topob_plugin_dispatch` internally performs two passes over the enabled plugin list. On each pass it sets `topo->namespace` (via `fd_cstr_ncpy( topo->namespace, ..., sizeof(topo->namespace) )`) to the plugin's name before calling its topology function, enforcing namespace prefixes. It controls pass behavior via `topo->skip_missing_links`: set to `1` for pass 1 (so `subscribe` silently skips missing links) and `0` for pass 2 (so `subscribe(FD_TOPOB_REQUIRED)` fails hard if the link still doesn't exist). After both passes, `topo->namespace` is cleared.

Plugin authors write a single function that freely mixes tile creation, internal connections (`connect` / `connect_many`), link publication (`publish`), and cross-boundary subscriptions (`subscribe`). The framework handles ordering automatically — after pass 1 every link exists, so all subscribes resolve on pass 2.

**Namespace prefix enforcement** is applied eagerly by `fd_topob_tile()` and `fd_topob_link()` at creation time — every tile name and link name created by a plugin must be prefixed with the plugin's namespace (from `topo->namespace`). E.g., plugin `"slotlog"` may only create tiles/links starting with `"sltlog"`. This enforcement is not applied during core topology construction (when `namespace` is `""`, i.e. `namespace[0]` is `'\0'`).

Note: version validation for `subscribe` is deferred to a future phase (`(void)version; /* TODO: version validation in Phase 3 */`).

**Runtime validation** in `fd_topob_finish()` catches:

- Plugin tile/link/workspace names colliding with core or other plugins
- Unresolved required subscriptions (link never created by anyone; optional subscriptions are silently skipped)
- Link graph is a DAG (no circular backpressure paths)
- Totals exceeding `FD_TOPO_MAX_*` limits
- Tile names > 6 chars, link names > 13 chars

---

## Phase 2: Plugin Folder Layout and Build Integration

### Folder Structure

```
plugin/
├── Plugin.mk                         # base makefile included by all plugins
├── myplugin/
│   ├── manifest.toml                   # link version manifest
│   ├── fd_myplugin_topo.c            # topology construction
│   ├── fd_myplugin_tile.c            # tile implementation
│   ├── fd_myplugin_tile.seccomppolicy # seccomp policy
│   ├── Local.mk                       # build rules (includes Plugin.mk)
│   └── generated/                     # build artifacts (gitignored)
│       └── fd_myplugin_tile_seccomp.h
```

### Build System Changes

1. **Discovery.** Glob `plugin/*/Local.mk` (via `find`). Manifest globbing is separate and only feeds the validation step.

2. **Codegen.** Seccomp header generation is handled by a separate global `make seccomp-policies` target that sweeps all `.seccomppolicy` files; it is not integrated per-plugin in `Plugin.mk`.

3. **Compile.** Include each plugin's `Local.mk`.

4. **Link.** Statically link plugin objects into the firedancer binary. Generate `fd_plugin_registry.h` to extend `TILES[]` and `PLUGIN_TOPOS[]`.

### Base Makefile (`plugin/Plugin.mk`)

A shared makefile that every plugin's `Local.mk` includes via `include plugin/Plugin.mk`. It provides:

- Standard `add-objs` and `make-lib` macros used directly for compilation and static library linking with minimal boilerplate.

- Documented variables (`PLUGIN_NAME`, `PLUGIN_TILE_SRCS`) that plugin authors set before the include.

A typical C plugin `Local.mk` reduces to:

```makefile
PLUGIN_NAME      := myplugin
PLUGIN_TOPO_SRCS := myplugin_topo
PLUGIN_TILE_SRCS := myplugin_tile
include plugin/Plugin.mk
```

`Plugin.mk` handles symbol exposure: it ensures each plugin's topology function and tile run struct are externally visible, and emits the metadata needed by the registry generator.

### Registry Generation

The types `fd_plugin_topo_fn` and `fd_plugin_entry_t` are defined in `fd_topob.h`. The generated registry header includes `fd_topob.h`.

```c
/* generated/fd_plugin_registry.h — do not edit */
#include "fd_topob.h"

/* Tile run callbacks — spliced into the TILES[] initializer */
extern fd_topo_run_tile_t fd_tile_myplug;

#define PLUGIN_TILE_LIST \
  &fd_tile_myplug,

/* Plugin topology functions (namespace + function pointer) */
extern void fd_myplugin_topo( fd_topo_t * topo );

fd_plugin_entry_t PLUGIN_TOPOS[] = {
  { "myplugin", fd_myplugin_topo },
  { NULL, NULL },
};
```

`main.c` splices the `PLUGIN_TILE_LIST` macro directly into the `TILES[]` initializer — no separate array or concatenation. `PLUGIN_TOPOS` is used during topology construction.

---

## Phase 3: Link Manifests and Version Safety

### Manifests

Both core firedancer and each plugin maintain a `manifest.toml` listing the links they expose across boundaries and their versions. Manifests serve a single purpose: **version compatibility checking**. They do not describe topology structure.

**Core manifest** (`src/app/firedancer/manifest.toml`):

```toml
# Links exposed to plugins.  Internal links (e.g., snapin_wm)
# are omitted — only cross-boundary links need entries.

[[produces]]
link    = "replay_out"
version = "3.0.0"
mtu     = 8192

[[produces]]
link    = "gossip_out"
version = "2.1.0"
mtu     = 4096

[[produces]]
link    = "shred_out"
version = "1.0.0"
mtu     = 1228

[[produces]]
link    = "tower_out"
version = "1.0.0"
mtu     = 256
```

**Plugin manifest** (`plugin/myplugin/manifest.toml`):

```toml
[plugin]
name = "myplugin"

[[consumes]]
link    = "replay_out"
version = "3.0.0"

[[produces]]
link    = "myplug_out"
version = "1.0.0"
mtu     = 1024
```

### Versioning Semantics

Versions use **semver** strings (e.g., `"3.0.0"`, `"2.1.0"`). A version covers the full link contract: **payload struct layout** and **signal/control (`sig`/`ctl`) schema** — i.e., the set of message types carried on the link and their semantics. Size parameters (`depth`, `mtu`, `burst`) are **not versioned** — the producer sets them and the consumer adapts.

Semver rules:
- **Major bump**: breaking change to payload layout or signal schema (fields removed, reordered, resized; signal values changed).
- **Minor bump**: backward-compatible addition (new optional field appended, new signal value added that old consumers can ignore).
- **Patch bump**: documentation or comment-only changes to the schema.

At validation time, the consumer's major version must match the producer's major version. The consumer's minor version must be ≤ the producer's minor version.

### Build-Time Validation

During `make`, a validation step loads all manifests (core + every plugin) and checks:

- **Namespace collisions**: no two `[[produces]]` entries across all manifests share a link name.
- **Consumer satisfaction**: every `[[consumes]]` entry has a matching `[[produces]]` entry from some other manifest. **Note:** the actual implementation (`validate_manifests.py`) treats missing producers as **warnings** for plugin manifests (those with a `[plugin]` section) but **errors** for core manifests.
- **Version compatibility**: consumer and producer major versions match; consumer minor ≤ producer minor.
- **MTU consistency**: if the consumer manifest specifies an MTU, it must match the producer's declared MTU.

```
ERROR: plugin "myplugin" consumes "replay_out" version "2.0.0",
  but core produces version "3.0.0" (major version mismatch).
  Recompile the plugin against the current firedancer version.
```

### Startup-Time Validation

> **Status: Not yet implemented.** The design below is planned but not yet coded.

After `fd_topob_finish()`, a runtime check compares the actual topology against all manifests. Because `publish`, `connect`, `connect_many`, and `subscribe` all record their `version` string directly in the topology (stored in `fd_topo_link_t`), the runtime has two independent sources of version truth: the **code-declared version** (from the API call) and the **manifest-declared version** (from `manifest.toml`). Startup validation cross-checks both:

- Every link listed in a manifest's `[[produces]]` must exist in the topology with a matching name and MTU.
- The manifest-declared version must match the code-declared version for the same link. A mismatch means the manifest is stale (or vice versa).
- Every `[[consumes]]` entry must correspond to an actual connection wiring.
- Consumer code-declared versions (from `subscribe`) must be compatible with producer code-declared versions (from `publish` / `connect`) — same major, consumer minor ≤ producer minor. This check is redundant with the pass-2 subscribe check but serves as a belt-and-suspenders backstop.

This catches manifest drift in both directions — a plugin author who adds a link in code but forgets to update the manifest, or who bumps the manifest version but forgets to update the version string in their `publish` / `subscribe` call. The manifest remains the source of truth for **build-time** validation (before the topology exists); the code-declared version is the source of truth at **runtime**.

---

## Phase 4: Unified Dynamic Config Loading

This phase covers how tiles — both core and plugin — access configuration at runtime. The end-state goal is for all tiles to read config from a shared pod, replacing the `fd_topo_configure_tile()` switch and the per-tile config union in `fd_topo_tile_t` with dynamic, string-keyed lookups. **That migration has not been started for core tiles.** What _has_ been implemented is the infrastructure that makes it possible:

- A `topo->config` field (full TOML config pod snapshot) that all tiles can read at runtime.
- `fd_topo_cfg.h` convenience wrappers for pod-based config access.
- Plugin tiles (e.g. RPC) already read exclusively from `topo->config`.

Core tiles still use `fd_topo_configure_tile()` and the per-tile union (`tile->gossip.*`, `tile->repair.*`, etc.) — migrating them is future work.

### Background: The Problem with `fd_topo_configure_tile()`

Today, tile config is plumbed through a ~500-line `if/else` switch in `topology.c` (`fd_topo_configure_tile()`), which manually copies fields from `fd_config_t` into per-tile structs in the `fd_topo_tile_t` union. This creates massive redundancy — `identity_key_path` is copied 11 times, `accdb_max_depth` is computed identically 7 times — and forces every new config field to touch three places: the config struct, the tile union in `fd_topo.h`, and the switch in `topology.c`. Plugins cannot participate in this scheme at all, since the tile union is not extensible by external code.

### What Exists Today: `topo->config` (Full TOML Config Pod)

Instead of selectively flattening individual config values into `topo->props`, the implementation copies the **entire parsed TOML config pod** into a dedicated field on `fd_topo_t`:

```c
/* In fd_topo.h: */
struct fd_topo {
  char   app_name[ 256UL ];
  uchar  props[ 32768UL ];       /* Object IDs and topology metadata */
  uchar  config[ 131072UL ];     /* Full parsed TOML config pod.
                                    Populated during config loading
                                    so tiles can read any config
                                    value dynamically. */
  /* ... */
};
```

The `config` field is a 128 KiB pod. It is initialized in `fd_topob_new()` (via `fd_pod_new`) and then populated at the start of every `fd_topo_initialize()` variant by copying the already-parsed config pod:

```c
/* In fd_topo_initialize() (topology.c): */
fd_topo_t * topo = fd_topob_new( &config->topo, config->name );
fd_memcpy( topo->config, config->config_pod, sizeof(topo->config) );
```

This pattern is consistent across all topology entry points — `firedancer/topology.c`, `fdctl/topology.c`, `backtest.c`, `forktest.c`, `gossip.c`, `repair.c`, etc.

The `topo->props` pod (32 KiB) remains in use but is **not** used for config values. It continues to hold object IDs and topology metadata exclusively.

### The Planned Unified Approach (Future Work)

> **Status: Not yet implemented.** The following describes the target end-state.

The long-term plan is for both core tiles and plugin tiles to read config from pods, replacing the per-tile union. The lifecycle:

1. **Write phase** (single-threaded, in `fd_topo_initialize()`): Config values are available in `topo->config` (already done). Optionally, commonly-needed derived values could be flattened into `topo->props` for convenience.

2. **Serialization**: The entire `config_t` (which embeds `topo` and therefore both `topo->props` and `topo->config`) is serialized to a memfd. Each tile process gets its own memory-mapped copy.

3. **Read phase** (per-tile, in `unprivileged_init()`): Each tile queries `topo->config` (or `topo->props`) for the keys it needs via `fd_pod_query_*` or the `fd_topo_cfg_*` wrappers.

No config values (like `paths.identity_key`, `runtime.max_live_slots`, etc.) are currently flattened into `topo->props`. The existing `fd_topo_configure_tile()` switch and the per-tile config structs in the `fd_topo_tile_t` union remain **fully intact** — no core tiles have been migrated to read from pods yet. See "Migration Strategy" below for the incremental plan.

### Plugin Config Access

Plugin config lives in `[plugins.<name>]` in the main firedancer TOML. For example, the RPC plugin:

```toml
[plugins.rpc]
    enabled = true

    rpc_listen_address = "127.0.0.1"
    rpc_listen_port = 8899
    max_http_connections = 1024
    max_http_request_length = 8192
    send_buffer_size_mb = 1024
    delay_startup = true
```

The config parser exempts the `plugins.*` namespace from the "unknown keys are fatal" rule. All plugin config — including the `enabled` flag and plugin-specific keys — is part of the full TOML config and is therefore available in `topo->config` after the `fd_memcpy` in `fd_topo_initialize()`.

Plugin tiles read their config directly from `topo->config` at runtime using `fd_pod_query_*`:

```c
/* In the RPC plugin tile (fd_rpc_tile.c): */
uchar const * cfg = topo->config;

long max_conns = fd_pod_query_long( cfg, "plugins.rpc.max_http_connections", 1024L );
long send_buf  = fd_pod_query_long( cfg, "plugins.rpc.send_buffer_size_mb",  1024L );
char const * listen_addr = fd_pod_query_cstr( cfg, "plugins.rpc.rpc_listen_address", "127.0.0.1" );
```

There is no `fd_topo_plugin_merge()` function — plugin config is not merged or copied into `topo->props`. The `topo->config` pod already contains the full TOML tree, so plugins simply query their namespace directly.

**Plugin enablement** is checked during topology construction by querying `topo->config`:

```c
/* In fd_topo_initialize() (firedancer/topology.c): */
char key[ 64 ];
FD_TEST( fd_cstr_printf_check( key, sizeof(key), NULL,
         "plugins.%s.enabled", PLUGIN_TOPOS[ i ].ns ) );
if( fd_pod_query_int( topo->config, key, 0 ) )
  enabled[ cnt++ ] = PLUGIN_TOPOS[ i ];
```

Note: the default for `enabled` is `0` (false) — a plugin must have `enabled = true` in the TOML to be included.

**`allow_shutdown`** is not a TOML config field. It is handled as a tile flag (`FD_TOPOB_TILE_ALLOW_SHUTDOWN`, defined in `fd_topob.h`) set at topology construction time.

### How Tiles Read Config (Planned Target)

> **Status: Not yet implemented.** No core tiles use the `fd_topo_cfg_*` wrappers — grepping for `fd_topo_cfg_` in tile `.c` files returns zero matches. Core tiles still read from `tile->gossip.*`, `tile->repair.*`, etc. via the per-tile union. Plugin tiles read from `topo->config` using raw `fd_pod_query_*` calls.

The planned target is for both core and plugin tiles to read config the same way in `unprivileged_init()` — via `fd_topo_cfg_*` wrappers or `fd_pod_query_*` against `topo->config`:

```c
/* Core tile example (repair) — planned: */
static void
unprivileged_init( fd_topo_t * topo, fd_topo_tile_t * tile ) {
  uchar const * cfg = topo->config;

  char const * identity_key_path =
      fd_topo_cfg_cstr( cfg, "paths.identity_key" );
  ushort repair_intake_port =
      fd_topo_cfg_ushort( cfg, "tiles.repair.repair_intake_listen_port" );
  /* ... use values directly in tile init ... */
}

/* Plugin tile example (rpc) — already works this way: */
static void
unprivileged_init( fd_topo_t * topo, fd_topo_tile_t * tile ) {
  uchar const * cfg = topo->config;

  long max_live_slots = fd_pod_query_long( cfg, "runtime.max_live_slots", -1L );
  FD_TEST( max_live_slots > 0L );
  long max_conns = fd_pod_query_long( cfg, "plugins.rpc.max_http_connections", 1024L );
  /* ... */
}
```

### Convenience Wrappers

`fd_topo_cfg.h` is **implemented** and provides fail-fast wrappers for required config values, ensuring clear startup errors instead of silent defaults. It also provides checked (optional) variants.

Two families of accessors:

- `fd_topo_cfg_<type>()` — required value, `FD_LOG_ERR` if missing or unreadable.
- `fd_topo_cfg_<type>_checked()` — optional value, returns 1 if present (writing to `*out`), 0 if absent (`*out` untouched).

**Supported types:** `ulong`, `uint`, `ushort`, `long`, `int`, `cstr`.

**Type-mismatch warnings:** Each accessor calls `fd_topo_cfg_check_type()` before querying. If the key exists but is stored with a different pod type than expected (e.g. TOML stores integers as `LONG` but the caller asks for `ULONG`), an `FD_LOG_WARNING` is emitted. The query still proceeds — `fd_pod_query_<type>` may or may not be able to decode the value depending on the mismatch.

```c
/* fd_topo_cfg.h — included by tiles that use dynamic config.

   Two families of accessors:

   fd_topo_cfg_<type>          required value.  FD_LOG_ERR
                               if the key is missing.

   fd_topo_cfg_<type>_checked  optional value.  Returns 1
                               if present (writes to *out),
                               0 if absent (*out untouched).

   All accessors emit FD_LOG_WARNING if the key exists but is
   stored with a different pod type than expected. */

/* Helper: warn if key exists with unexpected type. */
static inline int
fd_topo_cfg_check_type( uchar const * pod,
                        char const *  key,
                        int           expected_type ) {
  fd_pod_info_t info[1];
  if( FD_UNLIKELY( fd_pod_query( pod, key, info ) ) ) return -1;
  if( FD_UNLIKELY( info->val_type!=expected_type ) )
    FD_LOG_WARNING(( "config key `%s` has pod type %d, expected %d; "
                     "query may return default/sentinel",
                     key, info->val_type, expected_type ));
  return info->val_type;
}

/* --- Required (fail-fast) variants --- */

static inline ulong
fd_topo_cfg_ulong( uchar const * props, char const * key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_ULONG );
  ulong val = fd_pod_query_ulong( props, key, ULONG_MAX );
  if( FD_UNLIKELY( val==ULONG_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline uint
fd_topo_cfg_uint( uchar const * props, char const * key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_UINT );
  uint val = fd_pod_query_uint( props, key, UINT_MAX );
  if( FD_UNLIKELY( val==UINT_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline ushort
fd_topo_cfg_ushort( uchar const * props, char const * key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_USHORT );
  ushort val = fd_pod_query_ushort( props, key, USHORT_MAX );
  if( FD_UNLIKELY( val==USHORT_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline long
fd_topo_cfg_long( uchar const * props, char const * key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_LONG );
  long val = fd_pod_query_long( props, key, LONG_MAX );
  if( FD_UNLIKELY( val==LONG_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline int
fd_topo_cfg_int( uchar const * props, char const * key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_INT );
  int val = fd_pod_query_int( props, key, INT_MAX );
  if( FD_UNLIKELY( val==INT_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline char const *
fd_topo_cfg_cstr( uchar const * props, char const * key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_CSTR );
  char const * val = fd_pod_query_cstr( props, key, NULL );
  if( FD_UNLIKELY( !val ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

/* --- Checked (optional) variants ---
   Return 1 if the key is present and readable (value written
   to *out), 0 if absent or unreadable (*out is not modified).
   Warns on type mismatch even for optional keys. */

static inline int
fd_topo_cfg_ulong_checked( uchar const * props,
                           char const *  key,
                           ulong *       out ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_ULONG );
  ulong val = fd_pod_query_ulong( props, key, ULONG_MAX );
  if( FD_UNLIKELY( val==ULONG_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_uint_checked( uchar const * props,
                          char const *  key,
                          uint *        out ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_UINT );
  uint val = fd_pod_query_uint( props, key, UINT_MAX );
  if( FD_UNLIKELY( val==UINT_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_ushort_checked( uchar const * props,
                            char const *  key,
                            ushort *      out ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_USHORT );
  ushort val = fd_pod_query_ushort( props, key, USHORT_MAX );
  if( FD_UNLIKELY( val==USHORT_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_long_checked( uchar const * props,
                          char const *  key,
                          long *        out ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_LONG );
  long val = fd_pod_query_long( props, key, LONG_MAX );
  if( FD_UNLIKELY( val==LONG_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_int_checked( uchar const * props,
                         char const *  key,
                         int *         out ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_INT );
  int val = fd_pod_query_int( props, key, INT_MAX );
  if( FD_UNLIKELY( val==INT_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_cstr_checked( uchar const *  props,
                          char const *   key,
                          char const * * out ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_CSTR );
  char const * val = fd_pod_query_cstr( props, key, NULL );
  if( FD_UNLIKELY( !val ) ) return 0;
  *out = val;
  return 1;
}
```

The required variants (`fd_topo_cfg_<type>`) `FD_LOG_ERR` immediately if the key is missing or unreadable — suitable for config that a tile cannot run without. The checked variants (`fd_topo_cfg_<type>_checked`) return a boolean and accept an out pointer, suitable for optional config with caller-defined defaults or conditional behavior. Both families work identically for core and plugin tiles.

### `scratch_footprint` Signature Change

To support dynamic config access during memory sizing, the `scratch_footprint` tile callback signature was changed from:

```c
ulong (*scratch_footprint)( fd_topo_tile_t const * tile );
```

to:

```c
ulong (*scratch_footprint)( fd_topo_t const * topo, fd_topo_tile_t const * tile );
```

This allows plugin tiles (and eventually core tiles) to access `topo->config` when computing scratch memory requirements based on TOML config values. For example, the RPC plugin tile uses this to size its HTTP server buffer based on `plugins.rpc.max_http_connections` and `plugins.rpc.send_buffer_size_mb`.

### Migration Strategy

The refactor is incremental. Core tiles can be migrated one at a time while the old and new mechanisms coexist:

1. In the tile's `unprivileged_init`, read from `topo->config` instead of `tile->X.*`.
2. Remove the tile's `else if` branch from `fd_topo_configure_tile()`.
3. Remove the tile's struct from the union in `fd_topo.h` (once nothing else references it).

Plugin tiles use `topo->config` from day one — they never touch the tile union.

### Plugin Permissions

Standard fields under `[plugins.<name>]`:

| Field | Default | Status | Effect |
|-------|---------|--------|--------|
| `enabled` | `false` (`0`) | **Implemented** | Whether the plugin is included at all. Queried via `fd_pod_query_int(topo->config, "plugins.<name>.enabled", 0)` during topology construction. |
| `seccomp` | — | **Planned** | Enforce seccomp. `false` only for dev. Not yet implemented as a per-plugin TOML field. |
| `read_only` | — | **Planned** | Cannot access topo objects with read_write, cannot create links where an external tile is the consumer. Not yet implemented. |

---

## Phase 5: Seccomp for Plugins

### Challenges

- **Deriving the syscall set.** Non-trivial.
- **Debugging violations.** No visibility into which syscall triggered a kill.
- **Policy ownership.** Plugin maintainers are responsible for building and maintaining their own seccomp policies from the ground up. The `firedancer-dev` tooling (syscall tracing, violation debugging) exists to make this tractable, but no base policy is provided—runtime syscall surfaces vary across toolchains and are not stable enough to maintain centrally.

### Solutions

**5a. Seccomp violation debugger.** In `firedancer-dev`, use `SECCOMP_RET_TRAP` (not `SECCOMP_RET_KILL_PROCESS`) for plugin tiles. Install a `SIGSYS` handler that logs the syscall number, arguments, and instruction pointer before terminating:

```
SECCOMP VIOLATION: tile=myplug syscall=futex(202)
  arg0=0x7f... arg1=0x80 arg2=0x0
  ip=0x5555...
```

Production builds keep `SECCOMP_RET_KILL_PROCESS`.

**5b. Syscall tracing.** `firedancer-dev syscall-trace --tile myplug` runs the tile under strace/seccomp-log mode, collects unique syscalls, outputs a candidate `.seccomppolicy`.

**5c. Automatic compilation.** The build system runs `generate_filters.py` on `plugin/*/fd_*_tile.seccomppolicy` just like it does for core tiles.

**5d. Escape hatch.** `[plugins.myplugin] seccomp = false` for development.

---

## Case Study: Migrating the GUI Tile to a Plugin

The GUI tile is the best first candidate for plugin extraction — it is a pure consumer (read-only, no outgoing links that other core tiles depend on), conditionally enabled, and already gated behind `config->tiles.gui.enabled`. Converting it exercises every layer of the plugin system.

### Current State

**Source files.** `src/disco/gui/` — `fd_gui_tile.c` (tile implementation), `fd_gui.c` / `fd_gui.h` (GUI logic), `fd_gui_config_parse.c`, `fd_gui_peers.c`, `fd_gui_printf.c`, `fd_gui_tile.seccomppolicy`, plus bundled frontend assets in `dist_stable/`, `dist_alpha/`, `dist_dev/`.

**Tile struct fields.** The `gui` member of the `fd_topo_tile_t` union carries ~15 config fields (`listen_addr`, `listen_port`, `is_voting`, `cluster`, `identity_key_path`, `vote_key_path`, `max_http_connections`, `max_websocket_connections`, `max_http_request_length`, `send_buffer_size_mb`, `schedule_strategy`, `websocket_compression`, `frontend_release_channel`, `tile_cnt`, `wfs_bank_hash`, `expected_shred_version`).

**Config plumbing.** `fd_topo_configure_tile()` in `topology.c` copies ~15 values from `fd_config_t` into `tile->gui.*`. This is one of the largest branches in the configure switch.

**Link subscriptions (Firedancer).** The GUI tile subscribes to 14+ links:

| Link | Reliability | Notes |
|------|-------------|-------|
| `net_gossvf` (×N) | unreliable | raw gossip network traffic |
| `repair_net` | unreliable | raw repair network traffic |
| `shred_out` (×N) | reliable | completed shreds |
| `gossip_net` | unreliable | raw gossip packets |
| `gossip_out` | reliable | gossip state updates |
| `tower_out` | reliable | tower/vote messages |
| `replay_out` | reliable | replay state updates |
| `replay_epoch` | reliable | epoch boundary notifications |
| `genesi_out` | reliable | genesis load status |
| `pack_poh` | reliable | leader scheduling |
| `pack_execle` | reliable | pack → execute |
| `execle_poh` (×N) | reliable | execute → PoH |
| `execrp_replay` (×N) | reliable | exec replay results |
| `snapct_gui` | reliable | snapshot progress |
| `snapin_gui` | reliable | snapshot parse progress |
| `snapin_manif` | reliable | snapshot manifest |
| `bundle_status` | reliable | bundle engine updates |

**Link subscriptions (Frankendancer/fdctl).** `plugin_out`, `pohh_pack`, `pack_bank`, `pack_pohh`, `bank_pohh` (×N), `bundle_status`.

**Tile attributes.** `FD_TOPOB_TILE_CRITICAL | FD_TOPOB_TILE_USES_ID_KEYSWITCH`.

**Registration.** `fd_tile_gui` in `TILES[]` in both `firedancer/main.c` and `fdctl/main.c`.

**Core references to `fd_gui.h`.** Some files in the core repo include `fd_gui.h` directly. These must be identified and removed before extraction.

### Migration Plan

#### Step 1: Migrate config reads to `topo->config`

Before extracting GUI as a plugin, migrate its config from the tile union to `topo->config`. Since `topo->config` already contains the full parsed TOML config pod (copied at the start of `fd_topo_initialize`), GUI config is available under `tiles.gui.*` in the pod. This is not GUI-specific but a prerequisite for any tile migration.

1. In `fd_gui_tile.c` `unprivileged_init`, read from `topo->config` via `fd_pod_query_*` or `fd_topo_cfg_*`:

   ```c
   ctx->listen_addr = fd_topo_cfg_uint( topo->config, "tiles.gui.listen_addr" );
   ctx->listen_port = fd_topo_cfg_ushort( topo->config, "tiles.gui.listen_port" );
   ```

2. Remove the `gui` branch from `fd_topo_configure_tile()`.

3. Remove the `gui` struct from the `fd_topo_tile_t` union (once nothing references it).

#### Step 2: Remove core references to `fd_gui.h`

Identify and remove all `#include` references to `fd_gui.h` from files outside `src/disco/gui/`. Any core code that depends on GUI types or functions must be decoupled before the source files are moved to the plugin directory.

#### Step 3: Create the plugin directories

Firedancer and Frankendancer have substantially different link sets, so GUI is split into two plugin packages that share the same tile implementation and library code. Only the topology function, manifest, and registry entry differ.

```
plugin/
├── gui-common/                        ← shared code (not a plugin itself)
│   ├── fd_gui_tile.c                  ← moved from src/disco/gui/
│   ├── fd_gui_tile.seccomppolicy      ← moved
│   ├── fd_gui.c                       ← moved
│   ├── fd_gui.h                       ← moved
│   ├── fd_gui_config_parse.c          ← moved
│   ├── fd_gui_config_parse.h          ← moved
│   ├── fd_gui_peers.c                 ← moved
│   ├── fd_gui_peers.h                 ← moved
│   ├── fd_gui_printf.c               ← moved
│   ├── fd_gui_printf.h               ← moved
│   ├── fd_gui_metrics.h              ← moved
│   ├── dist_stable/                   ← moved (frontend assets)
│   ├── dist_alpha/
│   └── dist_dev/
│
├── gui-fd/                            ← Firedancer GUI plugin
│   ├── manifest.toml
│   ├── Local.mk
│   └── fd_gui_fd_topo.c              ← Firedancer topology function
│
└── gui-frank/                         ← Frankendancer GUI plugin
    ├── manifest.toml
    ├── Local.mk
    └── fd_gui_frank_topo.c            ← Frankendancer topology function
```

Only one of `gui-fd` or `gui-frank` is built per binary. The build system selects the correct plugin based on whether Firedancer or Frankendancer is being compiled.

#### Step 4: Write the topology functions

Each topology function uses `subscribe` with `FD_TOPOB_REQUIRED` for all its link inputs — GUI expects these links to exist and will fail at startup if they are absent.

**Firedancer (`plugin/gui-fd/fd_gui_fd_topo.c`):**

```c
#include "../../src/disco/topo/fd_topob.h"

void
fd_gui_fd_topo( fd_topo_t * topo ) {

  fd_topob_wksp( topo, "gui" );
  fd_topob_tile( topo, "gui", 0UL, "gui", "metric_in",
      FD_TOPOB_TILE_CRITICAL | FD_TOPOB_TILE_USES_ID_KEYSWITCH );

  /* Subscribe to all core links the Firedancer GUI monitors. */
  fd_topob_subscribe( topo, "gui", 1,
      "net_gossvf",    "1.0.0", FD_TOPOB_UNRELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "repair_net",    "1.0.0", FD_TOPOB_UNRELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "shred_out",     "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "gossip_net",    "1.0.0", FD_TOPOB_UNRELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "gossip_out",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "tower_out",     "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "replay_out",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "replay_epoch",  "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "genesi_out",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "pack_poh",      "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "pack_execle",   "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "execle_poh",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "execrp_replay", "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "snapct_gui",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "snapin_gui",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "snapin_manif",  "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "bundle_status", "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
}
```

**Frankendancer (`plugin/gui-frank/fd_gui_frank_topo.c`):**

```c
#include "../../src/disco/topo/fd_topob.h"

void
fd_gui_frank_topo( fd_topo_t * topo ) {

  fd_topob_wksp( topo, "gui" );
  fd_topob_tile( topo, "gui", 0UL, "gui", "metric_in",
      FD_TOPOB_TILE_CRITICAL | FD_TOPOB_TILE_USES_ID_KEYSWITCH );

  /* Subscribe to all core links the Frankendancer GUI monitors. */
  fd_topob_subscribe( topo, "gui", 1,
      "plugin_out",    "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "pohh_pack",     "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "pack_bank",     "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "pack_pohh",     "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "bank_pohh",     "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "gui", 1,
      "bundle_status", "1.0.0", FD_TOPOB_RELIABLE,
      FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
}
```

#### Step 5: Write `Local.mk` files

Both plugins reference the shared code in `plugin/gui-common/` and include the `frontend` / `frontend-clean` targets from the core build.

**`plugin/gui-fd/Local.mk`:**

```makefile
PLUGIN_NAME      := gui
PLUGIN_TOPO_SRCS := fd_gui_fd_topo
PLUGIN_TILE_SRCS :=
include plugin/Plugin.mk

# Shared GUI tile and library objects from gui-common/
$(call add-objs,../gui-common/fd_gui_tile,fd_plugin_gui)
$(call add-objs,../gui-common/fd_gui ../gui-common/fd_gui_config_parse ../gui-common/fd_gui_peers ../gui-common/fd_gui_printf,fd_plugin_gui)

# Frontend build targets (delegates to core build rules)
include src/disco/gui/frontend.mk
```

**`plugin/gui-frank/Local.mk`:**

```makefile
PLUGIN_NAME      := gui
PLUGIN_TOPO_SRCS := fd_gui_frank_topo
PLUGIN_TILE_SRCS :=
include plugin/Plugin.mk

# Shared GUI tile and library objects from gui-common/
$(call add-objs,../gui-common/fd_gui_tile,fd_plugin_gui)
$(call add-objs,../gui-common/fd_gui ../gui-common/fd_gui_config_parse ../gui-common/fd_gui_peers ../gui-common/fd_gui_printf,fd_plugin_gui)

# Frontend build targets (delegates to core build rules)
include src/disco/gui/frontend.mk
```

The `frontend.mk` include is extracted from the current `src/disco/gui/Local.mk` so that both plugin variants can build and clean the frontend assets. The `frontend` and `frontend-clean` phony targets continue to work as before.

#### Step 6: Write `manifest.toml` files

Each manifest declares the exact links its topology function subscribes to, including MTU.

**`plugin/gui-fd/manifest.toml`:**

```toml
[plugin]
name = "gui"

[[consumes]]
link    = "net_gossvf"
version = "1.0.0"
mtu     = 1500

[[consumes]]
link    = "repair_net"
version = "1.0.0"
mtu     = 1500

[[consumes]]
link    = "shred_out"
version = "1.0.0"
mtu     = 1264

[[consumes]]
link    = "gossip_net"
version = "1.0.0"
mtu     = 1500

[[consumes]]
link    = "gossip_out"
version = "1.0.0"
mtu     = 1320

[[consumes]]
link    = "tower_out"
version = "1.0.0"
mtu     = 3264

[[consumes]]
link    = "replay_out"
version = "1.0.0"
mtu     = 2240

[[consumes]]
link    = "replay_epoch"
version = "1.0.0"
mtu     = 17162328

[[consumes]]
link    = "genesi_out"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "pack_poh"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "pack_execle"
version = "1.0.0"
mtu     = 65535

[[consumes]]
link    = "execle_poh"
version = "1.0.0"
mtu     = 65535

[[consumes]]
link    = "execrp_replay"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "snapct_gui"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "snapin_gui"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "snapin_manif"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "bundle_status"
version = "1.0.0"
mtu     = 0

```

**`plugin/gui-frank/manifest.toml`:**

```toml
[plugin]
name = "gui"

[[consumes]]
link    = "plugin_out"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "pohh_pack"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "pack_bank"
version = "1.0.0"
mtu     = 65535

[[consumes]]
link    = "pack_pohh"
version = "1.0.0"
mtu     = 0

[[consumes]]
link    = "bank_pohh"
version = "1.0.0"
mtu     = 65535

[[consumes]]
link    = "bundle_status"
version = "1.0.0"
mtu     = 0
```

#### Step 7: Handle GUI-only links

Some links exist solely because GUI consumes them (e.g., `snapct_gui`, `snapin_gui`, `bundle_status`). The core topology continues to create these links via `fd_topob_publish` with `permit_no_consumers = 1`. GUI subscribes to them. This means the core topology retains awareness of these links, but the GUI plugin is the only consumer. If GUI is disabled (plugin not loaded), the links are created but unused — this is harmless.

#### Step 8: Remove GUI from core topologies

1. Remove the `if( config->tiles.gui.enabled )` block from both `firedancer/topology.c` and `fdctl/topology.c`. Replace GUI-only link creations (`snapct_gui`, `snapin_gui`, `bundle_status`) with `fd_topob_publish` calls so the links exist for the plugin to subscribe to.
2. Remove `&fd_tile_gui` from `TILES[]` in both `firedancer/main.c` and `fdctl/main.c`. The plugin registry auto-generates `PLUGIN_TILES[]` which now includes it.
3. Remove the `gui` config branch from `fd_topo_configure_tile()`.
4. Remove the `gui` struct from the `fd_topo_tile_t` union.

#### Challenges and Risks

- **Frontend assets.** The `dist_stable/`, `dist_alpha/`, `dist_dev/` directories contain ~50 MB of pre-built frontend JavaScript/CSS/fonts. These move into `plugin/gui-common/`. The `frontend.mk` include extracted from the core `Local.mk` provides the `frontend` and `frontend-clean` build targets to both plugin variants.

- **`tile_cnt` field.** The GUI currently reads `tile_cnt` (total number of tiles in the topology) from `tile->gui.tile_cnt`, which is set *after* all tiles are created. As a plugin, `tile_cnt` won't be final until after plugin dispatch. Solution: read `topo->tile_cnt` directly in `unprivileged_init` instead of storing it in the config.

- **Namespace enforcement for `subscribe`.** The current namespace enforcement applies to `fd_topob_tile` and `fd_topob_link` (creation) but not to `fd_topob_subscribe` (consumer wiring). Since GUI only subscribes to core links and never creates its own links, no namespace violations occur. If GUI needs to create a link (e.g., a debug output), it must be named `gui_*`.

- **Binary selection.** Only one of `gui-fd` or `gui-frank` should be compiled per binary. The build system should gate inclusion based on whether the Firedancer or Frankendancer target is active — e.g., via conditional logic in each plugin's `Local.mk` or by placing the plugin discovery glob under a target-specific directory.

## Open Questions

## Future Work

- **Tooling.** `firedancer-dev plugin-new` (scaffold generator), `firedancer-dev topo-viz` (topology visualizer), `firedancer-dev syscall-trace` (seccomp policy helper), and a topology integration test harness for running plugin tiles against mock producers.

- **Seccomp debugging.** SIGSYS handler + `SECCOMP_RET_TRAP` in `firedancer-dev` for diagnosing seccomp violations in plugin tiles.

- **Relax tile name limit.** The current 6-char tile name limit (`char name[7]`) is tight for a plugin ecosystem where names need a distinguishing prefix (e.g., `"sltlog"` is already at the limit). Raising this to 12 or 16 chars would reduce collision risk and improve readability. Low priority — the current limit is workable for an initial plugin set.

- **Custom plugin metrics.** Plugin tiles get automatic tile-level metrics via `fd_stem`. A mechanism for plugin authors to define custom application-level metrics (counters, gauges, histograms) and integrate them with the Prometheus scraper is TBD. The scraper currently discovers metrics by tile name, so plugin metrics would need a registration scheme. Deferred until there is concrete demand from plugin authors.

- **Non-C plugin support.** Rust (or other C-ABI languages) plugin support via an `fd-plugin-sdk` crate wrapping `fd_stem`, with a thin C bridge for FFI. Would require protobuf or similar schemas for link payload serialization, since non-C plugins can't read packed C structs directly. Gated behind `EXTRAS=rust-plugins`.

- **Link payload schemas.** `.proto` (or similar) schemas for plugin-facing links, versioned in lockstep with the manifest. Build-time validation to prevent schema drift from C structs. Not needed while plugins are C-only.
