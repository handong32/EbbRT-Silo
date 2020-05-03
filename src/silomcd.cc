#include <ebbrt/Debug.h>
#include <ebbrt/EbbAllocator.h>
#include <ebbrt/SharedIOBufRef.h>
#include <ebbrt/StaticSharedEbb.h>
#include <ebbrt/UniqueIOBuf.h>

#include <ebbrt/EventManager.h>
#include <ebbrt/Future.h>
#include <ebbrt/LocalIdMap.h>
#include <ebbrt/SpinBarrier.h>
#include <ebbrt/native/Cpu.h>
#include <ebbrt/native/Multiboot.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <string>
#include <set>

#include "Memcached.h"

#define MCDPORT 11211

using namespace util;
using namespace std;

/*int silotpcc_exec_one(int thread_id)
{
        auto worker = workers[thread_id];
	auto workload = worker->get_workload();

	double d = worker->get_r()->next_uniform();
	KPRINTF("d = %lf\n", d);
	for (size_t i = 0; i < workload.size(); i++) {
		if ((i + 1) == workload.size() || d < workload[i].frequency) {
		  KPRINTF("frequency = %lf\n", workload[i].frequency);
		  workload[i].fn(worker);
		  break;
		}
		d -= workload[i].frequency;
	}
	return 1;
	}*/


void AppMain() {
  KPRINTF("silomcd Start\n");

  nthreads = 15;
  scale_factor = 15;
  pin_cpus = 0;
  verbose = 1;

  vector<string> logfiles;
  vector<vector<unsigned>> assignments;
  int nofsync = 0;
  int do_compress = 0;
  int fake_writes = 0;
  
  db = new ndb_wrapper<transaction_proto2>(
    logfiles, assignments, !nofsync, do_compress, fake_writes);
  ALWAYS_ASSERT(!transaction_proto2_static::get_hack_status());
  
  runner = new my_bench_runner(db);

  KPRINTF("db=0x%X runner=0x%X\n", db, runner);

  const vector<bench_loader *> loaders = runner->call_make_loaders();
  for (vector<bench_loader *>::const_iterator it = loaders.begin(); it != loaders.end(); ++it) {
    (*it)->start();
  }

  db->do_txn_epoch_sync();
  auto persisted_info = db->get_ntxn_persisted();
  assert(get<0>(persisted_info) == get<1>(persisted_info));
  db->reset_ntxn_persisted();
  persisted_info = db->get_ntxn_persisted();
  ALWAYS_ASSERT(get<0>(persisted_info) == 0 && get<1>(persisted_info) == 0 && get<2>(persisted_info) == 0.0);

  int wk = 0;
  // This is a hack to access protected members of classes defined in silo
  for (auto w: runner->call_make_workers()) {    
    wk ++;
    workers.push_back((my_bench_worker *) w);
    KPRINTF("wk=%d call_make_workers w=0x%X\n", wk, w);
  }

  KPRINTF("workers=0x%X\n", workers);

  auto id = ebbrt::ebb_allocator->AllocateLocal();
  auto mc = ebbrt::EbbRef<ebbrt::Memcached>(id);
  mc->Start(MCDPORT, workers);
  ebbrt::kprintf("Memcached server listening on port %d\n", MCDPORT);
  
  
  /*for(int i = 0; i < 10; i ++) {
    silotpcc_exec_one(0);
    }*/
  
  KPRINTF("silomcd End\n");
}

