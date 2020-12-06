// cBlockPool.cpp
//{{{  includes
#include "cBlockPool.h"

#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

using namespace std;
using namespace fmt;
//}}}

// public
//{{{
cBlock* cBlockPool::newBlock() {

  cBlock* block;
  if (mFreeBlockCount) {
    block = mBlockPool;
    mBlockPool = block->mNextBlock;
    mFreeBlockCount--;
    }
  else {
    block = new cBlock();
    mAllocatedBlockCount++;
    mMaxBlockCount = max (mMaxBlockCount, mAllocatedBlockCount);
    }

  block->mNextBlock = NULL;
  block->mRefCount = 1;

  return block;
  }
//}}}
//{{{
cBlockPool::~cBlockPool() {

  // free blocks
  while (mFreeBlockCount) {
    cBlock* block = mBlockPool;
    mBlockPool = block->mNextBlock;
    delete block;
    mAllocatedBlockCount--;
    mFreeBlockCount--;
    }
  }
//}}}

//{{{
void cBlockPool::freeBlock (cBlock* block) {
// delte if too many blocks allocated else return to pool

  if (mFreeBlockCount >= mMaxBlocks) {
    delete block;
    mAllocatedBlockCount--;
    return;
    }

  block->mNextBlock = mBlockPool;
  mBlockPool = block;
  mFreeBlockCount++;
  }
//}}}
//{{{
void cBlockPool::unRefBlock (cBlock* block) {

  block->mRefCount--;
  if (!block->mRefCount)
    cBlockPool::freeBlock (block);
  }
//}}}
