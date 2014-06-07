#pragma once

#include "shmem_msg.h"
#include "fixed_types.h"
#include "directory_cache.h"
#include "hash_map_list.h"
#include <algorithm>

namespace PrL1ShL2SpDirMSI
{

class RTracker
{
public:
   RTracker(DirectoryCache* spdir);
   ~RTracker();
   int accessRTracker(tile_id_t sender, const ShmemMsg* shmsg);
private:
   DirectoryCache* _spdir;
   typedef struct tracker_info
   {
       tile_id_t owner;
       bool non_tp;
       //list<tile_id_t> refl; 
       //tracker_info(tile_id_t o,bool n):owner(o),non_tp(n){refl.push_back(o);}
       tracker_info(tile_id_t o,bool n):owner(o),non_tp(n){}
   } RT_info;
   HashMapList<IntPtr,RT_info*> _rt_map;

};

}
