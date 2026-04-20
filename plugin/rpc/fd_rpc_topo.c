/* fd_rpc_topo.c — RPC plugin topology function.

   Creates the RPC tile, its workspaces, the rpc_replay link,
   subscribes to core links, and wires shared objects (funk,
   funk_locks, accdb_data, vinyl_rq). */

#include "../../src/disco/topo/fd_topob.h"
#include "../../src/disco/topo/fd_topob_vinyl.h"
#include "../../src/util/pod/fd_pod.h"

void
fd_rpc_topo( fd_topo_t * topo ) {

  /*********************************************************************
   * Workspaces
   ********************************************************************/

  fd_topob_wksp( topo, "rpc" );
  fd_topob_wksp( topo, "rpc_replay" );

  /*********************************************************************
   * Tile
   ********************************************************************/

  fd_topo_tile_t * rpc_tile = fd_topob_tile( topo, "rpc", "rpc",
      "metric_in", FD_TOPOB_TILE_USES_ID_KEYSWITCH );

  /*********************************************************************
   * Links — rpc_replay (RPC -> replay)
   ********************************************************************/

  fd_topob_link( topo, "rpc_replay", "rpc_replay", 8UL, 0UL, 1UL );
  fd_topob_tile_out( topo, "rpc", 0UL, "rpc_replay", 0UL );

  /* Wire the replay tile to consume rpc_replay. */
  fd_topob_tile_in( topo, "replay", 0UL, "metric_in",
      "rpc_replay", 0UL, FD_TOPOB_RELIABLE, FD_TOPOB_POLLED );

  /*********************************************************************
   * Subscribe to core links
   ********************************************************************/

  fd_topob_subscribe( topo, "rpc", 1, "replay_out", "1.0.0",
      FD_TOPOB_RELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "rpc", 1, "genesi_out", "1.0.0",
      FD_TOPOB_RELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "rpc", 1, "gossip_out", "1.0.0",
      FD_TOPOB_RELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );

  /*********************************************************************
   * Shared objects — funk, funk_locks
   *
   * These are created by the core topology; their object IDs are
   * stored in topo->props.
   ********************************************************************/

  ulong funk_id = fd_pod_query_ulong( topo->props, "funk", ULONG_MAX );
  if( funk_id!=ULONG_MAX ) {
    fd_topob_tile_uses( topo, rpc_tile,
        &topo->objs[ funk_id ], FD_SHMEM_JOIN_MODE_READ_ONLY );
  }

  ulong funk_locks_id = fd_pod_query_ulong( topo->props, "funk_locks", ULONG_MAX );
  if( funk_locks_id!=ULONG_MAX ) {
    fd_topob_tile_uses( topo, rpc_tile,
        &topo->objs[ funk_locks_id ], FD_SHMEM_JOIN_MODE_READ_WRITE );
  }

  /*********************************************************************
   * Shared objects — accdb (vinyl)
   *
   * When the vinyl account database is enabled, the core topology
   * creates accdb_data and stores its object ID in topo->props
   * under "accdb.data".  The RPC tile needs read-only access to
   * the account data cache and a vinyl request queue for account
   * lookups.
   ********************************************************************/

  ulong accdb_data_id = fd_pod_query_ulong( topo->props, "accdb.data", ULONG_MAX );
  if( accdb_data_id!=ULONG_MAX ) {
    fd_topob_tile_uses( topo, rpc_tile,
        &topo->objs[ accdb_data_id ], FD_SHMEM_JOIN_MODE_READ_ONLY );

    /* Wire the vinyl request queue so the RPC tile can issue
       account lookups through the accdb tile.  Only attempt
       this if the accdb tile exists (i.e. vinyl is enabled). */
    ulong accdb_tile_id = fd_topo_find_tile( topo, "accdb", 0UL );
    if( accdb_tile_id!=ULONG_MAX ) {
      fd_topob_vinyl_rq( topo, "rpc", 0UL, "accdb_rpc", "rpc",
          4UL, 1UL, 1UL, FD_VINYL_PERM_READ_ONLY );
    }
  }
}