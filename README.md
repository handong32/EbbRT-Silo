# EbbRT-Silo

## Requirements
* Build and install EbbRT native toolchain, assume installed at `~/sysroot/native`

#### Native
```
$ cd build
$ mkdir bm
$ cd bm
$ EBBRT_SYSROOT=~/sysroots/release/sysroot/ cmake -DCMAKE_TOOLCHAIN_FILE=~/sysroots/release/sysroot/usr/misc/ebbrt.cmake -$ DCMAKE_BUILD_TYPE=Release ../../
$ make -j
```

