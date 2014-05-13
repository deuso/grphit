#pragma once

#include <string>
using std::string;

// Forward Decls
namespace PrL1ShL2SpDirMSI
{
   class MemoryManager;
}

#include "directory_cache.h"
#include "hash_map_list.h"
#include "dram_cntlr.h"
#include "address_home_lookup.h"
#include "shmem_req.h"
#include "shmem_msg.h"
#include "mem_component.h"

namespace PrL1ShL2SpDirMSI
{
   class SparseDirectoryCntlr
   {
   public:
      SparseDirectoryCntlr(MemoryManager* memory_manager,
            string sparse_directory_total_entries_str,
            UInt32 sparse_directory_associativity,
            UInt32 cache_block_size,
            UInt32 sparse_directory_max_num_sharers,
            UInt32 sparse_directory_max_hw_sharers,
            string sparse_directory_type_str,
            string sparse_directory_access_cycles_str,
            UInt32 num_dir_cntlrs);
      ~SparseDirectoryCntlr();

      void handleMsgFromL1Cache(tile_id_t sender, ShmemMsg* shmem_msg);

      DirectoryCache* getSparseDirectoryCache() { return _sparse_directory_cache; }
   
   private:
      // Functional Models
      MemoryManager* _memory_manager;
      DirectoryCache* _sparse_directory_cache;
      Cache* _L2_cache;

      HashMapList<IntPtr,ShmemReq*> _sparse_directory_req_queue;

      UInt32 getCacheLineSize();
      ShmemPerfModel* getShmemPerfModel();

      // Private Functions
      DirectoryEntry* processDirectoryEntryAllocationReq(ShmemReq* shmem_req);
      void processNullifyReq(ShmemReq* shmem_req);

      void processNextReqFromL1Cache(IntPtr address);
      void processExReqFromL1Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
      void processShReqFromL1Cache(ShmemReq* shmem_req, Byte* cached_data_buf = NULL);
      void retrieveDataAndSendToL1Cache(ShmemMsg::Type reply_msg_type, tile_id_t receiver, IntPtr address, Byte* cached_data_buf, bool msg_modeled);

      void processInvRepFromL1Cache(tile_id_t sender, ShmemMsg* shmem_msg);
      void processFlushRepFromL1Cache(tile_id_t sender, ShmemMsg* shmem_msg);
      void processWbRepFromL1Cache(tile_id_t sender, ShmemMsg* shmem_msg);
      void sendDataToL2(IntPtr address, Byte* data_buf, bool msg_modeled);
   };
}
