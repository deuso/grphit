#include "l2_cache_cntlr.h"
#include "memory_manager.h"
#include "directory_type.h"
//#include "l2_directory_cfg.h"
#include "directory_entry.h"
#include "l2_cache_replacement_policy.h"
#include "l2_cache_hash_fn.h"
#include "config.h"
#include "log.h"

#define TYPE(shmem_req)    (shmem_req->getShmemMsg()->getType())

namespace PrL1ShL2SpDirMSI
{

L2CacheCntlr::L2CacheCntlr(MemoryManager* memory_manager,
                           AddressHomeLookup* dram_home_lookup,
                           UInt32 cache_line_size,
                           UInt32 L2_cache_size,
                           UInt32 L2_cache_associativity,
                           UInt32 L2_cache_num_banks,
                           string L2_cache_replacement_policy,
                           UInt32 L2_cache_data_access_cycles,
                           UInt32 L2_cache_tags_access_cycles,
                           string L2_cache_perf_model_type,
                           bool L2_cache_track_miss_types,

                           string sparse_directory_total_entries_str,
                           UInt32 sparse_directory_associativity,
                           UInt32 sparse_directory_max_num_sharers,
                           UInt32 sparse_directory_max_hw_sharers,
                           string sparse_directory_type_str,
                           string sparse_directory_access_cycles_str,
                           UInt32 num_dir_cntlrs)
   : _memory_manager(memory_manager)
   , _dram_home_lookup(dram_home_lookup)
   , _enabled(false)
{
   _spdir_cache = new DirectoryCache(_memory_manager->getTile(),
         PR_L1_SH_L2_SPDIR_MSI,
         sparse_directory_type_str,
         sparse_directory_total_entries_str,
         sparse_directory_associativity,
         cache_line_size,
         sparse_directory_max_hw_sharers,
         sparse_directory_max_num_sharers,
         num_dir_cntlrs,
         sparse_directory_access_cycles_str,
         getShmemPerfModel());

   _L2_cache_replacement_policy_obj =
      new L2CacheReplacementPolicy(L2_cache_size, L2_cache_associativity, cache_line_size, _L2_cache_req_queue, _spdir_cache);
   _L2_cache_hash_fn_obj = new L2CacheHashFn(L2_cache_size, L2_cache_associativity, cache_line_size);

   // L2 cache
   _L2_cache = new Cache("L2",
         PR_L1_SH_L2_SPDIR_MSI,
         Cache::UNIFIED_CACHE,
         L2,
         Cache::WRITE_BACK,
         L2_cache_size, 
         L2_cache_associativity, 
         cache_line_size,
         L2_cache_num_banks,
         _L2_cache_replacement_policy_obj,
         _L2_cache_hash_fn_obj,
         L2_cache_data_access_cycles,
         L2_cache_tags_access_cycles,
         L2_cache_perf_model_type,
         L2_cache_track_miss_types,
         getShmemPerfModel());

}

L2CacheCntlr::~L2CacheCntlr()
{
   // Some eviction requests
   LOG_ASSERT_ERROR(_L2_cache_req_queue.size() == _evicted_cache_line_map.size(),
                    "Req list size(%u), Evicted cache line map size(%u)", _L2_cache_req_queue.size(), _evicted_cache_line_map.size());

   delete _L2_cache;
   delete _L2_cache_replacement_policy_obj;
   delete _L2_cache_hash_fn_obj;
   delete _spdir_cache;
}

DirectoryEntry* L2CacheCntlr::processDirectoryEntryAllocationReq(IntPtr address)
{
   //IntPtr address = shmem_req->getShmemMsg()->getAddress();
   //tile_id_t requester = shmem_req->getShmemMsg()->getRequester();
   Time msg_time = getShmemPerfModel()->getCurrTime();

   std::vector<DirectoryEntry*> replacement_candidate_list;
   _spdir_cache->getReplacementCandidates(address, replacement_candidate_list);

   std::vector<DirectoryEntry*>::iterator it;
   std::vector<DirectoryEntry*>::iterator replacement_candidate = replacement_candidate_list.end();
   for (it = replacement_candidate_list.begin(); it != replacement_candidate_list.end(); it++)
   {
      if ( ( (replacement_candidate == replacement_candidate_list.end()) ||
                  ((*replacement_candidate)->getNumSharers() > (*it)->getNumSharers()) 
           )
               &&
               (_L2_cache_req_queue.count((*it)->getAddress()) == 0)
         )
      {
         replacement_candidate = it;
      }
   }

   LOG_ASSERT_ERROR(replacement_candidate != replacement_candidate_list.end(),
            "Cant find a directory entry to be replaced with a non-zero request list");

   IntPtr replaced_address = (*replacement_candidate)->getAddress();

   // We get the entry with the lowest number of sharers
   DirectoryEntry* directory_entry = _spdir_cache->replaceDirectoryEntry(replaced_address, address);

   // The NULLIFY requests are always modeled in the network
   bool msg_modeled = true;
   ShmemMsg nullify_msg(ShmemMsg::NULLIFY_REQ, MemComponent::L2_CACHE, MemComponent::L2_CACHE, getTileId(), false,  replaced_address, msg_modeled);

   ShmemReq* sp_nullify_req = new ShmemReq(&nullify_msg, msg_time);
   //_sparse_directory_req_queue.enqueue(replaced_address, sp_nullify_req);

   // Insert the nullify_req into the set of requests to be processed
   _L2_cache_req_queue.enqueue(replaced_address, sp_nullify_req);
   
   // Insert the evicted cache line info into the evicted cache line map for future reference
   //_evicted_cache_line_map.insert(make_pair(replaced_address, evicted_cache_line_info));

   assert(_L2_cache_req_queue.count(replaced_address) == 1);
   processNullifyReq(sp_nullify_req, NULL);

   return directory_entry;
}

bool
L2CacheCntlr::getCacheLineInfo(IntPtr address, ShL2CacheLineInfo* L2_cache_line_info, ShmemMsg::Type shmem_msg_type, tile_id_t sender, bool first_call)
{
   map<IntPtr,ShL2CacheLineInfo>::iterator it = _evicted_cache_line_map.find(address);
   if (it == _evicted_cache_line_map.end())
   {
      //assert(shmem_msg_type != ShmemMsg::NULLIFY_REQ);
      // Read it from the cache
      _L2_cache->getCacheLineInfo(address, L2_cache_line_info);

      //DGD: block entry and region entry is not settled in the same half of ways, but they are simple hashed
      //address[10] == 1 indicates to find region entry in odd ways and block entry in even ways
      //address[10] == 0 indicates to find region entry in even ways and block entry in odd ways
      DirectoryEntry* directory_entry_region = _spdir_cache->getDirectoryEntryRegion(address, true);
      DirectoryEntry* directory_entry_block = _spdir_cache->getDirectoryEntry(address, true);

      bool cache_hit = (L2_cache_line_info->getCState() != CacheState::INVALID);
      bool spdir_region_own = (directory_entry_region->getOwner()==sender);
      bool spdir_hit_region = (directory_entry_region != NULL);
      bool spdir_hit_block = directory_entry_block != NULL;
      bool spdir_hit = spdir_hit_region || spdir_hit_block;

      if((shmem_msg_type == ShmemMsg::INV_REP) || (shmem_msg_type == ShmemMsg::FLUSH_REP) || (shmem_msg_type == ShmemMsg::WB_REP))
         assert(cache_hit && spdir_hit);
         
      if(shmem_msg_type == ShmemMsg::NULLIFY_REQ)//SP_NULLIFY_REQ
         assert(cache_hit);
      //some asserts to ensure the correctness of coherence protocol
      //assert(!cache_miss || spdir_miss);//sp-hit,cache-miss not allowed
      assert(first_call || (!first_call && cache_hit && spdir_hit));//non first_call should hit both
      if (first_call)
      {
         Core::mem_op_t mem_op_type = getMemOpTypeFromShmemMsgType(shmem_msg_type);
         _L2_cache->updateMissCounters(address, mem_op_type, !cache_hit);
         //_spdir_cache->updateXX();
      }

      //----------------set up dir-ptr in l2 entry------------------
      if(cache_hit)
      {
         //region hit, l2 should point to dir-entry
         if(spdir_hit_region && !spdir_hit_block) {
            if(spdir_region_own)
            {
               if(L2_cache_line_info->getDirectoryEntry() != directory_entry_region)
               {
                  assert(L2_cache_line_info->getDirectoryEntry()==NULL);
                  //LOG_ASSERT_WARNING(L2_cache_line_info->getDirectoryEntry()!=NULL, "l2 dir entry null, sp-dir entry 0x%x", directory_entry);
                  //rebuild connection of l2 entry and sp-dir entry
                  L2_cache_line_info->setDirectoryEntry(directory_entry_region);
               }
            }
            else //non-owner req hit a region and miss a block(cache hit)
            {
               //cannot be REP
               assert((shmem_msg_type == ShmemMsg::EX_REQ) || (shmem_msg_type == ShmemMsg::SH_REQ));
               //the dir ptr of cacheline may be null if not referenced or point to region entry if privately referenced
               assert(L2_cache_line_info->getDirectoryEntry()==directory_entry_region || L2_cache_line_info->getDirectoryEntry()==NULL);
               //allocate block entry
               directory_entry_block = _spdir_cache->getDirectoryEntry(address);
               if(!directory_entry_block)
                  directory_entry_block = processDirectoryEntryAllocationReqBlock(address);

               L2_cache_line_info->setDirectoryEntry(directory_entry_block);
            }
         }
         //block hit, l2 has ptr to dir-entry
         if(spdir_hit_block)
         {
            if(L2_cache_line_info->getDirectoryEntry() != directory_entry_block)
            {
               assert(L2_cache_line_info->getDirectoryEntry()==NULL);
               //LOG_ASSERT_WARNING(L2_cache_line_info->getDirectoryEntry()!=NULL, "l2 dir entry null, sp-dir entry 0x%x", directory_entry);
               //rebuild connection of l2 entry and sp-dir entry
               L2_cache_line_info->setDirectoryEntry(directory_entry_block);
            }
         }
         if(!spdir_hit)//l2-hit, sp-miss, l2's dir-ptr is null, alloc region entry
         {
            assert(L2_cache_line_info->getDirectoryEntry() == NULL);
            directory_entry_region = _spdir_cache->getDirectoryEntryRegion(address);
            if(!directory_entry_region)
               directory_entry_region = processDirectoryEntryAllocationReqRegion(address);
         }
      }
      else//!cache_hit
      {
         DirectoryEntry* directory_entry;
         assert((shmem_msg_type == ShmemMsg::EX_REQ) || (shmem_msg_type == ShmemMsg::SH_REQ));
         //region hit, l2 should point to dir-entry
         if(spdir_hit_region && !spdir_hit_block)
         {
            if(spdir_region_own)
            {
               directory_entry = directory_entry_region;
            }
            else //non-owner req hit a region and miss a block(cache miss)
            {
               //allocate block entry
               directory_entry = _spdir_cache->getDirectoryEntry(address);
               if(!directory_entry)
                  directory_entry = processDirectoryEntryAllocationReqBlock(address);
            }

         }
         else if(!spdir_hit)
         {
            //alloc region entry on first access
            directory_entry= _spdir_cache->getDirectoryEntryRegion(address);
            if(!directory_entry)
               directory_entry= processDirectoryEntryAllocationReqRegion(address);
            // allocate a cache line with garbage data (now the dir-entry exists)
         }
         else
         {
            //block hit and cache miss not possible
            assert(!spdir_hit_block);
            LOG_PRINT_ERROR("Unrecognized shmem msg type(%u)", shmem_msg_type);
         }

         allocateCacheLine(address, L2_cache_line_info, directory_entry);
      }

      return true;
   }
   else // (present in the evicted map [_evicted_cache_line_map])
   {
      assert(!first_call);
      // Read it from the evicted cache line map
      L2_cache_line_info->assign(&it->second);
      return false;
   }
}

bool
L2CacheCntlr::setCacheLineInfo(IntPtr address, ShL2CacheLineInfo* L2_cache_line_info)
{
   map<IntPtr,ShL2CacheLineInfo>::iterator it = _evicted_cache_line_map.find(address);
   if (it == _evicted_cache_line_map.end())
   {
      // Write it to the cache
      _L2_cache->setCacheLineInfo(address, L2_cache_line_info);
      return true;
   }
   else
   {
      // Write it to the evicted cache line map
      (it->second).assign(L2_cache_line_info);
      return false;
   }
}

void
L2CacheCntlr::readCacheLine(IntPtr address, Byte* data_buf)
{
  _L2_cache->accessCacheLine(address, Cache::LOAD, data_buf, getCacheLineSize());
}

void
L2CacheCntlr::writeCacheLine(IntPtr address, Byte* data_buf)
{
   _L2_cache->accessCacheLine(address, Cache::STORE, data_buf, getCacheLineSize());
}

void
L2CacheCntlr::allocateCacheLine(IntPtr address, ShL2CacheLineInfo* L2_cache_line_info, DirectoryEntry* directory_entry)
{
   assert(directory_entry!=NULL);
   // Construct meta-data info about L2 cache line
   *L2_cache_line_info = ShL2CacheLineInfo(_L2_cache->getTag(address), directory_entry);
   // Transition to a new cache state
   L2_cache_line_info->setCState(CacheState::DATA_INVALID);

   // Evicted Line Information
   bool eviction;
   IntPtr evicted_address;
   ShL2CacheLineInfo evicted_cache_line_info;
   Byte writeback_buf[getCacheLineSize()];

   _L2_cache->insertCacheLine(address, L2_cache_line_info, (Byte*) NULL,
                              &eviction, &evicted_address, &evicted_cache_line_info, writeback_buf);

   if (eviction)
   {
      assert(evicted_cache_line_info.isValid());
      LOG_ASSERT_ERROR(_L2_cache_req_queue.empty(evicted_address),
                       "Address(%#lx) is already being processed", evicted_address);
      LOG_ASSERT_ERROR(evicted_cache_line_info.getCState() == CacheState::CLEAN || evicted_cache_line_info.getCState() == CacheState::DIRTY,
                       "Cache Line State(%u)", evicted_cache_line_info.getCState());
      __attribute__((unused)) DirectoryEntry* evicted_directory_entry = evicted_cache_line_info.getDirectoryEntry();
      //LOG_ASSERT_ERROR(evicted_directory_entry, "Cant find directory entry for address(%#lx)", evicted_address);
      //LOG_ASSERT_WARNING(evicted_directory_entry, "No directory entry in eviction of address(%#lx) @L2", evicted_address);

      bool msg_modeled = Config::getSingleton()->isApplicationTile(getTileId());
      Time eviction_time = getShmemPerfModel()->getCurrTime();
      
      if(evicted_directory_entry)
      {
         LOG_PRINT("Eviction: Address(%#lx), Cache State(%u), Directory State(%u), Num Sharers(%i)",
                   evicted_address, evicted_cache_line_info.getCState(),
                   evicted_directory_entry->getDirectoryBlockInfo()->getDState(), evicted_directory_entry->getNumSharers());
      }
      else
      {
         LOG_PRINT("Eviction: Address(%#lx), Cache State(%u), No Directory Entry.",
                   evicted_address, evicted_cache_line_info.getCState());
      }

      // Create a nullify req and add it onto the queue for processing
      ShmemMsg nullify_msg(ShmemMsg::NULLIFY_REQ, MemComponent::L2_CACHE, MemComponent::L2_CACHE,
                           getTileId(), false, evicted_address,
                           msg_modeled); 
      // Create a new ShmemReq for removing the sharers of the evicted cache line
      ShmemReq* nullify_req = new ShmemReq(&nullify_msg, eviction_time);
      // Insert the nullify_req into the set of requests to be processed
      _L2_cache_req_queue.enqueue(evicted_address, nullify_req);
      
      // Insert the evicted cache line info into the evicted cache line map for future reference
      _evicted_cache_line_map.insert(make_pair(evicted_address, evicted_cache_line_info));
     
      // Process the nullify req
      processNullifyReq(nullify_req, writeback_buf);
   }
   else
   {
      assert(!evicted_cache_line_info.isValid());
      assert(evicted_cache_line_info.getDirectoryEntry() == NULL);
   }
}

void
L2CacheCntlr::handleMsgFromL1Cache(tile_id_t sender, ShmemMsg* shmem_msg)
{
   // add synchronization cost
   if (sender == getTileId()){
      getShmemPerfModel()->incrCurrTime(_L2_cache->getSynchronizationDelay(DVFSManager::convertToModule(shmem_msg->getSenderMemComponent())));
   }
   else{
      getShmemPerfModel()->incrCurrTime(_L2_cache->getSynchronizationDelay(NETWORK_MEMORY));
   }

   // Incr current time for every message that comes into the L2 cache
   _memory_manager->incrCurrTime(MemComponent::L2_CACHE, CachePerfModel::ACCESS_DATA_AND_TAGS);

   ShmemMsg::Type shmem_msg_type = shmem_msg->getType();
   Time msg_time = getShmemPerfModel()->getCurrTime();
   IntPtr address = shmem_msg->getAddress();
   
   if ( (shmem_msg_type == ShmemMsg::EX_REQ) || (shmem_msg_type == ShmemMsg::SH_REQ) )
   {
      // Add request onto a queue
      ShmemReq* shmem_req = new ShmemReq(shmem_msg, msg_time);
      _L2_cache_req_queue.enqueue(address, shmem_req);

      if (_L2_cache_req_queue.count(address) == 1)
      {
         // Process the request
         processShmemReq(shmem_req);
      }
   }

   else if ( (shmem_msg_type == ShmemMsg::INV_REP) || (shmem_msg_type == ShmemMsg::FLUSH_REP) || (shmem_msg_type == ShmemMsg::WB_REP) )
   {
      // Get the ShL2CacheLineInfo object
      ShL2CacheLineInfo L2_cache_line_info;
      //TODO:need to allocate dir-entry if icache-rep?
      getCacheLineInfo(address, &L2_cache_line_info, shmem_msg_type, sender);
      assert(L2_cache_line_info.isValid());

      if (L2_cache_line_info.getCachingComponent() != shmem_msg->getSenderMemComponent())
      {
         // LOG_PRINT_WARNING("Address(%#lx) removed from (L1-ICACHE)", address);
         LOG_ASSERT_ERROR(shmem_msg->getSenderMemComponent() == MemComponent::L1_ICACHE,
                          "Msg'ing component(%s)", SPELL_MEMCOMP(shmem_msg->getSenderMemComponent()));
         LOG_ASSERT_ERROR(L2_cache_line_info.getCachingComponent() == MemComponent::L1_DCACHE,
                          "Caching component(%s), Valid(%s), State(%s)",
                          SPELL_MEMCOMP(L2_cache_line_info.getCachingComponent()), L2_cache_line_info.isValid() ? "true" : "false",
                          SPELL_CSTATE(L2_cache_line_info.getCState()));
         assert(shmem_msg_type == ShmemMsg::INV_REP);
         // Drop the message
      }
      else // (L2_cache_line_info.getCachingComponent() == shmem_msg->getSenderMemComponent())
      {
         // I either find the cache line in the evicted_cache_line_map or in the L2 cache
         switch (shmem_msg_type)
         {
         case ShmemMsg::INV_REP:
            processInvRepFromL1Cache(sender, shmem_msg, &L2_cache_line_info);
            break;
         case ShmemMsg::FLUSH_REP:
            processFlushRepFromL1Cache(sender, shmem_msg, &L2_cache_line_info);
            break;
         case ShmemMsg::WB_REP:
            processWbRepFromL1Cache(sender, shmem_msg, &L2_cache_line_info);
            break;
         default:
            LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
            break;
         }
      }

      // Update the evicted cache line map or the L2 cache directly
      setCacheLineInfo(address, &L2_cache_line_info);

      // Get the latest request for the data (if any) and process it
      if (!_L2_cache_req_queue.empty(address))
      {
         ShmemReq* shmem_req = _L2_cache_req_queue.front(address);
         restartShmemReq(shmem_req, &L2_cache_line_info, shmem_msg->getDataBuf());
      }
   }

   else
   {
      LOG_PRINT_ERROR("Unrecognized shmem msg type(%u)", shmem_msg_type);
   }
}

void
L2CacheCntlr::handleMsgFromDram(tile_id_t sender, ShmemMsg* shmem_msg)
{
   // Incr curr time for every message that comes into the L2 cache
   _memory_manager->incrCurrTime(MemComponent::L2_CACHE, CachePerfModel::ACCESS_DATA_AND_TAGS);

   IntPtr address = shmem_msg->getAddress();

   ShL2CacheLineInfo L2_cache_line_info;
   _L2_cache->getCacheLineInfo(address, &L2_cache_line_info);

   // Write the data into the L2 cache if it is a SH_REQ
   ShmemReq* shmem_req = _L2_cache_req_queue.front(address);
   if (TYPE(shmem_req) == ShmemMsg::SH_REQ)
      writeCacheLine(address, shmem_msg->getDataBuf());
   else
      LOG_ASSERT_ERROR(TYPE(shmem_req) == ShmemMsg::EX_REQ, "Type(%u)", TYPE(shmem_req));

   // Set Cache State to CLEAN (irrespective of whether the data is written)
   L2_cache_line_info.setCState(CacheState::CLEAN);
   // Write-back the cache line info
   setCacheLineInfo(address, &L2_cache_line_info);

   // Restart the shmem request
   restartShmemReq(shmem_req, &L2_cache_line_info, shmem_msg->getDataBuf());
}

void
L2CacheCntlr::processNextReqFromL1Cache(IntPtr address)
{
   LOG_PRINT("Start processNextReqFromL1Cache(%#lx)", address);
   
   // Add 1 cycle to denote that we are moving to the next request
   getShmemPerfModel()->incrCurrTime(Latency(1,_L2_cache->getFrequency()));

   assert(_L2_cache_req_queue.count(address) >= 1);
   
   // Get the completed shmem req
   ShmemReq* completed_shmem_req = _L2_cache_req_queue.dequeue(address);

   // Delete the completed shmem req
   delete completed_shmem_req;

   if (!_L2_cache_req_queue.empty(address))
   {
      LOG_PRINT("A new shmem req for address(%#lx) found", address);
      ShmemReq* shmem_req = _L2_cache_req_queue.front(address);

      // Update the Shared Mem current time appropriately
      shmem_req->updateTime(getShmemPerfModel()->getCurrTime());
      getShmemPerfModel()->updateCurrTime(shmem_req->getTime());

      assert(TYPE(shmem_req) != ShmemMsg::NULLIFY_REQ);
      // Process the request
      processShmemReq(shmem_req);
   }

   LOG_PRINT("End processNextReqFromL1Cache(%#lx)", address);
}

void
L2CacheCntlr::processShmemReq(ShmemReq* shmem_req)
{
   ShmemMsg::Type msg_type = TYPE(shmem_req);

   // Process the request
   switch (msg_type)
   {
   case ShmemMsg::EX_REQ:
      processExReqFromL1Cache(shmem_req, NULL, true);
      break;
   case ShmemMsg::SH_REQ:
      processShReqFromL1Cache(shmem_req, NULL, true);
      break;
   default:
      LOG_PRINT_ERROR("Unrecognized Shmem Msg Type(%u)", TYPE(shmem_req));
      break;
   }
}

void
L2CacheCntlr::processNullifyReq(ShmemReq* nullify_req, Byte* data_buf)
{
   IntPtr address = nullify_req->getShmemMsg()->getAddress();
   tile_id_t requester = nullify_req->getShmemMsg()->getRequester();
   bool msg_modeled = nullify_req->getShmemMsg()->isModeled();

   // get cache line info 
   ShL2CacheLineInfo L2_cache_line_info;
   bool sp_nullify = getCacheLineInfo(address, &L2_cache_line_info, ShmemMsg::NULLIFY_REQ, -1);

   assert(L2_cache_line_info.getCState() != CacheState::INVALID);

   DirectoryEntry* directory_entry = L2_cache_line_info.getDirectoryEntry();
   DirectoryState::Type curr_dstate;
   if(directory_entry)
      curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();
   else
      curr_dstate = DirectoryState::UNCACHED;//not in sp-dir


   // Is the request completely processed or waiting for acknowledgements or data?
   bool completed = false;

   switch (curr_dstate)
   {
   case DirectoryState::MODIFIED:
      {
         assert(L2_cache_line_info.getCachingComponent() == MemComponent::L1_DCACHE);
         ShmemMsg shmem_msg(ShmemMsg::FLUSH_REQ, MemComponent::L2_CACHE, MemComponent::L1_DCACHE,
                            requester, false, address,
                            msg_modeled);
         _memory_manager->sendMsg(directory_entry->getOwner(), shmem_msg);
      }
      break;

   case DirectoryState::SHARED:
      {
         LOG_ASSERT_ERROR(directory_entry->getOwner() == INVALID_TILE_ID,
                          "Address(%#lx), State(SHARED), owner(%i)", address, directory_entry->getOwner());
         LOG_ASSERT_ERROR(directory_entry->getNumSharers() > 0, 
                          "Address(%#lx), Directory State(SHARED), Num Sharers(%u)",
                          address, directory_entry->getNumSharers());
         
         vector<tile_id_t> sharers_list;
         bool all_tiles_sharers = directory_entry->getSharersList(sharers_list);
         
         sendInvalidationMsg(ShmemMsg::NULLIFY_REQ,
                             address, L2_cache_line_info.getCachingComponent(),
                             all_tiles_sharers, sharers_list,
                             requester, msg_modeled);

         // Send line to DRAM_CNTLR if dirty
         if ((L2_cache_line_info.getCState() == CacheState::DIRTY) && data_buf && !sp_nullify)
            storeDataInDram(address, data_buf, requester, msg_modeled);
      }
      break;

   case DirectoryState::UNCACHED:
      {
         // Send line to DRAM_CNTLR if dirty
         if ((L2_cache_line_info.getCState() == CacheState::DIRTY) && data_buf && !sp_nullify)
            storeDataInDram(address, data_buf, requester, msg_modeled);

         // Set completed to true
         completed = true;
      }
      break;

   default:
      LOG_PRINT_ERROR("Unsupported Directory State: %u", curr_dstate);
      break;
   }

   if (completed)
   {
      if(sp_nullify)
      {
         //Set dir entry to invalid
         L2_cache_line_info.setDirectoryEntry(NULL);
         bool not_in_evict_map = setCacheLineInfo(address, &L2_cache_line_info);
         assert(not_in_evict_map);
         _spdir_cache->invalidateDirectoryEntry(address);
      }
      else
      {
         if(L2_cache_line_info.getDirectoryEntry())
         {
            L2_cache_line_info.getDirectoryEntry()->setAddress(INVALID_ADDRESS);
         }
         bool not_in_evict_map = setCacheLineInfo(address, &L2_cache_line_info);
         assert(!not_in_evict_map);
         // Remove the address from the evicted map since its handling is complete
         _evicted_cache_line_map.erase(address);
         //_spdir_cache->invalidateDirectoryEntry(address);
      }

      // Process the next request if completed
      processNextReqFromL1Cache(address);
   }
}

void
L2CacheCntlr::processExReqFromL1Cache(ShmemReq* shmem_req, Byte* data_buf, bool first_call)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   tile_id_t requester = shmem_req->getShmemMsg()->getRequester();
   __attribute__((unused)) MemComponent::Type requester_mem_component = shmem_req->getShmemMsg()->getSenderMemComponent();
   assert(requester_mem_component == MemComponent::L1_DCACHE);
   bool msg_modeled = shmem_req->getShmemMsg()->isModeled();

