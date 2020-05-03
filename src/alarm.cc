#include "alarm.h"
#include <ebbrt/Debug.h>
#include <ebbrt/Cpu.h>
#include <chrono>

void Alarm::enable_timer() {
  auto duration = std::chrono::seconds(1);
  ebbrt::timer->Start(*this, duration, /* repeat = */ true);
}

void Alarm::disable_timer() {
  ebbrt::timer->Stop(*this);
}

void Alarm::Fire() {
  ebbrt::kprintf_force("%s on Core %u\n", __FUNCTION__, static_cast<uint32_t>(ebbrt::Cpu::GetMine()));
}
