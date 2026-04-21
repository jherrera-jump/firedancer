#ifndef HEADER_fd_src_disco_topo_fd_topo_cfg_h
#define HEADER_fd_src_disco_topo_fd_topo_cfg_h

/* fd_topo_cfg.h provides convenience wrappers for reading
   config values from topo->props or topo->config.  Included
   by tiles that use dynamic config via a pod.

   Two families of accessors:

     fd_topo_cfg_<type>          required value.  FD_LOG_ERR
                                 if the key is missing.

     fd_topo_cfg_<type>_checked  optional value.  Returns 1
                                 if present (writes to *out),
                                 0 if absent (*out untouched).

   All accessors emit FD_LOG_WARNING if the key exists but is
   stored with a different pod type than expected (e.g. TOML
   stores integers as LONG but the caller asks for ULONG).
   The query still proceeds — fd_pod_query_<type> may or may
   not be able to decode the value depending on the mismatch.

   Both families work identically for core and plugin tiles. */

#include "../../util/pod/fd_pod.h"

FD_PROTOTYPES_BEGIN

/* Helper: warn if key exists with unexpected type.  Returns
   the actual val_type if found, or -1 if not found. */
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

/* ---- Required (fail-fast) variants ---- */

static inline ulong
fd_topo_cfg_ulong( uchar const * props,
                   char const *  key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_ULONG );
  ulong val = fd_pod_query_ulong( props, key, ULONG_MAX );
  if( FD_UNLIKELY( val==ULONG_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline uint
fd_topo_cfg_uint( uchar const * props,
                  char const *  key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_UINT );
  uint val = fd_pod_query_uint( props, key, UINT_MAX );
  if( FD_UNLIKELY( val==UINT_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline ushort
fd_topo_cfg_ushort( uchar const * props,
                    char const *  key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_USHORT );
  ushort val = fd_pod_query_ushort( props, key, USHORT_MAX );
  if( FD_UNLIKELY( val==USHORT_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline long
fd_topo_cfg_long( uchar const * props,
                  char const *  key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_LONG );
  long val = fd_pod_query_long( props, key, LONG_MAX );
  if( FD_UNLIKELY( val==LONG_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline int
fd_topo_cfg_int( uchar const * props,
                 char const *  key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_INT );
  int val = fd_pod_query_int( props, key, INT_MAX );
  if( FD_UNLIKELY( val==INT_MAX ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

static inline char const *
fd_topo_cfg_cstr( uchar const * props,
                  char const *  key ) {
  fd_topo_cfg_check_type( props, key, FD_POD_VAL_TYPE_CSTR );
  char const * val = fd_pod_query_cstr( props, key, NULL );
  if( FD_UNLIKELY( !val ) )
    FD_LOG_ERR(( "missing or unreadable required config `%s` in topo props", key ));
  return val;
}

/* ---- Checked (optional) variants ----
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

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_disco_topo_fd_topo_cfg_h */