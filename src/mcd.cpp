#include <ebbrt/Debug.h>
#include <ebbrt/EbbAllocator.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/Msr.h>
#include <ebbrt/native/EventManager.h>
#include <ebbrt/native/Cpu.h>

#include "Memcached.h"
#include "UdpCommand.h"

#define MCDPORT 11211

void AppMain()
{
  /*for (uint32_t i = 0; i < static_cast<uint32_t>(ebbrt::Cpu::Count()); i++) {
    ebbrt::event_manager->SpawnRemote(
	[i] () mutable {
	  // disables turbo boost, thermal control circuit
	  ebbrt::msr::Write(IA32_MISC_ENABLE, 0x4000850081);
	  // same p state as Linux with performance governor
	  ebbrt::msr::Write(IA32_PERF_CTL, 0x1D00);
	  // same p state as Linux with powersave governor
	  //ebbrt::msr::Write(IA32_PERF_CTL, 0xC00);
	}, i);
	}*/
  
  auto id = ebbrt::ebb_allocator->AllocateLocal();
  auto mc = ebbrt::EbbRef<ebbrt::Memcached>(id);
  mc->Start(MCDPORT);
  ebbrt::kprintf("Memcached server listening on port %d\n", MCDPORT);

  auto uid = ebbrt::ebb_allocator->AllocateLocal();
  auto udpc = ebbrt::EbbRef<ebbrt::UdpCommand>(uid);
  udpc->Start(6666);
  ebbrt::kprintf("UdpCommand server listening on port %d\n", 6666);
}