   ShL2CacheLineInfo L2_cache_line_info;
   getCacheLineInfo(address, &L2_cache_line_info, ShmemMsg::EX_REQ, requester, first_call);
 
   assert(L2_cache_line_info.getCState() != CacheState::INVALID);

   // Is the request completely processed or waiting for acknowledgements or data?
   bool completed = false;

   if (L2_cache_line_info.getCState() != CacheState::DATA_INVALID)
   {
      // Data need not be fetched from DRAM
      // The cache line is present in the L2 cache
      DirectoryEntry* directory_entry = L2_cache_line_info.getDirectoryEntry();
      assert(directory_entry != NULL);
      DirectoryState::Type curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();

      switch (curr_dstate)
      {
      case DirectoryState::MODIFIED:
         {
            // Flush the owner
            LOG_ASSERT_ERROR(directory_entry->getOwner() != INVALID_TILE_ID,
                             "Address(%#lx), State(MODIFIED), owner(INVALID)", address);
            LOG_ASSERT_ERROR(L2_cache_line_info.getCachingComponent() == MemComponent::L1_DCACHE,
                             "Caching component(%u)", L2_cache_line_info.getCachingComponent());

            ShmemMsg shmem_msg(ShmemMsg::FLUSH_REQ, MemComponent::L2_CACHE, MemComponent::L1_DCACHE,
                               requester, false, address,
                               msg_modeled);
            _memory_manager->sendMsg(directory_entry->getOwner(), shmem_msg);
         }
         break;

      case DirectoryState::SHARED:
         {
            LOG_ASSERT_ERROR(directory_entry->getOwner() == INVALID_TILE_ID,
                             "Address(%#lx), State(SHARED), owner(%i)", address, directory_entry->getOwner());
            LOG_ASSERT_ERROR(directory_entry->getNumSharers() > 0, 
                             "Address(%#lx), Directory State(SHARED), Num Sharers(%u)",
                             address, directory_entry->getNumSharers());
            LOG_ASSERT_ERROR(L2_cache_line_info.getCachingComponent() == MemComponent::L1_DCACHE,
                             "Caching component(%u)", L2_cache_line_info.getCachingComponent());
      
            if ((directory_entry->hasSharer(requester)) && (directory_entry->getNumSharers() == 1))
            {
               // Upgrade miss - shortcut - set state to MODIFIED
               directory_entry->setOwner(requester);
               directory_entry->getDirectoryBlockInfo()->setDState(DirectoryState::MODIFIED);

               ShmemMsg shmem_msg(ShmemMsg::UPGRADE_REP, MemComponent::L2_CACHE, MemComponent::L1_DCACHE,
                                  requester, false, address,
                                  msg_modeled);
               _memory_manager->sendMsg(requester, shmem_msg);
               
               // Set completed to true
               completed = true;
            }
            else
            {
               // Invalidate all the sharers
               vector<tile_id_t> sharers_list;
               bool all_tiles_sharers = directory_entry->getSharersList(sharers_list);
              
               sendInvalidationMsg(ShmemMsg::EX_REQ,
                                   address, MemComponent::L1_DCACHE,
                                   all_tiles_sharers, sharers_list,
                                   requester, msg_modeled);
            }
         }
         break;

      case DirectoryState::UNCACHED:
         {
            assert(directory_entry->getNumSharers() == 0);
          
            // Set caching component 
            L2_cache_line_info.setCachingComponent(MemComponent::L1_DCACHE);

            // Add the sharer and set that as the owner 
            __attribute__((unused)) bool add_result = directory_entry->addSharer(requester);
            assert(add_result);
            directory_entry->setOwner(requester);
            directory_entry->getDirectoryBlockInfo()->setDState(DirectoryState::MODIFIED);

            readCacheLineAndSendToL1Cache(ShmemMsg::EX_REP, address, MemComponent::L1_DCACHE, data_buf, requester, msg_modeled);

            // Set completed to true
            completed = true;
         }
         break;

      default:
         LOG_PRINT_ERROR("Unrecognized DirectoryState(%u)", curr_dstate);
         break;
      }
   }

