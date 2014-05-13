#pragma once

#include "../cache/cache_line_info.h"
#include "cache_level.h"
#include "directory_entry.h"

namespace PrL1ShL2SpDirMSI
{

CacheLineInfo* createCacheLineInfo(SInt32 cache_level);

typedef CacheLineInfo PrL1CacheLineInfo;

class ShL2CacheLineInfo : public CacheLineInfo
{
public:
   ShL2CacheLineInfo(IntPtr tag = ~0, bool spdir= false);
   ~ShL2CacheLineInfo();

   void assign(CacheLineInfo* cache_line_info);

   bool getSpDir() const
   { return _spdir; }
   void setSpDir(bool spdir)
   { _spdir = spdir; }

private:
   bool _spdir;
};
}
