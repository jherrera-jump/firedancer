#ifndef HEADER_fd_src_disco_topo_fd_topo_cfg_h
#define HEADER_fd_src_disco_topo_fd_topo_cfg_h

/* fd_topo_cfg.h provides convenience wrappers for reading
   config values from topo->props.  Included by tiles that
   use dynamic config via the props pod.

   Two families of accessors:

     fd_topo_cfg_<type>          required value.  FD_LOG_ERR
                                 if the key is missing.

     fd_topo_cfg_<type>_checked  optional value.  Returns 1
                                 if present (writes to *out),
                                 0 if absent (*out untouched).

   Both families work identically for core and plugin tiles. */

#include "../../util/pod/fd_pod.h"

FD_PROTOTYPES_BEGIN

/* ---- Required (fail-fast) variants ---- */

static inline ulong
fd_topo_cfg_ulong( uchar const * props,
                   char const *  key ) {
  ulong val = fd_pod_query_ulong( props, key, ULONG_MAX );
  if( FD_UNLIKELY( val==ULONG_MAX ) )
    FD_LOG_ERR(( "missing required config `%s` in topo props", key ));
  return val;
}

static inline uint
fd_topo_cfg_uint( uchar const * props,
                  char const *  key ) {
  uint val = fd_pod_query_uint( props, key, UINT_MAX );
  if( FD_UNLIKELY( val==UINT_MAX ) )
    FD_LOG_ERR(( "missing required config `%s` in topo props", key ));
  return val;
}

static inline ushort
fd_topo_cfg_ushort( uchar const * props,
                    char const *  key ) {
  ushort val = fd_pod_query_ushort( props, key, USHORT_MAX );
  if( FD_UNLIKELY( val==USHORT_MAX ) )
    FD_LOG_ERR(( "missing required config `%s` in topo props", key ));
  return val;
}

static inline long
fd_topo_cfg_long( uchar const * props,
                  char const *  key ) {
  long val = fd_pod_query_long( props, key, LONG_MAX );
  if( FD_UNLIKELY( val==LONG_MAX ) )
    FD_LOG_ERR(( "missing required config `%s` in topo props", key ));
  return val;
}

static inline int
fd_topo_cfg_int( uchar const * props,
                 char const *  key ) {
  int val = fd_pod_query_int( props, key, INT_MAX );
  if( FD_UNLIKELY( val==INT_MAX ) )
    FD_LOG_ERR(( "missing required config `%s` in topo props", key ));
  return val;
}

static inline char const *
fd_topo_cfg_cstr( uchar const * props,
                  char const *  key ) {
  char const * val = fd_pod_query_cstr( props, key, NULL );
  if( FD_UNLIKELY( !val ) )
    FD_LOG_ERR(( "missing required config `%s` in topo props", key ));
  return val;
}

/* ---- Checked (optional) variants ----
   Return 1 if the key is present (value written to *out),
   0 if absent (*out is not modified). */

static inline int
fd_topo_cfg_ulong_checked( uchar const * props,
                           char const *  key,
                           ulong *       out ) {
  ulong val = fd_pod_query_ulong( props, key, ULONG_MAX );
  if( FD_UNLIKELY( val==ULONG_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_uint_checked( uchar const * props,
                          char const *  key,
                          uint *        out ) {
  uint val = fd_pod_query_uint( props, key, UINT_MAX );
  if( FD_UNLIKELY( val==UINT_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_ushort_checked( uchar const * props,
                            char const *  key,
                            ushort *      out ) {
  ushort val = fd_pod_query_ushort( props, key, USHORT_MAX );
  if( FD_UNLIKELY( val==USHORT_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_long_checked( uchar const * props,
                          char const *  key,
                          long *        out ) {
  long val = fd_pod_query_long( props, key, LONG_MAX );
  if( FD_UNLIKELY( val==LONG_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_int_checked( uchar const * props,
                         char const *  key,
                         int *         out ) {
  int val = fd_pod_query_int( props, key, INT_MAX );
  if( FD_UNLIKELY( val==INT_MAX ) ) return 0;
  *out = val;
  return 1;
}

static inline int
fd_topo_cfg_cstr_checked( uchar const *  props,
                          char const *   key,
                          char const * * out ) {
  char const * val = fd_pod_query_cstr( props, key, NULL );
  if( FD_UNLIKELY( !val ) ) return 0;
  *out = val;
  return 1;
}

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_disco_topo_fd_topo_cfg_h */