   else // (!L2_cache_line_info->getCState() == CacheState::DATA_INVALID)
   {
      // Cache line is not present in the L2 cache
      // Send a message to the memory controller asking it to fetch the line from DRAM
      fetchDataFromDram(address, requester, msg_modeled);
   }

   if (completed)
   {
      // Write the cache line info back
      setCacheLineInfo(address, &L2_cache_line_info);

      // Process the next request if completed
      processNextReqFromL1Cache(address);
   }
}

void
L2CacheCntlr::processShReqFromL1Cache(ShmemReq* shmem_req, Byte* data_buf, bool first_call)
{
   IntPtr address = shmem_req->getShmemMsg()->getAddress();
   tile_id_t requester = shmem_req->getShmemMsg()->getRequester();
   // Get the requesting mem component (L1-I/L1-D cache)
   MemComponent::Type requester_mem_component = shmem_req->getShmemMsg()->getSenderMemComponent();
   bool msg_modeled = shmem_req->getShmemMsg()->isModeled();

   ShL2CacheLineInfo L2_cache_line_info;
   getCacheLineInfo(address, &L2_cache_line_info, ShmemMsg::SH_REQ, requester, first_call);
 
   assert(L2_cache_line_info.getCState() != CacheState::INVALID);

   // Is the request completely processed or waiting for acknowledgements or data?
   bool completed = false;

   if (L2_cache_line_info.getCState() != CacheState::DATA_INVALID)
   {
      // Data need not be fetched from DRAM
      // The cache line is present in the L2 cache
      DirectoryEntry* directory_entry = L2_cache_line_info.getDirectoryEntry();
      assert(directory_entry != NULL);
      DirectoryState::Type curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();

      switch (curr_dstate)
      {
      case DirectoryState::MODIFIED:
         {
            LOG_ASSERT_ERROR(directory_entry->getOwner() != INVALID_TILE_ID,
                             "Address(%#lx), State(MODIFIED), owner(INVALID)", address);
            LOG_ASSERT_ERROR(L2_cache_line_info.getCachingComponent() == requester_mem_component,
                             "caching component(%u), requester component(%u)",
                             L2_cache_line_info.getCachingComponent(), requester_mem_component);

            ShmemMsg shmem_msg(ShmemMsg::WB_REQ, MemComponent::L2_CACHE, MemComponent::L1_DCACHE,
                               requester, false, address,
                               msg_modeled);
            _memory_manager->sendMsg(directory_entry->getOwner(), shmem_msg);
         }
         break;

      case DirectoryState::SHARED:
         {
            LOG_ASSERT_ERROR(directory_entry->getNumSharers() > 0, "Address(%#lx), State(%u), Num Sharers(%u)",
                             address, curr_dstate, directory_entry->getNumSharers());
            
            if (L2_cache_line_info.getCachingComponent() != requester_mem_component)
            {
               assert(directory_entry->hasSharer(requester));
               
               // LOG_PRINT_WARNING("Address(%#lx) cached first in (%s), then in (%s)",
               //                   address, SPELL_MEMCOMP(L2_cache_line_info.getCachingComponent()),
               //                   SPELL_MEMCOMP(requester_mem_component));
               L2_cache_line_info.setCachingComponent(MemComponent::L1_DCACHE);
               
               // Read the cache-line from the L2 cache and send it to L1
               readCacheLineAndSendToL1Cache(ShmemMsg::SH_REP, address, requester_mem_component, data_buf, requester, msg_modeled);
               // set completed to true 
               completed = true;
               break;
            }

            LOG_ASSERT_ERROR(L2_cache_line_info.getCachingComponent() == requester_mem_component,
                             "Address(%#lx), Num sharers(%i), caching component(%u), requester component(%u)",
                             address, directory_entry->getNumSharers(),
                             L2_cache_line_info.getCachingComponent(), requester_mem_component);

            // Try to add the sharer to the sharer list
            bool add_result = directory_entry->addSharer(requester);
            if (add_result == false)
            {
               // Invalidate one sharer
               tile_id_t sharer_id = directory_entry->getOneSharer();

               // Invalidate the sharer
               ShmemMsg shmem_msg(ShmemMsg::INV_REQ, MemComponent::L2_CACHE, requester_mem_component,
                                  requester, false, address,
                                  msg_modeled);
               _memory_manager->sendMsg(sharer_id, shmem_msg);
            }
            else // succesfully added the sharer
            {
               // Read the cache-line from the L2 cache and send it to L1
               readCacheLineAndSendToL1Cache(ShmemMsg::SH_REP, address, requester_mem_component, data_buf, requester, msg_modeled);
            
               // set completed to true 
               completed = true; 
            }
         }
         break;

      case DirectoryState::UNCACHED:
         {
            assert(directory_entry->getNumSharers() == 0);
          
            // Set caching component 
            L2_cache_line_info.setCachingComponent(requester_mem_component);
            
            // Modifiy the directory entry contents
            __attribute__((unused)) bool add_result = directory_entry->addSharer(requester);
            LOG_ASSERT_ERROR(add_result, "Address(%#lx), Requester(%i), State(UNCACHED), Num Sharers(%u)",
                             address, requester, directory_entry->getNumSharers());
            directory_entry->getDirectoryBlockInfo()->setDState(DirectoryState::SHARED);

            // Read the cache-line from the L2 cache and send it to L1
            readCacheLineAndSendToL1Cache(ShmemMsg::SH_REP, address, requester_mem_component, data_buf, requester, msg_modeled);
       
            // set completed to true 
            completed = true; 
         }
         break;

      default:
         LOG_PRINT_ERROR("Unsupported Directory State: %u", curr_dstate);
         break;
      }
   }
   
   else // (!L2_cache_line_info->getCState() == CacheState::DATA_INVALID)
   {
      // Cache line is not present in the L2 cache
      // Send a message to the memory controller asking it to fetch the line from DRAM
      fetchDataFromDram(address, requester, msg_modeled);
   }

   if (completed)
   {
      // Write the cache line info back
      setCacheLineInfo(address, &L2_cache_line_info);

      // Process the next request if completed
      processNextReqFromL1Cache(address);
   }
}

