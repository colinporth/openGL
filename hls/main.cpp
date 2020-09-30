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

  #define YSIZE 600
#else
  #define YSIZE 480
  const int COINIT_MULTITHREADED = 0;

  void CoInitializeEx (void*, int) {}
  void CoUninitialize() {}
#endif

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <string>
#include <thread>
#include <chrono>

#include "../../shared/date/date.h"
#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

#include "../../shared/utils/cSong.h"

#include "../../shared/decoders/cAudioDecode.h"

#ifdef _WIN32
  #include "../../shared/audio/audioWASAPI.h"
  #include "../../shared/audio/cWinAudio16.h"
  #include "../../shared/audio/cWinAudio32.h"
  #include "../../shared/net/cWinSockHttp.h"
#else
  #include "../../shared/audio/cLinuxAudio16.h"
  #include "../../shared/net/cLinuxHttp.h"
#endif

#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"

#include "../../shared/widgets/cTextBox.h"

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
//class cSongBox : public cD2dWindow::cBox {
//public:
  //{{{
  //cSongBox (cD2dWindow* window, float width, float height, cSong& song) :
      //cBox("songBox", window, width, height), mSong(song) {

    //mPin = true;

    //// time display font
    //mWindow->getDwriteFactory()->CreateTextFormat (L"Consolas", NULL,
      //DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 25.f, L"en-us",
      //&mSmallTimeTextFormat);

    //mWindow->getDwriteFactory()->CreateTextFormat (L"Consolas", NULL,
      //DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 45.f, L"en-us",
      //&mBigTimeTextFormat);
    //mBigTimeTextFormat->SetTextAlignment (DWRITE_TEXT_ALIGNMENT_CENTER);
    //}
  //}}}
  //{{{
  //virtual ~cSongBox() {

    //if (mSmallTimeTextFormat)
      //mSmallTimeTextFormat->Release();
    //if (mBigTimeTextFormat)
      //mBigTimeTextFormat->Release();

    //if (mBitmapTarget)
      //mBitmapTarget->Release();

    //if (mBitmap)
      //mBitmap->Release();
    //}
  //}}}

  //{{{
  //void layout() {

    //cD2dWindow::cBox::layout();
    //cLog::log (LOGINFO, "cSongBox::layout %d %d %d %d", mRect.left, mRect.top, mRect.right, mRect.bottom);

    //// invalidate frame bitmap
    //mFramesBitmapOk = false;

    //// invalidate overview bitmap
    //mOverviewBitmapOk = false;
    //}
  //}}}
  //{{{
  //bool onDown (bool right, cPoint pos)  {

    ////std::shared_lock<std::shared_mutex> lock (mSong.getSharedMutex());
    //if (pos.y > mDstOverviewTop) {
      //auto frame = mSong.getFirstFrame() + int((pos.x * mSong.getTotalFrames()) / getWidth());
      //mSong.setPlayFrame (frame);
      //mOverviewPressed = true;
      //}

    //else if (pos.y > mDstRangeTop) {
      //mPressedFrame = mSong.getPlayFrame() + ((pos.x - (getWidth()/2.f)) * mFrameStep / mFrameWidth);
      //mSong.getSelect().start (int(mPressedFrame));
      //mRangePressed = true;
      //mWindow->changed();
      //}

    //else
      //mPressedFrame = (float)mSong.getPlayFrame();

    //return true;
    //}
  //}}}
  //{{{
  //bool onMove (bool right, cPoint pos, cPoint inc) {

    ////std::shared_lock<std::shared_mutex> lock (mSong.getSharedMutex());
    //if (mOverviewPressed)
      //mSong.setPlayFrame (mSong.getFirstFrame() + int((pos.x * mSong.getTotalFrames()) / getWidth()));

    //else if (mRangePressed) {
      //mPressedFrame += (inc.x / mFrameWidth) * mFrameStep;
      //mSong.getSelect().move ((int)mPressedFrame);
      //mWindow->changed();
      //}

    //else {
      //mPressedFrame -= (inc.x / mFrameWidth) * mFrameStep;
      //mSong.setPlayFrame ((int)mPressedFrame);
      //}

    //return true;
    //}
  //}}}
  //{{{
  //bool onUp (bool right, bool mouseMoved, cPoint pos) {

    //mSong.getSelect().end();
    //mOverviewPressed = false;
    //mRangePressed = false;
    //return true;
    //}
  //}}}
  //{{{
  //bool onWheel (int delta, cPoint pos)  {

    //if (getShow()) {
      //setZoom (mZoom - (delta/120));
      //return true;
      //}

    //return false;
    //}
  //}}}
  //{{{
  //void onDraw (ID2D1DeviceContext* dc) {
  //// draw stuff centred at playFrame

    //{{{  src bitmap layout
    //mWaveHeight = 100.f;
    //mOverviewHeight = 100.f;
    //mRangeHeight = 8.f;
    //mFreqHeight = getHeight() - mOverviewHeight - mRangeHeight - mWaveHeight;

    //mSrcPeakHeight = mWaveHeight;
    //mSrcSilenceHeight = 2.f;
    //mSrcHeight = mFreqHeight + mWaveHeight + mSrcPeakHeight + mSrcSilenceHeight + mOverviewHeight;

    //mSrcFreqTop = 0.f;
    //mSrcWaveTop = mSrcFreqTop + mFreqHeight;
    //mSrcPeakTop = mSrcWaveTop + mWaveHeight;
    //mSrcSilenceTop = mSrcPeakTop + mSrcPeakHeight;
    //mSrcOverviewTop = mSrcSilenceTop + mSrcSilenceHeight;

    //mSrcWaveCentre = mSrcWaveTop + (mWaveHeight/2.f);
    //mSrcPeakCentre = mSrcPeakTop + (mSrcPeakHeight/2.f);
    //mSrcOverviewCentre = mSrcOverviewTop + (mOverviewHeight/2.f);
    //}}}
    //{{{  dst layout
    //mDstFreqTop = mRect.top;
    //mDstWaveTop = mDstFreqTop + mFreqHeight;
    //mDstRangeTop = mDstWaveTop + mWaveHeight;
    //mDstOverviewTop = mDstRangeTop + mRangeHeight;

    //mDstWaveCentre = mDstWaveTop + (mWaveHeight/2.f);
    //mDstOverviewCentre = mDstOverviewTop + (mOverviewHeight/2.f);
    //}}}

    //// lock
    //std::shared_lock<std::shared_mutex> lock (mSong.getSharedMutex());

    //reallocBitmap (mWindow->getDc());

    //if (mSong.getId() != mSongLastId) {
      //mFramesBitmapOk = false;
      //mOverviewBitmapOk = false;
      //mSongLastId = mSong.getId();
      //}

    //// draw
    //auto playFrame = mSong.getPlayFrame();

    //// wave left right frames, clip right not left
    //auto leftWaveFrame = playFrame - (((getWidthInt()+mFrameWidth)/2) * mFrameStep) / mFrameWidth;
    //auto rightWaveFrame = playFrame + (((getWidthInt()+mFrameWidth)/2) * mFrameStep) / mFrameWidth;
    //rightWaveFrame = std::min (rightWaveFrame, mSong.getLastFrame());
    //bool mono = mSong.getNumChannels() == 1;

    //drawRange (dc, playFrame, leftWaveFrame, rightWaveFrame);

    //if (mSong.getNumFrames()) {
      //drawWave (dc, playFrame, leftWaveFrame, rightWaveFrame, mono);
      //drawOverview (dc, playFrame, mono);
      //drawFreq (dc, playFrame);
      //}

    //if (mSong.hasHlsBase())
      //drawTime (dc, getFrameString (mSong.getFirstFrame()),
                    //getFrameString (mSong.getPlayFrame()), getFrameString (mSong.getLastFrame()));
    //else
      //drawTime (dc, L"", getFrameString (mSong.getPlayFrame()), getFrameString (mSong.getTotalFrames()));
    //}
  //}}}

