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
#include "../../shared/widgets/cSongWidget.h"

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
