#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <string>

#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
//#include <sys/sysinfo.h>

#include "bench.h"
#include "extvar.h"

#include "../counter.h"
#include "../scopedperf.hh"
#include "../allocator.h"

#ifdef USE_JEMALLOC
//cannot include this header b/c conflicts with malloc.h
//#include <jemalloc/jemalloc.h>
extern "C" void malloc_stats_print(void (*write_cb)(void *, const char *), void *cbopaque, const char *opts);
extern "C" int mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
#endif
#ifdef USE_TCMALLOC
#include <google/heap-profiler.h>
#endif

using namespace std;
using namespace util;

size_t nthreads = 15;
volatile bool running = true;
int verbose = 0;
uint64_t txn_flags = 0;
double scale_factor = 15.0;
uint64_t runtime = 30;
uint64_t ops_per_worker = 0;
int run_mode = RUNMODE_TIME;
int enable_parallel_loading = false;
int pin_cpus = 0;
int slow_exit = 0;
int retry_aborted_transaction = 0;
int no_reset_counters = 0;
int backoff_aborted_transaction = 0;

template <typename T>
static void
delete_pointers(const vector<T *> &pts)
{
  for (size_t i = 0; i < pts.size(); i++)
    delete pts[i];
}

template <typename T>
static vector<T>
elemwise_sum(const vector<T> &a, const vector<T> &b)
{
  INVARIANT(a.size() == b.size());
  vector<T> ret(a.size());
  for (size_t i = 0; i < a.size(); i++)
    ret[i] = a[i] + b[i];
  return ret;
}

template <typename K, typename V>
static void
map_agg(map<K, V> &agg, const map<K, V> &m)
{
  for (typename map<K, V>::const_iterator it = m.begin();
       it != m.end(); ++it)
    agg[it->first] += it->second;
}

// returns <free_bytes, total_bytes>
static pair<uint64_t, uint64_t>
get_system_memory_info()
{
  //struct sysinfo inf;
  //sysinfo(&inf);
  //return make_pair(inf.mem_unit * inf.freeram, inf.mem_unit * inf.totalram);
  return make_pair(0,0);
}

static bool
clear_file(const char *name)
{
  ofstream ofs(name);
  ofs.close();
  return true;
}

static void
write_cb(void *p, const char *s) UNUSED;
static void
write_cb(void *p, const char *s)
{
  const char *f = "jemalloc.stats";
  static bool s_clear_file UNUSED = clear_file(f);
  ofstream ofs(f, ofstream::app);
  ofs << s;
  ofs.flush();
  ofs.close();
}

static event_avg_counter evt_avg_abort_spins("avg_abort_spins");

