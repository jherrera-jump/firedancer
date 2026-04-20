PLUGIN_NAME      := rpc
PLUGIN_TOPO_SRCS := fd_rpc_topo
include plugin/Plugin.mk

# The tile run struct fd_tile_rpc is defined in
# src/discof/rpc/fd_rpc_tile.c and compiled into fd_discof.
# The plugin registers the struct name but does not compile the
# tile source itself.
PLUGIN_TILE_RUNS_rpc := fd_tile_rpc