project("silo-ebbrt" CXX)
cmake_minimum_required(VERSION 2.6 FATAL_ERROR)
set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g3")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE        "-O4 -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g3")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++14 -DCONFIG_H=config/config-perf.h -DNDB_MASSTREE -DNO_MYSQL -include ../src/masstree/config.h")

include_directories(${PROJECT_SRC_DIR})
#set_source_files_properties(src/xxhash.c PROPERTIES LANGUAGE C)
add_executable(silo.elf src/varint.cc src/allocator.cc src/counter.cc src/core.cc
  src/xxhash.cc src/lz4.cc src/memory.cc src/ticker.cc src/rcu.cc src/masstree/compiler.cc
  src/masstree/str.cc src/masstree/string.cc src/masstree/straccum.cc src/masstree/json.cc
  src/btree.cc src/tuple.cc src/txn_btree.cc src/txn.cc src/txn_proto2_impl.cc
  src/benchmarks/masstree/kvrandom.cc src/thread.cc
  src/benchmarks/tpcc.cc src/benchmarks/bench.cc
  src/silo.cc)

add_custom_command(TARGET silo.elf POST_BUILD
  COMMAND objcopy -O elf32-i386 silo.elf silo.elf32)