void
L2CacheCntlr::processInvRepFromL1Cache(tile_id_t sender, const ShmemMsg* shmem_msg, ShL2CacheLineInfo* L2_cache_line_info)
{
   __attribute__((unused)) IntPtr address = shmem_msg->getAddress();

   DirectoryEntry* directory_entry = L2_cache_line_info->getDirectoryEntry();
   DirectoryState::Type curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();
  
   switch (curr_dstate)
   {
   case DirectoryState::SHARED:
      LOG_ASSERT_ERROR((directory_entry->getOwner() == INVALID_TILE_ID) && (directory_entry->getNumSharers() > 0),
                       "Address(%#lx), State(SHARED), sender(%i), num sharers(%u), owner(%i)",
                       address, sender, directory_entry->getNumSharers(), directory_entry->getOwner());

      // Remove the sharer and set the directory state to UNCACHED if the number of sharers is 0
      directory_entry->removeSharer(sender, shmem_msg->isReplyExpected());
      if (directory_entry->getNumSharers() == 0)
      {
         directory_entry->getDirectoryBlockInfo()->setDState(DirectoryState::UNCACHED);
      }
      break;

   case DirectoryState::MODIFIED:
   case DirectoryState::UNCACHED:
   default:
      LOG_PRINT_ERROR("Address(%#lx), INV_REP, State(%u), num sharers(%u), owner(%i)",
                      address, curr_dstate, directory_entry->getNumSharers(), directory_entry->getOwner());
      break;
   }
}

