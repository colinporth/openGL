// main.cpp
//{{{  includes
#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <winsock2.h>
  #include <WS2tcpip.h>
  #include <objbase.h>
#endif

// c
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <emmintrin.h>

// c++
#include <string>
#include <thread>
#include <chrono>

#include "../../shared/date/date.h"
#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

#include "../../shared/decoders/cAudioDecode.h"
#include "../../shared/utils/cSong.h"

#ifdef _WIN32
  #include "../../shared/audio/audioWASAPI.h"
  #include "../../shared/audio/cWinAudio16.h"
  #include "../../shared/audio/cWinAudio32.h"
#else
  #include "../../shared/audio/cLinuxAudio16.h"
#endif

#ifdef _WIN32
  #include "../../shared/net/cWinSockHttp.h"
#else
  #include "../../shared/net/cLinuxHttp.h"
#endif

#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"

#include "../../shared/widgets/cTextBox.h"

#ifdef _WIN32
  #define YSIZE 600
#else
  #define YSIZE 480
#endif

using namespace std;
using namespace chrono;
//}}}
//{{{  channels
constexpr int kDefaultChannelNum = 3;
constexpr int kAudBitrate = 128000; //  96000  128000
constexpr int kVidBitrate = 827008; // 827008 1604032 2812032 5070016
const string kHost = "vs-hls-uk-live.akamaized.net";
const vector <string> kChannels = { "bbc_one_hd",          "bbc_two_hd",          "bbc_four_hd", // pa4
                                    "bbc_news_channel_hd", "bbc_one_scotland_hd", "s4cpbs",      // pa4
                                    "bbc_one_south_west",  "bbc_parliament" };                   // pa3

//}}}

//{{{
class cVideoDecode {
public:
  //{{{
  class cFrame {
  public:
    cFrame (uint64_t pts) : mOk(false), mPts(pts) {}
    //{{{
    virtual ~cFrame() {

    #ifdef __linux__
      free (mYbuf);
      free (mUbuf);
      free (mVbuf);
      free (mBgra);
    #else
      _aligned_free (mYbuf);
      _aligned_free (mUbuf);
      _aligned_free (mVbuf);
      _aligned_free (mBgra);
     #endif
      }
    //}}}

    bool ok() { return mOk; }
    uint64_t getPts() { return mPts; }

    int getWidth() { return mWidth; }
    int getHeight() { return mHeight; }
    uint32_t* getBgra() { return mBgra; }

