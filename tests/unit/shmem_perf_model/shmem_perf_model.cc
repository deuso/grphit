#include "tile.h"
#include "core.h"
#include "memory_manager.h"

#include "shmem_perf_model.h"

#include "tile_manager.h"
#include "simulator.h"

#include "carbon_user.h"
#include "fixed_types.h"

using namespace std;

int main (int argc, char *argv[])
{
   printf("Starting (shmem_perf_model_unit_test)\n");
   CarbonStartSim(argc, argv);

   UInt32 address[2] = {0x0, 0x1000};

   // 1) Get a tile object
   // 2) Get a memory_manager object from it
   // 3) Do initiateSharedMemReq() on the memory_manager object

   Tile* tile = Sim()->getTileManager()->getTileFromIndex(0);
   MemoryManager* memory_manager = tile->getMemoryManager();
   ShmemPerfModel* shmem_perf_model = memory_manager->getShmemPerfModel();
   Core* core = Sim()->getTileManager()->getTileFromID(0)->getCore();

   Byte data_buf[4];
   bool cache_hit;
   Time shmem_time;

   Time time1;

   // ACCESS - 0
   //shmem_perf_model->setCycleCount(0);
   time1 = shmem_perf_model->getCurrTime();
   cache_hit = memory_manager->__coreInitiateMemoryAccess(MemComponent::L1_DCACHE, Core::NONE, Core::READ, address[0], 0, data_buf, 4);
   shmem_time = shmem_perf_model->getCurrTime()-time1;
   printf("Access(0x%x) - READ : Cache Hit(%s), Shmem Time(%llu)\n", address[0], (cache_hit == true) ? "YES" : "NO", shmem_time);

   // ACCESS - 1
   time1 = shmem_perf_model->getCurrTime();
   cache_hit = memory_manager->__coreInitiateMemoryAccess(MemComponent::L1_DCACHE, Core::NONE, Core::WRITE, address[1], 0, data_buf, 4);
   shmem_time = shmem_perf_model->getCurrTime()-time1;
   printf("Access(0x%x)- WRITE : Cache Hit(%s), Shmem Time(%llu)\n", address[1], (cache_hit == true) ? "YES" : "NO", shmem_time);

   // ACCESS - 2
   time1 = shmem_perf_model->getCurrTime();
   cache_hit = memory_manager->__coreInitiateMemoryAccess(MemComponent::L1_DCACHE, Core::NONE, Core::WRITE, address[0], 0, data_buf, 4);
   shmem_time = shmem_perf_model->getCurrTime()-time1;
   printf("Access(0x%x)- WRITE : Cache Hit(%s), Shmem Time(%llu)\n", address[0], (cache_hit == true) ? "YES" : "NO", shmem_time);

   // ACCESS - 2
   time1 = shmem_perf_model->getCurrTime();
   cache_hit = memory_memager->__coreInitiateMemoryAccess(MemComponent::L1_DCACHE, Core::NONE, Core::READ, address[0], 0, data_buf, 4);
   shmem_time = shmem_perf_model->getCurrTime()-time1;
   printf("Access(0x%x)- READ : Cache Hit(%s), Shmem Time(%llu)\n", address[0], (cache_hit == true) ? "YES" : "NO", shmem_time);

   CarbonStopSim();

   printf("Finished (shmem_perf_model_unit_test) - SUCCESS\n");
   return 0;
}