void
bench_worker::run()
{
  size_t mcore;
  ebbrt::rapl::RaplCounter rp;
  ebbrt::perf::PerfCounter nins;
  ebbrt::perf::PerfCounter ncyc;
  ebbrt::perf::PerfCounter nllcm;
  ebbrt::perf::PerfCounter nllcr;

  ninstructions = 0;
  ncycles = 0;
  nllc_miss = 0;
  nllc_ref = 0;
  joules = 0.0;
  mcore = static_cast<size_t>(ebbrt::Cpu::GetMine());

  nins = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::fixed_instructions);
  ncyc = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::fixed_cycles);
  nllcm = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::llc_misses);
  nllcr = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::llc_references);

  if(mcore == 0 || mcore == 1) {
    rp = ebbrt::rapl::RaplCounter();
    rp.Start();
  }

  nins.Start();
  ncyc.Start();
  nllcm.Start();
  nllcr.Start();
  
  on_run_setup();
  scoped_db_thread_ctx ctx(db, false);
  const workload_desc_vec workload = get_workload();
  txn_counts.resize(workload.size());
  bool myrunning = true;

  //auto curd = ebbrt::clock::Wall::Now().time_since_epoch();
  //auto tstart = std::chrono::duration_cast<std::chrono::seconds>(curd).count();
  mycount = 0;
  while (myrunning && (run_mode != RUNMODE_OPS || ntxn_commits < ops_per_worker)) {
    mycount ++;
    double d = r.next_uniform();
    for (size_t i = 0; i < workload.size(); i++) {
      if ((i + 1) == workload.size() || d < workload[i].frequency) {
	retry:
        timer t;
        const unsigned long old_seed = r.get_seed();
        const auto ret = workload[i].fn(this);
        if (likely(ret.first)) {
          ++ntxn_commits;
          latency_numer_us += t.lap();
          backoff_shifts >>= 1;
        } else {
          ++ntxn_aborts;
          if (retry_aborted_transaction && running) {
            if (backoff_aborted_transaction) {
              if (backoff_shifts < 63)
                backoff_shifts++;
              uint64_t spins = 1UL << backoff_shifts;
              spins *= 100; // XXX: tuned pretty arbitrarily
              evt_avg_abort_spins.offer(spins);
              while (spins) {
                nop_pause();
                spins--;
              }
            }
            r.set_seed(old_seed);
            goto retry;
          }
        }
        size_delta += ret.second; // should be zero on abort
        txn_counts[i]++; // txn_counts aren't used to compute throughput (is
                         // just an informative number to print to the console
                         // in verbose mode)
        break;
      }
      d -= workload[i].frequency;
    }
    
    if (mycount > 1000000) {
      myrunning = false;
    }

    /*curd = ebbrt::clock::Wall::Now().time_since_epoch();
    auto tend = std::chrono::duration_cast<std::chrono::seconds>(curd).count();
    if(static_cast<double>(tend - tstart) > 30.0) {
      myrunning = false;
      }*/
  }

  nins.Stop();
  ncyc.Stop();
  nllcm.Stop();
  nllcr.Stop();

  if(mcore == 0 || mcore == 1) {
    rp.Stop();
    joules = rp.Read();
    rp.Clear();
  }
  ninstructions = nins.Read();
  ncycles = ncyc.Read();
  nllc_ref = nllcr.Read();
  nllc_miss = nllcm.Read();

  nins.Clear();
  ncyc.Clear();
  nllcm.Clear();
  nllcr.Clear();
}