void
L2CacheCntlr::processFlushRepFromL1Cache(tile_id_t sender, const ShmemMsg* shmem_msg, ShL2CacheLineInfo* L2_cache_line_info)
{
   IntPtr address = shmem_msg->getAddress();

   DirectoryEntry* directory_entry = L2_cache_line_info->getDirectoryEntry();
   DirectoryState::Type curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();
   
   switch (curr_dstate)
   {
   case DirectoryState::MODIFIED:
      {
         LOG_ASSERT_ERROR(sender == directory_entry->getOwner(),
                          "Address(%#lx), State(MODIFIED), sender(%i), owner(%i)",
                          address, sender, directory_entry->getOwner());

         assert(!shmem_msg->isReplyExpected());

         // Write the line to the L2 cache if there is no request (or a SH_REQ)
         ShmemReq* shmem_req = _L2_cache_req_queue.front(address);
         if ( (shmem_req == NULL) || (TYPE(shmem_req) == ShmemMsg::SH_REQ) )
         {
            writeCacheLine(address, shmem_msg->getDataBuf());
         }
         // Set the line to dirty even if it is logically so
         L2_cache_line_info->setCState(CacheState::DIRTY);

         // Remove the sharer from the directory entry and set state to UNCACHED
         directory_entry->removeSharer(sender, false);
         directory_entry->setOwner(INVALID_TILE_ID);
         directory_entry->getDirectoryBlockInfo()->setDState(DirectoryState::UNCACHED);
      }
      break;

   case DirectoryState::SHARED:
   case DirectoryState::UNCACHED:
   default:
      LOG_PRINT_ERROR("Address(%#lx), FLUSH_REP, Sender(%i), Sender mem component(%u), State(%u), num sharers(%u), owner(%i)",
                      address, sender, shmem_msg->getSenderMemComponent(),
                      curr_dstate, directory_entry->getNumSharers(), directory_entry->getOwner());
      break;
   }
}