    //{{{
    void set (uint64_t pts) {
      mOk = false;
      mPts = pts;
      }
    //}}}
  #ifdef __linux__
    //{{{
    void setNv12 (uint8_t* buffer, int width, int height, int stride) {

      mOk = false;
      mYStride = stride;
      mUVStride = stride/2;

      if ((mWidth != width) || (mHeight != height)) {
        mWidth = width;
        mHeight = height;

        free (mYbuf);
        free (mUbuf);
        free (mVbuf);
        free (mBgra);

        mYbuf = (uint8_t*)aligned_alloc (height * mYStride * 3 / 2, 128);
        mUbuf = (uint8_t*)aligned_alloc ((height/2) * mUVStride, 128);
        mVbuf = (uint8_t*)aligned_alloc ((height/2) * mUVStride, 128);
        mBgra = (uint32_t*)aligned_alloc (mWidth * 4 * mHeight, 128);
        }

      // copy all of nv12 to yBuf
      memcpy (mYbuf, buffer, height * mYStride * 3 / 2);

      // unpack nv12 to planar uv
      uint8_t* uv = mYbuf + (height * mYStride);
      uint8_t* u = mUbuf;
      uint8_t* v = mVbuf;
      for (int i = 0; i < height/2 * mUVStride; i++) {
        *u++ = *uv++;
        *v++ = *uv++;
        }

      int argbStride = mWidth;

      __m128i ysub  = _mm_set1_epi32 (0x00100010);
      __m128i uvsub = _mm_set1_epi32 (0x00800080);
      __m128i facy  = _mm_set1_epi32 (0x004a004a);
      __m128i facrv = _mm_set1_epi32 (0x00660066);
      __m128i facgu = _mm_set1_epi32 (0x00190019);
      __m128i facgv = _mm_set1_epi32 (0x00340034);
      __m128i facbu = _mm_set1_epi32 (0x00810081);
      __m128i zero  = _mm_set1_epi32 (0x00000000);

      for (int y = 0; y < mHeight; y += 2) {
        __m128i* srcy128r0 = (__m128i*)(mYbuf + mYStride*y);
        __m128i* srcy128r1 = (__m128i*)(mYbuf + mYStride*y + mYStride);
        __m64* srcu64 = (__m64*)(mUbuf + mUVStride * (y/2));
        __m64* srcv64 = (__m64*)(mVbuf + mUVStride * (y/2));
        __m128i* dstrgb128r0 = (__m128i*)(mBgra + argbStride * y);
        __m128i* dstrgb128r1 = (__m128i*)(mBgra + argbStride * y + argbStride);

        for (int x = 0; x < mWidth; x += 16) {
          __m128i u0 = _mm_loadl_epi64 ((__m128i *)srcu64 );
          srcu64++;
          __m128i v0 = _mm_loadl_epi64 ((__m128i *)srcv64 );
          srcv64++;
          __m128i y0r0 = _mm_load_si128 (srcy128r0++);
          __m128i y0r1 = _mm_load_si128 (srcy128r1++);
          //{{{  constant y factors
          __m128i y00r0 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpacklo_epi8 (y0r0, zero), ysub), facy);
          __m128i y01r0 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpackhi_epi8 (y0r0, zero), ysub), facy);
          __m128i y00r1 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpacklo_epi8 (y0r1, zero), ysub), facy);
          __m128i y01r1 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpackhi_epi8 (y0r1, zero), ysub), facy);
          //}}}
          //{{{  expand u and v so they're aligned with y values
          u0 = _mm_unpacklo_epi8 (u0, zero);
          __m128i u00 = _mm_sub_epi16 (_mm_unpacklo_epi16 (u0, u0), uvsub);
          __m128i u01 = _mm_sub_epi16 (_mm_unpackhi_epi16 (u0, u0), uvsub);

          v0 = _mm_unpacklo_epi8( v0,  zero );
          __m128i v00 = _mm_sub_epi16 (_mm_unpacklo_epi16 (v0, v0), uvsub);
          __m128i v01 = _mm_sub_epi16 (_mm_unpackhi_epi16 (v0, v0), uvsub);
          //}}}
          //{{{  common factors on both rows.
          __m128i rv00 = _mm_mullo_epi16 (facrv, v00);
          __m128i rv01 = _mm_mullo_epi16 (facrv, v01);
          __m128i gu00 = _mm_mullo_epi16 (facgu, u00);
          __m128i gu01 = _mm_mullo_epi16 (facgu, u01);
          __m128i gv00 = _mm_mullo_epi16 (facgv, v00);
          __m128i gv01 = _mm_mullo_epi16 (facgv, v01);
          __m128i bu00 = _mm_mullo_epi16 (facbu, u00);
          __m128i bu01 = _mm_mullo_epi16 (facbu, u01);
          //}}}
          //{{{  row 0
          __m128i r00 = _mm_srai_epi16 (_mm_add_epi16 (y00r0, rv00), 6);
          __m128i r01 = _mm_srai_epi16 (_mm_add_epi16 (y01r0, rv01), 6);
          __m128i g00 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y00r0, gu00), gv00), 6);
          __m128i g01 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y01r0, gu01), gv01), 6);
          __m128i b00 = _mm_srai_epi16 (_mm_add_epi16 (y00r0, bu00), 6);
          __m128i b01 = _mm_srai_epi16 (_mm_add_epi16 (y01r0, bu01), 6);

          r00 = _mm_packus_epi16 (r00, r01);                // rrrr.. saturated
          g00 = _mm_packus_epi16 (g00, g01);                // gggg.. saturated
          b00 = _mm_packus_epi16 (b00, b01);                // bbbb.. saturated

          r01 = _mm_unpacklo_epi8 (r00, zero);              // 0r0r..
          __m128i gbgb    = _mm_unpacklo_epi8 (b00, g00);   // gbgb..
          __m128i rgb0123 = _mm_unpacklo_epi16 (gbgb, r01); // 0rgb0rgb..
          __m128i rgb4567 = _mm_unpackhi_epi16 (gbgb, r01); // 0rgb0rgb..

          r01 = _mm_unpackhi_epi8 (r00, zero);
          gbgb = _mm_unpackhi_epi8 (b00, g00);
          __m128i rgb89ab = _mm_unpacklo_epi16 (gbgb, r01);
          __m128i rgbcdef = _mm_unpackhi_epi16 (gbgb, r01);

          _mm_stream_si128 (dstrgb128r0++, rgb0123);
          _mm_stream_si128 (dstrgb128r0++, rgb4567);
          _mm_stream_si128 (dstrgb128r0++, rgb89ab);
          _mm_stream_si128 (dstrgb128r0++, rgbcdef);
          //}}}
          //{{{  row 1
          r00 = _mm_srai_epi16 (_mm_add_epi16 (y00r1, rv00), 6);
          r01 = _mm_srai_epi16 (_mm_add_epi16 (y01r1, rv01), 6);
          g00 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y00r1, gu00), gv00), 6);
          g01 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y01r1, gu01), gv01), 6);
          b00 = _mm_srai_epi16 (_mm_add_epi16 (y00r1, bu00), 6);
          b01 = _mm_srai_epi16 (_mm_add_epi16 (y01r1, bu01), 6);

          r00 = _mm_packus_epi16 (r00, r01);        // rrrr.. saturated
          g00 = _mm_packus_epi16 (g00, g01);        // gggg.. saturated
          b00 = _mm_packus_epi16 (b00, b01);        // bbbb.. saturated

          r01     = _mm_unpacklo_epi8 (r00, zero);  // 0r0r..
          gbgb    = _mm_unpacklo_epi8 (b00, g00);   // gbgb..
          rgb0123 = _mm_unpacklo_epi16 (gbgb, r01); // 0rgb0rgb..
          rgb4567 = _mm_unpackhi_epi16 (gbgb, r01); // 0rgb0rgb..

          r01     = _mm_unpackhi_epi8 (r00, zero);
          gbgb    = _mm_unpackhi_epi8 (b00, g00);
          rgb89ab = _mm_unpacklo_epi16 (gbgb, r01);
          rgbcdef = _mm_unpackhi_epi16 (gbgb, r01);

          _mm_stream_si128 (dstrgb128r1++, rgb0123);
          _mm_stream_si128 (dstrgb128r1++, rgb4567);
          _mm_stream_si128 (dstrgb128r1++, rgb89ab);
          _mm_stream_si128 (dstrgb128r1++, rgbcdef);
          //}}}
          }
        }
      mOk = true;
      }
    //}}}
  #else
    //{{{
    void setNv12 (uint8_t* buffer, int width, int height, int stride) {

      mOk = false;
      mYStride = stride;
      mUVStride = stride/2;

      mWidth = width;
      mHeight = height;

      // copy all of nv12 to yBuf
      mYbuf = (uint8_t*)_aligned_realloc (mYbuf, height * mYStride * 3 / 2, 128);
      memcpy (mYbuf, buffer, height * mYStride * 3 / 2);

      // unpack nv12 to planar uv
      mUbuf = (uint8_t*)_aligned_realloc (mUbuf, (mHeight/2) * mUVStride, 128);
      mVbuf = (uint8_t*)_aligned_realloc (mVbuf, (mHeight/2) * mUVStride, 128);

      uint8_t* uv = mYbuf + (mHeight * mYStride);
      uint8_t* u = mUbuf;
      uint8_t* v = mVbuf;
      for (int i = 0; i < mHeight/2 * mUVStride; i++) {
        *u++ = *uv++;
        *v++ = *uv++;
        }

      int argbStride = mWidth;
      mBgra = (uint32_t*)_aligned_realloc (mBgra, mWidth * 4 * mHeight, 128);

      __m128i ysub  = _mm_set1_epi32 (0x00100010);
      __m128i uvsub = _mm_set1_epi32 (0x00800080);
      __m128i facy  = _mm_set1_epi32 (0x004a004a);
      __m128i facrv = _mm_set1_epi32 (0x00660066);
      __m128i facgu = _mm_set1_epi32 (0x00190019);
      __m128i facgv = _mm_set1_epi32 (0x00340034);
      __m128i facbu = _mm_set1_epi32 (0x00810081);
      __m128i zero  = _mm_set1_epi32 (0x00000000);

      for (int y = 0; y < mHeight; y += 2) {
        __m128i* srcy128r0 = (__m128i*)(mYbuf + mYStride*y);
        __m128i* srcy128r1 = (__m128i*)(mYbuf + mYStride*y + mYStride);
        __m64* srcu64 = (__m64*)(mUbuf + mUVStride * (y/2));
        __m64* srcv64 = (__m64*)(mVbuf + mUVStride * (y/2));
        __m128i* dstrgb128r0 = (__m128i*)(mBgra + argbStride * y);
        __m128i* dstrgb128r1 = (__m128i*)(mBgra + argbStride * y + argbStride);

        for (int x = 0; x < mWidth; x += 16) {
          __m128i u0 = _mm_loadl_epi64 ((__m128i *)srcu64 );
          srcu64++;
          __m128i v0 = _mm_loadl_epi64 ((__m128i *)srcv64 );
          srcv64++;
          __m128i y0r0 = _mm_load_si128 (srcy128r0++);
          __m128i y0r1 = _mm_load_si128 (srcy128r1++);
          //{{{  constant y factors
          __m128i y00r0 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpacklo_epi8 (y0r0, zero), ysub), facy);
          __m128i y01r0 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpackhi_epi8 (y0r0, zero), ysub), facy);
          __m128i y00r1 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpacklo_epi8 (y0r1, zero), ysub), facy);
          __m128i y01r1 = _mm_mullo_epi16 (_mm_sub_epi16 (_mm_unpackhi_epi8 (y0r1, zero), ysub), facy);
          //}}}
          //{{{  expand u and v so they're aligned with y values
          u0 = _mm_unpacklo_epi8 (u0, zero);
          __m128i u00 = _mm_sub_epi16 (_mm_unpacklo_epi16 (u0, u0), uvsub);
          __m128i u01 = _mm_sub_epi16 (_mm_unpackhi_epi16 (u0, u0), uvsub);

          v0 = _mm_unpacklo_epi8( v0,  zero );
          __m128i v00 = _mm_sub_epi16 (_mm_unpacklo_epi16 (v0, v0), uvsub);
          __m128i v01 = _mm_sub_epi16 (_mm_unpackhi_epi16 (v0, v0), uvsub);
          //}}}
          //{{{  common factors on both rows.
          __m128i rv00 = _mm_mullo_epi16 (facrv, v00);
          __m128i rv01 = _mm_mullo_epi16 (facrv, v01);
          __m128i gu00 = _mm_mullo_epi16 (facgu, u00);
          __m128i gu01 = _mm_mullo_epi16 (facgu, u01);
          __m128i gv00 = _mm_mullo_epi16 (facgv, v00);
          __m128i gv01 = _mm_mullo_epi16 (facgv, v01);
          __m128i bu00 = _mm_mullo_epi16 (facbu, u00);
          __m128i bu01 = _mm_mullo_epi16 (facbu, u01);
          //}}}
          //{{{  row 0
          __m128i r00 = _mm_srai_epi16 (_mm_add_epi16 (y00r0, rv00), 6);
          __m128i r01 = _mm_srai_epi16 (_mm_add_epi16 (y01r0, rv01), 6);
          __m128i g00 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y00r0, gu00), gv00), 6);
          __m128i g01 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y01r0, gu01), gv01), 6);
          __m128i b00 = _mm_srai_epi16 (_mm_add_epi16 (y00r0, bu00), 6);
          __m128i b01 = _mm_srai_epi16 (_mm_add_epi16 (y01r0, bu01), 6);

          r00 = _mm_packus_epi16 (r00, r01);                // rrrr.. saturated
          g00 = _mm_packus_epi16 (g00, g01);                // gggg.. saturated
          b00 = _mm_packus_epi16 (b00, b01);                // bbbb.. saturated

          r01 = _mm_unpacklo_epi8 (r00, zero);              // 0r0r..
          __m128i gbgb    = _mm_unpacklo_epi8 (b00, g00);   // gbgb..
          __m128i rgb0123 = _mm_unpacklo_epi16 (gbgb, r01); // 0rgb0rgb..
          __m128i rgb4567 = _mm_unpackhi_epi16 (gbgb, r01); // 0rgb0rgb..

          r01 = _mm_unpackhi_epi8 (r00, zero);
          gbgb = _mm_unpackhi_epi8 (b00, g00);
          __m128i rgb89ab = _mm_unpacklo_epi16 (gbgb, r01);
          __m128i rgbcdef = _mm_unpackhi_epi16 (gbgb, r01);

          _mm_stream_si128 (dstrgb128r0++, rgb0123);
          _mm_stream_si128 (dstrgb128r0++, rgb4567);
          _mm_stream_si128 (dstrgb128r0++, rgb89ab);
          _mm_stream_si128 (dstrgb128r0++, rgbcdef);
          //}}}
          //{{{  row 1
          r00 = _mm_srai_epi16 (_mm_add_epi16 (y00r1, rv00), 6);
          r01 = _mm_srai_epi16 (_mm_add_epi16 (y01r1, rv01), 6);
          g00 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y00r1, gu00), gv00), 6);
          g01 = _mm_srai_epi16 (_mm_sub_epi16 (_mm_sub_epi16 (y01r1, gu01), gv01), 6);
          b00 = _mm_srai_epi16 (_mm_add_epi16 (y00r1, bu00), 6);
          b01 = _mm_srai_epi16 (_mm_add_epi16 (y01r1, bu01), 6);

          r00 = _mm_packus_epi16 (r00, r01);        // rrrr.. saturated
          g00 = _mm_packus_epi16 (g00, g01);        // gggg.. saturated
          b00 = _mm_packus_epi16 (b00, b01);        // bbbb.. saturated

          r01     = _mm_unpacklo_epi8 (r00, zero);  // 0r0r..
          gbgb    = _mm_unpacklo_epi8 (b00, g00);   // gbgb..
          rgb0123 = _mm_unpacklo_epi16 (gbgb, r01); // 0rgb0rgb..
          rgb4567 = _mm_unpackhi_epi16 (gbgb, r01); // 0rgb0rgb..

          r01     = _mm_unpackhi_epi8 (r00, zero);
          gbgb    = _mm_unpackhi_epi8 (b00, g00);
          rgb89ab = _mm_unpacklo_epi16 (gbgb, r01);
          rgbcdef = _mm_unpackhi_epi16 (gbgb, r01);

          _mm_stream_si128 (dstrgb128r1++, rgb0123);
          _mm_stream_si128 (dstrgb128r1++, rgb4567);
          _mm_stream_si128 (dstrgb128r1++, rgb89ab);
          _mm_stream_si128 (dstrgb128r1++, rgbcdef);
          //}}}
          }
        }
      mOk = true;
      }
    //}}}
  #endif
    //{{{
    void clear() {
      mOk = false;
      mPts = 0;
      }
    //}}}

  private:
    bool mOk = false;
    uint64_t mPts = 0;

    int mWidth = 0;
    int mHeight = 0;
    int mYStride = 0;
    int mUVStride = 0;

    uint8_t* mYbuf = nullptr;
    uint8_t* mUbuf = nullptr;
    uint8_t* mVbuf = nullptr;

    uint32_t* mBgra = nullptr;
    };
  //}}}

  cVideoDecode() {}
  //{{{
  virtual ~cVideoDecode() {

    for (auto frame : mFramePool)
      delete frame;
    }
  //}}}

  int getWidth() { return mWidth; }
  int getHeight() { return mHeight; }
  int getFramePoolSize() { return (int)mFramePool.size(); }
  virtual int getSurfacePoolSize() = 0;

  void setPlayPts (uint64_t playPts) { mPlayPts = playPts; }
  //{{{
  cFrame* findPlayFrame() {
  // returns nearest frame within a 25fps frame of mPlayPts, nullptr if none

    uint64_t nearDist = 90000 / 25;

    cFrame* nearFrame = nullptr;
    for (auto frame : mFramePool) {
      if (frame->ok()) {
        uint64_t dist = frame->getPts() > mPlayPts ? frame->getPts() - mPlayPts : mPlayPts - frame->getPts();
        if (dist < nearDist) {
          nearDist = dist;
          nearFrame = frame;
          }
        }
      }

    return nearFrame;
    }
  //}}}
  //{{{
  void clear() {
  // returns nearest frame within a 25fps frame of mPlayPts, nullptr if none

    for (auto frame : mFramePool)
      frame->clear();
    }
  //}}}

  virtual void decode (uint64_t pts, uint8_t* pesBuffer, unsigned int pesBufferLen) = 0;

