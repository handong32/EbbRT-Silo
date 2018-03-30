#ifndef _NDB_THREAD_H_
#define _NDB_THREAD_H_

#include <vector>
#include <string>
//#include <thread>

#include <ebbrt/EventManager.h>
#include <ebbrt/Future.h>
#include <ebbrt/LocalIdMap.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/SpinBarrier.h>
#include <ebbrt/native/Cpu.h>
#include <ebbrt/Debug.h>

#include "macros.h"

/**
 * WARNING: This class is DEPRECATED. New code should use std::thread directly
 *
 * ndb_thread: threads in NuDB
 *
 * Note that ndb_threads are thin wrappers around std::thread.
 *
 * There is really no point to use this-- in the past we used this to grab
 * hooks into threads when they exited. This is no longer necessary, so we
 * removed the hook code and this exists just for legacy reasons.
 */

//static std::vector<ebbrt::Future<uint32_t>> vecfut;
//static std::unordered_map<uint32_t, ebbrt::Promise<uint32_t>> thdmap;

//static ebbrt::SpinLock thdlock_;


class ndb_thread {
public:

  typedef void (*run_t)(void);

  ndb_thread(bool daemon = false, const std::string &name = "thd")
    : body_(nullptr), daemon_(daemon), name_(name), tid_(0) {
    //ebbrt::kprintf_force("In %s daemon = %d\n", __PRETTY_FUNCTION__, daemon);
  }
ndb_thread(run_t body, int tid, bool daemon = false, const std::string &name = "thd")
  : body_(body), daemon_(daemon), name_(name) {

    tid_ = static_cast<size_t>(tid);
    
    //ebbrt::kprintf_force("In %s daemon = %d\n", __PRETTY_FUNCTION__, daemon);
  }

  ndb_thread(const ndb_thread &) = delete;
  ndb_thread(ndb_thread &&) = delete;
  ndb_thread &operator=(const ndb_thread &) = delete;

  virtual ~ndb_thread();

  inline const std::string &
  get_name() const
  {
    return name_;
  }

  void start();
  void join();
  virtual void run();

private:
  run_t body_;
  //std::thread thd_;
  size_t tid_;
  const bool daemon_;
  const std::string name_;
};

#endif /* _NDB_THREAD_H_ */