void
L2CacheCntlr::processWbRepFromL1Cache(tile_id_t sender, const ShmemMsg* shmem_msg, ShL2CacheLineInfo* L2_cache_line_info)
{
   IntPtr address = shmem_msg->getAddress();

   DirectoryEntry* directory_entry = L2_cache_line_info->getDirectoryEntry();
   DirectoryState::Type curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();

   assert(!shmem_msg->isReplyExpected());

   switch (curr_dstate)
   {
   case DirectoryState::MODIFIED:
      {
         LOG_ASSERT_ERROR(sender == directory_entry->getOwner(),
                          "Address(%#lx), sender(%i), owner(%i)", address, sender, directory_entry->getOwner());
         LOG_ASSERT_ERROR(!_L2_cache_req_queue.empty(address),
                          "Address(%#lx), WB_REP, req queue empty!!", address);

         // Write the data into the L2 cache and mark the cache line dirty
         writeCacheLine(address, shmem_msg->getDataBuf());
         L2_cache_line_info->setCState(CacheState::DIRTY);

         // Set the directory state to SHARED
         directory_entry->setOwner(INVALID_TILE_ID);
         directory_entry->getDirectoryBlockInfo()->setDState(DirectoryState::SHARED);
      }
      break;

   case DirectoryState::SHARED:
   case DirectoryState::UNCACHED:
   default:
      LOG_PRINT_ERROR("Address(%#llx), WB_REP, State(%u), num sharers(%u), owner(%i)",
                      address, curr_dstate, directory_entry->getNumSharers(), directory_entry->getOwner());
      break;
   }
}