protected:
  static constexpr int kMaxVideoFramePoolSize = 200;
  //{{{
  cFrame* getFreeFrame (uint64_t pts) {
  // return first frame older than mPlayPts, otherwise add new frame

    while (true) {
      for (auto frame : mFramePool) {
        if (frame->ok() && (frame->getPts() < mPlayPts)) {
          // reuse frame
          frame->set (pts);
          return frame;
          }
        }

      if (mFramePool.size() < kMaxVideoFramePoolSize) {
        // allocate new frame
        mFramePool.push_back (new cFrame (pts));
        cLog::log (LOGINFO1, "allocated newFrame %d for %u at play:%u", mFramePool.size(), pts, mPlayPts);
        return mFramePool.back();
        }
      else
        this_thread::sleep_for (40ms);
      }

    // cannot reach here
    return nullptr;
    }
  //}}}

  int mWidth = 0;
  int mHeight = 0;

  uint64_t mPlayPts = 0;
  vector <cFrame*> mFramePool;
  };
//}}}
//{{{
class cSongWidget : public cWidget {
public:
  cSongWidget (cSong* song, float width, float height) : cWidget (COL_BLUE, width, height), mSong(song) {}
  virtual ~cSongWidget() {}

  //{{{
  void layout() {

    //cD2dWindow::cBox::layout();
    //cLog::log (LOGINFO, "cSongBox::layout %d %d %d %d", mRect.left, mRect.top, mRect.right, mRect.bottom);

    // invalidate frame bitmap
    //mFramesBitmapOk = false;

    // invalidate overview bitmap
    //mOverviewBitmapOk = false;
    }
  //}}}
  //{{{
  void onWheel (float delta) {

    //if (getShow())
      setZoom (mZoom - ((int)delta/120));
    }
  //}}}
  //{{{
  void onDown (float x, float y) {

    cWidget::onDown (x, y);

    //std::shared_lock<std::shared_mutex> lock (mSong->getSharedMutex());
    if (y > mDstOverviewTop) {
      auto frame = mSong->getFirstFrame() + int((x * mSong->getTotalFrames()) / getWidth());
      mSong->setPlayFrame (frame);
      mOverviewPressed = true;
      }

    else if (y > mDstRangeTop) {
      mPressedFrame = mSong->getPlayFrame() + ((x - (getWidth()/2.f)) * mFrameStep / mFrameWidth);
      mSong->getSelect().start (int(mPressedFrame));
      mRangePressed = true;
      //mWindow->changed();
      }

    else
      mPressedFrame = (float)mSong->getPlayFrame();
    }
  //}}}
  //{{{
  void onMove (float x, float y, float xinc, float yinc) {

    cWidget::onMove (x, y, xinc, yinc);

    //std::shared_lock<std::shared_mutex> lock (mSong.getSharedMutex());
    if (mOverviewPressed)
      mSong->setPlayFrame (mSong->getFirstFrame() + int((x * mSong->getTotalFrames()) / getWidth()));

    else if (mRangePressed) {
      mPressedFrame += (xinc / mFrameWidth) * mFrameStep;
      mSong->getSelect().move ((int)mPressedFrame);
      //mWindow->changed();
      }

    else {
      mPressedFrame -= (xinc / mFrameWidth) * mFrameStep;
      mSong->setPlayFrame ((int)mPressedFrame);
      }
    }
  //}}}
  //{{{
  void onUp() {
    cWidget::onUp();

    mSong->getSelect().end();
    mOverviewPressed = false;
    mRangePressed = false;
    }
  //}}}

