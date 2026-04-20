/* fd_gui_topo.c — GUI plugin topology function.

   Detects whether the topology is Firedancer or Frankendancer
   at runtime and subscribes to the appropriate core links.
   Called twice by fd_topob_plugin_dispatch (two-pass). */

#include "../../src/disco/topo/fd_topob.h"

void
fd_gui_topo( fd_topo_t * topo ) {

  fd_topob_wksp( topo, "gui" );
  fd_topob_tile( topo, "gui", "gui", "metric_in",
      FD_TOPOB_TILE_CRITICAL |
      FD_TOPOB_TILE_USES_ID_KEYSWITCH );

  /* Detect topology variant.  Firedancer has a "replay" tile;
     Frankendancer has a "pohh" tile. */

  int is_firedancer = ( fd_topo_find_tile( topo, "replay", 0UL )!=ULONG_MAX );

  if( is_firedancer ) {

    /* Firedancer GUI link subscriptions */

    fd_topob_subscribe( topo, "gui", 1, "net_gossvf",    "1.0.0", FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "repair_net",    "1.0.0", FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "shred_out",     "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "gossip_net",    "1.0.0", FD_TOPOB_UNRELIABLE, FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "gossip_out",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "tower_out",     "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "replay_out",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "replay_epoch",  "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "genesi_out",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "pack_poh",      "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "pack_execle",   "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "execle_poh",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "execrp_replay", "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "snapct_gui",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "snapin_gui",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "snapin_manif",  "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "bundle_status", "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );

  } else {

    /* Frankendancer GUI link subscriptions */

    fd_topob_subscribe( topo, "gui", 1, "plugin_out",    "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "pohh_pack",     "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "pack_bank",     "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "pack_pohh",     "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "bank_pohh",     "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );
    fd_topob_subscribe( topo, "gui", 1, "bundle_status", "1.0.0", FD_TOPOB_RELIABLE,   FD_TOPOB_POLLED, FD_TOPOB_REQUIRED );

  }
}