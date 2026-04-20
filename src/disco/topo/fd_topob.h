#ifndef HEADER_fd_src_disco_topo_fd_topob_h
#define HEADER_fd_src_disco_topo_fd_topob_h

/* fd_topob is a builder for fd_topo, providing many convenience
   functions for creating a useful topology. */

#include "../../disco/topo/fd_topo.h"
#include "fd_cpu_topo.h"

/* A link in the topology is either unpolled or polled.  Almost all
   links are polled, which means a tile which has this link as an in
   will read fragments from it and pass them to the tile handling
   code.  An unpolled link will not read off the link by default and
   the user code will need to specifically read it as needed. */

#define FD_TOPOB_UNPOLLED 0
#define FD_TOPOB_POLLED 1

/* A reliable link is a flow controlled one, where the producer will
   not send fragments if any downstream consumer does not have enough
   capacity (credits) to handle it. */

#define FD_TOPOB_UNRELIABLE 0
#define FD_TOPOB_RELIABLE 1

/* When subscribing to a link, whether it is required or
   optional.  An optional link will silently skip if it doesn't
   exist. */

#define FD_TOPOB_REQUIRED 0
#define FD_TOPOB_OPTIONAL 1

/* Tile lifecycle and behavioral attribute flags for
   fd_topob_tile.  Combined via bitwise OR into a single
   ulong flags parameter. */

#define FD_TOPOB_TILE_FLOATING        (1UL<<0) /* not pinned to a core             */
#define FD_TOPOB_TILE_STARTUP         (1UL<<1) /* runs only during startup         */
#define FD_TOPOB_TILE_POST_START      (1UL<<2) /* starts after main tiles          */
#define FD_TOPOB_TILE_CRITICAL        (1UL<<3) /* hyperthread twin bad             */
#define FD_TOPOB_TILE_IS_AGAVE        (1UL<<4) /* Agave/JVM tile                   */
#define FD_TOPOB_TILE_ALLOW_SHUTDOWN  (1UL<<5) /* tile can be shut down gracefully */
#define FD_TOPOB_TILE_USES_ID_KEYSWITCH (1UL<<6) /* uses identity keyswitch       */
#define FD_TOPOB_TILE_USES_AV_KEYSWITCH (1UL<<7) /* uses authority voter keyswitch */

/* Plugin topology function signature and registry entry.
   The build system generates a registry of plugin entries;
   fd_topob_plugin_dispatch iterates this registry in two
   passes. */

typedef void (*fd_plugin_topo_fn)( fd_topo_t * topo );

typedef struct {
  char const *      ns;
  fd_plugin_topo_fn fn;
} fd_plugin_entry_t;

FD_PROTOTYPES_BEGIN

/* Initialize a new fd_topo_t with the given app name and at the memory address
   provided.  Returns the topology at given address.  The topology will be empty
   with no tiles, objects, links. */

fd_topo_t *
fd_topob_new( void * mem,
              char const * app_name );

/* Add a workspace with the given name to the topology.  Workspace names
   must be unique and adding the same workspace twice will produce an
   error. */

fd_topo_wksp_t *
fd_topob_wksp( fd_topo_t *  topo,
               char const * name );

/* Add an object with the given type to the toplogy.  An object is
   something that takes up space in memory, in a workspace.

   The workspace must exist and have been added to the topology.
   Adding an object will cause it to occupt space in memory, but not
   be mapped into any tiles.  If you wish the object to be readable or
   writable by a tile, you need to add a fd_topob_tile_uses relationship. */

fd_topo_obj_t *
fd_topob_obj( fd_topo_t *  topo,
              char const * obj_type,
              char const * wksp_name );

/* Same as fd_topo_obj, but labels the object. */

fd_topo_obj_t *
fd_topob_obj_named( fd_topo_t *  topo,
                    char const * obj_type,
                    char const * wksp_name,
                    char const * label );

/* Add a relationship saying that a certain tile uses a given object.
   This has the effect that when memory mapping required workspaces
   for a tile, it will map the workspace required for this object in
   the appropriate mode.

   mode should be one of FD_SHMEM_JOIN_MODE_READ_ONLY or
   FD_SHMEM_JOIN_MODE_READ_WRITE. */

void
fd_topob_tile_uses( fd_topo_t *           topo,
                    fd_topo_tile_t *      tile,
                    fd_topo_obj_t const * obj,
                    int                   mode );

/* Add a link to the toplogy.  The link will not have any producer or
   consumer(s) by default, and those need to be added after.  The link
   can have no backing data buffer, a dcache, or a reassembly buffer
   behind it. */

fd_topo_link_t *
fd_topob_link( fd_topo_t *  topo,
               char const * link_name,
               char const * wksp_name,
               ulong        depth,
               ulong        mtu,
               ulong        burst );

/* Add a tile to the topology.  This creates various objects needed for
   a standard tile, including tile scratch memory, metrics memory and so
   on.  These objects will be created and linked to the respective
   workspaces provided, and the tile will be specified to map those
   workspaces when it is attached. */

