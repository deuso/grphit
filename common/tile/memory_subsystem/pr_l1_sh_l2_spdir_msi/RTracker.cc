#include "RTracker.h"
#include "log.h"
#include "config.h"

namespace PrL1ShL2SpDirMSI
{

RTracker::RTracker(DirectoryCache* spdir)
   :_spdir(spdir)
{}

int RTracker::accessRTracker(tile_id_t sender, const ShmemMsg* shmem_msg)
{

   RT_info* rt_info;

   IntPtr address = shmem_msg->getAddress();
   ShmemMsg::Type shmem_msg_type = shmem_msg->getType();

   size_t count = _rt_map.count(address);

   if ( (shmem_msg_type == ShmemMsg::EX_REQ) || (shmem_msg_type == ShmemMsg::SH_REQ) )
   {
      if(!count)//no entry
      {
         rt_info= new RT_info(sender,false);
         _rt_map.enqueue(address, rt_info);
         //LOG_PRINT_WARNING("accessRTracker alloc 0x%x, owner %d", address,sender);
      } else {
         assert(count == 1);
         rt_info = _rt_map.front(address);
         assert(rt_info);
         if(sender != rt_info->owner)
         {
            rt_info->non_tp = true;
            //if(find(rt_info->refl.begin(), rt_info->refl.end(), sender) == rt_info->refl.end())
            //{
            //   rt_info->refl.push_back(sender);
               //LOG_PRINT_WARNING("accessRTracker add 0x%x, tile %d", address,sender);
            //}
         }
      }
   }
   else if ( (shmem_msg_type == ShmemMsg::INV_REP) || (shmem_msg_type == ShmemMsg::FLUSH_REP) || (shmem_msg_type == ShmemMsg::WB_REP) )
   {
      if(sender == shmem_msg->getRequester())//an eviction happens
      {
         //LOG_PRINT_WARNING("accessRTracker rm 0x%x, tile %d", address,sender);
         assert(count==1);
         rt_info = _rt_map.front(address);
         assert(rt_info);

         //list<tile_id_t>::iterator iter = find(rt_info->refl.begin(), rt_info->refl.end(), sender);
         //assert(iter != rt_info->refl.end());
         //rt_info->refl.erase(iter);

         if((sender == rt_info->owner) && (rt_info->non_tp == false))
         {
            //LOG_PRINT_WARNING("accessRTracker eviction tp");
            //assert(rt_info->refl.size() == 0);
            _spdir->inc_tp_blocks();
            _spdir->inc_blocks();
            delete rt_info;
            _rt_map.dequeue(address);
            return 0;
         }
         if((rt_info->non_tp == true))
         {
            //if(rt_info->refl.size() == 0) 
            //{
               //LOG_PRINT_WARNING("accessRTracker eviction ts");
               _spdir->inc_blocks();
               delete rt_info;
               _rt_map.dequeue(address);
               return 0;
            //}
         }
      } else {
         //LOG_PRINT_WARNING("RTracker msg indirect");
      }
   } else {
      LOG_PRINT_ERROR("RTracker msg error 2");
   }
   return 1;

}

}
