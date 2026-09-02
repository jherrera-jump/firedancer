#ifndef HEADER_fd_src_discof_backup_fd_backup_accidx_h
#define HEADER_fd_src_discof_backup_fd_backup_accidx_h

/* fd_backup_accidx.h provides a read-only view of the accdb in-memory
   account index for snapshot production.

   The index is a chained hash map keyed by account address.  acc_map is
   an array of (chain_mask+1) chain heads, each an index into acc_pool
   or UINT_MAX for "end of chain". */

#include "../../flamenco/accdb/fd_accdb_private.h"
#include "../../util/fd_hash32.h"

struct fd_backup_accidx {
  uint const *               acc_map;      /* cold / legacy map chains */
  fd_accdb_accmeta_t const * acc_pool;     /* cold / legacy pool */
  ulong                      max_accounts; /* map ele pool max */
  uint const *               hot_map;
  fd_accdb_accmeta_t const * hot_pool;
  ulong                      hot_max;
  uint                       hot_chain_mask;
  int                        tiered;
  ulong                      seed;         /* map hash function */
  uint                       chain_mask;   /* map chain count - 1 */

  ulong *       epoch_slot; /* epoch announced by this reader */
  ulong const * epoch;      /* accdb global epoch */

  uint root_generation;     /* newest generation in the snapshot */
};

typedef struct fd_backup_accidx fd_backup_accidx_t;

FD_PROTOTYPES_BEGIN

/* fd_backup_accidx_chain returns the acc_map chain that pubkey hashes
   to. */

FD_FN_PURE static inline ulong
fd_backup_accidx_chain( fd_backup_accidx_t const * idx,
                        uchar const                pubkey[ static 32 ] ) {
  return fd_hash32( pubkey, idx->seed ) & idx->chain_mask;
}

/* fd_backup_accidx_valid returns 1 if ele addresses an acc_pool element,
   0 otherwise.  The UINT_MAX chain terminator always fails this test
   because accdb rejects max_accounts>=UINT_MAX at creation, so callers
   walking a chain need only this one bound check. */

FD_FN_PURE static inline int
fd_backup_accidx_valid( fd_backup_accidx_t const * idx,
                        uint                       ele ) {
  if( ele==UINT_MAX ) return 0;
  ulong raw = fd_accdb_acc_ref_idx( ele );
  return fd_accdb_acc_ref_is_hot( ele ) ? (idx->tiered && raw<idx->hot_max) : raw<idx->max_accounts;
}

FD_FN_PURE static inline fd_accdb_accmeta_t const *
fd_backup_accidx_meta( fd_backup_accidx_t const * idx,
                       uint                       ref ) {
  uint raw = fd_accdb_acc_ref_idx( ref );
  return fd_accdb_acc_ref_is_hot( ref ) ? &idx->hot_pool[ raw ] : &idx->acc_pool[ raw ];
}

FD_FN_PURE static inline uint const *
fd_backup_accidx_map( fd_backup_accidx_t const * idx,
                      uint                       ref ) {
  return fd_accdb_acc_ref_is_hot( ref ) ? idx->hot_map : idx->acc_map;
}

FD_FN_PURE static inline uint
fd_backup_accidx_chain_for_ref( fd_backup_accidx_t const * idx,
                                uchar const                pubkey[ static 32 ],
                                uint                       ref ) {
  uint mask = fd_accdb_acc_ref_is_hot( ref ) ? idx->hot_chain_mask : idx->chain_mask;
  return (uint)(fd_hash32( pubkey, idx->seed ) & mask);
}

FD_FN_PURE static inline ulong
fd_backup_accidx_visited_idx( fd_backup_accidx_t const * idx,
                              uint                       ref ) {
  return fd_accdb_acc_ref_is_hot( ref ) ? idx->max_accounts + (ulong)fd_accdb_acc_ref_idx( ref )
                                        : (ulong)fd_accdb_acc_ref_idx( ref );
}

/* fd_backup_accidx_rooted returns 1 if the account version described by
   (generation,lamports) belongs in the snapshot: committed at or below
   the root generation, and not a tombstone. */

FD_FN_PURE static inline int
fd_backup_accidx_rooted( fd_backup_accidx_t const * idx,
                         uint                       generation,
                         ulong                      lamports ) {
  return ( generation<=idx->root_generation ) & ( lamports!=0UL );
}

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_discof_backup_fd_backup_accidx_h */
