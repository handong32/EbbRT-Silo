
//          Copyright Boston University SESA Group 2013 - 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
#include <cstdlib>
#include <sstream>
#include <string>
#include <ebbrt/SharedIOBufRef.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/Msr.h>

#include <ebbrt/Debug.h>
#include <ebbrt/EbbAllocator.h>
#include <ebbrt/StaticSharedEbb.h>

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

#include "benchmarks/tpcc.cc"

#include "UdpCommand.h"

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

ebbrt::UdpCommand::UdpCommand() {}

void ebbrt::UdpCommand::Start(uint16_t port) {
  udp_pcb.Bind(port);
  udp_pcb.Receive([this](Ipv4Address from_addr, uint16_t from_port,
			 std::unique_ptr<MutIOBuf> buf) {
		    ReceiveCommand(from_addr, from_port, std::move(buf));
		  });
  uint32_t mcore = static_cast<uint32_t>(Cpu::GetMine());
  ebbrt::kprintf_force("Core: %u %s\n", mcore, __PRETTY_FUNCTION__);
}

void ebbrt::UdpCommand::ReceiveCommand(
    Ipv4Address from_addr, uint16_t from_port, std::unique_ptr<MutIOBuf> buf) {
  //uint32_t mcore = static_cast<uint32_t>(Cpu::GetMine());
  std::string s(reinterpret_cast<const char*>(buf->Data()));
  std::string delimiter = ",";
  uint32_t pos = 0, param = 0;
  std::string token1, token2;

  pos = s.find(delimiter);
  token1 = s.substr(0, pos);
  token2 = s.substr(pos+1, s.length());
  param = static_cast<uint32_t>(atoi(token2.c_str()));

  /*std::string tmp = "test test test";
  auto newbuf = MakeUniqueIOBuf(tmp.length(), false);
  auto dp = newbuf->GetMutDataPointer();
  std::memcpy(static_cast<void*>(dp.Data()), tmp.data(), tmp.length());*/
      
  if(token1 == "print") {
    network_manager->Config(token1, param);
  }
  else if(token1 == "get") {
    auto re = network_manager->ReadNic();
    if(re.length() % 2 == 1) {
      re += " ";
    }
    ebbrt::kprintf_force("get %d\n", re.length());
    auto newbuf = MakeUniqueIOBuf(re.length(), false);
    auto dp = newbuf->GetMutDataPointer();
    std::memcpy(static_cast<void*>(dp.Data()), re.data(), re.length());
    udp_pcb.SendTo(from_addr, from_port, std::move(newbuf));
  }
  else if(token1 == "performance") {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cpu::Count()); i++) {
      event_manager->SpawnRemote(
	[this, i] () mutable {
	  // disables turbo boost, thermal control circuit
	  ebbrt::msr::Write(IA32_MISC_ENABLE, 0x4000850081);
	  // same p state as Linux with performance governor
	  ebbrt::msr::Write(IA32_PERF_CTL, 0x1D00);
	}, i);
    }
  }
  else if(token1 == "powersave") {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cpu::Count()); i++) {
      event_manager->SpawnRemote(
	[this, i] () mutable {
	  // disables turbo boost, thermal control circuit
	  ebbrt::msr::Write(IA32_MISC_ENABLE, 0x4000850081);
	  // same p state as Linux with powersave governor
	  ebbrt::msr::Write(IA32_PERF_CTL, 0xC00);
	}, i);
    }
  }
  else if(token1 == "cpu_config_read") {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cpu::Count()); i++) {
      event_manager->SpawnRemote(
	[this, i] () mutable {

	  uint64_t tmp1 = ebbrt::msr::Read(IA32_MISC_ENABLE);
	  uint64_t tmp2 = ebbrt::msr::Read(IA32_PERF_STATUS);
	  uint64_t tmp3 = ebbrt::msr::Read(IA32_PERF_CTL);
		  
	  ebbrt::kprintf_force("Core %u: IA32_MISC_ENABLE(0x%X) = 0x%llX IA32_PERF_STATUS(0x%X) = 0x%llX IA32_PERF_CTL(0x%X) = 0x%llX \n", i, IA32_MISC_ENABLE, tmp1, IA32_PERF_STATUS, tmp2, IA32_PERF_CTL, tmp3);
	  
	  //uint64_t tmp = ebbrt::msr::Read(IA32_APIC_BASE);
	  //ebbrt::kprintf_force("Core %u: IA32_APIC_BASE(0x%X) = 0x%llX\n", i, IA32_APIC_BASE, tmp);

	  /*uint64_t tmp = ebbrt::msr::Read(IA32_FEATURE_CONTROL);
	  ebbrt::kprintf_force("Core %u: IA32_FEATURE_CONTROL(0x%X) = 0x%llX\n", i, IA32_FEATURE_CONTROL, tmp);

	  //tmp = ebbrt::msr::Read(IA32_SMM_MONITOR_CTL);
	  //ebbrt::kprintf_force("Core %u: IA32_SMM_MONITOR_CTL(0x%X) = 0x%llX\n", i, IA32_SMM_MONITOR_CTL, tmp);

	  tmp = ebbrt::msr::Read(IA32_MTRRCAP);
	  ebbrt::kprintf_force("Core %u: IA32_MTRRCAP(0x%X) = 0x%llX\n", i, IA32_MTRRCAP, tmp);

	  //tmp = ebbrt::msr::Read(IA32_SYSENTER_CS);
	  //ebbrt::kprintf_force("Core %u: IA32_SYSENTER_CS(0x%X) = 0x%llX\n", i, IA32_SYSENTER_CS, tmp);

	  tmp = ebbrt::msr::Read(IA32_MCG_CAP);
	  ebbrt::kprintf_force("Core %u: IA32_MCG_CAP(0x%X) = 0x%llX\n", i, IA32_MCG_CAP, tmp);
	  
	  tmp = ebbrt::msr::Read(IA32_PERF_STATUS);
	  ebbrt::kprintf_force("Core %u: IA32_PERF_STATUS(0x%X) = 0x%llX\n", i, IA32_PERF_STATUS, tmp);
	  
	  tmp = ebbrt::msr::Read(IA32_PERF_CTL);
	  ebbrt::kprintf_force("Core %u: IA32_PERF_CTL(0x%X) = 0x%llX\n", i, IA32_PERF_CTL, tmp);

	  tmp = ebbrt::msr::Read(IA32_CLOCK_MODULATION);
	  ebbrt::kprintf_force("Core %u: IA32_CLOCK_MODULATION(0x%X) = 0x%llX\n", i, IA32_CLOCK_MODULATION, tmp);

	  tmp = ebbrt::msr::Read(IA32_THERM_INTERRUPT);
	  ebbrt::kprintf_force("Core %u: IA32_THERM_INTERRUPT(0x%X) = 0x%llX\n", i, IA32_THERM_INTERRUPT, tmp);

	  tmp = ebbrt::msr::Read(IA32_THERM_STATUS);
	  ebbrt::kprintf_force("Core %u: IA32_THERM_STATUS(0x%X) = 0x%llX\n", i, IA32_THERM_STATUS, tmp);

	  tmp = ebbrt::msr::Read(IA32_MISC_ENABLE);
	  ebbrt::kprintf_force("Core %u: IA32_MISC_ENABLE(0x%X) = 0x%llX\n", i, IA32_MISC_ENABLE, tmp);

	  tmp = ebbrt::msr::Read(IA32_PACKAGE_THERM_INTERRUPT);
	  ebbrt::kprintf_force("Core %u: IA32_PACKAGE_THERM_INTERRUPT(0x%X) = 0x%llX\n", i, IA32_PACKAGE_THERM_INTERRUPT, tmp);

	  tmp = ebbrt::msr::Read(IA32_PACKAGE_THERM_STATUS);
	  ebbrt::kprintf_force("Core %u: IA32_PACKAGE_THERM_STATUS(0x%X) = 0x%llX\n", i, IA32_PACKAGE_THERM_STATUS, tmp);
	  
	  tmp = ebbrt::msr::Read(IA32_PLATFORM_DCA_CAP);
	  ebbrt::kprintf_force("Core %u: IA32_PLATFORM_DCA_CAP(0x%X) = 0x%llX\n", i, IA32_PLATFORM_DCA_CAP, tmp);

	  tmp = ebbrt::msr::Read(IA32_CPU_DCA_CAP);
	  ebbrt::kprintf_force("Core %u: IA32_CPU_DCA_CAP(0x%X) = 0x%llX\n", i, IA32_CPU_DCA_CAP, tmp);
	  
	  tmp = ebbrt::msr::Read(IA32_DCA_0_CAP);
	  ebbrt::kprintf_force("Core %u: IA32_DCA_0_CAP(0x%X) = 0x%llX\n", i, IA32_DCA_0_CAP, tmp);

	  // sandy bridge only
	  tmp = ebbrt::msr::Read(MSR_PLATFORM_INFO);
	  ebbrt::kprintf_force("Core %u: MSR_PLATFORM_INFO(0x%X) = 0x%llX\n", i, MSR_PLATFORM_INFO, tmp);

	  tmp = ebbrt::msr::Read(MSR_PKG_CST_CONFIG_CONTROL);
	  ebbrt::kprintf_force("Core %u: MSR_PKG_CST_CONFIG_CONTROL(0x%X) = 0x%llX\n", i, MSR_PKG_CST_CONFIG_CONTROL, tmp);

	  tmp = ebbrt::msr::Read(MSR_PMG_IO_CAPTURE_BASE);
	  ebbrt::kprintf_force("Core %u: MSR_PMG_IO_CAPTURE_BASE(0x%X) = 0x%llX\n", i, MSR_PMG_IO_CAPTURE_BASE, tmp);

	  tmp = ebbrt::msr::Read(MSR_TEMPERATURE_TARGET);
	  ebbrt::kprintf_force("Core %u: MSR_TEMPERATURE_TARGET(0x%X) = 0x%llX\n", i, MSR_TEMPERATURE_TARGET, tmp);

	  tmp = ebbrt::msr::Read(MSR_MISC_FEATURE_CONTROL);
	  ebbrt::kprintf_force("Core %u: MSR_MISC_FEATURE_CONTROL(0x%X) = 0x%llX\n", i, MSR_MISC_FEATURE_CONTROL, tmp);
	  */
	}, i);
    }
  } else if(token1 == "start_silo") {
     KPRINTF("Silo main start\n");
  
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
       db = new ndb_wrapper<transaction_proto2>(
	 logfiles, assignments, !nofsync, do_compress, fake_writes);
       ALWAYS_ASSERT(!transaction_proto2_static::get_hack_status());

       KPRINTF("system properties:\n");
       KPRINTF("  btree_internal_node_size: %d\n", concurrent_btree::InternalNodeSize());
       KPRINTF("  btree_leaf_node_size    : %d\n", concurrent_btree::LeafNodeSize());

       tpcc_bench_runner r(db);
       r.run();
  
       //tpcc_do_test(db);
       KPRINTF("Finished running EbbRT-silo\n");
       r.run();
       
       ebbrt::event_manager->SaveContext(context_);
       KPRINTF("context restarted\n");
     }          
  } else if (token1 == "activate") {
    KPRINTF("activate context\n");
    ebbrt::event_manager->ActivateContext(std::move(context_));
  } else {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cpu::Count()); i++) {
      event_manager->SpawnRemote(
	[this, token1, param, i] () mutable {
	  //ebbrt::kprintf_force("SpawnRemote %u\n", i);
	  network_manager->Config(token1, param);
	}, i);
    }
  }  
  //ebbrt::kprintf_force("Core: %u ReceiveCommand() from_port=%u message:%s\n", mcore, from_port, s.c_str());
}
