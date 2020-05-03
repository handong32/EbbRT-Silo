//          Copyright Boston University SESA Group 2013 - 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UDP_COMMAND_H
#define UDP_COMMAND_H

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

// Vol. 3C Page 35-3, Table 35-2. IA-32 Architectural MSRs
#define IA32_APIC_BASE 0x1B
#define IA32_FEATURE_CONTROL 0x3A
#define IA32_SMM_MONITOR_CTL 0x9B
#define IA32_MTRRCAP 0xFE
#define IA32_SYSENTER_CS 0x174
#define IA32_MCG_CAP 0x179
#define IA32_PERF_STATUS 0x198
#define IA32_PERF_CTL    0x199
#define IA32_CLOCK_MODULATION 0x19A
#define IA32_THERM_INTERRUPT 0x19B
#define IA32_THERM_STATUS 0x19C
#define IA32_MISC_ENABLE 0x1A0
#define IA32_PACKAGE_THERM_STATUS 0x1B1
#define IA32_PACKAGE_THERM_INTERRUPT 0x1B2
#define IA32_PLATFORM_DCA_CAP 0x1F8
#define IA32_CPU_DCA_CAP 0x1F9
#define IA32_DCA_0_CAP 0x1FA

// Vol. 3C Page 35-143, Table 35-18. Intel Sandy Bridge MSRs
#define MSR_PLATFORM_INFO 0xCE
#define MSR_PKG_CST_CONFIG_CONTROL 0xE2
#define MSR_PMG_IO_CAPTURE_BASE 0xE4
#define MSR_TEMPERATURE_TARGET 0x1A2
#define MSR_MISC_FEATURE_CONTROL 0x1A4
#define MSR_PEBS_LD_LAT 0x3F6
#define MSR_PKG_C3_RESIDENCY 0x3F8
#define MSR_PKG_C6_RESIDENCY 0x3F9

// TODO
#define MSR_PKGC3_IRTL 0x60A
#define MSR_PKGC6_IRTL 0x60B

namespace ebbrt {
  class UdpCommand: public StaticSharedEbb<UdpCommand>, public CacheAligned {
  public:
    UdpCommand();
    void Start(uint16_t port);

  private:
    void ReceiveCommand(Ipv4Address from_addr, uint16_t from_port,
			std::unique_ptr<MutIOBuf> buf);
    ebbrt::NetworkManager::UdpPcb udp_pcb;
    uint64_t runtime{0};
    ebbrt::EventManager::EventContext context_;
  };
  
} // namespace ebbrt

#endif // UDP_COMMAND_H