void
L2CacheCntlr::restartShmemReq(ShmemReq* shmem_req, ShL2CacheLineInfo* L2_cache_line_info, Byte* data_buf)
{
   // Add 1 cycle to denote that we are restarting the request
   getShmemPerfModel()->incrCurrTime(Latency(1, _L2_cache->getFrequency()));

   // Update ShmemReq & ShmemPerfModel internal time
   shmem_req->updateTime(getShmemPerfModel()->getCurrTime());
   getShmemPerfModel()->updateCurrTime(shmem_req->getTime());

   DirectoryEntry* directory_entry = L2_cache_line_info->getDirectoryEntry();
   DirectoryState::Type curr_dstate = directory_entry->getDirectoryBlockInfo()->getDState();

   ShmemMsg::Type msg_type = TYPE(shmem_req);
   switch (msg_type)
   {
   case ShmemMsg::EX_REQ:
      if (curr_dstate == DirectoryState::UNCACHED)
         processExReqFromL1Cache(shmem_req, data_buf);
      break;

   case ShmemMsg::SH_REQ:
      processShReqFromL1Cache(shmem_req, data_buf);
      break;

   case ShmemMsg::NULLIFY_REQ:
      if (curr_dstate == DirectoryState::UNCACHED)
         processNullifyReq(shmem_req, data_buf);
      break;

   default:
      LOG_PRINT_ERROR("Unrecognized request type(%u)", msg_type);
      break;
   }
}