  //{{{
  void onDraw (iDraw* draw) {

    auto context = draw->getContext();

    mWaveHeight = 100.f;
    mOverviewHeight = 100.f;
    mRangeHeight = 8.f;
    mFreqHeight = mHeight - mOverviewHeight - mRangeHeight - mWaveHeight;

    mDstFreqTop = 0;
    mDstWaveTop = mDstFreqTop + mFreqHeight;
    mDstRangeTop = mDstWaveTop + mWaveHeight;
    mDstOverviewTop = mDstRangeTop + mRangeHeight;
    mDstWaveCentre = mDstWaveTop + (mWaveHeight/2.f);
    mDstOverviewCentre = mDstOverviewTop + (mOverviewHeight/2.f);

    // lock
    std::shared_lock<std::shared_mutex> lock (mSong->getSharedMutex());

    // draw
    auto playFrame = mSong->getPlayFrame();

    // wave left right frames, clip right not left
    auto leftWaveFrame = playFrame - (((int(mWidth)+mFrameWidth)/2) * mFrameStep) / mFrameWidth;
    auto rightWaveFrame = playFrame + (((int(mWidth)+mFrameWidth)/2) * mFrameStep) / mFrameWidth;
    rightWaveFrame = std::min (rightWaveFrame, mSong->getLastFrame());
    bool mono = mSong->getNumChannels() == 1;

    drawRange (context, playFrame, leftWaveFrame, rightWaveFrame);

    if (mSong->getNumFrames()) {
      drawWave (context, playFrame, leftWaveFrame, rightWaveFrame, mono);
      drawOverview (context, playFrame, mono);
      drawFreq (context, playFrame);
      }

    if (mSong->hasHlsBase())
      drawTime (context,
                getFrameString (mSong->getFirstFrame()),
                getFrameString (mSong->getPlayFrame()),
                getFrameString (mSong->getLastFrame()));
    else
      drawTime (context, "", getFrameString (mSong->getPlayFrame()), getFrameString (mSong->getTotalFrames()));
    }
  //}}}

private:
  //{{{
  std::string getFrameString (int frame) {

    if (mSong->getSamplesPerFrame() && mSong->getSampleRate()) {
      // can turn frame into seconds
      auto value = ((uint64_t)frame * mSong->getSamplesPerFrame()) / (mSong->getSampleRate() / 100);
      auto subSeconds = value % 100;

      value /= 100;
      //value += mWindow->getDayLightSeconds();
      auto seconds = value % 60;

      value /= 60;
      auto minutes = value % 60;

      value /= 60;
      auto hours = value % 60;

      // !!! must be a better formatter lib !!!
      return (hours > 0) ? (dec (hours) + ':' + dec (minutes, 2, '0') + ':' + dec(seconds, 2, '0')) :
               ((minutes > 0) ? (dec (minutes) + ':' + dec(seconds, 2, '0') + ':' + dec(subSeconds, 2, '0')) :
                 (dec(seconds) + ':' + dec(subSeconds, 2, '0')));
      }
    else
      return ("--:--:--");
    }
  //}}}

  //{{{
  void setZoom (int zoom) {

    mZoom = std::min (std::max (zoom, mMinZoom), mMaxZoom);

    // zoomIn expanding frame to mFrameWidth pix
    mFrameWidth = (mZoom < 0) ? -mZoom+1 : 1;

    // zoomOut summing mFrameStep frames per pix
    mFrameStep = (mZoom > 0) ? mZoom+1 : 1;
    }
  //}}}

