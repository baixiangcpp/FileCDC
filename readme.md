## Introduction
This is an implementation Content Defined Chunking based on [Rabin Footprint ](http://www.cs.cmu.edu/~hakim/software/).It is extracted from the source code of [seafile](https://github.com/haiwen/seafile) project.

You can build this project by running `make`.


## Usage

![cdc](http://baixiangcpp.oss-cn-shanghai.aliyuncs.com/github/cdc.gif)

## Set  min block size and max block size

```
#define CDC_MIN_BLOCK_SIZE (6 * (1 << 20))     /* 6MB */
#define CDC_MAX_BLOCK_SIZE (10 * (1 << 20))    /* 10MB */
```

If you need the block to be larger or smaller,just change these macros in `cdc.c` to the value you need.