void
L2CacheCntlr::sendInvalidationMsg(ShmemMsg::Type requester_msg_type,
                                  IntPtr address, MemComponent::Type receiver_mem_component,
                                  bool all_tiles_sharers, vector<tile_id_t>& sharers_list,
                                  tile_id_t requester, bool msg_modeled)
{
   if (all_tiles_sharers)
   {
      //bool reply_expected = (L2DirectoryCfg::getDirectoryType() == LIMITED_BROADCAST);
      bool reply_expected = (_spdir_cache->getDirectoryType() == LIMITED_BROADCAST);
      // Broadcast invalidation request to all tiles 
      // (irrespective of whether they are sharers or not)
      ShmemMsg shmem_msg(ShmemMsg::INV_REQ, MemComponent::L2_CACHE, receiver_mem_component, 
                         requester, reply_expected, address,
                         msg_modeled);
      _memory_manager->broadcastMsg(shmem_msg);
   }
   else // not all tiles are sharers
   {
      // Send Invalidation Request to only a specific set of sharers
      for (UInt32 i = 0; i < sharers_list.size(); i++)
      {
         ShmemMsg shmem_msg(ShmemMsg::INV_REQ, MemComponent::L2_CACHE, receiver_mem_component,
                            requester, false, address,
                            msg_modeled);
         _memory_manager->sendMsg(sharers_list[i], shmem_msg);
      }
   }
}

void
L2CacheCntlr::readCacheLineAndSendToL1Cache(ShmemMsg::Type reply_msg_type,
                                            IntPtr address, MemComponent::Type requester_mem_component,
                                            Byte* data_buf,
                                            tile_id_t requester, bool msg_modeled)
{
   if (data_buf != NULL)
   {
      // I already have the data I need cached (by reply from an owner)
      ShmemMsg shmem_msg(reply_msg_type, MemComponent::L2_CACHE, requester_mem_component,
                         requester, false, address, 
                         data_buf, getCacheLineSize(),
                         msg_modeled);
      _memory_manager->sendMsg(requester, shmem_msg);
   }
   else
   {
      // Read the data from L2 cache
      Byte L2_data_buf[getCacheLineSize()];
      readCacheLine(address, L2_data_buf);
      
      ShmemMsg shmem_msg(reply_msg_type, MemComponent::L2_CACHE, requester_mem_component,
                         requester, false, address,
                         L2_data_buf, getCacheLineSize(),
                         msg_modeled);
      _memory_manager->sendMsg(requester, shmem_msg); 
   }
}

void
L2CacheCntlr::fetchDataFromDram(IntPtr address, tile_id_t requester, bool msg_modeled)
{
   ShmemMsg fetch_msg(ShmemMsg::DRAM_FETCH_REQ, MemComponent::L2_CACHE, MemComponent::DRAM_CNTLR,
                      requester, false, address,
                      msg_modeled);
   _memory_manager->sendMsg(getDramHome(address), fetch_msg);
}

void
L2CacheCntlr::storeDataInDram(IntPtr address, Byte* data_buf, tile_id_t requester, bool msg_modeled)
{
   ShmemMsg send_msg(ShmemMsg::DRAM_STORE_REQ, MemComponent::L2_CACHE, MemComponent::DRAM_CNTLR,
                     requester, false, address,
                     data_buf, getCacheLineSize(),
                     msg_modeled);
   _memory_manager->sendMsg(getDramHome(address), send_msg);
}

Core::mem_op_t
L2CacheCntlr::getMemOpTypeFromShmemMsgType(ShmemMsg::Type shmem_msg_type)
{
   switch (shmem_msg_type)
   {
   case ShmemMsg::SH_REQ:
      return Core::READ;
   case ShmemMsg::EX_REQ:
      return Core::WRITE;
   default:
      LOG_PRINT_ERROR("Unrecognized Msg(%u)", shmem_msg_type);
      return Core::READ;                 
   }
}

tile_id_t
L2CacheCntlr::getTileId()
{
   return _memory_manager->getTile()->getId();
}

UInt32
L2CacheCntlr::getCacheLineSize()
{ 
   return _memory_manager->getCacheLineSize();
}
 
ShmemPerfModel*
L2CacheCntlr::getShmemPerfModel()
{ 
   return _memory_manager->getShmemPerfModel();
}

}
