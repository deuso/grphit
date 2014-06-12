#pragma once

#include "shmem_msg.h"
#include "fixed_types.h"
#include "directory_cache.h"
#include "hash_map_list.h"
#include "dvfs.h"
#include "dvfs_manager.h"

#include <set>
#include <algorithm>

using std::ostream;
using std::set;

namespace PrL1ShL2SpDirMSI
{

class RTracker
{
public:
   RTracker(DirectoryCache* spdir);
   ~RTracker();
   int accessRTracker(tile_id_t sender, const ShmemMsg* shmsg);
	void outputSummary(ostream& out);

   void enable() { _enabled = true; }
   void disable() { _enabled = false; }

   int getDVFS(double &frequency, double &voltage);
   int setDVFS(double frequency, voltage_option_t voltage_flag, const Time& curr_time);
   Time getSynchronizationDelay(module_t module);

private:
   Tile* _tile;
   bool _enabled;
   DirectoryCache* _spdir;
//   typedef struct req_node
//   {
//   	tile_id_t req;
//   	ShmemMsg::Type type;
//   	req_node(tile_id_t r,ShmemMsg::Type t):req(r),type(t){}
//   } req_node;
   typedef struct tracker_info
   {
       tile_id_t owner;
       //tile_id_t exid; //ex req tile
       bool non_tp;// false if temporarily private
       bool sharing;
       UInt32 state;//0 inv, 1 sh, 2 ex
       set<tile_id_t> refset;
       //tracker_info(tile_id_t o,bool n):owner(o),non_tp(n){refl.push_back(o);}
       tracker_info(tile_id_t o,bool n):owner(o),non_tp(n), state(0){}
   } RT_info;
   HashMapList<IntPtr,RT_info*> _rt_map;
   //map<IntPtr,RT_info*> _rt_map;

   //Counters
   UInt64 _total_blocks;
   UInt64 _total_rp_blocks;//accessed only by one core
   UInt64 _total_wp_blocks;//accessed only by one core
   UInt64 _total_tp_blocks;//shared read by one core, and shared read by another after its eviction
   UInt64 _total_ts_blocks;//shared read by one core, and shared read by another before its eviction
   UInt64 _total_wtp_blocks;//shared read by one core, and shared read by another after its eviction
   UInt64 _total_wts_blocks;//shared read by one core, and shared read by another before its eviction

   void initializeCounters();
	void comp_blocks();

   //DVFS and McPAT related properties
   McPATCacheInterface* _mcpat_cache_interface;
   ShmemPerfModel* getShmemPerfModel();
   double _frequency;
   double _voltage;
   module_t _module;
   DVFSManager::AsynchronousMap _asynchronous_map;
   ShmemPerfModel* _shmem_perf_model;
};

}