//private:
  //{{{
  //int getSrcIndex (int frame) {
    //return (frame / mFrameStep) & mBitmapMask;
    //}
  //}}}
  //{{{
  //int getSignedSrcIndex (int frame) {
    //if (frame >= 0)
      //return (frame / mFrameStep) & mBitmapMask;
    //else {
      //return (-((mFrameStep-1 - frame) / mFrameStep)) & mBitmapMask;
      //}
    //}
  //}}}
  //{{{
  //std::wstring getFrameString (int frame) {

    //if (mSong.getSamplesPerFrame() && mSong.getSampleRate()) {
      //// can turn frame into seconds
      //auto value = ((uint64_t)frame * mSong.getSamplesPerFrame()) / (mSong.getSampleRate() / 100);
      //auto subSeconds = value % 100;

      //value /= 100;
      //value += mWindow->getDayLightSeconds();
      //auto seconds = value % 60;

      //value /= 60;
      //auto minutes = value % 60;

      //value /= 60;
      //auto hours = value % 60;

      //// !!! must be a better formatter lib !!!
      //return (hours > 0) ? (wdec (hours) + L':' + wdec (minutes, 2, '0') + L':' + wdec(seconds, 2, '0')) :
               //((minutes > 0) ? (wdec (minutes) + L':' + wdec(seconds, 2, '0') + L':' + wdec(subSeconds, 2, '0')) :
                 //(wdec(seconds) + L':' + wdec(subSeconds, 2, '0')));
      //}
    //else
      //return (L"--:--:--");
    //}
  //}}}

  //{{{
  //void setZoom (int zoom) {

    //mZoom = std::min (std::max (zoom, mMinZoom), mMaxZoom);

    //// zoomIn expanding frame to mFrameWidth pix
    //mFrameWidth = (mZoom < 0) ? -mZoom+1 : 1;

    //// zoomOut summing mFrameStep frames per pix
    //mFrameStep = (mZoom > 0) ? mZoom+1 : 1;
    //}
  //}}}

  //{{{
  //void reallocBitmap (ID2D1DeviceContext* dc) {
  //// fixed bitmap width for big cache, src bitmap height tracks dst box height

    //mBitmapWidth = 0x800; // 2048, power of 2
    //mBitmapMask =  0x7FF; // wrap mask

    //uint32_t bitmapHeight = (int)mSrcHeight;

    //if (!mBitmapTarget || (bitmapHeight != mBitmapTarget->GetSize().height)) {
      //cLog::log (LOGINFO, "reallocBitmap %d %d", bitmapHeight, mBitmapTarget ? mBitmapTarget->GetSize().height : -1);

      //// invalidate bitmap caches
      //mFramesBitmapOk = false;
      //mOverviewBitmapOk = false;

      //// release old
      //if (mBitmap)
        //mBitmap->Release();
      //if (mBitmapTarget)
        //mBitmapTarget->Release();

      //// wave bitmapTarget
      //D2D1_SIZE_U bitmapSizeU = { mBitmapWidth, bitmapHeight };
      //D2D1_PIXEL_FORMAT pixelFormat = { DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_STRAIGHT };
      //dc->CreateCompatibleRenderTarget (NULL, &bitmapSizeU, &pixelFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                        //&mBitmapTarget);
      //mBitmapTarget->GetBitmap (&mBitmap);
      //}
    //}
  //}}}
  //{{{
  //bool drawBitmapFrames (int fromFrame, int toFrame, int playFrame, int rightFrame, bool mono) {

    ////cLog::log (LOGINFO, "drawFrameToBitmap %d %d %d", fromFrame, toFrame, playFrame);
    //bool allFramesOk = true;
    //auto firstFrame = mSong.getFirstFrame();

    //// clear bitmap as chunks
    //auto fromSrcIndex = (float)getSignedSrcIndex (fromFrame);
    //auto toSrcIndex = (float)getSrcIndex (toFrame);
    //bool wrap = toSrcIndex <= fromSrcIndex;
    //float endSrcIndex = wrap ? float(mBitmapWidth) : toSrcIndex;

    //mBitmapTarget->BeginDraw();
    //cRect bitmapRect;
    //{{{  clear bitmap chunk before wrap
    //bitmapRect = { fromSrcIndex, mSrcFreqTop, endSrcIndex, mSrcSilenceTop + mSrcSilenceHeight };
    //mBitmapTarget->PushAxisAlignedClip (bitmapRect, D2D1_ANTIALIAS_MODE_ALIASED);
    //mBitmapTarget->Clear ( { 0.f,0.f,0.f, 0.f } );
    //mBitmapTarget->PopAxisAlignedClip();
    //}}}
    //if (wrap) {
      //{{{  clear bitmap chunk after wrap
      //bitmapRect = { 0.f, mSrcFreqTop, toSrcIndex, mSrcSilenceTop + mSrcSilenceHeight };
      //mBitmapTarget->PushAxisAlignedClip (bitmapRect, D2D1_ANTIALIAS_MODE_ALIASED);
      //mBitmapTarget->Clear ( { 0.f,0.f,0.f, 0.f } );
      //mBitmapTarget->PopAxisAlignedClip();
      //}
      //}}}

    //// draw bitmap as frames
    //for (auto frame = fromFrame; frame < toFrame; frame += mFrameStep) {
      //{{{  draw bitmap for frame
      //float srcIndex = (float)getSrcIndex (frame);

      //bool silence = false;
      //float powerValues[2] = { 0.f };

      //if (mFrameStep == 1) {
        //{{{  simple case, draw peak and power scaled to maxPeak
        //auto framePtr = mSong.getAudioFramePtr (frame);
        //if (framePtr) {
          //if (framePtr->getPowerValues()) {
            //float valueScale = mWaveHeight / 2.f / mSong.getMaxPeakValue();
            //silence = framePtr->isSilence();

            //float peakValues[2];
            //auto peakValuesPtr =  framePtr->getPeakValues();
            //auto powerValuesPtr = framePtr->getPowerValues();
            //for (auto i = 0; i < 2; i++) {
              //powerValues[i] = *powerValuesPtr++ * valueScale;
              //peakValues[i] = *peakValuesPtr++ * valueScale;
              //}


            //bitmapRect = { srcIndex,
                           //mono ?  (mSrcPeakTop + mWaveHeight) - (peakValues[0] * 2.f) : mSrcPeakCentre - peakValues[0],
                           //srcIndex + 1.f,
                           //mono ? mSrcPeakTop + mWaveHeight : mSrcPeakCentre + peakValues[1] };
            //mBitmapTarget->FillRectangle (bitmapRect, mWindow->getWhiteBrush());
            //}
          //}
        //else
          //allFramesOk = false;
        //}
        //}}}
      //else {
        //{{{  sum mFrameStep frames, mFrameStep aligned, just draw power scaled to maxPower
        //float valueScale = mWaveHeight / 2.f / mSong.getMaxPowerValue();
        //silence = false;

        //for (auto i = 0; i < 2; i++)
          //powerValues[i] = 0.f;

        //auto alignedFrame = frame - (frame % mFrameStep);
        //auto toSumFrame = std::min (alignedFrame + mFrameStep, rightFrame);
        //for (auto sumFrame = alignedFrame; sumFrame < toSumFrame; sumFrame++) {
          //auto framePtr = mSong.getAudioFramePtr (sumFrame);
          //if (framePtr) {
            //silence |= framePtr->isSilence();
            //if (framePtr->getPowerValues()) {
              //auto powerValuesPtr = framePtr->getPowerValues();
              //for (auto i = 0; i < 2; i++)
                //powerValues[i] += *powerValuesPtr++ * valueScale;
              //}
            //}
          //else
            //allFramesOk = false;
          //}

        //for (auto i = 0; i < 2; i++)
          //powerValues[i] /= toSumFrame - alignedFrame + 1;
        //}
        //}}}

      //bitmapRect = { srcIndex,
                     //mono ?  (mSrcWaveTop + mWaveHeight) - (powerValues[0] * 2.f) : mSrcWaveCentre - powerValues[0],
                     //srcIndex + 1.f,
                     //mono ? mSrcWaveTop + mWaveHeight : mSrcWaveCentre + powerValues[1] };
      //mBitmapTarget->FillRectangle (bitmapRect, mWindow->getWhiteBrush());

      //if (silence) {
        //// draw silence bitmap
        //bitmapRect.top = mSrcSilenceTop;
        //bitmapRect.bottom = mSrcSilenceTop + 1.f;
        //mBitmapTarget->FillRectangle (bitmapRect, mWindow->getWhiteBrush());
        //}
      //}
      //}}}
    //mBitmapTarget->EndDraw();

    //// copy reversed spectrum column to bitmap, clip high freqs to height
    //int freqSize = std::min (mSong.getNumFreqBytes(), (int)mFreqHeight);
    //int freqOffset = mSong.getNumFreqBytes() > (int)mFreqHeight ? mSong.getNumFreqBytes() - (int)mFreqHeight : 0;

    //// bitmap sampled aligned to mFrameStep, !!! could sum !!! ?? ok if neg frame ???
    //auto alignedFromFrame = fromFrame - (fromFrame % mFrameStep);
    //for (auto frame = alignedFromFrame; frame < toFrame; frame += mFrameStep) {
      //auto framePtr = mSong.getAudioFramePtr (frame);
      //if (framePtr) {
        //if (framePtr->getFreqLuma()) {
          //uint32_t bitmapIndex = getSrcIndex (frame);
          //D2D1_RECT_U bitmapRectU = { bitmapIndex, 0, bitmapIndex+1, (UINT32)freqSize };
          //mBitmap->CopyFromMemory (&bitmapRectU, framePtr->getFreqLuma() + freqOffset, 1);
          //}
        //}
      //}

    //return allFramesOk;
    //}
  //}}}

  //{{{
  //void drawRange (ID2D1DeviceContext* dc, int playFrame, int leftFrame, int rightFrame) {

    //cRect dstRect = { mRect.left, mDstRangeTop, mRect.right, mDstRangeTop + mRangeHeight };
    //dc->FillRectangle (dstRect, mWindow->getDarkGrayBrush());

    //for (auto &item : mSong.getSelect().getItems()) {
      //auto firstx = (getWidth()/2.f) + (item.getFirstFrame() - playFrame) * mFrameWidth / mFrameStep;
      //float lastx = item.getMark() ? firstx + 1.f :
                                     //(getWidth()/2.f) + (item.getLastFrame() - playFrame) * mFrameWidth / mFrameStep;

      //dstRect = { mRect.left + firstx, mDstRangeTop, mRect.left + lastx, mDstRangeTop + mRangeHeight };
      //dc->FillRectangle (dstRect, mWindow->getYellowBrush());

      //auto title = item.getTitle();
      //if (!title.empty()) {
        //dstRect = { mRect.left + firstx + 2.f, mDstRangeTop + mRangeHeight - mWindow->getTextFormat()->GetFontSize(),
                    //mRect.right, mDstRangeTop + mRangeHeight + 4.f };
        //dc->DrawText (std::wstring (title.begin(), title.end()).data(), (uint32_t)title.size(), mWindow->getTextFormat(),
                      //dstRect, mWindow->getWhiteBrush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        //}
      //}
    //}
  //}}}
  //{{{
  //void drawWave (ID2D1DeviceContext* dc, int playFrame, int leftFrame, int rightFrame, bool mono) {

    //bool allFramesOk = true;
    //if (mFramesBitmapOk &&
        //(mFrameStep == mBitmapFrameStep) &&
        //(rightFrame > mBitmapFirstFrame) && (leftFrame < mBitmapLastFrame)) {
      //// overlap
      //if (leftFrame < mBitmapFirstFrame) {
        //{{{  draw new bitmap leftFrames
        //allFramesOk &= drawBitmapFrames (leftFrame, mBitmapFirstFrame, playFrame, rightFrame, mono);

        //mBitmapFirstFrame = leftFrame;
        //if (mBitmapLastFrame - mBitmapFirstFrame > (int)mBitmapWidth)
          //mBitmapLastFrame = mBitmapFirstFrame + mBitmapWidth;
        //}
        //}}}
      //if (rightFrame > mBitmapLastFrame) {
        //{{{  draw new bitmap rightFrames
        //allFramesOk &= drawBitmapFrames (mBitmapLastFrame, rightFrame, playFrame, rightFrame, mono);

        //mBitmapLastFrame = rightFrame;
        //if (mBitmapLastFrame - mBitmapFirstFrame > (int)mBitmapWidth)
          //mBitmapFirstFrame = mBitmapLastFrame - mBitmapWidth;
        //}
        //}}}
      //}
    //else {
      //{{{  no overlap, draw all bitmap frames
      //allFramesOk &= drawBitmapFrames (leftFrame, rightFrame, playFrame, rightFrame, mono);

      //mBitmapFirstFrame = leftFrame;
      //mBitmapLastFrame = rightFrame;
      //}
      //}}}
    //mFramesBitmapOk = allFramesOk;
    //mBitmapFrameStep = mFrameStep;

    //// calc bitmap wrap chunks
    //float leftSrcIndex = (float)getSrcIndex (leftFrame);
    //float rightSrcIndex = (float)getSrcIndex (rightFrame);
    //float playSrcIndex = (float)getSrcIndex (playFrame);

    //bool wrap = rightSrcIndex <= leftSrcIndex;
    //float endSrcIndex = wrap ? float(mBitmapWidth) : rightSrcIndex;

    //// draw dst chunks, mostly stamping colour through alpha bitmap
    //cRect srcRect;
    //cRect dstRect;
    //dc->SetAntialiasMode (D2D1_ANTIALIAS_MODE_ALIASED);
    //{{{  stamp playFrame mark
    //auto dstPlay = playSrcIndex - leftSrcIndex + (playSrcIndex < leftSrcIndex ? endSrcIndex : 0);
    //dstRect = { mRect.left + (dstPlay+0.5f) * mFrameWidth, mDstFreqTop,
                //mRect.left + ((dstPlay+0.5f) * mFrameWidth) + 1.f, mDstRangeTop + mRangeHeight };
    //dc->FillRectangle (dstRect, mWindow->getDimGrayBrush());
    //}}}
    //{{{  stamp chunk before wrap
    //// freq
    //srcRect = { leftSrcIndex, mSrcFreqTop, endSrcIndex, mSrcFreqTop + mFreqHeight };
    //dstRect = { mRect.left, mDstFreqTop,
                //mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth, mDstFreqTop + mFreqHeight };
    //dc->FillOpacityMask (mBitmap, mWindow->getWhiteBrush(), dstRect, srcRect);

    //// silence
    //srcRect = { leftSrcIndex, mSrcSilenceTop, endSrcIndex, mSrcSilenceTop + mSrcSilenceHeight };
    //dstRect = { mRect.left,
                //mono ? mDstWaveTop + mWaveHeight - 4.f :  mDstWaveCentre - 2.f,
                //mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth,
                //mono ? mDstWaveTop + mWaveHeight :  mDstWaveCentre + 2.f };
    //dc->FillOpacityMask (mBitmap, mWindow->getRedBrush(), dstRect, srcRect);

    //// wave
    //bool split = (playSrcIndex >= leftSrcIndex) && (playSrcIndex < endSrcIndex);


    //// wave chunk before play
    //srcRect = { leftSrcIndex, mSrcPeakTop, (split ? playSrcIndex : endSrcIndex), mSrcPeakTop + mSrcPeakHeight };
    //dstRect = { mRect.left, mDstWaveTop,
                //mRect.left + ((split ? playSrcIndex : endSrcIndex) - leftSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
    //dc->FillOpacityMask (mBitmap, mWindow->getDarkBlueBrush(), dstRect, srcRect);

    //srcRect = { leftSrcIndex, mSrcWaveTop, (split ? playSrcIndex : endSrcIndex), mSrcWaveTop + mWaveHeight };
    //dstRect = { mRect.left, mDstWaveTop,
                //mRect.left + ((split ? playSrcIndex : endSrcIndex) - leftSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
    //dc->FillOpacityMask (mBitmap, mWindow->getBlueBrush(), dstRect, srcRect);

    //if (split) {
      //// split wave chunk after play
      //srcRect = { playSrcIndex+1.f, mSrcPeakTop, endSrcIndex, mSrcPeakTop + mSrcPeakHeight };
      //dstRect = { mRect.left + (playSrcIndex+1.f - leftSrcIndex) * mFrameWidth, mDstWaveTop,
                  //mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
      //dc->FillOpacityMask (mBitmap, mWindow->getDimGrayBrush(), dstRect, srcRect);

      //srcRect = { playSrcIndex+1.f, mSrcWaveTop, endSrcIndex, mSrcWaveTop + mWaveHeight };
      //dstRect = { mRect.left + (playSrcIndex+1.f - leftSrcIndex) * mFrameWidth, mDstWaveTop,
                  //mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
      //dc->FillOpacityMask (mBitmap, mWindow->getLightGrayBrush(), dstRect, srcRect);
      //}
    //}}}
    //if (wrap) {
      //{{{  stamp second chunk after wrap
      //// Freq
      //srcRect = { 0.f, mSrcFreqTop,  rightSrcIndex, mSrcFreqTop + mFreqHeight };
      //dstRect = { mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth, mDstFreqTop,
                   //mRect.left + (endSrcIndex - leftSrcIndex + rightSrcIndex) * mFrameWidth, mDstFreqTop + mFreqHeight };
      //dc->FillOpacityMask (mBitmap, mWindow->getWhiteBrush(), dstRect, srcRect);

      //// silence
      //srcRect = { 0.f, mSrcSilenceTop, rightSrcIndex, mSrcSilenceTop + mSrcSilenceHeight };
      //dstRect = { mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth,
                  //mono ? mDstWaveTop + mWaveHeight - 4.f :  mDstWaveCentre - 2.f,
                  //mRect.left + (endSrcIndex - leftSrcIndex + rightSrcIndex) * mFrameWidth,
                  //mono ? mDstWaveTop + mWaveHeight :  mDstWaveCentre + 2.f };
      //dc->FillOpacityMask (mBitmap, mWindow->getRedBrush(), dstRect, srcRect);

      //// wave
      //bool split = playSrcIndex < rightSrcIndex;
      //if (split) {
        //// split chunk before play
        //srcRect = { 0.f, mSrcPeakTop,  playSrcIndex, mSrcPeakTop + mSrcPeakHeight };
        //dstRect = { mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth, mDstWaveTop,
                    //mRect.left + (endSrcIndex - leftSrcIndex + playSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
        //dc->FillOpacityMask (mBitmap, mWindow->getDarkBlueBrush(), dstRect, srcRect);

        //srcRect = { 0.f, mSrcWaveTop,  playSrcIndex, mSrcWaveTop + mWaveHeight };
        //dstRect = { mRect.left + (endSrcIndex - leftSrcIndex) * mFrameWidth, mDstWaveTop,
                    //mRect.left + (endSrcIndex - leftSrcIndex + playSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
        //dc->FillOpacityMask (mBitmap, mWindow->getBlueBrush(), dstRect, srcRect);
        //}

      //// chunk after play
      //srcRect = { split ? playSrcIndex+1.f : 0.f, mSrcPeakTop,  rightSrcIndex, mSrcPeakTop + mSrcPeakHeight };
      //dstRect = { mRect.left + (endSrcIndex - leftSrcIndex + (split ? (playSrcIndex+1.f) : 0.f)) * mFrameWidth, mDstWaveTop,
                  //mRect.left + (endSrcIndex - leftSrcIndex + rightSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
      //dc->FillOpacityMask (mBitmap, mWindow->getDimGrayBrush(), dstRect, srcRect);

      //srcRect = { split ? playSrcIndex+1.f : 0.f, mSrcWaveTop,  rightSrcIndex, mSrcWaveTop + mWaveHeight };
      //dstRect = { mRect.left + (endSrcIndex - leftSrcIndex + (split ? (playSrcIndex+1.f) : 0.f)) * mFrameWidth, mDstWaveTop,
                  //mRect.left + (endSrcIndex - leftSrcIndex + rightSrcIndex) * mFrameWidth, mDstWaveTop + mWaveHeight };
      //dc->FillOpacityMask (mBitmap, mWindow->getLightGrayBrush(), dstRect, srcRect);
      //}
      //}}}
    //dc->SetAntialiasMode (D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    //{{{  draw playFrame
    //auto framePtr = mSong.getAudioFramePtr (playFrame);
    //if (framePtr) {
      //float valueScale = mWaveHeight / 2.f / mSong.getMaxPeakValue();
      //auto peakValues = framePtr->getPeakValues();

      //dstRect = { mRect.left + (dstPlay * mFrameWidth)-1.f,
                  //mono ? (mDstWaveTop + mWaveHeight) - (*peakValues++ * valueScale) * 2.f : mDstWaveCentre + (*peakValues++ * valueScale),
                  //mRect.left + ((dstPlay+1.f) * mFrameWidth)+1.f,
                  //mono ? mDstWaveTop + mWaveHeight : mDstWaveCentre + (*peakValues * valueScale) };
      //dc->FillRectangle (dstRect, mWindow->getGreenBrush());
      //}
    //}}}
    //}
  //}}}
  //{{{
  //void drawOverview (ID2D1DeviceContext* dc, int playFrame, bool mono) {

    //if (!mSong.getTotalFrames())
      //return;

    //int firstFrame = mSong.getFirstFrame();
    //float playFrameX = ((playFrame - firstFrame) * getWidth()) / mSong.getTotalFrames();
    //float valueScale = mOverviewHeight / 2.f / mSong.getMaxPowerValue();
    //drawOverviewWave (dc, firstFrame, playFrame, playFrameX, valueScale, mono);

    //if (mOverviewPressed) {
      //{{{  animate on
      //if (mOverviewLens < getWidth() / 16.f) {
        //mOverviewLens += getWidth() / 16.f / 6.f;
        //mWindow->changed();
        //}
      //}
      //}}}
    //else {
      //{{{  animate off
      //if (mOverviewLens > 1.f) {
        //mOverviewLens /= 2.f;
        //mWindow->changed();
        //}
      //else if (mOverviewLens > 0.f) {
        //// finish animate
        //mOverviewLens = 0.f;
        //mWindow->changed();
        //}
      //}
      //}}}

    //if (mOverviewLens > 0.f) {
      //float overviewLensCentreX = (float)playFrameX;
      //if (overviewLensCentreX - mOverviewLens < 0.f)
        //overviewLensCentreX = (float)mOverviewLens;
      //else if (overviewLensCentreX + mOverviewLens > getWidth())
        //overviewLensCentreX = getWidth() - mOverviewLens;

      //drawOverviewLens (dc, playFrame, overviewLensCentreX, mOverviewLens-1.f, mono);
      //}
    //else {
      ////  draw playFrame
      //auto framePtr = mSong.getAudioFramePtr (playFrame);
      //if (framePtr && framePtr->getPowerValues()) {
        //auto powerValues = framePtr->getPowerValues();
        //cRect dstRect = { mRect.left + playFrameX,
                          //mono ? mDstOverviewTop + mOverviewHeight - (*powerValues++ * valueScale)*2.f :
                                 //mDstOverviewCentre - (*powerValues++ * valueScale),
                          //mRect.left + playFrameX+1.f,
                          //mono ? mDstOverviewTop + mOverviewHeight :
                                 //mDstOverviewCentre + (*powerValues * valueScale) };
        //dc->FillRectangle (dstRect, mWindow->getWhiteBrush());
        //}
      //}
    //}
  //}}}
  //{{{
  //void drawOverviewWave (ID2D1DeviceContext* dc, int firstFrame, int playFrame, float playFrameX, float valueScale, bool mono) {
  //// draw Overview using bitmap cache

    //int lastFrame = mSong.getLastFrame();
    //int totalFrames = mSong.getTotalFrames();

    //bool forceRedraw = !mOverviewBitmapOk ||
                       //(valueScale != mOverviewValueScale) ||
                       //(firstFrame != mOverviewFirstFrame) || (totalFrames > mOverviewTotalFrames);

    //if (forceRedraw || (lastFrame > mOverviewLastFrame)) {
      //mBitmapTarget->BeginDraw();
      //if (forceRedraw) {
        //{{{  clear overview bitmap
        //cRect bitmapRect = { 0.f, mSrcOverviewTop, getWidth(), mSrcOverviewTop + mOverviewHeight };
        //mBitmapTarget->PushAxisAlignedClip (bitmapRect, D2D1_ANTIALIAS_MODE_ALIASED);
        //mBitmapTarget->Clear ( {0.f,0.f,0.f, 0.f} );
        //mBitmapTarget->PopAxisAlignedClip();
        //}
        //}}}

      //bool allFramesOk = true;
      //for (auto x = 0; x < getWidthInt(); x++) {
        //{{{  iterate widget width
        //int frame = firstFrame + ((x * totalFrames) / getWidthInt());
        //int toFrame = firstFrame + (((x+1) * totalFrames) / getWidthInt());
        //if (toFrame > lastFrame)
          //toFrame = lastFrame+1;

        //if (forceRedraw || (frame >= mOverviewLastFrame)) {
          //auto framePtr = mSong.getAudioFramePtr (frame);
          //if (framePtr) {
            //// !!! should accumulate silence and distinguish from silence better !!!
            //if (framePtr->getPowerValues()) {
              //float* powerValues = framePtr->getPowerValues();
              //float leftValue = *powerValues++;
              //float rightValue = mono ? 0 : *powerValues;
              //if (frame < toFrame) {
                //int numSummedFrames = 1;
                //frame++;
                //while (frame < toFrame) {
                  //framePtr = mSong.getAudioFramePtr (frame);
                  //if (framePtr) {
                    //if (framePtr->getPowerValues()) {
                      //auto powerValues = framePtr->getPowerValues();
                      //leftValue += *powerValues++;
                      //rightValue += mono ? 0 : *powerValues;
                      //numSummedFrames++;
                      //}
                    //}
                  //else
                    //allFramesOk = false;
                  //frame++;
                  //}
                //leftValue /= numSummedFrames;
                //rightValue /= numSummedFrames;
                //}

              //cRect bitmapRect = {
                //(float)x,
                //mono ? mSrcOverviewTop + mOverviewHeight - ((leftValue * valueScale)*2.f) - 2.f :
                       //mSrcOverviewCentre - (leftValue * valueScale) - 2.f,
                //x+1.f,
                //mono ? mSrcOverviewTop + mOverviewHeight :
                       //mSrcOverviewCentre + (rightValue * valueScale) + 2.f };
              //mBitmapTarget->FillRectangle (bitmapRect, mWindow->getWhiteBrush());
              //}
            //else
              //allFramesOk = false;
            //}
          //}
        //}
        //}}}
      //mBitmapTarget->EndDraw();

      //mOverviewBitmapOk = allFramesOk;
      //mOverviewValueScale = valueScale;
      //mOverviewFirstFrame = firstFrame;
      //mOverviewLastFrame = lastFrame;
      //mOverviewTotalFrames = totalFrames;
      //}

    //// draw overview, stamping colours through alpha bitmap
    //dc->SetAntialiasMode (D2D1_ANTIALIAS_MODE_ALIASED);

    //// before playFrame
    //cRect srcRect = { 0.f, mSrcOverviewTop,  playFrameX, mSrcOverviewTop + mOverviewHeight };
    //cRect dstRect = { mRect.left, mDstOverviewTop, mRect.left + playFrameX, mDstOverviewTop + mOverviewHeight };
    //dc->FillOpacityMask (mBitmap, mWindow->getBlueBrush(), dstRect, srcRect);

    //// after playFrame
    //srcRect = { playFrameX+1.f, mSrcOverviewTop,  getWidth(), mSrcOverviewTop + mOverviewHeight };
    //dstRect = { mRect.left + playFrameX+1.f, mDstOverviewTop, mRect.right, mDstOverviewTop + mOverviewHeight };
    //dc->FillOpacityMask (mBitmap, mWindow->getGrayBrush(), dstRect, srcRect);

    //dc->SetAntialiasMode (D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    //}
  //}}}
  //{{{
  //void drawOverviewLens (ID2D1DeviceContext* dc, int playFrame, float centreX, float width, bool mono) {
  //// draw frames centred at playFrame -/+ width in pixels, centred at centreX

    //// cut hole and frame it
    //cRect dstRect = { mRect.left + centreX - mOverviewLens, mDstOverviewTop,
                      //mRect.left + centreX + mOverviewLens, mRect.bottom - 1.f };
    //dc->FillRectangle (dstRect, mWindow->getBlackBrush());
    //dc->DrawRectangle (dstRect, mWindow->getYellowBrush(), 1.f);

    //// calc leftmost frame, clip to valid frame, adjust firstX which may overlap left up to mFrameWidth
    //float leftFrame = playFrame - width;
    //float firstX = centreX - (playFrame - leftFrame);
    //if (leftFrame < 0) {
      //firstX += -leftFrame;
      //leftFrame = 0;
      //}

    //int rightFrame = (int)(playFrame + width);
    //rightFrame = std::min (rightFrame, mSong.getLastFrame());

    //// calc lens max power
    //float maxPowerValue = 0.f;
    //for (auto frame = int(leftFrame); frame <= rightFrame; frame++) {
      //auto framePtr = mSong.getAudioFramePtr (frame);
      //if (framePtr && framePtr->getPowerValues()) {
        //auto powerValues = framePtr->getPowerValues();
        //maxPowerValue = std::max (maxPowerValue, *powerValues++);
        //if (!mono)
          //maxPowerValue = std::max (maxPowerValue, *powerValues);
        //}
      //}

    //// simple draw of unzoomed waveform, no use of bitmap cache
    //auto colour = mWindow->getBlueBrush();
    //float valueScale = mOverviewHeight / 2.f / maxPowerValue;
    //dstRect.left = mRect.left + firstX;
    //for (auto frame = int(leftFrame); frame <= rightFrame; frame++) {
      //dstRect.right = dstRect.left + 1.f;

      //auto framePtr = mSong.getAudioFramePtr (frame);
      //if (framePtr && framePtr->getPowerValues()) {
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

        //auto powerValues = framePtr->getPowerValues();
        //dstRect.top = mono ? mDstOverviewTop + mOverviewHeight - (*powerValues++ * valueScale)* 2.f :
                             //mDstOverviewCentre - (*powerValues++ * valueScale);
        //dstRect.bottom = mono ? mDstOverviewTop + mOverviewHeight :
                                //mDstOverviewCentre + ((mono ? 0 : *powerValues) * valueScale);
        //if (frame == playFrame)
          //colour = mWindow->getWhiteBrush();
        //dc->FillRectangle (dstRect, colour);
        //if (frame == playFrame)
          //colour = mWindow->getGrayBrush();
       //}

      //dstRect.left = dstRect.right;
      //}
    //}
  //}}}
  //{{{
  //void drawFreq (ID2D1DeviceContext* dc, int playFrame) {

    //float valueScale = 100.f / 255.f;

    //auto framePtr = mSong.getAudioFramePtr (playFrame);
    //if (framePtr && framePtr->getFreqValues()) {
      //auto freqValues = framePtr->getFreqValues();
      //for (auto i = 0; (i < mSong.getNumFreqBytes()) && ((i*2) < getWidthInt()); i++) {
        //auto value =  freqValues[i] * valueScale;
        //if (value > 1.f)  {
          //cRect dstRect = { mRect.left + (i*2),mRect.bottom - value, mRect.left + ((i+1) * 2),mRect.bottom };
          //dc->FillRectangle (dstRect, mWindow->getYellowBrush());
          //}
        //}
      //}
    //}
  //}}}
  //{{{
  //void drawTime (ID2D1DeviceContext* dc, const std::wstring& first, const std::wstring& play, const std::wstring& last) {

    //// big play, centred
    //cRect dstRect = mRect;
    //dstRect.top = mRect.bottom - mBigTimeTextFormat->GetFontSize();
    //dc->DrawText (play.data(), (uint32_t)play.size(), mBigTimeTextFormat, dstRect, mWindow->getWhiteBrush());

    //// small first, left
    //mSmallTimeTextFormat->SetTextAlignment (DWRITE_TEXT_ALIGNMENT_LEADING);
    //dstRect.top = mRect.bottom - mSmallTimeTextFormat->GetFontSize();
    //dc->DrawText (first.data(), (uint32_t)first.size(), mSmallTimeTextFormat, dstRect, mWindow->getWhiteBrush());

    //// small coloured last, right
    //mSmallTimeTextFormat->SetTextAlignment (DWRITE_TEXT_ALIGNMENT_TRAILING);
    //dstRect.top = mRect.bottom - mSmallTimeTextFormat->GetFontSize();
    //dc->DrawText (last.data(), (uint32_t)last.size(), mSmallTimeTextFormat, dstRect,
                  //(mSong.getHlsLoad() == cSong::eHlsIdle) ? mWindow->getWhiteBrush() :
                    //(mSong.getHlsLoad() == cSong::eHlsFailed) ? mWindow->getRedBrush() : mWindow->getGreenBrush());
    //}
  //}}}

  //{{{  private vars
  //cSong& mSong;
  //int mSongLastId = 0;

  //// zoom - 0 unity, > 0 zoomOut framesPerPix, < 0 zoomIn pixPerFrame
  //int mZoom = 0;
  //int mMinZoom = -8;
  //int mMaxZoom = 8;
  //int mFrameWidth = 1;
  //int mFrameStep = 1;

  //float mPressedFrame = 0.f;
  //bool mOverviewPressed = false;
  //bool mRangePressed = false;

  //bool mFramesBitmapOk = false;
  //int mBitmapFirstFrame = 0;
  //int mBitmapLastFrame = 0;
  //int mBitmapFrameStep = 1;

  //bool mOverviewBitmapOk = false;
  //int mOverviewFirstFrame = 0;
  //int mOverviewLastFrame = 0;
  //int mOverviewTotalFrames = 0;
  //float mOverviewValueScale = 1.f;
  //float mOverviewLens = 0.f;

  //uint32_t mBitmapWidth = 0;
  //uint32_t mBitmapMask = 0;
  //ID2D1Bitmap* mBitmap = nullptr;
  //ID2D1BitmapRenderTarget* mBitmapTarget = nullptr;

  //IDWriteTextFormat* mSmallTimeTextFormat = nullptr;
  //IDWriteTextFormat* mBigTimeTextFormat = nullptr;

  //// vertical layout
  //float mFreqHeight = 0.f;
  //float mWaveHeight = 0.f;
  //float mRangeHeight = 0.f;
  //float mOverviewHeight = 0.f;

  //float mSrcPeakHeight = 0.f;
  //float mSrcSilenceHeight = 0.f;

  //float mSrcFreqTop = 0.f;
  //float mSrcWaveTop = 0.f;
  //float mSrcPeakTop = 0.f;
  //float mSrcSilenceTop = 0.f;
  //float mSrcOverviewTop = 0.f;
  //float mSrcHeight = 0.f;

  //float mSrcWaveCentre = 0.f;
  //float mSrcPeakCentre = 0.f;
  //float mSrcOverviewCentre = 0.f;

  //float mDstFreqTop = 0.f;
  //float mDstWaveTop = 0.f;
  //float mDstRangeTop = 0.f;
  //float mDstOverviewTop = 0.f;

  //float mDstWaveCentre = 0.f;
  //float mDstOverviewCentre = 0.f;
  //}}}
  //};
