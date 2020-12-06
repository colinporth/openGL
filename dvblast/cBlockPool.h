// cBlockPool.h - dvb transport stream block pool, produced by cDvb, consumed by cDvbRtp
#pragma once
//{{{  includes
#include <cstdlib>
#include <cstdint>
class cBlockPool;
//}}}

class cBlock {
friend cBlockPool;
public:
  void incRefCount() { mRefCount++; }
  void decRefCount() { mRefCount--; }

  cBlock* mNextBlock = nullptr;
  int64_t mDts = 0;
  uint8_t mTs[188];

private:
  int mRefCount = 0;
  };


class cBlockPool {
public:
  cBlockPool (int maxBlocks) : mMaxBlocks(maxBlocks) {}
  ~cBlockPool();

  cBlock* newBlock();
  void freeBlock (cBlock* block);
  void unRefBlock (cBlock* block);

private:
  const int mMaxBlocks = 0;

  int mFreeBlockCount = 0;
  int mAllocatedBlockCount = 0;
  int mMaxBlockCount = 0;
  cBlock* mBlockPool = NULL;
  };