  //{{{
  void drawRange (cVg* context, int playFrame, int leftFrame, int rightFrame) {

    //cRect dstRect = { mRect.left, mDstRangeTop, mRect.right, mDstRangeTop + mRangeHeight };
    //dc->FillRectangle (dstRect, mWindow->getDarkGrayBrush());

    for (auto &item : mSong->getSelect().getItems()) {
      auto firstx = (getWidth()/2.f) + (item.getFirstFrame() - playFrame) * mFrameWidth / mFrameStep;
      float lastx = item.getMark() ? firstx + 1.f :
                                     (getWidth()/2.f) + (item.getLastFrame() - playFrame) * mFrameWidth / mFrameStep;

      //dstRect = { mRect.left + firstx, mDstRangeTop, mRect.left + lastx, mDstRangeTop + mRangeHeight };
      //dc->FillRectangle (dstRect, mWindow->getYellowBrush());

      auto title = item.getTitle();
      if (!title.empty()) {
        //dstRect = { mRect.left + firstx + 2.f, mDstRangeTop + mRangeHeight - mWindow->getTextFormat()->GetFontSize(),
        //            mRect.right, mDstRangeTop + mRangeHeight + 4.f };
        //dc->DrawText (std::wstring (title.begin(), title.end()).data(), (uint32_t)title.size(), mWindow->getTextFormat(),
        //              dstRect, mWindow->getWhiteBrush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
      }
    }
  //}}}
  //{{{
  void drawWave (cVg* context, int playFrame, int leftFrame, int rightFrame, bool mono) {

    float values[2] = { 0.f };
    float xlen = (float)mFrameStep;

    float peakValueScale = mWaveHeight / 2.f / mSong->getMaxPeakValue();
    float powerValueScale = mWaveHeight / 2.f / mSong->getMaxPowerValue();

    float xorg = mX;
    if (mFrameStep == 1) {
      //{{{  draw all peak values
      context->beginPath();

      for (auto frame = leftFrame; frame < rightFrame; frame += mFrameStep) {
        // get peak values scaled to maxPeak
        auto framePtr = mSong->getAudioFramePtr (frame);
        if (framePtr) {
          //{{{  draw frame peak values
          if (framePtr->getPowerValues()) {
            auto peakValuesPtr = framePtr->getPeakValues();
            for (auto i = 0; i < 2; i++)
              values[i] = *peakValuesPtr++ * peakValueScale;
            }
          context->rect (xorg, mY + mDstWaveCentre - values[0], xlen, values[0] + values[1]);
          }
          //}}}
        xorg += 1.f;
        }

      context->fillColor (kVgDarkGrey);
      context->triangleFill();
      }
      //}}}

    xorg = mX;
    //{{{  draw powerValues before playFrame
    context->beginPath();

    for (auto frame = leftFrame; frame < playFrame; frame += mFrameStep) {
      auto framePtr = mSong->getAudioFramePtr (frame);
      if (framePtr) {
        if (mFrameStep == 1) {
          //{{{  power scaled to maxPeak
          if (framePtr->getPowerValues()) {
           auto powerValuesPtr = framePtr->getPowerValues();
            for (auto i = 0; i < 2; i++)
              values[i] = *powerValuesPtr++ * peakValueScale;
            }
          }
          //}}}
        else {
          //{{{  sum mFrameStep frames, mFrameStep aligned, power scaled to maxPower
          for (auto i = 0; i < 2; i++)
            values[i] = 0.f;

          auto alignedFrame = frame - (frame % mFrameStep);
          auto toSumFrame = std::min (alignedFrame + mFrameStep, rightFrame);
          for (auto sumFrame = alignedFrame; sumFrame < toSumFrame; sumFrame++) {
            auto framePtr = mSong->getAudioFramePtr (sumFrame);
            if (framePtr) {
              if (framePtr->getPowerValues()) {
                auto powerValuesPtr = framePtr->getPowerValues();
                for (auto i = 0; i < 2; i++)
                  values[i] += *powerValuesPtr++ * powerValueScale;
                }
              }
            }

          for (auto i = 0; i < 2; i++)
            values[i] /= toSumFrame - alignedFrame + 1;
          }
          //}}}
        context->rect (xorg, mY + mDstWaveCentre - values[0], xlen, values[0] + values[1]);
        }

      xorg += 1.f;
      }

    context->fillColor (kVgBlue);
    context->triangleFill();
    //}}}
    //{{{  draw playFrame powerValue
    // power scaled to maxPeak
    context->beginPath();

    auto framePtr = mSong->getAudioFramePtr (playFrame);
    if (framePtr) {
      //  draw play frame power scaled to maxPeak
      if (framePtr->getPowerValues()) {
        auto powerValuesPtr = framePtr->getPowerValues();
        for (auto i = 0; i < 2; i++)
          values[i] = *powerValuesPtr++ * peakValueScale;
        }
      context->rect (xorg, mY + mDstWaveCentre - values[0], xlen, values[0] + values[1]);
      }

    xorg += 1.f;

    context->fillColor (kVgWhite);
    context->triangleFill();
    //}}}
    //{{{  draw powerValues after playFrame
    context->beginPath();

    for (auto frame = playFrame+mFrameStep; frame < rightFrame; frame += mFrameStep) {
      auto framePtr = mSong->getAudioFramePtr (frame);
      if (framePtr) {
        if (mFrameStep == 1) {
          //{{{  power scaled to maxPeak
          if (framePtr->getPowerValues()) {
            auto powerValuesPtr = framePtr->getPowerValues();
            for (auto i = 0; i < 2; i++)
              values[i] = *powerValuesPtr++ * peakValueScale;
            }
          }
          //}}}
        else {
          //{{{  sum mFrameStep frames, mFrameStep aligned, power scaled to maxPower
          for (auto i = 0; i < 2; i++)
            values[i] = 0.f;

          auto alignedFrame = frame - (frame % mFrameStep);
          auto toSumFrame = std::min (alignedFrame + mFrameStep, rightFrame);
          for (auto sumFrame = alignedFrame; sumFrame < toSumFrame; sumFrame++) {
            auto framePtr = mSong->getAudioFramePtr (sumFrame);
            if (framePtr) {
              if (framePtr->getPowerValues()) {
                auto powerValuesPtr = framePtr->getPowerValues();
                for (auto i = 0; i < 2; i++)
                  values[i] += *powerValuesPtr++ * powerValueScale;
                }
              }
            }

          for (auto i = 0; i < 2; i++)
            values[i] /= toSumFrame - alignedFrame + 1;
          }
          //}}}
        context->rect (xorg, mY + mDstWaveCentre - values[0], xlen, values[0] + values[1]);
        }

      xorg += 1.f;
      }

    context->fillColor (kVgGrey);
    context->triangleFill();
    //}}}

    //{{{  copy reversed spectrum column to bitmap, clip high freqs to height
    //int freqSize = std::min (mSong->getNumFreqBytes(), (int)mFreqHeight);
    //int freqOffset = mSong->getNumFreqBytes() > (int)mFreqHeight ? mSong->getNumFreqBytes() - (int)mFreqHeight : 0;

    // bitmap sampled aligned to mFrameStep, !!! could sum !!! ?? ok if neg frame ???
    //auto alignedFromFrame = fromFrame - (fromFrame % mFrameStep);
    //for (auto frame = alignedFromFrame; frame < toFrame; frame += mFrameStep) {
      //auto framePtr = mSong->getAudioFramePtr (frame);
      //if (framePtr) {
        //if (framePtr->getFreqLuma()) {
          //uint32_t bitmapIndex = getSrcIndex (frame);
          //D2D1_RECT_U bitmapRectU = { bitmapIndex, 0, bitmapIndex+1, (UINT32)freqSize };
          //mBitmap->CopyFromMemory (&bitmapRectU, framePtr->getFreqLuma() + freqOffset, 1);
          //}
        //}
      //}
    //}}}
    }
  //}}}
  //{{{
  void drawFreq (cVg* context, int playFrame) {

    float valueScale = 100.f / 255.f;
    float xorg = mX;
    float xlen = 2.f;

    context->beginPath();
    auto framePtr = mSong->getAudioFramePtr (playFrame);
    if (framePtr && framePtr->getFreqValues()) {
      auto freqValues = framePtr->getFreqValues();
      for (auto i = 0; (i < mSong->getNumFreqBytes()) && ((i*2) < int(mWidth)); i++) {
        auto value =  freqValues[i] * valueScale;
        if (value > 1.f)
          context->rect (xorg, mY + mHeight - value, xlen, value);
        xorg += 2.f;
        }
      }

    context->fillColor (kVgYellow);
    context->triangleFill();
    }
  //}}}
  //{{{
  void drawTime (cVg* context, const std::string& firstFrameString,
                 const std::string& playFrameString, const std::string& lastFrameString) {

    // small coloured lastFrameString, right
    context->fontSize ((float)getFontHeight());
    context->textAlign (cVg::ALIGN_RIGHT | cVg::ALIGN_TOP);
    if (mSong->getHlsLoad() == cSong::eHlsIdle)
      context->fillColor (kVgWhite);
    else if (mSong->getHlsLoad() == cSong::eHlsFailed)
      context->fillColor (kVgRed);
    else
      context->fillColor (kVgGreen);
    context->text (mWidth, mHeight-getFontHeight(), lastFrameString);

    // white
    context->fillColor (kVgWhite);

    // small firstFrameString white left
    context->textAlign (cVg::ALIGN_LEFT | cVg::ALIGN_TOP);
    context->text (0.f, mHeight-getFontHeight(), firstFrameString);

    // big playFrameString white centred
    context->fontSize ((float)getBigFontHeight());
    context->textAlign (cVg::ALIGN_CENTER | cVg::ALIGN_TOP);
    context->text (mWidth/2.f, mHeight-getBigFontHeight(), playFrameString);
    }
  //}}}

  //{{{
  void drawOverviewWave (cVg* context, int firstFrame, int playFrame, float playFrameX, float valueScale, bool mono) {
  // dumb overview, expensive but simple, should cache but don't need bitmap

    int lastFrame = mSong->getLastFrame();
    int totalFrames = mSong->getTotalFrames();

    context->beginPath();

    float values[2] = { 0.f };
    float xorg = mX;
    float xlen = 1.f;
    for (auto x = 0; x < int(mWidth); x++) {
      // iterate widget width
      int frame = firstFrame + ((x * totalFrames) / int(mWidth));
      int toFrame = firstFrame + (((x+1) * totalFrames) / int(mWidth));
      if (toFrame > lastFrame)
        toFrame = lastFrame+1;

      auto framePtr = mSong->getAudioFramePtr (frame);
      if (framePtr && framePtr->getPowerValues()) {
        // accumulate frame, handle silence better
        float* powerValues = framePtr->getPowerValues();
        values[0] = powerValues[0];
        values[1] = mono ? 0 : powerValues[1];

        if (frame < toFrame) {
          int numSummedFrames = 1;
          frame++;
          while (frame < toFrame) {
            framePtr = mSong->getAudioFramePtr (frame);
            if (framePtr) {
              if (framePtr->getPowerValues()) {
                auto powerValues = framePtr->getPowerValues();
                values[0] += powerValues[0];
                values[1] += mono ? 0 : powerValues[1];
                numSummedFrames++;
                }
              }
            frame++;
            }
          values[0] /= numSummedFrames;
          values[1] /= numSummedFrames;
          }
        values[0] *= valueScale;
        values[1] *= valueScale;
        context->rect (xorg, mY + mDstOverviewCentre -  values[0], xlen,  values[0] + values[1]);
        }

      xorg += 1.f;
      }
    context->fillColor (kVgGrey);
    context->triangleFill();

    // possible cache to stop recalc
    mOverviewValueScale = valueScale;
    mOverviewFirstFrame = firstFrame;
    mOverviewLastFrame = lastFrame;
    mOverviewTotalFrames = totalFrames;
    }
  //}}}
  //{{{
  void drawOverviewLens (cVg* context, int playFrame, float centreX, float width, bool mono) {
  // draw frames centred at playFrame -/+ width in pixels, centred at centreX

    cLog::log (LOGINFO, "drawOverviewLens %d %f %f", playFrame, centreX, width);

    // cut hole and frame it
    context->beginPath();
    context->rect (mX + centreX - width, mY + mDstOverviewTop, width * 2.f, mOverviewHeight);
    context->fillColor (kVgBlack);
    context->triangleFill();
    // frame in yellow

    // calc leftmost frame, clip to valid frame, adjust firstX which may overlap left up to mFrameWidth
    float leftFrame = playFrame - width;
    float firstX = centreX - (playFrame - leftFrame);
    if (leftFrame < 0) {
      firstX += -leftFrame;
      leftFrame = 0;
      }

    int rightFrame = (int)(playFrame + width);
    rightFrame = std::min (rightFrame, mSong->getLastFrame());

    // calc lens max power
    float maxPowerValue = 0.f;
    for (auto frame = int(leftFrame); frame <= rightFrame; frame++) {
      auto framePtr = mSong->getAudioFramePtr (frame);
      if (framePtr && framePtr->getPowerValues()) {
        auto powerValues = framePtr->getPowerValues();
        maxPowerValue = std::max (maxPowerValue, powerValues[0]);
        if (!mono)
          maxPowerValue = std::max (maxPowerValue, powerValues[1]);
        }
      }

    // draw unzoomed waveform, start before playFrame
    context->beginPath();
    float xorg = mX + firstX;
    float valueScale = mOverviewHeight / 2.f / maxPowerValue;
    for (auto frame = int(leftFrame); frame <= rightFrame; frame++) {
      auto framePtr = mSong->getAudioFramePtr (frame);
      if (framePtr && framePtr->getPowerValues()) {
        //if (framePtr->hasTitle()) {
          //{{{  draw song title yellow bar and text
          //cRect barRect = { dstRect.left-1.f, mDstOverviewTop, dstRect.left+1.f, mRect.bottom };
          //dc->FillRectangle (barRect, mWindow->getYellowBrush());

          //auto str = framePtr->getTitle();
          //dc->DrawText (std::wstring (str.begin(), str.end()).data(), (uint32_t)str.size(), mWindow->getTextFormat(),
                        //{ dstRect.left+2.f, mDstOverviewTop, getWidth(), mDstOverviewTop + mOverviewHeight },
                        //mWindow->getWhiteBrush());
          //}
          //}}}
        //if (framePtr->isSilence()) {
          //{{{  draw red silence
          //dstRect.top = mDstOverviewCentre - 2.f;
          //dstRect.bottom = mDstOverviewCentre + 2.f;
          //dc->FillRectangle (dstRect, mWindow->getRedBrush());
          //}
          //}}}

        if (frame == playFrame) {
          //{{{  finish before playFrame
          context->fillColor (kVgBlue);
          context->triangleFill();
          context->beginPath();
          }
          //}}}

        auto powerValues = framePtr->getPowerValues();
        float yorg = mono ? mDstOverviewTop + mOverviewHeight - (powerValues[0] * valueScale * 2.f) :
                            mDstOverviewCentre - (powerValues[0] * valueScale);
        float ylen = mono ? powerValues[0] * valueScale * 2.f  :
                            (powerValues[0] + powerValues[1]) * valueScale;
        context->rect (xorg, mY + yorg, 1.f, ylen);

        if (frame == playFrame) {
          //{{{  finish playFrame, start after playFrame
          context->fillColor (kVgWhite);
          context->triangleFill();
          context->beginPath();
          }
          //}}}
        }

      xorg += 1.f;
      }
    // finish after playFrame
    context->fillColor (kVgGrey);
    context->triangleFill();
    }
  //}}}
  //{{{
  void drawOverview (cVg* context, int playFrame, bool mono) {

    if (!mSong->getTotalFrames())
      return;

    int firstFrame = mSong->getFirstFrame();
    float playFrameX = ((playFrame - firstFrame) * mWidth) / mSong->getTotalFrames();
    float valueScale = mOverviewHeight / 2.f / mSong->getMaxPowerValue();
    drawOverviewWave (context, firstFrame, playFrame, playFrameX, valueScale, mono);

    if (mOverviewPressed) {
      //{{{  animate on
      if (mOverviewLens < getWidth() / 16.f) {
        mOverviewLens += getWidth() / 16.f / 6.f;
        //mWindow->changed();
        }
      }
      //}}}
    else {
      //{{{  animate off
      if (mOverviewLens > 1.f) {
        mOverviewLens /= 2.f;
        //mWindow->changed();
        }
      else if (mOverviewLens > 0.f) {
        // finish animate
        mOverviewLens = 0.f;
        //mWindow->changed();
        }
      }
      //}}}

    if (mOverviewLens > 0.f) {
      float overviewLensCentreX = (float)playFrameX;
      if (overviewLensCentreX - mOverviewLens < 0.f)
        overviewLensCentreX = (float)mOverviewLens;
      else if (overviewLensCentreX + mOverviewLens > mWidth)
        overviewLensCentreX = mWidth - mOverviewLens;

      drawOverviewLens (context, playFrame, overviewLensCentreX, mOverviewLens-1.f, mono);
      }

    else {
      //  draw playFrame

      auto framePtr = mSong->getAudioFramePtr (playFrame);
      if (framePtr && framePtr->getPowerValues()) {
        context->beginPath();
        auto powerValues = framePtr->getPowerValues();
        float yorg = mono ? (mDstOverviewTop + mOverviewHeight - (powerValues[0] * valueScale * 2.f)) :
                            (mDstOverviewCentre - (powerValues[0] * valueScale));
        float ylen = mono ? (powerValues[0] * valueScale * 2.f) : ((powerValues[0] + powerValues[1]) * valueScale);
        context->rect (mX+playFrameX, mY+yorg, 1.f, ylen);
        context->fillColor (kVgWhite);
        context->triangleFill();
        }
      }
    }
  //}}}

  //{{{  vars
  cSong* mSong;
  float mMove = 0;
  bool mMoved = false;
  float mPressInc = 0;

  // zoom - 0 unity, > 0 zoomOut framesPerPix, < 0 zoomIn pixPerFrame
  int mZoom = 0;
  int mMinZoom = -8;
  int mMaxZoom = 8;
  int mFrameWidth = 1;
  int mFrameStep = 1;

  float mPressedFrame = 0.f;
  bool mOverviewPressed = false;
  bool mRangePressed = false;

  int mOverviewFirstFrame = 0;
  int mOverviewLastFrame = 0;
  int mOverviewTotalFrames = 0;
  float mOverviewValueScale = 1.f;
  float mOverviewLens = 0.f;

  // vertical layout
  float mFreqHeight = 0.f;
  float mWaveHeight = 0.f;
  float mRangeHeight = 0.f;
  float mOverviewHeight = 0.f;

  float mDstFreqTop = 0.f;
  float mDstWaveTop = 0.f;
  float mDstRangeTop = 0.f;
  float mDstOverviewTop = 0.f;

  float mDstWaveCentre = 0.f;
  float mDstOverviewCentre = 0.f;
  //}}}
  };
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height, bool headless, bool moreLogInfo,
            int channelNum, int audBitrate, int vidBitrate)  {

    mMoreLogInfo = moreLogInfo;

    //mVideoDecode = new cFFmpegVideoDecode();

    if (headless) {
      thread ([=](){ hlsThread (kHost, kChannels[channelNum], audBitrate, vidBitrate); }).detach();
      while (true)
        this_thread::sleep_for (200ms);
       }

    else {
      initialise (title, width, height, (unsigned char*)droidSansMono);
      add (new cSongWidget (&mSong, 0,0));
      thread ([=](){ hlsThread (kHost, kChannels[channelNum], audBitrate, vidBitrate); }).detach();
      glClearColor (0, 0, 0, 1.f);
      cGlWindow::run();
      }

    cLog::log (LOGINFO, "run exit");
    }
  //}}}

