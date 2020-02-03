#ifndef __PMC_H
#define __PMC_H

#include <ebbrt/native/Perf.h>

static ebbrt::perf::PerfCounter perfcyc = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::cycles);
static ebbrt::perf::PerfCounter perfinst = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::instructions);
static ebbrt::perf::PerfCounter perftlbloadmisses = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::tlb_load_misses);
static ebbrt::perf::PerfCounter perftlbstoremisses = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::tlb_store_misses);
static ebbrt::perf::PerfCounter perfitlbmisses = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::itlb_misses);

#endif
