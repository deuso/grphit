#include "cache_line_info.h"
#include "log.h"

namespace PrL1ShL2SpDirMSI
{

CacheLineInfo* createCacheLineInfo(SInt32 cache_level)
{
   switch (cache_level)
   {
   case L1:
      return new PrL1CacheLineInfo();
   case L2:
      return new ShL2CacheLineInfo();
   default:
      LOG_PRINT_ERROR("Unrecognized Cache Level(%u)", cache_level);
      return (CacheLineInfo*) NULL;
   }
}

// ShL2 CacheLineInfo

ShL2CacheLineInfo::ShL2CacheLineInfo(IntPtr tag, bool spdir)
   : CacheLineInfo(tag, CacheState::INVALID)
   , _spdir(spdir)
{}

ShL2CacheLineInfo::~ShL2CacheLineInfo()
{}

void
ShL2CacheLineInfo::assign(CacheLineInfo* cache_line_info)
{
   CacheLineInfo::assign(cache_line_info);
   ShL2CacheLineInfo* L2_cache_line_info = dynamic_cast<ShL2CacheLineInfo*>(cache_line_info);
   _spdir= L2_cache_line_info->getSpDir();
}

}
