#include "macros.h"
#include "thread.h"

using namespace std;

ndb_thread::~ndb_thread()
{
}

void
ndb_thread::start()
{
  //tid_ = static_cast<size_t>(tid);
  //ebbrt::kprintf_force("START %s tid = %d\n", __PRETTY_FUNCTION__, tid_);
  
  
  /*if(daemon_) {
    ebbrt::kabort("daemon started\n");
    }*/

  this->run();
  
  /*ebbrt::Promise<uint32_t> promise;
  bool inserted;
  auto f = promise.GetFuture();
  vecfut.push_back(std::move(f));
  std::tie(std::ignore, inserted) =
    thdmap.emplace(static_cast<uint32_t>(tid_), std::move(promise));

  //auto tfunc = &ndb_thread::run;
  ebbrt::kprintf_force("SpawnRemote:%s tid=%d\n", __PRETTY_FUNCTION__, tid_);
  ebbrt::event_manager->SpawnRemote(
    [this]() {
      ebbrt::kprintf_force("SpawnRemote %d\n", static_cast<int>(ebbrt::Cpu::GetMine()));
      //this->run();

      std::lock_guard<ebbrt::SpinLock> l(thdlock_);
      {
	auto it = thdmap.find(tid_);
	assert(it != thdmap.end());	
	it->second.SetValue(tid_);
	thdmap.erase(it);
      }
    }, tid_);
  
    //ebbrt::kprintf_force("END %s tid = %d\n", __PRETTY_FUNCTION__, tid_);*/
}

void
ndb_thread::join()
{
  //vecfut[tid_].Block();
  ebbrt::kprintf_force("END %s tid = %d\n", __PRETTY_FUNCTION__, tid_);
}

// can be overloaded by subclasses
void
ndb_thread::run()
{
  //ebbrt::kabort("In ndb::thread run\n");
  //ebbrt::kprintf_force("START %s tid = %d\n", __PRETTY_FUNCTION__, tid_);

  //ebbrt::kprintf_force("END %s tid = %d\n", __PRETTY_FUNCTION__, tid_);
}