fd_topo_tile_t *
fd_topob_tile( fd_topo_t *  topo,
               char const * tile_name,
               char const * tile_wksp,
               char const * metrics_wksp,
               ulong        flags );

/* Add an input link to the tile.  If the tile is created with fd_stem,
   it will automatically poll the in link and forward fragments to the
   user code (unless the link is specified as unpolled).

   An input link has an fseq which is a ulong used for returning the
   current reader position in sequence space, used for wiring flow
   control to the producer.  The producer will not produce fragments
   while any downstream consumer link is not ready to receive them,
   unless the link is marked as unreliable. */

void
fd_topob_tile_in( fd_topo_t *  topo,
                  char const * tile_name,
                  ulong        tile_kind_id,
                  char const * fseq_wksp,
                  char const * link_name,
                  ulong        link_kind_id,
                  int          reliable,
                  int          polled );

/* Add an output link to the tile.  This doesn't do much by itself,
   but will cause the link to get mapped in as writable for the tile,
   and the tile can later look up the link by name and write to it
   as it wants. */

void
fd_topob_tile_out( fd_topo_t *  topo,
                   char const * tile_name,
                   ulong        tile_kind_id,
                   char const * link_name,
                   ulong        link_kind_id );

/* Automatically layout the tiles onto CPUs in the topology for a
   best effort.  fd_topob_auto_layout reads CPU topology from the OS.
   fd_topob_auto_layout_cpus takes a pre-built CPU topology, useful
   for testing. */

void
fd_topob_auto_layout( fd_topo_t * topo,
                      int         reserve_agave_cores );

void
fd_topob_auto_layout_cpus( fd_topo_t *      topo,
                           fd_topo_cpus_t * cpus,
                           int              reserve_agave_cores );

/* Finish creating the topology.  Lays out all the objects in the
   given workspaces, and sizes everything correctly.  Also validates
   the topology before returning.

   This must be called to finish creating the topology. */

void
fd_topob_finish( fd_topo_t *                topo,
                 fd_topo_obj_callbacks_t ** callbacks );

/* fd_topob_connect creates a link and wires both producer and
   consumer in one call.  Idempotent on the link name -- if the
   link already exists, the producer/depth/mtu/burst/link_wksp
   parameters are ignored and only the consumer side is wired.
   1:1 -- one producer instance to one consumer instance.
   version may be NULL for core-internal links. */

void
fd_topob_connect( fd_topo_t *  topo,
                  char const * producer,      char const * consumer,
                  char const * link_name,     char const * link_wksp,
                  char const * version,
                  ulong depth, ulong mtu, ulong burst,
                  int reliable, int polled );

/* fd_topob_connect_many creates N link instances and wires each
   producer:i out on link:i, each consumer:j in on all
   link:0..N-1.  Idempotent on the link name -- if the links
   already exist, only consumer side is wired.
   Subsumes 1:N (producer_cnt=1) and N:1 (consumer_cnt=1).
   version may be NULL for core-internal links. */

void
fd_topob_connect_many( fd_topo_t *  topo,
                       char const * producer,      ulong producer_cnt,
                       char const * consumer,      ulong consumer_cnt,
                       char const * link_name,     char const * link_wksp,
                       char const * version,
                       ulong depth, ulong mtu, ulong burst,
                       int reliable, int polled );

/* fd_topob_publish creates link(s) with a producer but no
   consumer.  Used when a tile exposes a link for others to
   subscribe to or connect to later.  Idempotent -- no-ops if the
   link already exists.
   version may be NULL for core-internal links. */

void
fd_topob_publish( fd_topo_t *  topo,
                  char const * producer,      ulong producer_cnt,
                  char const * link_name,     char const * link_wksp,
                  char const * version,
                  ulong depth, ulong mtu, ulong burst );

/* fd_topob_subscribe wires consumer(s) to an existing link
   without knowing its creation parameters.  Behavior depends on
   topo->pass (set by fd_topob_plugin_dispatch):
     Pass 1: silently skips if the link doesn't exist yet.
     Pass 2: FD_LOG_ERR if the link still doesn't exist,
             UNLESS optional -- then silently skip on both
             passes.
   version is the consumer's expected semver. */

void
fd_topob_subscribe( fd_topo_t *  topo,
                    char const * consumer,      ulong consumer_cnt,
                    char const * link_name,     char const * version,
                    int reliable, int polled,   int optional );

/* fd_topob_plugin_dispatch runs the two-pass plugin dispatch
   protocol.  For each plugin in the registry, sets
   topo->namespace to the plugin's namespace and calls the
   plugin's topology function.  Pass 1 allows all creation
   calls and silently skips unresolved subscribes.  Pass 2
   re-runs all plugins; creation calls are idempotent no-ops,
   and required subscribes fail hard if still unresolved.
   Clears topo->namespace after dispatch completes.

   plugins is a NULL-terminated array of fd_plugin_entry_t. */

void
fd_topob_plugin_dispatch( fd_topo_t *             topo,
                          fd_plugin_entry_t const * plugins );

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_disco_topo_fd_topob_h */
