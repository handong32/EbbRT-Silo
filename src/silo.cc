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

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include <boost/algorithm/string.hpp>

#include "allocator.h"
#include "benchmarks/bench.h"
#include "benchmarks/tpcc.h"
#include "benchmarks/ndb_wrapper.h"
#include "benchmarks/ndb_wrapper_impl.h"
#include "benchmarks/kvdb_wrapper.h"
#include "benchmarks/kvdb_wrapper_impl.h"

//#include "alarm.h"
using namespace std;
using namespace util;

static vector<string>
split_ws(const string &s)
{
  vector<string> r;
  istringstream iss(s);
  copy(istream_iterator<string>(iss),
       istream_iterator<string>(),
       back_inserter<vector<string>>(r));
  return r;
}

static size_t
parse_memory_spec(const string &s)
{
  string x(s);
  size_t mult = 1;
  if (x.back() == 'G') {
    mult = static_cast<size_t>(1) << 30;
    x.pop_back();
  } else if (x.back() == 'M') {
    mult = static_cast<size_t>(1) << 20;
    x.pop_back();
  } else if (x.back() == 'K') {
    mult = static_cast<size_t>(1) << 10;
    x.pop_back();
  }
  return strtoul(x.c_str(), nullptr, 10) * mult;
}

void AppMain() {
  //auto cmdline = std::string(ebbrt::multiboot::CmdLine());
  //KPRINTF("Silo main, args: %s\n", cmdline.c_str());
  KPRINTF("Silo main start\n");

  // for (uint32_t i = 0; i < 16; i++) {
  // ebbrt::event_manager->SpawnRemote(
  //   [i] () mutable {
  //     auto alrm = new Alarm(49159);
  //     alrm->enable_timer();
  //     printf("******* Alarm started on core %u\n", i);
  //   }, i);
  //   }
  
  abstract_db *db = NULL;
  void (*test_fn)(abstract_db *) = NULL;
  string bench_type = "ycsb";
  string db_type = "ndb-proto2";
  string bench_opts;
  size_t numa_memory = 0;
  int saw_run_spec = 0;
  int nofsync = 0;
  int do_compress = 0;
  int fake_writes = 0;
  int disable_gc = 0;
  int disable_snapshots = 0;
  string stats_server_sockfile;
  vector<string> logfiles;
  vector<vector<unsigned>> assignments;

  // std::vector<std::string> strs;
  // boost::split(strs, cmdline, boost::is_any_of(" "));
  // int argc = strs.size();
  // char **argv = (char **) malloc (argc * sizeof(char **));
  // for(int i = 0; i < argc; i ++)
  // {
  //   argv[i] = (char*)strs[i].c_str();
  //   }
  
  int argc = 8;
  char *argv[argc] = {"silo.elf32", "--verbose", "--bench", "tpcc", "--num-threads", "15", "--scale-factor", "15"};

  
  while (1) {
    static struct option long_options[] =
    {
      {"verbose"                    , no_argument       , &verbose                   , 1}   ,
      {"parallel-loading"           , no_argument       , &enable_parallel_loading   , 1}   ,
      {"pin-cpus"                   , no_argument       , &pin_cpus                  , 1}   ,
      {"slow-exit"                  , no_argument       , &slow_exit                 , 1}   ,
      {"retry-aborted-transactions" , no_argument       , &retry_aborted_transaction , 1}   ,
      {"backoff-aborted-transactions" , no_argument     , &backoff_aborted_transaction , 1}   ,
      {"bench"                      , required_argument , 0                          , 'b'} ,
      {"scale-factor"               , required_argument , 0                          , 's'} ,
      {"num-threads"                , required_argument , 0                          , 't'} ,
      {"db-type"                    , required_argument , 0                          , 'd'} ,
      {"txn-flags"                  , required_argument , 0                          , 'f'} ,
      {"runtime"                    , required_argument , 0                          , 'r'} ,
      {"ops-per-worker"             , required_argument , 0                          , 'n'} ,
      {"bench-opts"                 , required_argument , 0                          , 'o'} ,
      {"numa-memory"                , required_argument , 0                          , 'm'} , // implies --pin-cpus
      {"log-nofsync"                , no_argument       , &nofsync                   , 1}   ,
      {"log-compress"               , no_argument       , &do_compress               , 1}   ,
      {"log-fake-writes"            , no_argument       , &fake_writes               , 1}   ,
      {"disable-gc"                 , no_argument       , &disable_gc                , 1}   ,
      {"disable-snapshots"          , no_argument       , &disable_snapshots         , 1}   ,
      {"stats-server-sockfile"      , required_argument , 0                          , 'x'} ,
      {"no-reset-counters"          , no_argument       , &no_reset_counters         , 1}   ,
      {0, 0, 0, 0}
    };
    int option_index = 0;
    int c = getopt_long(argc, argv, "b:s:t:d:B:f:r:n:o:m:l:a:x:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 0:
      if (long_options[option_index].flag != 0)
        break;
      abort();
      break;

    case 'b':
      bench_type = optarg;
      break;

    case 's':
      scale_factor = strtod(optarg, NULL);
      ALWAYS_ASSERT(scale_factor > 0.0);
      break;

    case 't':
      nthreads = strtoul(optarg, NULL, 10);
      ALWAYS_ASSERT(nthreads > 0);
      break;

    case 'd':
      db_type = optarg;
      break;

    case 'f':
      txn_flags = strtoul(optarg, NULL, 10);
      break;

    case 'r':
      ALWAYS_ASSERT(!saw_run_spec);
      saw_run_spec = 1;
      runtime = strtoul(optarg, NULL, 10);
      ALWAYS_ASSERT(runtime > 0);
      run_mode = RUNMODE_TIME;
      break;

    case 'n':
      ALWAYS_ASSERT(!saw_run_spec);
      saw_run_spec = 1;
      ops_per_worker = strtoul(optarg, NULL, 10);
      ALWAYS_ASSERT(ops_per_worker > 0);
      run_mode = RUNMODE_OPS;

    case 'o':
      bench_opts = optarg;
      break;

    case 'm':
      {
        pin_cpus = 1;
        const size_t m = parse_memory_spec(optarg);
        ALWAYS_ASSERT(m > 0);
        numa_memory = m;
      }
      break;

    case 'x':
      stats_server_sockfile = optarg;
      break;

    case '?':
      // getopt_long already printed an error message.
      //exit(1);
      KPRINTF("error in getopt\n");
      ebbrt::kabort();
    default:
      KPRINTF("error in getopt\n");
      ebbrt::kabort();
    }
  }

  if (verbose) {
    const unsigned long ncpus = scale_factor;
    KPRINTF("Database Benchmark:\n");
    KPRINTF("settings:\n");
    KPRINTF("  par-loading : %d\n", enable_parallel_loading);
    KPRINTF("  pin-cpus    : %d\n", pin_cpus);
    KPRINTF("  slow-exit   : %d\n", slow_exit);
    KPRINTF("  retry-txns  : %d\n", retry_aborted_transaction);
    KPRINTF("  backoff-txns: %d\n", backoff_aborted_transaction);
    KPRINTF("  bench       : %s\n", bench_type.c_str());
    KPRINTF("  scale       : %d\n", static_cast<int>(scale_factor));
    KPRINTF("  num-cpus    : %d\n", ncpus);
    KPRINTF("  num-threads : %d\n", nthreads);
    KPRINTF("  db-type     : %s\n", db_type.c_str());
    if (run_mode == RUNMODE_TIME)
      KPRINTF("  runtime     : %d\n", runtime);
#ifdef USE_VARINT_ENCODING
    KPRINTF("  var-encode  : yes\n");
#else
    KPRINTF("  var-encode  : no\n");
#endif
    
    KPRINTF("  allocator   : libc\n");
    
    if (numa_memory == 0) {
      KPRINTF("  numa-memory : disabled\n");
    }
  }

  if (bench_type == "tpcc") {
    test_fn = tpcc_do_test;
    db = new ndb_wrapper<transaction_proto2>(
      logfiles, assignments, !nofsync, do_compress, fake_writes);
    ALWAYS_ASSERT(!transaction_proto2_static::get_hack_status());

    KPRINTF("system properties:\n");
    KPRINTF("  btree_internal_node_size: %d\n", concurrent_btree::InternalNodeSize());
    KPRINTF("  btree_leaf_node_size    : %d\n", concurrent_btree::LeafNodeSize());   
    test_fn(db);
  }
  else {
    KPRINTF("Abort unknown bench type: %s\n", bench_type);
    ebbrt::kabort();
  }

  KPRINTF("Finished running EbbRT-silo\n");
  ebbrt::kabort();
}


