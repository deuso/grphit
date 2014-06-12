#include "RTracker.h"
#include "log.h"
#include "config.h"
#include "utils.h"


namespace PrL1ShL2SpDirMSI
{

RTracker::RTracker(DirectoryCache* spdir)
   :_spdir(spdir)
{
	initializeCounters();
}

int RTracker::accessRTracker(tile_id_t sender, const ShmemMsg* shmem_msg)
{

   RT_info* rt_info;

   IntPtr address = shmem_msg->getAddress();
   ShmemMsg::Type shmem_msg_type = shmem_msg->getType();

   size_t count = _rt_map.count(address);
   assert(count==0 || count==1);
   if ( (shmem_msg_type == ShmemMsg::EX_REQ) || (shmem_msg_type == ShmemMsg::SH_REQ) )
   {
      if(count==0)//no entry
      {
         rt_info= new RT_info(sender,false);
         _rt_map.enqueue(address, rt_info);
      } else {
      	//if state is 0 but entry exists, it was previously accessed
         rt_info = _rt_map.front(address);
         assert(rt_info);
         if(!rt_info->sharing) rt_info->owner = sender;//new sharing start

         if((sender != rt_info->owner) && rt_info->sharing)
         {
            rt_info->non_tp = true;
         }
      }
      if(rt_info->state == 0) rt_info->state = shmem_msg_type;
      if(shmem_msg_type == ShmemMsg::EX_REQ) rt_info->state = 2;
      rt_info->sharing = true;
      rt_info->refset.insert(sender);
   }
   else if ( (shmem_msg_type == ShmemMsg::INV_REP) || (shmem_msg_type == ShmemMsg::FLUSH_REP))
   {
      if(sender == shmem_msg->getRequester())//last eviction happens
      {
         LOG_ASSERT_ERROR(count==1, "accessRTracker 0x%x, tile %d, count %d", address,sender,count);

         rt_info = _rt_map.front(address);
         assert(rt_info);

         rt_info->sharing = false;
      } else {
         LOG_PRINT_WARNING("RTracker msg indirect");
      }
   } else {
      LOG_PRINT_ERROR("RTracker msg error 2");
   }
   return 1;

}

void RTracker::initializeCounters()
{
   _total_blocks = 0;
   _total_rp_blocks = 0;
   _total_wp_blocks = 0;
   _total_tp_blocks = 0;
   _total_ts_blocks = 0;
   _total_wtp_blocks = 0;
   _total_wts_blocks = 0;

}

void
RTracker::comp_blocks()
{
	RT_info* rt_info;
	map<IntPtr,RT_info*>::iterator it;
	for(it=_rt_map.begin(); it!=_rt_map.end(); ++it)
	{
		rt_info = it->second;
		int refcnt = rt_info->refset.size();
		int state = rt_info->state;
		assert(refcnt>=1 && state>0 && state<=2);
		bool nontp = rt_info->non_tp;
		if(refcnt==1)
		{
			if(state==1)
			   _total_rp_blocks++;
			else
			   _total_wp_blocks++;
		}
		else
		{
			if(nontp)
			{
				if(state==1)
				   _total_ts_blocks++;
				else
					_total_wts_blocks++;
			}
			else
			{
				if(state==1)
					_total_tp_blocks++;
				else
					_total_wtp_blocks++;
			}
		}
		_total_blocks++;
	}
}

void
RTracker::outputSummary(ostream& out)
{
   comp_blocks();
   out << "    Total blocks: " << _total_blocks << endl;
   out << "      Private read Blocks: " << _total_rp_blocks << endl;
   out << "      Private write Blocks: " << _total_wp_blocks << endl;
   out << "      Temp-private blocks: " << _total_tp_blocks << endl;
   out << "      Temp-shared blocks: " << _total_ts_blocks << endl;
   out << "      Temp-private write blocks: " << _total_wtp_blocks << endl;
   out << "      Temp-shared write blocks: " << _total_wts_blocks << endl;


   // Output Power and Area Summaries
   if (Config::getSingleton()->getEnablePowerModeling() || Config::getSingleton()->getEnableAreaModeling())
   {
      // FIXME: Get total cycles from core model
      // _mcpat_cache_interface->computeEnergy(this, 10000);
      // _mcpat_cache_interface->outputSummary(out);
   }

   // Asynchronous communication
   DVFSManager::printAsynchronousMap(out, _module, _asynchronous_map);
}

ShmemPerfModel*
RTracker::getShmemPerfModel()
{
   return _tile->getMemoryManager()->getShmemPerfModel();
}

}