protected:
  //{{{
  void onKey (int key, int scancode, int action, int mods) {

    //mods == GLFW_MOD_SHIFT
    //mods == GLFW_MOD_CONTROL
    if ((action == GLFW_PRESS) || (action == GLFW_REPEAT)) {
      switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose (mWindow, GL_TRUE); break;
        case GLFW_KEY_SPACE : break;
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
        case GLFW_KEY_5:
        case GLFW_KEY_6: break;

        case GLFW_KEY_RIGHT: break;
        case GLFW_KEY_LEFT:  break;
        case GLFW_KEY_DOWN:  break;
        case GLFW_KEY_UP:    break;
        case GLFW_KEY_PAGE_UP:   break;
        case GLFW_KEY_PAGE_DOWN: break;
        //case GLFW_KEY_HOME:
        //case GLFW_KEY_END:

        case GLFW_KEY_V: toggleVsync(); break;
        case GLFW_KEY_P: togglePerf(); break;
        case GLFW_KEY_S: toggleStats(); break;
        case GLFW_KEY_T: toggleTests(); break;

        case GLFW_KEY_I: toggleSolid(); break;
        case GLFW_KEY_A: toggleEdges(); break;
        case GLFW_KEY_Q: fringeWidth (getFringeWidth() - 0.25f); break;
        case GLFW_KEY_W: fringeWidth (getFringeWidth() + 0.25f); break;

        case GLFW_KEY_L:
          mMoreLogInfo = ! mMoreLogInfo;
          cLog::setLogLevel (mMoreLogInfo ? LOGINFO3 : LOGNOTICE);
          break;

        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
  void onChar (char ch, int mods) {}

private:
  //{{{
  static uint64_t getPts (const uint8_t* ts) {
  // return 33 bits of pts,dts

    if ((ts[0] & 0x01) && (ts[2] & 0x01) && (ts[4] & 0x01)) {
      // valid marker bits
      uint64_t pts = ts[0] & 0x0E;
      pts = (pts << 7) | ts[1];
      pts = (pts << 8) | (ts[2] & 0xFE);
      pts = (pts << 7) | ts[3];
      pts = (pts << 7) | (ts[4] >> 1);
      return pts;
      }
    else {
      cLog::log (LOGERROR, "getPts marker bits - %02x %02x %02x %02x 0x02", ts[0], ts[1], ts[2], ts[3], ts[4]);
      return 0;
      }
    }
  //}}}
  //{{{
  static string getTagValue (uint8_t* buffer, const char* tag) {

    const char* tagPtr = strstr ((const char*)buffer, tag);
    const char* valuePtr = tagPtr + strlen (tag);
    const char* endPtr = strchr (valuePtr, '\n');

    return string (valuePtr, endPtr - valuePtr);
    }
  //}}}

  //{{{
  void hlsThread (const string& host, const string& channel, int audBitrate, int vidBitrate) {
  // hls http chunk load and decode thread

    cLog::setThreadName ("hls ");

    constexpr int kHlsPreload = 2;

    uint8_t* pesBuffer = nullptr;
    int pesBufferLen = 0;

    mSong.setChannel (channel);
    mSong.setBitrate (audBitrate, audBitrate < 128000 ? 180 : 360); // audBitrate, audioFrames per chunk

    while (true) {
      const string path = "pool_902/live/uk/" + mSong.getChannel() +
                          "/" + mSong.getChannel() +
                          ".isml/" + mSong.getChannel() +
                          (mSong.getBitrate() < 128000 ? "-pa3=" : "-pa4=") + dec(mSong.getBitrate()) +
                          "-video=" + dec(vidBitrate);
      cPlatformHttp http;
      string redirectedHost = http.getRedirect (host, path + ".m3u8");
      if (http.getContent()) {
        //{{{  got .m3u8, parse for mediaSequence, programDateTimePoint
        int mediaSequence = stoi (getTagValue (http.getContent(), "#EXT-X-MEDIA-SEQUENCE:"));

        istringstream inputStream (getTagValue (http.getContent(), "#EXT-X-PROGRAM-DATE-TIME:"));
        system_clock::time_point programDateTimePoint;
        inputStream >> date::parse ("%FT%T", programDateTimePoint);

        http.freeContent();
        //}}}

        mSong.init (cAudioDecode::eAac, 2, 48000, mSong.getBitrate() < 128000 ? 2048 : 1024); // samplesPerFrame
        mSong.setHlsBase (mediaSequence, programDateTimePoint, -37s, (2*60*60) - 30);
        cAudioDecode audioDecode (cAudioDecode::eAac);

        thread player;
        bool firstTime = true;
        mSongChanged = false;
        while (true && !mSongChanged) {
          int chunkNum = mSong.getHlsLoadChunkNum (system_clock::now(), 12s, kHlsPreload);
          if (chunkNum) {
            // get hls chunkNum chunk
            mSong.setHlsLoad (cSong::eHlsLoading, chunkNum);
            if (http.get (redirectedHost, path + '-' + dec(chunkNum) + ".ts") == 200) {
              //{{{  process audio first
              int seqFrameNum = mSong.getHlsFrameFromChunkNum (chunkNum);

              // parse ts packets
              uint64_t pts = 0;
              uint8_t* ts = http.getContent();
              uint8_t* tsEnd = ts + http.getContentSize();
              while ((ts < tsEnd) && (*ts++ == 0x47)) {
                auto payStart = ts[0] & 0x40;
                auto pid = ((ts[0] & 0x1F) << 8) | ts[1];
                auto headerBytes = (ts[2] & 0x20) ? 4 + ts[3] : 3;
                ts += headerBytes;
                auto tsBodyBytes = 187 - headerBytes;

                if (pid == 34) {
                  // audio pid
                  if (payStart && !ts[0] && !ts[1] && (ts[2] == 1) && (ts[3] == 0xC0)) {
                    if (pesBufferLen) {
                      //  process prev audioPes
                      uint8_t* pesBufferPtr = pesBuffer;
                      uint8_t* pesBufferEnd = pesBuffer + pesBufferLen;
                      while (audioDecode.parseFrame (pesBufferPtr, pesBufferEnd)) {
                        // several aacFrames per audio pes
                        float* samples = audioDecode.decodeFrame (seqFrameNum);
                        if (samples) {
                          if (firstTime)
                            mSong.setFixups (audioDecode.getNumChannels(), audioDecode.getSampleRate(), audioDecode.getNumSamples());
                          mSong.addAudioFrame (seqFrameNum++, samples, true, mSong.getNumFrames(), nullptr, pts);
                          pts += (audioDecode.getNumSamples() * 90) / 48;
                          changed();
                          if (firstTime) {
                            firstTime = false;
                            player = thread ([=](){ playThread16 (true); });  // playThread16 playThread32 playThreadWSAPI
                            }
                          }
                        pesBufferPtr += audioDecode.getNextFrameOffset();
                        }
                      pesBufferLen = 0;
                      }

                    if (ts[7] & 0x80)
                      pts = getPts (ts+9);

                    // skip header
                    int pesHeaderBytes = 9 + ts[8];
                    ts += pesHeaderBytes;
                    tsBodyBytes -= pesHeaderBytes;
                    }

                  // copy ts payload into pesBuffer
                  pesBuffer = (uint8_t*)realloc (pesBuffer, pesBufferLen + tsBodyBytes);
                  memcpy (pesBuffer + pesBufferLen, ts, tsBodyBytes);
                  pesBufferLen += tsBodyBytes;
                  }

                ts += tsBodyBytes;
                }

              if (pesBufferLen) {
                //{{{  process last audioPes
                uint8_t* pesBufferPtr = pesBuffer;
                uint8_t* pesBufferEnd = pesBuffer + pesBufferLen;

                while (audioDecode.parseFrame (pesBufferPtr, pesBufferEnd)) {
                  // several aacFrames per audio pes
                  float* samples = audioDecode.decodeFrame (seqFrameNum);
                  if (samples) {
                    mSong.addAudioFrame (seqFrameNum++, samples, true, mSong.getNumFrames(), nullptr, pts);
                    pts += (audioDecode.getNumSamples() * 90) / 48;
                    changed();
                    }

                  pesBufferPtr += audioDecode.getNextFrameOffset();
                  }

                pesBufferLen = 0;
                }
                //}}}
              //}}}
              mSong.setHlsLoad (cSong::eHlsIdle, chunkNum);
              //{{{  process video second, may block waiting for free videoFrames
              // parse ts packets
              ts = http.getContent();
              while ((ts < tsEnd) && (*ts++ == 0x47)) {
                auto payStart = ts[0] & 0x40;
                auto pid = ((ts[0] & 0x1F) << 8) | ts[1];
                auto headerBytes = (ts[2] & 0x20) ? 4 + ts[3] : 3;
                ts += headerBytes;
                auto tsBodyBytes = 187 - headerBytes;

                if (pid == 33) {
                  //  video pid
                  if (payStart && !ts[0] && !ts[1] && (ts[2] == 1) && (ts[3] == 0xe0)) {
                    if (pesBufferLen) {
                      // process prev videoPes
                      //mVideoDecode->decode (pts, pesBuffer, pesBufferLen);
                      pesBufferLen = 0;
                      }

                    if (ts[7] & 0x80)
                      pts = getPts (ts+9);

                    // skip header
                    int pesHeaderBytes = 9 + ts[8];
                    ts += pesHeaderBytes;
                    tsBodyBytes -= pesHeaderBytes;
                    }

                  // copy ts payload into pesBuffer
                  pesBuffer = (uint8_t*)realloc (pesBuffer, pesBufferLen + tsBodyBytes);
                  memcpy (pesBuffer + pesBufferLen, ts, tsBodyBytes);
                  pesBufferLen += tsBodyBytes;
                  }

                ts += tsBodyBytes;
                }

              if (pesBufferLen) {
                // process last videoPes
                //mVideoDecode->decode (pts, pesBuffer, pesBufferLen);
                pesBufferLen = 0;
                }
              //}}}
              http.freeContent();
              }
            else {
              //{{{  failed to load expected available chunk, back off for 250ms
              mSong.setHlsLoad (cSong::eHlsFailed, chunkNum);
              changed();

              cLog::log (LOGERROR, "late " + dec(chunkNum));
              this_thread::sleep_for (250ms);
              }
              //}}}
            }
          else // no chunk available, back off for 100ms
            this_thread::sleep_for (100ms);
          }
        player.join();
        }
      }

    free (pesBuffer);
    cLog::log (LOGINFO, "exit");
    }
  //}}}

  // 3 versions of playThread
  //{{{
  void playThread16 (bool streaming) {
  // audio player thread, video just follows play pts

    cLog::setThreadName ("play");
    shared_mutex lock;

  #ifdef _WIN32
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  #endif

    int16_t samples [2048*2] = { 0 };
    int16_t silence [2048*2] = { 0 };

    cAudio16 audio (2, mSong.getSampleRate());
    cAudioDecode decode (mSong.getFrameType());

    while (true && !mSongChanged) {
      lock.lock();
      auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
      if (mPlaying && framePtr && framePtr->getSamples()) {
        float* src = framePtr->getSamples();
        int16_t* dst = samples;
        for (int i = 0; i <mSong.getSamplesPerFrame(); i++) {
          *dst++ = int16_t((*src++) * 0x8000);
          *dst++ = int16_t((*src++) * 0x8000);
          }
        lock.unlock();
        audio.play (2, samples, mSong.getSamplesPerFrame(), 1.f);
        }
      else {
        lock.unlock();
        audio.play (2, silence, mSong.getSamplesPerFrame(), 1.f);
        }

      if (mPlaying && framePtr) {
        if (mVideoDecode)
          mVideoDecode->setPlayPts (framePtr->getPts());
        mSong.incPlayFrame (1, true);
        changed();
        }

      if (!streaming && (mSong.getPlayFrame() > mSong.getLastFrame()))
        break;
      }

    cLog::log (LOGINFO, "exit");
    }
  //}}}
#ifdef _WIN32
  //{{{
  void playThread32 (bool streaming) {
  // audio player thread, video just follows play pts

    cLog::setThreadName ("play");

    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    float silence [2048*2] = { 0.f };
    float samples [2048*2] = { 0.f };

    cAudio16 audio (2, mSong.getSampleRate());
    cAudioDecode decode (mSong.getFrameType());

    while (true && !mSongChanged) {
      shared_lock<shared_mutex> lock (mSong.getSharedMutex());

      auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
      if (mPlaying && framePtr && framePtr->getSamples())
        audio.play (2, framePtr->getSamples(), mSong.getSamplesPerFrame(), 1.f);
      else
        audio.play (2, silence, mSong.getSamplesPerFrame(), 1.f);

      if (mPlaying && framePtr) {
        if (mVideoDecode)
          mVideoDecode->setPlayPts (framePtr->getPts());
        mSong.incPlayFrame (1, true);
        changed();
        }

      if (!streaming && (mSong.getPlayFrame() > mSong.getLastFrame()))
        break;
      }

    cLog::log (LOGINFO, "exit");
    }
  //}}}
  //{{{
  void playThreadWSAPI (bool streaming) {
  // audio player thread, video just follows play pts

    cLog::setThreadName ("play");
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    float silence [2048*2] = { 0.f };
    float samples [2048*2] = { 0.f };

    auto device = getDefaultAudioOutputDevice();
    if (device) {
      cLog::log (LOGINFO, "device @ %d", mSong.getSampleRate());
      device->setSampleRate (mSong.getSampleRate());
      cAudioDecode decode (mSong.getFrameType());

      device->start();
      while (true && !mSongChanged) {
        device->process ([&](float*& srcSamples, int& numSrcSamples) mutable noexcept {
          // lambda callback - load srcSamples
          shared_lock<shared_mutex> lock (mSong.getSharedMutex());

          auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
          if (mPlaying && framePtr && framePtr->getSamples()) {
            if (mSong.getNumChannels() == 1) {
              //{{{  mono to stereo
              auto src = framePtr->getSamples();
              auto dst = samples;
              for (int i = 0; i < mSong.getSamplesPerFrame(); i++) {
                *dst++ = *src;
                *dst++ = *src++;
                }
              }
              //}}}
            else
              memcpy (samples, framePtr->getSamples(), mSong.getSamplesPerFrame() * mSong.getNumChannels() * sizeof(float));
            srcSamples = samples;
            }
          else
            srcSamples = silence;
          numSrcSamples = mSong.getSamplesPerFrame();

          if (mPlaying && framePtr) {
            if (mVideoDecode)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mSong.incPlayFrame (1, true);
            changed();
            }
          });

        if (!streaming && (mSong.getPlayFrame() > mSong.getLastFrame()))
          break;
        }

      device->stop();
      }

    cLog::log (LOGINFO, "exit");
    }
  //}}}
#endif
  //{{{  vars
  cSong mSong;
  bool mSongChanged = false;
  bool mPlaying = true;
  cVideoDecode* mVideoDecode = nullptr;
  bool mMoreLogInfo = false;
  //}}}
  };

int main (int numArgs, char* args[]) {

#ifdef _WIN32
  CoInitializeEx (NULL, COINIT_MULTITHREADED);
#endif
  cLog::init();

  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);

  bool headless = false;
  bool moreLog = false;
  for (size_t i = 0; i < argStrings.size(); i++) {
    if (argStrings[i] == "h") headless = true;
    else if (argStrings[i] == "l") moreLog = true;
    }

  cLog::log (LOGNOTICE, "hls " + moreLog ? "moreLog " : "" + headless ? "headless " : "");
  cLog::setLogLevel (moreLog ? LOGINFO3 : LOGINFO);

  cAppWindow appWindow;
  appWindow.run ("hls", 790, YSIZE, headless, moreLog, kDefaultChannelNum, kAudBitrate, kVidBitrate);

  return 0;
  }