/*#include <ebbrt/Debug.h>
#include <ebbrt/EbbAllocator.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/Msr.h>
#include <ebbrt/native/EventManager.h>
#include <ebbrt/native/Cpu.h>

//#include "UdpCommand.h"

void AppMain()
{
  //ebbrt::kprintf("UdpCommand server listening on port %d\n", 6666);
  auto uid = ebbrt::ebb_allocator->AllocateLocal();
  auto udpc = ebbrt::EbbRef<ebbrt::UdpCommand>(uid);
  udpc->Start(6666);
  ebbrt::kprintf("UdpCommand server listening on port %d\n", 6666); 
  
  for (uint32_t i = 0; i < static_cast<uint32_t>(ebbrt::Cpu::Count()); i++) {
    ebbrt::event_manager->SpawnRemote(
      [i] () mutable {
	// disables turbo boost, thermal control circuit
	ebbrt::msr::Write(IA32_MISC_ENABLE, 0x4000850081);
	// same p state as Linux with performance governor
	ebbrt::msr::Write(IA32_PERF_CTL, 0x1D00);
	ebbrt::kprintf_force("Core %u: disabled turbo boost\n", i);
      }, i);
  }
  ebbrt::clock::SleepMilli(1000);
  
  ebbrt::event_manager->SpawnRemote(
    [] () mutable {            
      auto uid = ebbrt::ebb_allocator->AllocateLocal();
      auto udpc = ebbrt::EbbRef<ebbrt::UdpCommand>(uid);
      udpc->Start(6666);
      ebbrt::kprintf_force("Core %u: UdpCommand server listening on port %d\n", static_cast<uint32_t>(ebbrt::Cpu::GetMine()), 6666);
      }, 1);
}*/


