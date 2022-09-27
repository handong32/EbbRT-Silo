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
#include "TcpCommand.h"

#define MCDPORT 11211

using namespace util;
using namespace std;

vector<my_bench_worker *> workers;
double dsum[16];
double dfreqsum[16];
long long dworkerloop[16];
long long dprocessbinary[16];
long long dsiloexec[16];
/*uint64_t dins[16];
uint64_t dcyc[16];
uint64_t drefcyc[16];
uint64_t dllcmiss[16];*/

void silotpcc_exec_one(int thread_id)
{
  auto worker = workers[thread_id];
  auto workload = worker->get_workload();
  
  double d = worker->get_r()->next_uniform();
  //KPRINTF("d = %lf\n", d);
  for (size_t i = 0; i < workload.size(); i++) {
    if ((i + 1) == workload.size() || d < workload[i].frequency) {
      //KPRINTF("frequency = %lf\n", workload[i].frequency);
      workload[i].fn(worker);
      break;
    }
    d -= workload[i].frequency;
  }
  return;
}

void AppMain() {
  KPRINTF("%d: silomcd Start\n", static_cast<size_t>(ebbrt::Cpu::GetMine()));
  
  nthreads = 15;
  scale_factor = 15;
  pin_cpus = 0;
  verbose = 1;
  KPRINTF("a\n");
  
  vector<string> logfiles;
  vector<vector<unsigned>> assignments;
  int nofsync = 0;
  int do_compress = 0;
  int fake_writes = 0;
  
  db = new ndb_wrapper<transaction_proto2>(
    logfiles, assignments, !nofsync, do_compress, fake_writes);
  KPRINTF("b\n");
  
  ALWAYS_ASSERT(!transaction_proto2_static::get_hack_status());
  KPRINTF("c\n");
  
  runner = new my_bench_runner(db);
  KPRINTF("d\n");
  
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
    workers.push_back((my_bench_worker *) w);
    KPRINTF("wk=%d call_make_workers w=0x%X\n", wk, w);
    wk ++;
  }

  KPRINTF("workers=0x%X\n", workers);
  uint32_t ncores = static_cast<uint32_t>(ebbrt::Cpu::Count());
  for (uint32_t i = 0; i < ncores-1; i++) {
    ebbrt::Promise<void> p;
    auto f = p.GetFuture();
    ebbrt::event_manager->SpawnRemote(
      [ncores, i, &p] () mutable {
	// disables turbo boost, thermal control circuit
	ebbrt::msr::Write(IA32_MISC_ENABLE, 0x4000850081);
	// same p state as Linux with performance governor
	ebbrt::msr::Write(IA32_PERF_CTL, 0xC00);
	
	uint64_t ii, jj, sum=0, sum2=0;
	for(ii=0;ii<ncores-1;ii++) {
	  silotpcc_exec_one(ii);
	  for(jj=0;jj<IXGBE_LOG_SIZE;jj++) {
	    sum += ixgbe_logs[ii][jj].Fields.tsc;
	  }
	 
	  uint8_t* ptr = bsendbufs[ii]->MutData();
	  for(jj=0;jj<IXGBE_MAX_DATA_PER_TXD;jj++) {
	    sum2 += ptr[ii];
	  }
	}

	ebbrt::kprintf_force("Cpu=%u Sum=%llu Sum2=%llu\n", i, sum, sum2);
	p.SetValue();
      }, i);
    f.Block();
  }

  memset(dsum, 0x0, sizeof(dsum));
  memset(dfreqsum, 0x0, sizeof(dfreqsum));
  memset(dworkerloop, 0x0, sizeof(dworkerloop));
  memset(dprocessbinary, 0x0, sizeof(dprocessbinary));
  memset(dsiloexec, 0x0, sizeof(dsiloexec));
  
  /*memset(dins, 0x0, sizeof(dins));
  memset(dcyc, 0x0, sizeof(dcyc));
  memset(drefcyc, 0x0, sizeof(drefcyc));
  memset(dllcmiss, 0x0, sizeof(dllcmiss));*/
  
  auto id = ebbrt::ebb_allocator->AllocateLocal();
  auto mc = ebbrt::EbbRef<ebbrt::Memcached>(id);
  mc->Start(MCDPORT);
  ebbrt::kprintf("Memcached server listening on port %d\n", MCDPORT);

  auto id2 = ebbrt::ebb_allocator->AllocateLocal();
  auto tcps = ebbrt::EbbRef<ebbrt::TcpCommand>(id2);
  tcps->Start(5002);
  ebbrt::kprintf("TcpCommand server listening on port %d\n", 5002);
}