//}}}
//{{{
class cSongWidget : public cWidget {
public:
  cSongWidget (cSong* song, float width, float height) : cWidget (COL_BLUE, width, height), mSong(song) {}
  virtual ~cSongWidget() {}

  void onDown (float x, float y) {
    //cWidget::onDown (x, y);
    //mMove = 0;
    //mPressInc = x - (mWidth/2.f);
    //mHls->setScrub();
    //setZoomAnim (1.0f + ((mWidth/2.f) - abs(x - (mWidth/2.f))) / (mWidth/6.f), 4);
    }

  void onMove (float x, float y, float xinc, float yinc) {
    //cWidget::onMove (x, y, xinc, yinc);
    //mMove += abs(xinc) + abs(yinc);
    //mHls->incPlaySample ((-xinc * kSamplesPerFrame) / mZoom);
    }

  void onUp() {
    //cWidget::onUp();
    //if (mMove < getBoxHeight()/2.f) {
    //  mAnimation = mPressInc;
    //  mHls->incPlaySample (mPressInc * kSamplesPerFrame / kNormalZoom);
    //  mHls->setPlay();
    //  }
    //mHls->setPause();
    //setZoomAnim (kNormalZoom, 3);
    }

  void onDraw (iDraw* draw) {
    //{{{  animate
    //mAnimation /= 2;

    //if (mZoomInc > 0) {
      //if (mZoom + mZoomInc > mTargetZoom)
        //mZoom = mTargetZoom;
      //else
        //mZoom += mZoomInc;
      //}
    //else  {
      //if (mZoom + mZoomInc < mTargetZoom)
        //mZoom = mTargetZoom;
      //else
        //mZoom += mZoomInc;
      //}
    //}}}
    //float midx = mX + (mWidth/2.f);
    //float samplesPerPixF = kSamplesPerFrame / mZoom;
    //float pixPerSec = kSamplesPerSecondF / samplesPerPixF;
    //double sample = mHls->getPlaySample() - ((mWidth/2.f) + mAnimation) * samplesPerPixF;

    //float y = mY + mHeight - getBoxHeight()/2.f;
    //int secs = int (sample / kSamplesPerSecond);
    //float subSecSamples = (float)fmod (sample, kSamplesPerSecondD);
    //float nextxF = (kSamplesPerSecondF - subSecSamples) / samplesPerPixF;

    //{{{  draw waveform rects
    //auto context = draw->getContext();

    //context->fillColor (kVgDarkGrey);
    //context->beginPath();
    //for (float x = 0; x < mWidth; secs++, nextxF += pixPerSec) {
      //if (secs & 1)
        //context->rect (mX+x, y, nextxF - x, getBoxHeight()/2.0f);
      //x = nextxF;
      //}
    //context->triangleFill();

    //y = mY + mHeight - getBigFontHeight();

    //context->fontSize ((float)getBigFontHeight());
    //context->textAlign (cVg::ALIGN_LEFT | cVg::ALIGN_TOP);
    //context->fillColor (kVgWhite);
    //context->text (midx-60.0f+3.0f, y+1.0f, getTimeString (mHls->getPlayTzSeconds()));

    //float midy = (float)mY + (mHeight/2);
    //float midWidth = midx + int(mHls->getPlaying() == cHls::eScrub ? kScrubFrames*mZoom : mZoom);

    //context->beginPath();
    //uint8_t* samples = nullptr;
    //uint32_t numSamples = 0;
    //for (float x = mX; x < mX+mWidth; x++) {
      //if (!numSamples) {
        //samples = mHls->getPeakSamples (sample, numSamples, mZoom);
        //if (samples)
          //sample += numSamples * samplesPerPixF;
        //}

      //if (samples) {
        //if (x == midx) {
          //context->fillColor (nvgRGBA32 (COL_BLUE));
          //context->triangleFill();
          //context->beginPath();
          //}
        //else if (x == midWidth) {
          //context->fillColor (nvgRGBA32 (COL_DARKGREEN));
          //context->triangleFill();
          //context->beginPath();
          //}

        //auto left = (*samples++ * mHeight) / 0x100;
        //auto right = (*samples++ * mHeight) / 0x100;
        //context->rect (x, midy - left, 1, (float)(left+right));
        //numSamples--;
        //}
      //else
        //sample += samplesPerPixF;
      //}

    //context->fillColor (nvgRGBA32 (COL_DARKGREY));
    //context->triangleFill();
    //}}}
    }

private:
  cSong* mSong;
  float mMove = 0;
  bool mMoved = false;
  float mPressInc = 0;
  };
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height, bool headless, bool moreLogInfo, bool all,
            int channelNum, int audBitrate, int vidBitrate)  {

    mMoreLogInfo = moreLogInfo;

    if (!headless) {
      initialise (title, width, height, (unsigned char*)droidSansMono);
      add (new cSongWidget (&mSong, 0,0));
      }

    thread ([=](){ hlsThread (kHost, kChannels[channelNum], audBitrate, vidBitrate); }).detach();

    if (headless) {
      while (true)
        this_thread::sleep_for (1s);
      }
    else {
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
  static uint64_t getPts (uint8_t* ts) {
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
  static string getTagValue (uint8_t* buffer, char* tag) {

    const char* tagPtr = strstr ((char*)buffer, tag);
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

              uint64_t pts = 0;

              // parse ts packets
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
                      //{{{  process prev audioPes
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
                            // launch player
                            firstTime = false;
                            player = thread ([=](){ playThread16 (true); });
                            //player = thread ([=](){ playThread32 (true); });
                            //player = thread ([=](){ playThreadWSAPI (true); });
                            }
                          }

                        pesBufferPtr += audioDecode.getNextFrameOffset();
                        }

                      pesBufferLen = 0;
                      }
                      //}}}

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

  #ifdef _WIN32
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  #endif

    int16_t silence [2048*2] = { 0 };
    int16_t samples [2048*2] = { 0 };

    cAudio16 audio (2, mSong.getSampleRate());
    cAudioDecode decode (mSong.getFrameType());

    while (true && !mSongChanged) {
      shared_lock<shared_mutex> lock (mSong.getSharedMutex());

      auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
      if (mPlaying && framePtr && framePtr->getSamples()) {
        float* src = framePtr->getSamples();
        int16_t* dst = samples;
        for (int i = 0; i <mSong.getSamplesPerFrame(); i++) {
          *dst++ = int16_t((*src++) * 0x8000);
          *dst++ = int16_t((*src++) * 0x8000);
          }
        audio.play (2, samples, mSong.getSamplesPerFrame(), 1.f);
        }
      else
        audio.play (2, silence, mSong.getSamplesPerFrame(), 1.f);

      if (mPlaying && framePtr) {
        //mVideoDecode->setPlayPts (framePtr->getPts());
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
        //mVideoDecode->setPlayPts (framePtr->getPts());
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
            //mVideoDecode->setPlayPts (framePtr->getPts());
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
  //cVideoDecode* mVideoDecode = nullptr;
  bool mMoreLogInfo = false;
  //}}}
  };

int main (int numArgs, char* args[]) {

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
  cLog::init();

  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);

  bool all = false;
  bool headless = false;
  bool moreLogInfo = false;

  for (size_t i = 0; i < argStrings.size(); i++) {
    if (argStrings[i] == "all") all = true;
    else if (argStrings[i] == "h") headless = true;
    else if (argStrings[i] == "l") moreLogInfo = true;
    }

  cLog::log (LOGNOTICE, "hls - moreLog:" + dec(moreLogInfo));
  if (moreLogInfo)
    cLog::setLogLevel (LOGINFO3);

  cAppWindow appWindow;
  appWindow.run ("hls", 790, YSIZE, headless, moreLogInfo, all, kDefaultChannelNum, kAudBitrate, kVidBitrate);

  return 0;
  }
