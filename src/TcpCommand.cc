#include <cstdlib>
#include <sstream>
#include <ebbrt/SharedIOBufRef.h>
#include <ebbrt/UniqueIOBuf.h>

#include "TcpCommand.h"
#include "Memcached.h"

ebbrt::TcpCommand::TcpCommand() {}

void ebbrt::TcpCommand::Start(uint16_t port) {
  listening_pcb_.Bind(port, [this](NetworkManager::TcpPcb pcb) {
    // new connection callback
    static std::atomic<size_t> cpu_index{0};
    auto index = 1; //cpu_index.fetch_add(1) % ebbrt::Cpu::Count();
    pcb.BindCpu(index);
    auto connection = new TcpSession(this, std::move(pcb));
    connection->Install();
  });
}

void ebbrt::TcpCommand::TcpSession::Receive(std::unique_ptr<MutIOBuf> b) {
  kassert(b->Length() != 0);

  uint32_t ncores = static_cast<uint32_t>(ebbrt::Cpu::Count());  
  uint32_t mcore = static_cast<uint32_t>(Cpu::GetMine());
  std::string s(reinterpret_cast<const char*>(b->Data()));
  std::string delimiter = ",";
  uint32_t param = 0;
  std::string token1, token2;  
  
  std::size_t pos = s.find(delimiter);
  token1 = s.substr(0, pos);
  token2 = s.substr(pos+1, s.length());
  param = static_cast<uint32_t>(atoi(token2.c_str()));
  ebbrt::kprintf_force("Core: %u TcpCommand::Receive() s=%s token1=%s param=%u\n", mcore, s.c_str(), token1.c_str(), param);
  
  if (token1 == "start") {
    ebbrt::kprintf_force("start()\n");
    for (uint32_t i = 0; i < ncores-1; i++) {
      network_manager->Config("start_stats", i);
    }    
  } else if (token1 == "stop") {
    ebbrt::kprintf_force("stop()\n");
    for (uint32_t i = 0; i < ncores-1; i++) {
      network_manager->Config("stop_stats", i);
    }    
  } else if (token1 == "clear") {
    ebbrt::kprintf_force("clear()\n");
    for (uint32_t i = 0; i < ncores-1; i++) {
      network_manager->Config("clear_stats", i);
    }
  } else if (token1 == "rx_usecs") {
    ebbrt::kprintf_force("itr %u\n", param);
    for (uint32_t i = 0; i < ncores-1; i++) {
      event_manager->SpawnRemote(
	[token1, param, i] () mutable {
	  network_manager->Config(token1, param);
	}, i);
    }
    
  } else if (token1 == "dvfs") {
    //ebbrt::kprintf_force("dvfs %s\n", token2.c_str());
    for (uint32_t i = 0; i < ncores-1; i++) {
      event_manager->SpawnRemote(
	[param, i] () mutable {
	  // same p state as Linux with performance governor
	  ebbrt::msr::Write(IA32_PERF_CTL, param);    
	}, i);
    }
    ebbrt::kprintf_force("dvfs %s finished\n", token2.c_str());
    
  } else if (token1 == "sleep_state") {
    ebbrt::kprintf_force("sleep_state EbbRT-mcdsilo TcpCommand.cc: %u\n", param);
    for (uint32_t i = 0; i < ncores-1; i++) {
      event_manager->SpawnRemote(
	[token1, param, i] () mutable {
	  network_manager->Config(token1, param);
	}, i);
    }
    
  } else if (token1 == "rapl") {    
    for (uint32_t i = 0; i < 2; i++) {
      event_manager->SpawnRemote(
	[token1, param, i] () mutable {
	  network_manager->Config(token1, param);
	}, i);
    }
    ebbrt::kprintf_force("rapl %u finished\n", param);
    
  } else if (token1 == "reta") {
    ebbrt::kprintf_force("reta %u\n", param);
    event_manager->SpawnRemote(
      [token1, param] () mutable {
	network_manager->Config(token1, param);
      }, 0);
  } else if (token1 == "rdtsc") {
    uint64_t tstart, tclose;
    std::stringstream ss;
    
    tstart = 0;
    tclose = 0;
    
    for (uint32_t i = 0; i < ncores-1; i++) {
      if(ixgbe_stats[mcore].rdtsc_start > tstart) {
	tstart = ixgbe_stats[mcore].rdtsc_start;
      }

      if(ixgbe_stats[mcore].rdtsc_end != 0) {
	
	if(tclose == 0) {
	  tclose = ixgbe_stats[mcore].rdtsc_end;
	} else if (ixgbe_stats[mcore].rdtsc_end < tclose) {
	  tclose = ixgbe_stats[mcore].rdtsc_end;
	}
      }
    }
    ss << tstart << ' ' << tclose;
    std::string s = ss.str();
    ebbrt::kprintf_force("rdtsc %s\n", s.c_str());
    auto rbuf = MakeUniqueIOBuf(s.length(), false);
    auto dp = rbuf->GetMutDataPointer();
    std::memcpy(static_cast<void*>(dp.Data()), s.data(), s.length());
    Send(std::move(rbuf));
    
  } else if (token1 == "get") {
    uint8_t* re = (uint8_t*)(ixgbe_logs[param]);
    uint64_t msg_size = ixgbe_stats[param].itr_cnt * sizeof(union IxgbeLogEntry);
    ebbrt::kprintf_force("get msg_size=%lu\n", msg_size);
    while(msg_size > IXGBE_MAX_DATA_PER_TXD) {
      auto buf = std::make_unique<ebbrt::StaticIOBuf>(re, IXGBE_MAX_DATA_PER_TXD);
      Send(std::move(buf));      
      msg_size -= IXGBE_MAX_DATA_PER_TXD;
      re += IXGBE_MAX_DATA_PER_TXD;
    }
    if(msg_size) {
      auto buf = std::make_unique<ebbrt::StaticIOBuf>(re, msg_size);
      Send(std::move(buf));      
    }
    
  } else if (token1 == "getcounters") {
    double tdsum = 0.0;
    double tdfreqsum = 0.0;
    long long tworkerloop = 0;
    long long tprocessbinary = 0;
    long long tsiloexec = 0;
    uint32_t tspawn_local, tspawn_remote;    
    int i;    
    std::stringstream ss;
    uint64_t tno_rx_pkt_cnt = 0;

    tspawn_local = tspawn_remote = 0;
    
    ss << "Core" << ' '
       << "dsum" << ' '
       << "dfreqsum" << ' '
       << "dworkerloop" << ' '
       << "dprocessbinary" << ' '
       << "dsiloexec" << ' '
       << "tcp_spawn_local" << ' '
       << "tcp_spawn_remote" << '\n';
    //<< "processCnt" << ' '
    // << "swEventCnt" << ' '
    // << "idleEventCnt" << '\n';
    
    for(i = 0; i < ncores; i++) {
      ss << i << ' '
	 << dsum[i] << ' '
	 << dfreqsum[i] << ' '
	 << dworkerloop[i] << ' '
	 << dprocessbinary[i] << ' '
	 << dsiloexec[i] << ' '
	 << tcp_spawn_local[i] << ' '
	 << tcp_spawn_remote[i] << '\n';
      //<< processCnt[i] << ' '
      // << swEventCnt[i] << ' '
      // << idleEventCnt[i] << '\n';
      
      //ebbrt::kprintf_force("[%d]: dsum=%.2lf dfreqsum=%.2lf dworkerloop=%ld dprocessbinary=%ld\n",
      //		   i, dsum[i], dfreqsum[i], dworkerloop[i], dprocessbinary[i]);
      tdsum += dsum[i];
      tdfreqsum += dfreqsum[i];
      tworkerloop += dworkerloop[i];
      tprocessbinary += dprocessbinary[i];
      tsiloexec += dsiloexec[i];
      tno_rx_pkt_cnt += ixgbe_stats[i].no_rx_pkt_cnt;
      tspawn_local += tcp_spawn_local[i];
      tspawn_remote += tcp_spawn_remote[i];
    }

    ss << "tdsum=" << tdsum << ' '
       << "tdfreqsum=" << tdfreqsum << ' '
       << "tworkerloop=" << tworkerloop << ' '
       << "tprocessbinary=" << tprocessbinary << ' '
       << "tsiloexec=" << tsiloexec << ' '
       << "tno_rx_pkt_cnt=" << tno_rx_pkt_cnt << ' '
       << "tspawn_local=" << tspawn_local << ' '
       << "tspawn_remote=" << tspawn_remote << ' '
       << '\n';
    
    //ebbrt::kprintf_force("tdsum = %.2lf\n", tdsum);
    //ebbrt::kprintf_force("tdfreqsum = %.2lf\n", tdfreqsum);
    //ebbrt::kprintf_force("tworkerloop = %ld\n", tworkerloop);
    //ebbrt::kprintf_force("tprocessbinary = %ld\n", tprocessbinary);

    // clear counters
    memset(dsum, 0x0, sizeof(dsum));
    memset(dfreqsum, 0x0, sizeof(dfreqsum));
    memset(dworkerloop, 0x0, sizeof(dworkerloop));
    memset(dprocessbinary, 0x0, sizeof(dprocessbinary));
    memset(dsiloexec, 0x0, sizeof(dsiloexec));
    memset(tcp_spawn_local, 0, sizeof(tcp_spawn_local));
    memset(tcp_spawn_remote, 0, sizeof(tcp_spawn_remote));
    
    std::string s = ss.str();
    ebbrt::kprintf_force("getcounters\n%s\n", s.c_str());
    auto rbuf = MakeUniqueIOBuf(s.length(), false);
    auto dp = rbuf->GetMutDataPointer();
    std::memcpy(static_cast<void*>(dp.Data()), s.data(), s.length());
    Send(std::move(rbuf));
  }
  else {
    ebbrt::kprintf_force("Unknown command %s\n", token1.c_str());
  }

}