void
bench_runner::run()
{
  const vector<bench_loader *> loaders = make_loaders();
  {
    scoped_timer t("dataloading", verbose);
  
    for (vector<bench_loader *>::const_iterator it = loaders.begin();
	 it != loaders.end(); ++it) {
      (*it)->start();
    }
  }
  
  db->do_txn_epoch_sync(); // also waits for worker threads to be persisted
  {
    const auto persisted_info = db->get_ntxn_persisted();
    if (get<0>(persisted_info) != get<1>(persisted_info))
      KPRINTF("ERROR: persisted info\n");
    //ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
    if (verbose)
      cerr << persisted_info << " txns persisted in loading phase" << endl;
  }
  db->reset_ntxn_persisted();
  
  if (!no_reset_counters) {
    event_counter::reset_all_counters(); // XXX: for now - we really should have a before/after loading
    PERF_EXPR(scopedperf::perfsum_base::resetall());
  }
  {
    const auto persisted_info = db->get_ntxn_persisted();
    if (get<0>(persisted_info) != 0 ||
        get<1>(persisted_info) != 0 ||
        get<2>(persisted_info) != 0.0) {
      cerr << persisted_info << endl;
      ALWAYS_ASSERT(false);
    }
  }

  map<string, size_t> table_sizes_before;
  if (verbose) {
    for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
         it != open_tables.end(); ++it) {
      scoped_rcu_region guard;
      const size_t s = it->second->size();
      cerr << "table " << it->first << " size " << s << endl;
      table_sizes_before[it->first] = s;
    }
    cerr << "starting benchmark..." << endl;
  }
  
  //const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();
  
  const vector<bench_worker *> workers = make_workers();
  ALWAYS_ASSERT(!workers.empty());
  timer t2, t_nosync;

  size_t mainCPU = ebbrt::Cpu::GetMine();
  ebbrt::EventManager::EventContext context;
  std::atomic<size_t> count(0);
  static ebbrt::SpinBarrier bar(nthreads);
  size_t i = 0;
  uint64_t rdtsc_start = ebbrt::rdtsc();
  for (vector<bench_worker *>::const_iterator it = workers.begin();
       it != workers.end(); ++it) {
    ebbrt::event_manager->SpawnRemote([&context, &count, mainCPU, it]() {
	int mycpu = static_cast<int>(ebbrt::Cpu::GetMine());
	//KPRINTF("running on cpu: %d\n", mycpu);
	
	(*it)->start();
	count ++;
	bar.Wait();
	while(count < nthreads);
	if (ebbrt::Cpu::GetMine() == mainCPU)
	  ebbrt::event_manager->ActivateContext(std::move(context));
      }, i);
    i++;
  }
  
  ebbrt::event_manager->SaveContext(context);  
  
  // EbbRT don't need barriers, running for set number of iterations
  //barrier_a.wait_for(); // wait for all threads to start up
  //timer t2, t_nosync;
  //barrier_b.count_down(); // bombs away!
  //if (run_mode == RUNMODE_TIME) {
  //sleep(runtime);
  //  ebbrt::clock::SleepMilli(runtime * 1000);
  //running = false;
  //}
  //__sync_synchronize();
  
  const unsigned long elapsed_nosync = t_nosync.lap();
  db->do_txn_finish(); // waits for all worker txns to persist
  uint64_t rdtsc_end = ebbrt::rdtsc();
  
  size_t n_commits = 0;
  size_t n_aborts = 0;
  uint64_t latency_numer_us = 0;
  int64_t ninstructions = 0;
  uint64_t ncycles = 0;
  uint64_t nllc_ref = 0;
  uint64_t nllc_miss = 0;
  double joules = 0.0;
    
  for (size_t i = 0; i < nthreads; i++) {
    n_commits += workers[i]->get_ntxn_commits();
    n_aborts += workers[i]->get_ntxn_aborts();
    latency_numer_us += workers[i]->get_latency_numer_us();
    
    ninstructions += workers[i]->get_instructions();
    ncycles += workers[i]->get_cycles();
    nllc_ref += workers[i]->get_llc_ref();
    nllc_miss += workers[i]->get_llc_miss();
    
    joules += workers[i]->get_joules();
  }
  const auto persisted_info = db->get_ntxn_persisted();
  
  const unsigned long elapsed = t2.lap(); // lap() must come after do_txn_finish(),
  // because do_txn_finish() potentially
  // waits a bit
  
  // various sanity checks
  ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
  // not == b/c persisted_info does not count read-only txns
  ALWAYS_ASSERT(n_commits >= get<1>(persisted_info));	  
  
  const double elapsed_nosync_sec = double(elapsed_nosync) / 1000000.0;
  const double agg_nosync_throughput = double(n_commits) / elapsed_nosync_sec;
  const double avg_nosync_per_core_throughput = agg_nosync_throughput / double(workers.size());

  const double elapsed_sec = double(elapsed) / 1000000.0;
  const double agg_throughput = double(n_commits) / elapsed_sec;
  const double avg_per_core_throughput = agg_throughput / double(workers.size());

  const double agg_abort_rate = double(n_aborts) / elapsed_sec;
  const double avg_per_core_abort_rate = agg_abort_rate / double(workers.size());

  // we can use n_commits here, because we explicitly wait for all txns
  // run to be durable
  const double agg_persist_throughput = double(n_commits) / elapsed_sec;
  const double avg_per_core_persist_throughput =
    agg_persist_throughput / double(workers.size());

  // XXX(stephentu): latency currently doesn't account for read-only txns
  const double avg_latency_us =
    double(latency_numer_us) / double(n_commits);
  const double avg_latency_ms = avg_latency_us / 1000.0;
  const double avg_persist_latency_ms =
    get<2>(persisted_info) / 1000.0;

  map<string, size_t> agg_txn_counts = workers[0]->get_txn_counts();
  for (size_t i = 1; i < workers.size(); i++) {
    map_agg(agg_txn_counts, workers[i]->get_txn_counts());
  }
  
  // KPRINTF("runtime: %lf sec\n",elapsed_sec);
  // KPRINTF("agg_nosync_throughput: %lf ops/sec\n",agg_nosync_throughput);
  // KPRINTF("avg_nosync_per_core_throughput: %lf ops/sec/core\n",avg_nosync_per_core_throughput);
  // KPRINTF("agg_throughput: %lf ops/sec\n",agg_throughput);
  // KPRINTF("avg_per_core_throughput: %lf ops/sec/core\n",avg_per_core_throughput);
  // KPRINTF("agg_persist_throughput: %lf ops/sec\n",agg_persist_throughput);
  // KPRINTF("avg_per_core_persist_throughput: %lf ops/sec/core\n",avg_per_core_persist_throughput);
  // KPRINTF("avg_latency: %lf ms\n",avg_latency_ms);
  // KPRINTF("avg_persist_latency: %lf ms\n",avg_persist_latency_ms);
  // KPRINTF("agg_abort_rate: %lf aborts/sec\n",agg_abort_rate);
  // KPRINTF("avg_per_core_abort_rate: %lf aborts/sec/core\n",avg_per_core_abort_rate);
  KPRINTF("txn breakdown : ");
  for (auto it = agg_txn_counts.begin(); it != agg_txn_counts.end(); ++it) {
    KPRINTF("%d ", it->second);
  }
  KPRINTF("\n");
  //KPRINTF("mycounts : \n");
  //for (auto it = workers.begin(); it != workers.end(); ++it) {
  //  KPRINTF("MyCount = %ld\n", (*it)->getmycount());
  //  }

  KPRINTF("***DVFS RAPL COMMITS ELAPSED_SEC INSTRUCTIONS CYCLES LLC_MISS LLC_REF JOULES agg_tput agg_persist_tput avg_latency_ms avg_persist_latency_ms agg_abort_rate RDTSC_DIFF RDTSC_START RDTSC_END\n");
  KPRINTF("+++0x%X %u %d %.2lf %llu %llu %llu %llu %.2lf %.2lf %.2lf %.2lf %.2lf %.2lf %llu %llu %llu\n",
	  dvfs, rapl, n_commits,
	  elapsed_sec, ninstructions,
	  ncycles, nllc_miss, nllc_ref, joules,
	  agg_throughput, agg_persist_throughput, avg_latency_ms,
	  avg_persist_latency_ms, agg_abort_rate, rdtsc_end - rdtsc_start, rdtsc_start, rdtsc_end);
}

template <typename K, typename V>
struct map_maxer {
  typedef map<K, V> map_type;
  void
  operator()(map_type &agg, const map_type &m) const
  {
    for (typename map_type::const_iterator it = m.begin();
        it != m.end(); ++it)
      agg[it->first] = std::max(agg[it->first], it->second);
  }
};

//template <typename KOuter, typename KInner, typename VInner>
//struct map_maxer<KOuter, map<KInner, VInner>> {
//  typedef map<KInner, VInner> inner_map_type;
//  typedef map<KOuter, inner_map_type> map_type;
//};

#ifdef ENABLE_BENCH_TXN_COUNTERS
void
bench_worker::measure_txn_counters(void *txn, const char *txn_name)
{
  auto ret = db->get_txn_counters(txn);
  map_maxer<string, uint64_t>()(local_txn_counters[txn_name], ret);
}
#endif

map<string, size_t>
bench_worker::get_txn_counts() const
{
  map<string, size_t> m;
  const workload_desc_vec workload = get_workload();
  for (size_t i = 0; i < txn_counts.size(); i++)
    m[workload[i].name] = txn_counts[i];
  return m;
}
