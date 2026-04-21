/* fd_rpc_topo.c — RPC plugin topology function.

   Creates the RPC tile, the rpc_replay link, subscribes to core
   links, and wires shared objects (funk, funk_locks, accdb_data,
   vinyl_rq). */

#include "../../src/disco/topo/fd_topob.h"
#include "../../src/disco/topo/fd_topob_vinyl.h"

void
fd_rpc_topo( fd_topo_t * topo ) {

  /*********************************************************************
   * Tile
   ********************************************************************/

  fd_topo_tile_t * rpc_tile = fd_topob_tile( topo, "rpc", 0UL, "rpc", "metric_in", FD_TOPOB_TILE_USES_ID_KEYSWITCH );

  /*********************************************************************
   * Links — rpc_replay (RPC -> replay)
   ********************************************************************/

  fd_topob_publish( topo, "rpc", 1, "rpc_replay", "rpc_replay", "1.0.0", 8UL, 0UL, 1UL );

  /*********************************************************************
   * Subscribe to core links
   ********************************************************************/

  fd_topob_subscribe( topo, "rpc", 1, "replay_out", "1.0.0", FD_TOPOB_RELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "rpc", 1, "genesi_out", "1.0.0", FD_TOPOB_RELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
  fd_topob_subscribe( topo, "rpc", 1, "gossip_out", "1.0.0", FD_TOPOB_RELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );

  /*********************************************************************
   * Shared objects — funk, funk_locks
   *
   * These are created by the core topology; their object IDs are
   * stored in topo->props.
   ********************************************************************/

  fd_topob_tile_uses_obj( topo, rpc_tile, "funk", FD_SHMEM_JOIN_MODE_READ_ONLY );
  fd_topob_tile_uses_obj( topo, rpc_tile, "funk_locks", FD_SHMEM_JOIN_MODE_READ_WRITE );

  /*********************************************************************
   * Shared objects — accdb (vinyl)
   *
   * When the vinyl account database is enabled, the core topology
   * creates accdb_data and stores its object ID in topo->props
   * under "accdb.data".  The RPC tile needs read-only access to
   * the account data cache and a vinyl request queue for account
   * lookups.
   ********************************************************************/

  if( fd_topob_tile_uses_obj( topo, rpc_tile, "accdb.data", FD_SHMEM_JOIN_MODE_READ_ONLY ) )
    fd_topob_vinyl_rq( topo, "rpc", 0UL, "accdb_rpc", "rpc", 4UL, 1UL, 1UL, FD_VINYL_PERM_READ_ONLY );
}
