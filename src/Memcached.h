//          Copyright Boston University SESA Group 2013 - 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef MEMCACHED_H
#define MEMCACHED_H

#include <memory>
#include <mutex>

#include <ebbrt/AtomicUniquePtr.h>
#include <ebbrt/CacheAligned.h>
#include <ebbrt/SharedIOBufRef.h>
#include <ebbrt/SpinLock.h>
#include <ebbrt/StaticSharedEbb.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/native/RcuTable.h>

#include "protocol_binary.h"

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include <boost/algorithm/string.hpp>

#include "allocator.h"
#include "benchmarks/bench.h"
#include "benchmarks/ndb_wrapper.h"
#include "benchmarks/ndb_wrapper_impl.h"

using namespace util;
using namespace std;

  // These are hacks to access protected members of classes defined in silo
class tpcc_bench_runner : public bench_runner
{
public:
	tpcc_bench_runner(abstract_db *db);
	vector<bench_loader*> make_loaders(void);
	vector<bench_worker*> make_workers(void);
	map<string, vector<abstract_ordered_index *>> partitions;
};

class my_bench_runner : public tpcc_bench_runner
{
public:
	my_bench_runner(abstract_db *db) : tpcc_bench_runner(db) { }
	vector<bench_loader*> call_make_loaders(void)
	{
		return make_loaders();
	}
	vector<bench_worker*> call_make_workers(void)
	{
		return make_workers();
	}
};

class my_bench_worker : public bench_worker
{
public:
	unsigned int get_worker_id(void)
	{
		return worker_id;
	}

	util::fast_random *get_r(void)
	{
		return &r;
	}

	void call_on_run_setup(void)
	{
		on_run_setup();
	}
};

static abstract_db *db;
static my_bench_runner *runner;
static vector<my_bench_worker *> workers;

namespace ebbrt {
class Memcached : public StaticSharedEbb<Memcached>, public CacheAligned {
public:
  Memcached();
  void Start(uint16_t port, std::vector<my_bench_worker*>& tworkers);

private:
  /**
   * GetResponse - response strings are stored as the value of hash table
   * allowing minimal packet construction on a GET response.
   *
   * GetReponse binary format: <ext,key,value> e.g, <0001123>
   */
  class GetResponse {
  public:
    GetResponse();
    /** GetResponse() - store only request string on default path
     */
    GetResponse(std::unique_ptr<IOBuf>);
    /** GetResponse::Binary() - return binary formatted response string.
     * Format the string from original request if it does not exist.
     */
    std::unique_ptr<IOBuf> Binary();
    static std::unique_ptr<MutSharedIOBufRef>
    CreateBinaryResponse(std::unique_ptr<IOBuf> b);
    std::unique_ptr<MutSharedIOBufRef>
    Swap(std::unique_ptr<MutSharedIOBufRef> b);

  private:
    ebbrt::atomic_unique_ptr<MutSharedIOBufRef> binary_response_{nullptr};
  };

  class TableEntry {
  public:
    TableEntry(std::string key, std::unique_ptr<IOBuf> val)
        : key(std::move(key)), value(std::move(val)) {}
    /** Rcu data */
    ebbrt::RcuHListHook hook;
    std::string key;
    GetResponse value;
  };

  class TcpSession : public ebbrt::TcpHandler {
  public:
    TcpSession(Memcached *mcd, ebbrt::NetworkManager::TcpPcb pcb)
        : ebbrt::TcpHandler(std::move(pcb)), mcd_(mcd) {}
    void Close() {}
    void Abort() {}
    void Receive(std::unique_ptr<MutIOBuf> b);

  private:
    std::unique_ptr<ebbrt::MutIOBuf> buf_;
    ebbrt::NetworkManager::TcpPcb pcb_;
    Memcached *mcd_;
  };
  
  std::unique_ptr<IOBuf> ProcessAscii(std::unique_ptr<IOBuf>, std::string);
  std::unique_ptr<IOBuf> ProcessBinary(std::unique_ptr<IOBuf>,
                                       protocol_binary_response_header *);
  static const char *com2str(uint8_t);
  GetResponse *Get(std::unique_ptr<IOBuf>, std::string);
  void Set(std::unique_ptr<IOBuf>, std::string);
  void Quit();
  void Flush();
  NetworkManager::ListeningTcpPcb listening_pcb_;
  RcuHashTable<TableEntry, std::string, &TableEntry::hook, &TableEntry::key>
      table_{13}; // 8k buckets
  ebbrt::SpinLock table_lock_;
  std::vector<my_bench_worker*> tworkers_;
    
  // fixme: below two are binary specific.. for now
  void Nop(protocol_binary_request_header &);
  void Unimplemented(protocol_binary_request_header &);
};
} // namespace ebbrt

#endif // MEMCACHED_H
