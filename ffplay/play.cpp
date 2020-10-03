 // ffplay.c
//{{{  includes
#include "config.h"

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

extern "C" {
  #include "libavutil/avstring.h"
  #include "libavutil/eval.h"
  #include "libavutil/mathematics.h"
  #include "libavutil/pixdesc.h"
  #include "libavutil/imgutils.h"
  #include "libavutil/dict.h"
  #include "libavutil/parseutils.h"
  #include "libavutil/samplefmt.h"
  #include "libavutil/avassert.h"
  #include "libavutil/time.h"
  #include "libavutil/bprint.h"
  #include "libavformat/avformat.h"
  #include "libavdevice/avdevice.h"
  #include "libswscale/swscale.h"
  #include "libavutil/opt.h"
  #include "libavcodec/avfft.h"
  #include "libswresample/swresample.h"
  }

#include <SDL.h>
#include <SDL_thread.h>

#include "cmdutils.h"

#include <assert.h>
//}}}
//{{{  defines
const char program_name[] = "ffplay";
const int program_birth_year = 2003;

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512

/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04

/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1

/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)
//}}}
//{{{  enums
enum { AV_SYNC_AUDIO_MASTER, AV_SYNC_VIDEO_MASTER, AV_SYNC_EXTERNAL_CLOCK };

enum ShowMode { SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB };
//}}}
//{{{  structs
//{{{
struct sMyAVPacketList {
  AVPacket pkt;
  sMyAVPacketList* next;
  int serial;
  };
//}}}
//{{{
struct sPacketQueue {
  sMyAVPacketList* first_pkt;
  sMyAVPacketList* last_pkt;
  int nb_packets;
  int size;

  int64_t duration;
  int abort_request;

  int serial;

  SDL_mutex* mutex;
  SDL_cond* cond;
  };
//}}}

//{{{
struct sAudioParams {
  int freq;
  int channels;
  int64_t channel_layout;
  enum AVSampleFormat fmt;
  int frame_size;
  int bytes_per_sec;
  };
//}}}
//{{{
struct sClock {
  double pts;           /* clock base */
  double pts_drift;     /* clock base minus time at which we updated the clock */
  double last_updated;

  double speed;

  int serial;           /* clock is based on a packet with this serial */
  int paused;
  int* queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
  };
//}}}

//{{{
// Common struct for handling all types of decoded data and allocated render buffers. */
struct sFrame {
  AVFrame* frame;
  AVSubtitle sub;

  int serial;
  double pts;           // presentation timestamp for the frame */
  double duration;      // estimated duration of the frame */

  int64_t pos;          // byte position of the frame in the input file */
  int width;
  int height;

  int format;
  AVRational sar;

  int uploaded;
  int flip_v;
  };
//}}}
//{{{
struct sFrameQueue {
  sFrame queue[FRAME_QUEUE_SIZE];
  int rindex;
  int windex;

  int size;
  int max_size;
  int keep_last;
  int rindex_shown;

  SDL_mutex* mutex;
  SDL_cond* cond;

  sPacketQueue* pktq;
  };
//}}}

//{{{
struct sDecoder {
  AVPacket pkt;
  sPacketQueue* queue;
  AVCodecContext* avctx;
  int pkt_serial;

  int finished;
  int packet_pending;

  SDL_cond* empty_queue_cond;

  int64_t start_pts;
  AVRational start_pts_tb;

  int64_t next_pts;
  AVRational next_pts_tb;

  SDL_Thread* decoder_tid;
  };
//}}}
//{{{
struct sVideoState {
  char* filename;
  AVInputFormat* iformat;
  AVFormatContext* ic;
  int eof;

  int width, height, xleft, ytop;
  int last_video_stream, last_audio_stream, last_subtitle_stream;
  int step;

  SDL_Thread* read_tid;
  SDL_cond* continue_read_thread;

  double frame_timer;
  double frame_last_returned_time;
  double frame_last_filter_delay;

  int abort_request;
  int force_refresh;
  int paused;
  int last_paused;

  int queue_attachments_req;
  int seek_req;
  int seek_flags;
  int64_t seek_pos;
  int64_t seek_rel;
  int read_pause_return;
  int realtime;

  //{{{  clock
  sClock audclk;
  sClock vidclk;
  sClock extclk;
  //}}}
  //{{{  frameQueue
  sFrameQueue pictq;
  sFrameQueue subpq;
  sFrameQueue sampq;
  //}}}
  //{{{  decoders
  sDecoder auddec;
  sDecoder viddec;
  sDecoder subdec;
  //}}}
  //{{{  textures
  SDL_Texture* vis_texture;
  SDL_Texture* sub_texture;
  SDL_Texture* vid_texture;
  //}}}

  //{{{  video
  int video_stream;
  AVStream* video_st;
  sPacketQueue videoq;
  double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
  struct SwsContext* img_convert_ctx;
  struct SwsContext* sub_convert_ctx;
  //}}}
  //{{{  audio
  int audio_stream;
  int av_sync_type;
  double audio_clock;
  int audio_clock_serial;
  double audio_diff_cum; // used for AV difference average computation */
  double audio_diff_avg_coef;
  double audio_diff_threshold;
  int audio_diff_avg_count;
  AVStream* audio_st;
  sPacketQueue audioq;
  int audio_hw_buf_size;
  uint8_t* audio_buf;
  uint8_t* audio_buf1;
  unsigned int audio_buf_size; // in bytes */
  unsigned int audio_buf1_size;
  int audio_buf_index; // in bytes */
  int audio_write_buf_size;
  int audio_volume;
  int muted;

  sAudioParams audio_src;
  sAudioParams audio_tgt;
  struct SwrContext* swr_ctx;
  int frame_drops_early;
  int frame_drops_late;
  //}}}
  //{{{  visualise
  enum ShowMode show_mode;

  int16_t sample_array[SAMPLE_ARRAY_SIZE];
  int sample_array_index;
  int last_i_start;
  RDFTContext* rdft;
  int rdft_bits;
  FFTSample* rdft_data;
  int xpos;
  double last_vis_time;
  //}}}
  //{{{  subtitle
  int subtitle_stream;
  AVStream* subtitle_st;
  sPacketQueue subtitleq;
  //}}}
  };
//}}}
//}}}

namespace {
  //{{{  const
  const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
  const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
  //}}}
  //{{{  vars
  unsigned sws_flags = SWS_BICUBIC;
  int64_t last_time;

  AVInputFormat* file_iformat;
  const char* input_filename;

  int default_width  = 640;
  int default_height = 360;
  int screen_width  = 0;
  int screen_height = 0;
  int screen_left = SDL_WINDOWPOS_CENTERED;
  int screen_top = SDL_WINDOWPOS_CENTERED;

  int audio_disable;
  int video_disable;
  int subtitle_disable;

  const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
  int seek_by_bytes = -1;
  float seek_interval = 10;
  int display_disable;
  int borderless;
  int alwaysontop;

  int startup_volume = 100;
  int show_status = -1;

  int av_sync_type = AV_SYNC_AUDIO_MASTER;

  int64_t start_time = AV_NOPTS_VALUE;
  int64_t duration = AV_NOPTS_VALUE;

  int fast = 1;
  int genpts = 0;
  int lowres = 0;
  int decoder_reorder_pts = -1;

  int framedrop = -1;
  int infinite_buffer = -1;

  enum ShowMode show_mode = SHOW_MODE_NONE;

  const char* audio_codec_name;
  const char* subtitle_codec_name;
  const char* video_codec_name;

  double rdftspeed = 0.01;
  int64_t cursor_last_shown;
  int cursor_hidden = 0;

  int autorotate = 1;
  int find_stream_info = 1;
  int filter_nbthreads = 0;

  /* current context */
  int is_full_screen;
  int64_t audio_callback_time;

  AVPacket flush_pkt;

  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_RendererInfo renderer_info = {0};
  SDL_AudioDeviceID audio_dev;
  //}}}
  //{{{
  const struct TextureFormatEntry {
    enum AVPixelFormat format;
    int texture_fmt;
    } sdl_texture_format_map[] = {
      { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
      { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
      { AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
      { AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
      { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
      { AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
      { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
      { AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
      { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
      { AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
      { AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
      { AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
      { AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
      { AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
      { AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
      { AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
      { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
      { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
      { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
      { AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
  };
  //}}}
  //{{{
  inline int compute_mod (int a, int b) { return a < 0 ? a%b + b : a%b; }
  //}}}
  //{{{
  inline int cmp_audio_fmts (enum AVSampleFormat fmt1, int64_t channel_count1,
                                    enum AVSampleFormat fmt2, int64_t channel_count2) {
  // If channel count == 1, planar and non-planar formats are the same

    if (channel_count1 == 1 && channel_count2 == 1)
      return av_get_packed_sample_fmt (fmt1) != av_get_packed_sample_fmt (fmt2);
    else
      return channel_count1 != channel_count2 || fmt1 != fmt2;
    }
  //}}}
  //{{{
  inline int64_t get_valid_channel_layout (int64_t channel_layout, int channels) {

    if (channel_layout && av_get_channel_layout_nb_channels (channel_layout) == channels)
      return channel_layout;
    else
      return 0;
    }
  //}}}

  //{{{  packetQueue
  //{{{
  int packet_queue_put_private (sPacketQueue* packetQueue, AVPacket* packet) {

    sMyAVPacketList* pkt1;

    if (packetQueue->abort_request)
      return -1;

    pkt1 = (sMyAVPacketList*)av_malloc (sizeof(sMyAVPacketList));
    if (!pkt1)
      return -1;

    pkt1->pkt = *packet;
    pkt1->next = NULL;

    if (packet == &flush_pkt)
      packetQueue->serial++;
    pkt1->serial = packetQueue->serial;

    if (!packetQueue->last_pkt)
      packetQueue->first_pkt = pkt1;
    else
      packetQueue->last_pkt->next = pkt1;

    packetQueue->last_pkt = pkt1;
    packetQueue->nb_packets++;
    packetQueue->size += pkt1->pkt.size + sizeof(*pkt1);
    packetQueue->duration += pkt1->pkt.duration;

    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal (packetQueue->cond);
    return 0;
    }
  //}}}
  //{{{
  int packet_queue_put (sPacketQueue* packetQueue, AVPacket* packet) {

    int ret;

    SDL_LockMutex (packetQueue->mutex);
    ret = packet_queue_put_private (packetQueue, packet);
    SDL_UnlockMutex (packetQueue->mutex);

    if (packet != &flush_pkt && ret < 0)
      av_packet_unref (packet);

    return ret;
    }
  //}}}
  //{{{
  int packet_queue_put_nullpacket (sPacketQueue* packetQueue, int stream_index) {

    AVPacket pkt1;
    AVPacket* pkt = &pkt1;

    av_init_packet (pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;

    return packet_queue_put (packetQueue, pkt);
    }
  //}}}

  //{{{
  int packet_queue_init (sPacketQueue* packetQueue) {

    memset (packetQueue, 0, sizeof(sPacketQueue));
    packetQueue->mutex = SDL_CreateMutex();
    if (!packetQueue->mutex) {
      av_log (NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
      return AVERROR(ENOMEM);
      }

    packetQueue->cond = SDL_CreateCond();
    if (!packetQueue->cond) {
      av_log (NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
      return AVERROR(ENOMEM);
      }

    packetQueue->abort_request = 1;
    return 0;
    }
  //}}}

  //{{{
  void packet_queue_flush (sPacketQueue* packetQueue) {

    SDL_LockMutex (packetQueue->mutex);

    sMyAVPacketList* pkt1;
    for (auto pkt = packetQueue->first_pkt; pkt; pkt = pkt1) {
      pkt1 = pkt->next;
      av_packet_unref (&pkt->pkt);
      av_freep (&pkt);
      }

    packetQueue->last_pkt = NULL;
    packetQueue->first_pkt = NULL;
    packetQueue->nb_packets = 0;
    packetQueue->size = 0;
    packetQueue->duration = 0;

    SDL_UnlockMutex (packetQueue->mutex);
    }
  //}}}
  //{{{
  void packet_queue_destroy (sPacketQueue* packetQueue) {

    packet_queue_flush (packetQueue);
    SDL_DestroyMutex (packetQueue->mutex);
    SDL_DestroyCond (packetQueue->cond);
    }
  //}}}

  //{{{
  void packet_queue_abort (sPacketQueue* packetQueue) {

    SDL_LockMutex (packetQueue->mutex);

    packetQueue->abort_request = 1;

    SDL_CondSignal (packetQueue->cond);
    SDL_UnlockMutex(packetQueue->mutex);
    }
  //}}}
  //{{{
  void packet_queue_start (sPacketQueue* packetQueue) {

    SDL_LockMutex (packetQueue->mutex);

    packetQueue->abort_request = 0;
    packet_queue_put_private (packetQueue, &flush_pkt);

    SDL_UnlockMutex (packetQueue->mutex);
    }
  //}}}

  //{{{
  int packet_queue_get (sPacketQueue* packetQueue, AVPacket* packet, int block, int* serial) {
  /* return < 0 if aborted, 0 if no packet and > 0 if packet.  */

    sMyAVPacketList* pkt1;
    int ret;

    SDL_LockMutex (packetQueue->mutex);

    for (;;) {
      if (packetQueue->abort_request) {
        ret = -1;
        break;
        }

      pkt1 = packetQueue->first_pkt;
      if (pkt1) {
        packetQueue->first_pkt = pkt1->next;
        if (!packetQueue->first_pkt)
          packetQueue->last_pkt = NULL;
        packetQueue->nb_packets--;
        packetQueue->size -= pkt1->pkt.size + sizeof(*pkt1);
        packetQueue->duration -= pkt1->pkt.duration;
        *packet = pkt1->pkt;

        if (serial)
          *serial = pkt1->serial;
        av_free (pkt1);
        ret = 1;
        break;
        }

      else if (!block) {
        ret = 0;
        break;
        }

      else
        SDL_CondWait (packetQueue->cond, packetQueue->mutex);
      }

    SDL_UnlockMutex (packetQueue->mutex);
    return ret;
    }
  //}}}
  //}}}
  //{{{  frameQueue
  //{{{
  void frame_queue_unref_item (sFrame* frame) {

    av_frame_unref (frame->frame);
    avsubtitle_free (&frame->sub);
    }
  //}}}

  //{{{
  int frame_queue_init (sFrameQueue* frameQueue, sPacketQueue* packetQueue, int max_size, int keep_last) {

    memset (frameQueue, 0, sizeof(sFrameQueue));
    if (!(frameQueue->mutex = SDL_CreateMutex())) {
      av_log (NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
      return AVERROR(ENOMEM);
      }

    if (!(frameQueue->cond = SDL_CreateCond())) {
      av_log (NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
      return AVERROR(ENOMEM);
      }

    frameQueue->pktq = packetQueue;
    frameQueue->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    frameQueue->keep_last = !!keep_last;

    for (int i = 0; i < frameQueue->max_size; i++)
      if (!(frameQueue->queue[i].frame = av_frame_alloc()))
        return AVERROR(ENOMEM);

    return 0;
    }
  //}}}

  //{{{
  void frame_queue_destory (sFrameQueue* frameQueue) {

    for (int i = 0; i < frameQueue->max_size; i++) {
      sFrame* vp = &frameQueue->queue[i];
      frame_queue_unref_item (vp);
      av_frame_free (&vp->frame);
      }

    SDL_DestroyMutex (frameQueue->mutex);
    SDL_DestroyCond (frameQueue->cond);
    }
  //}}}
  //{{{
  void frame_queue_signal (sFrameQueue* frameQueue) {

    SDL_LockMutex (frameQueue->mutex);
    SDL_CondSignal (frameQueue->cond);
    SDL_UnlockMutex (frameQueue->mutex);
   }
  //}}}

  //{{{
  sFrame* frame_queue_peek (sFrameQueue* frameQueue) {
    return &frameQueue->queue[(frameQueue->rindex + frameQueue->rindex_shown) % frameQueue->max_size];
    }
  //}}}
  //{{{
  sFrame* frame_queue_peek_next (sFrameQueue* frameQueue) {
    return &frameQueue->queue[(frameQueue->rindex + frameQueue->rindex_shown + 1) % frameQueue->max_size];
    }
  //}}}
  //{{{
  sFrame* frame_queue_peek_last (sFrameQueue* frameQueue) {
    return &frameQueue->queue[frameQueue->rindex];
    }
  //}}}
  //{{{
  sFrame* frame_queue_peek_writable (sFrameQueue* frameQueue) {

    /* wait until we have space to put a new frame */
    SDL_LockMutex(frameQueue->mutex);
    while (frameQueue->size >= frameQueue->max_size && !frameQueue->pktq->abort_request) {
      SDL_CondWait (frameQueue->cond, frameQueue->mutex);
      }
    SDL_UnlockMutex (frameQueue->mutex);

    if (frameQueue->pktq->abort_request)
      return NULL;

    return &frameQueue->queue[frameQueue->windex];
    }
  //}}}
  //{{{
  sFrame* frame_queue_peek_readable (sFrameQueue* frameQueue) {
  /* wait until we have a readable a new frame */

    SDL_LockMutex (frameQueue->mutex);
    while (frameQueue->size - frameQueue->rindex_shown <= 0 && !frameQueue->pktq->abort_request) {
      SDL_CondWait(frameQueue->cond, frameQueue->mutex);
      }
    SDL_UnlockMutex (frameQueue->mutex);

    if (frameQueue->pktq->abort_request)
      return NULL;

    return &frameQueue->queue[(frameQueue->rindex + frameQueue->rindex_shown) % frameQueue->max_size];
    }
  //}}}

  //{{{
  void frame_queue_push (sFrameQueue* frameQueue) {

    if (++frameQueue->windex == frameQueue->max_size)
      frameQueue->windex = 0;

    SDL_LockMutex (frameQueue->mutex);
    frameQueue->size++;
    SDL_CondSignal (frameQueue->cond);
    SDL_UnlockMutex (frameQueue->mutex);
    }
  //}}}
  //{{{
  void frame_queue_next (sFrameQueue* frameQueue) {

    if (frameQueue->keep_last && !frameQueue->rindex_shown) {
      frameQueue->rindex_shown = 1;
      return;
      }

    frame_queue_unref_item(&frameQueue->queue[frameQueue->rindex]);
    if (++frameQueue->rindex == frameQueue->max_size)
      frameQueue->rindex = 0;

    SDL_LockMutex (frameQueue->mutex);

    frameQueue->size--;

    SDL_CondSignal (frameQueue->cond);
    SDL_UnlockMutex (frameQueue->mutex);
    }
  //}}}

  //{{{
  int frame_queue_nb_remaining (sFrameQueue* frameQueue) {
  /* return the number of undisplayed frames in the queue */

    return frameQueue->size - frameQueue->rindex_shown;
    }
  //}}}
  //{{{
  int64_t frame_queue_last_pos (sFrameQueue* frameQueue) {
  /* return last shown position */

    sFrame *fp = &frameQueue->queue[frameQueue->rindex];
    if (frameQueue->rindex_shown && fp->serial == frameQueue->pktq->serial)
      return fp->pos;
    else
      return -1;
    }
  //}}}
  //}}}
  //{{{  decoder
  //{{{
  void decoder_init (sDecoder* decoder, AVCodecContext* avctx, sPacketQueue* queue, SDL_cond* empty_queue_cond) {

    memset (decoder, 0, sizeof(sDecoder));
    decoder->avctx = avctx;
    decoder->queue = queue;
    decoder->empty_queue_cond = empty_queue_cond;
    decoder->start_pts = AV_NOPTS_VALUE;
    decoder->pkt_serial = -1;
    }
  //}}}
  //{{{
  int decoder_start (sDecoder* decoder, int (*fn)(void*), const char *thread_name, void* arg) {

    packet_queue_start (decoder->queue);
    decoder->decoder_tid = SDL_CreateThread (fn, thread_name, arg);
    if (!decoder->decoder_tid) {
      av_log (NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
      return AVERROR (ENOMEM);
      }

    return 0;
    }
  //}}}

  //{{{
  int decoder_decode_frame (sDecoder* decoder, AVFrame* frame, AVSubtitle* sub) {

    int ret = AVERROR(EAGAIN);
    for (;;) {
      AVPacket pkt;
      if (decoder->queue->serial == decoder->pkt_serial) {
        do {
          if (decoder->queue->abort_request)
            return -1;

          switch (decoder->avctx->codec_type) {
            //{{{
            case AVMEDIA_TYPE_VIDEO:
              ret = avcodec_receive_frame(decoder->avctx, frame);

              if (ret >= 0) {
                if (decoder_reorder_pts == -1)
                  frame->pts = frame->best_effort_timestamp;
                else if (!decoder_reorder_pts)
                  frame->pts = frame->pkt_dts;
                }

              break;
            //}}}
            //{{{
            case AVMEDIA_TYPE_AUDIO:
              ret = avcodec_receive_frame(decoder->avctx, frame);
              if (ret >= 0) {
                AVRational tb = { 1, frame->sample_rate };

                if (frame->pts != AV_NOPTS_VALUE)
                  frame->pts = av_rescale_q(frame->pts, decoder->avctx->pkt_timebase, tb);

                else if (decoder->next_pts != AV_NOPTS_VALUE)
                  frame->pts = av_rescale_q(decoder->next_pts, decoder->next_pts_tb, tb);

                if (frame->pts != AV_NOPTS_VALUE) {
                  decoder->next_pts = frame->pts + frame->nb_samples;
                  decoder->next_pts_tb = tb;
                  }
                }

              break;
            //}}}
            default: break;
            }
          if (ret == AVERROR_EOF) {
            decoder->finished = decoder->pkt_serial;
            avcodec_flush_buffers (decoder->avctx);
            return 0;
            }
          if (ret >= 0)
            return 1;
          } while (ret != AVERROR(EAGAIN));
        }

      do {
        if (decoder->queue->nb_packets == 0)
          SDL_CondSignal (decoder->empty_queue_cond);
        if (decoder->packet_pending) {
          av_packet_move_ref (&pkt, &decoder->pkt);
          decoder->packet_pending = 0;
          }
        else {
          if (packet_queue_get (decoder->queue, &pkt, 1, &decoder->pkt_serial) < 0)
            return -1;
          }
        if (decoder->queue->serial == decoder->pkt_serial)
          break;
        av_packet_unref (&pkt);
        } while (1);

      if (pkt.data == flush_pkt.data) {
        avcodec_flush_buffers (decoder->avctx);
        decoder->finished = 0;
        decoder->next_pts = decoder->start_pts;
        decoder->next_pts_tb = decoder->start_pts_tb;
        }
      else {
        if (decoder->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
          //{{{  subtitle
          int got_frame = 0;
          ret = avcodec_decode_subtitle2 (decoder->avctx, sub, &got_frame, &pkt);
          if (ret < 0) {
            ret = AVERROR(EAGAIN);
            }

          else {
            if (got_frame && !pkt.data) {
              decoder->packet_pending = 1;
              av_packet_move_ref (&decoder->pkt, &pkt);
              }

            ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }

          }
          //}}}
        else {
          if (avcodec_send_packet (decoder->avctx, &pkt) == AVERROR(EAGAIN)) {
            av_log (decoder->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
            decoder->packet_pending = 1;
            av_packet_move_ref (&decoder->pkt, &pkt);
            }
          }
        av_packet_unref (&pkt);
        }
      }
    }
  //}}}

  //{{{
  void decoder_abort (sDecoder* decoder, sFrameQueue* frameQueue) {

    packet_queue_abort (decoder->queue);
    frame_queue_signal (frameQueue);

    SDL_WaitThread (decoder->decoder_tid, NULL);

    decoder->decoder_tid = NULL;
    packet_queue_flush (decoder->queue);
    }
  //}}}
  //{{{
  void decoder_destroy (sDecoder* decoder) {

    av_packet_unref (&decoder->pkt);
    avcodec_free_context (&decoder->avctx);
    }
  //}}}
  //}}}
  //{{{  clock
  //{{{
  double get_clock (sClock* c) {

    if (*c->queue_serial != c->serial)
      return NAN;

    if (c->paused) {
      return c->pts;
      }
    else {
      double time = av_gettime_relative() / 1000000.0;
      return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
      }
    }
  //}}}

  //{{{
  void set_clock_at (sClock* c, double pts, int serial, double time) {

    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
    }
  //}}}
  //{{{
  void set_clock (sClock* c, double pts, int serial) {

    double time = av_gettime_relative() / 1000000.0;
    set_clock_at (c, pts, serial, time);
    }
  //}}}
  //{{{
  void set_clock_speed (sClock* c, double speed) {

    set_clock (c, get_clock(c), c->serial);
    c->speed = speed;
    }
  //}}}

  //{{{
  void init_clock (sClock* c, int* queue_serial) {

    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock (c, NAN, -1);
    }
  //}}}
  //{{{
  void sync_clock_to_slave (sClock* c, sClock* slave) {

    double clock = get_clock(c);
    double slave_clock = get_clock(slave);

    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
      set_clock(c, slave_clock, slave->serial);
    }
  //}}}

  //{{{
  int get_master_sync_type (sVideoState* videoState) {

    if (videoState->av_sync_type == AV_SYNC_VIDEO_MASTER) {
      if (videoState->video_st)
        return AV_SYNC_VIDEO_MASTER;
      else
        return AV_SYNC_AUDIO_MASTER;
      }

    else if (videoState->av_sync_type == AV_SYNC_AUDIO_MASTER) {
      if (videoState->audio_st)
        return AV_SYNC_AUDIO_MASTER;
      else
        return AV_SYNC_EXTERNAL_CLOCK;
      }

    else {
      return AV_SYNC_EXTERNAL_CLOCK;
      }

    }
  //}}}
  //{{{
  double get_master_clock (sVideoState* videoState) {

    double val;

    switch (get_master_sync_type (videoState)) {
      case AV_SYNC_VIDEO_MASTER:
        val = get_clock (&videoState->vidclk);
        break;

      case AV_SYNC_AUDIO_MASTER:
        val = get_clock (&videoState->audclk);
        break;

      default:
        val = get_clock (&videoState->extclk);
        break;
        }

    return val;
    }
  //}}}

  //{{{
  void check_external_clock_speed (sVideoState* videoState) {

    if ((videoState->video_stream >= 0 && videoState->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
        (videoState->audio_stream >= 0 && videoState->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES))
      set_clock_speed (&videoState->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, videoState->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));

    else if ((videoState->video_stream < 0 || videoState->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
             (videoState->audio_stream < 0 || videoState->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES))
      set_clock_speed (&videoState->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, videoState->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));

    else {
      double speed = videoState->extclk.speed;
      if (speed != 1.0)
        set_clock_speed (&videoState->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
      }

    }
  //}}}
  //}}}

  //{{{
  void stream_component_close (sVideoState* videoState, int stream_index) {

    AVFormatContext* ic = videoState->ic;
    if (stream_index < 0 || stream_index >= (int)ic->nb_streams)
      return;

    AVCodecParameters* codecpar = ic->streams[stream_index]->codecpar;
    switch (codecpar->codec_type) {
      case AVMEDIA_TYPE_AUDIO:
        decoder_abort (&videoState->auddec, &videoState->sampq);
        SDL_CloseAudioDevice (audio_dev);
        decoder_destroy (&videoState->auddec);
        swr_free (&videoState->swr_ctx);
        av_freep (&videoState->audio_buf1);
        videoState->audio_buf1_size = 0;
        videoState->audio_buf = NULL;

        if (videoState->rdft) {
          av_rdft_end (videoState->rdft);
          av_freep (&videoState->rdft_data);
          videoState->rdft = NULL;
          videoState->rdft_bits = 0;
          }
        break;

      case AVMEDIA_TYPE_VIDEO:
        decoder_abort (&videoState->viddec, &videoState->pictq);
        decoder_destroy (&videoState->viddec);
        break;

      case AVMEDIA_TYPE_SUBTITLE:
        decoder_abort (&videoState->subdec, &videoState->subpq);
        decoder_destroy (&videoState->subdec);
        break;

      default:
        break;
      }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
      case AVMEDIA_TYPE_AUDIO:
        videoState->audio_st = NULL;
        videoState->audio_stream = -1;
        break;

      case AVMEDIA_TYPE_VIDEO:
        videoState->video_st = NULL;
        videoState->video_stream = -1;
        break;

      case AVMEDIA_TYPE_SUBTITLE:
        videoState->subtitle_st = NULL;
        videoState->subtitle_stream = -1;
        break;

      default:
        break;
      }
    }
  //}}}
  //{{{
  void stream_close (sVideoState* videoState) {

    // XXX: use a special url_shutdown call to abort parse cleanly
    videoState->abort_request = 1;
    SDL_WaitThread (videoState->read_tid, NULL);

    // close each stream
    if (videoState->audio_stream >= 0)
      stream_component_close (videoState, videoState->audio_stream);
    if (videoState->video_stream >= 0)
      stream_component_close (videoState, videoState->video_stream);
    if (videoState->subtitle_stream >= 0)
      stream_component_close (videoState, videoState->subtitle_stream);

    avformat_close_input(&videoState->ic);

    packet_queue_destroy (&videoState->videoq);
    packet_queue_destroy (&videoState->audioq);
    packet_queue_destroy (&videoState->subtitleq);

    // free all pictures
    frame_queue_destory (&videoState->pictq);
    frame_queue_destory (&videoState->sampq);
    frame_queue_destory (&videoState->subpq);
    SDL_DestroyCond (videoState->continue_read_thread);
    sws_freeContext (videoState->img_convert_ctx);
    sws_freeContext (videoState->sub_convert_ctx);

    av_free(videoState->filename);

    if (videoState->vis_texture)
      SDL_DestroyTexture (videoState->vis_texture);
    if (videoState->vid_texture)
      SDL_DestroyTexture (videoState->vid_texture);
    if (videoState->sub_texture)
      SDL_DestroyTexture (videoState->sub_texture);
    av_free (videoState);
    }
  //}}}
  //{{{
  void stream_seek (sVideoState* videoState, int64_t pos, int64_t rel, int seek_by_bytes) {

    if (!videoState->seek_req) {
      videoState->seek_pos = pos;
      videoState->seek_rel = rel;
      videoState->seek_flags &= ~AVSEEK_FLAG_BYTE;
      if (seek_by_bytes)
        videoState->seek_flags |= AVSEEK_FLAG_BYTE;
      videoState->seek_req = 1;

      SDL_CondSignal (videoState->continue_read_thread);
      }
    }
  //}}}
  //{{{
  void stream_toggle_pause (sVideoState* videoState) {

    if (videoState->paused) {
      videoState->frame_timer += av_gettime_relative() / 1000000.0 - videoState->vidclk.last_updated;
      if (videoState->read_pause_return != AVERROR(ENOSYS))
        videoState->vidclk.paused = 0;
      set_clock (&videoState->vidclk, get_clock (&videoState->vidclk), videoState->vidclk.serial);
      }

    set_clock (&videoState->extclk, get_clock (&videoState->extclk), videoState->extclk.serial);

    videoState->paused = !videoState->paused;
    videoState->audclk.paused = !videoState->paused;
    videoState->vidclk.paused = !videoState->paused;
    videoState->extclk.paused = !videoState->paused;
    }
  //}}}
  //{{{
  void step_to_next_frame (sVideoState* videoState) {
  // if the stream is paused unpause it, then step

    if (videoState->paused)
      stream_toggle_pause (videoState);

    videoState->step = 1;
    }
  //}}}

  // subtitle
  //{{{
  int subtitle_thread (void* arg) {

    auto videoState = (sVideoState*)arg;

    sFrame* sp;
    for (;;) {
      if (!(sp = frame_queue_peek_writable (&videoState->subpq)))
        return 0;

      int got_subtitle = decoder_decode_frame (&videoState->subdec, NULL, &sp->sub);
      if (got_subtitle < 0)
        break;

      if (got_subtitle && sp->sub.format == 0) {
        double pts = 0;
        if (sp->sub.pts != AV_NOPTS_VALUE)
          pts = sp->sub.pts / (double)AV_TIME_BASE;
        sp->pts = pts;
        sp->serial = videoState->subdec.pkt_serial;
        sp->width = videoState->subdec.avctx->width;
        sp->height = videoState->subdec.avctx->height;
        sp->uploaded = 0;

        // now we can update the picture count
        frame_queue_push (&videoState->subpq);
        }
      else if (got_subtitle)
        avsubtitle_free (&sp->sub);
      }

    return 0;
    }
  //}}}

  //  audio
  //{{{
  void update_sample_display (sVideoState* videoState, short* samples, int samples_size) {
  // copy samples for viewing in editor window

    int size = samples_size / sizeof(short);
    while (size > 0) {
      int len = SAMPLE_ARRAY_SIZE - videoState->sample_array_index;
      if (len > size)
        len = size;

      memcpy (videoState->sample_array + videoState->sample_array_index, samples, len * sizeof(short));
      samples += len;

      videoState->sample_array_index += len;
      if (videoState->sample_array_index >= SAMPLE_ARRAY_SIZE)
        videoState->sample_array_index = 0;

      size -= len;
      }
    }
  //}}}
  //{{{
  int synchronize_audio (sVideoState* videoState, int nb_samples) {
  // return the wanted number of samples to get better sync if sync_type is video or external master clock

    int wanted_nb_samples = nb_samples;

    // if not master, then we try to remove or add samples to correct the clock
    if (get_master_sync_type (videoState) != AV_SYNC_AUDIO_MASTER) {
      double diff, avg_diff;
      int min_nb_samples, max_nb_samples;

      diff = get_clock (&videoState->audclk) - get_master_clock (videoState);

      if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
          videoState->audio_diff_cum = diff + videoState->audio_diff_avg_coef * videoState->audio_diff_cum;
        if (videoState->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
          // not enough measures to have a correct estimate
          videoState->audio_diff_avg_count++;
          }
        else {
          // estimate the A-V difference
          avg_diff = videoState->audio_diff_cum * (1.0 - videoState->audio_diff_avg_coef);

          if (fabs(avg_diff) >= videoState->audio_diff_threshold) {
            wanted_nb_samples = nb_samples + (int)(diff * videoState->audio_src.freq);
            min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
            max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
            wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
            }
          av_log (NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                  diff, avg_diff, wanted_nb_samples - nb_samples,
                  videoState->audio_clock, videoState->audio_diff_threshold);
          }
        }
      else {
        // too big difference : may be initial PTS errors, so reset A-V filter
        videoState->audio_diff_avg_count = 0;
        videoState->audio_diff_cum       = 0;
        }
      }

    return wanted_nb_samples;
    }
  //}}}
  //{{{
  int audio_decode_frame (sVideoState* videoState) {
  // Decode one audio frame and return its uncompressed size.
  // The processed audio frame is decoded, converted if required, and
  // stored in videoState->audio_buf, with size in bytes given by the return value

    if (videoState->paused)
      return -1;

    sFrame* af;
    do {
      #if defined(_WIN32)
        while (frame_queue_nb_remaining(&videoState->sampq) == 0) {
          if ((av_gettime_relative() - audio_callback_time) > 1000000LL * videoState->audio_hw_buf_size / videoState->audio_tgt.bytes_per_sec / 2)
            return -1;
          av_usleep (1000);
          }
      #endif
      if (!(af = frame_queue_peek_readable (&videoState->sampq)))
        return -1;
      frame_queue_next (&videoState->sampq);
      } while (af->serial != videoState->audioq.serial);

    int data_size = av_samples_get_buffer_size (
      NULL, af->frame->channels, af->frame->nb_samples, (AVSampleFormat)af->frame->format, 1);

    int64_t dec_channel_layout =
      (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
      af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    int wanted_nb_samples = synchronize_audio (videoState, af->frame->nb_samples);

    if (af->frame->format != videoState->audio_src.fmt ||
        dec_channel_layout != videoState->audio_src.channel_layout ||
        af->frame->sample_rate != videoState->audio_src.freq ||
        (wanted_nb_samples != af->frame->nb_samples && !videoState->swr_ctx)) {
      swr_free(&videoState->swr_ctx);
      videoState->swr_ctx = swr_alloc_set_opts (
        NULL, videoState->audio_tgt.channel_layout, videoState->audio_tgt.fmt, videoState->audio_tgt.freq, dec_channel_layout,
        (AVSampleFormat)af->frame->format, af->frame->sample_rate, 0, NULL);
      if (!videoState->swr_ctx || swr_init(videoState->swr_ctx) < 0) {
        av_log (NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                videoState->audio_tgt.freq, av_get_sample_fmt_name(videoState->audio_tgt.fmt), videoState->audio_tgt.channels);
        swr_free (&videoState->swr_ctx);
        return -1;
        }
      videoState->audio_src.channel_layout = dec_channel_layout;
      videoState->audio_src.channels = af->frame->channels;
      videoState->audio_src.freq = af->frame->sample_rate;
      videoState->audio_src.fmt = (AVSampleFormat)af->frame->format;
      }

    int resampled_data_size;
    if (videoState->swr_ctx) {
      const uint8_t** in = (const uint8_t**)af->frame->extended_data;
      uint8_t** out = &videoState->audio_buf1;
      int out_count = (int64_t)wanted_nb_samples * videoState->audio_tgt.freq / af->frame->sample_rate + 256;
      int out_size  = av_samples_get_buffer_size(NULL, videoState->audio_tgt.channels, out_count, videoState->audio_tgt.fmt, 0);
      int len2;
      if (out_size < 0) {
        //{{{  error return
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
        return -1;
        }
        //}}}
      if (wanted_nb_samples != af->frame->nb_samples) {
        if (swr_set_compensation (videoState->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * videoState->audio_tgt.freq / af->frame->sample_rate,
                                  wanted_nb_samples * videoState->audio_tgt.freq / af->frame->sample_rate) < 0) {
          //{{{  error return
          av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
          return -1;
          }
          //}}}
        }

      av_fast_malloc (&videoState->audio_buf1, &videoState->audio_buf1_size, out_size);
      if (!videoState->audio_buf1)
        return AVERROR(ENOMEM);

      len2 = swr_convert (videoState->swr_ctx, out, out_count, in, af->frame->nb_samples);
      if (len2 < 0) {
        //{{{  error return
        av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
        return -1;
        }
        //}}}

      if (len2 == out_count) {
        av_log (NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
        if (swr_init (videoState->swr_ctx) < 0)
          swr_free (&videoState->swr_ctx);
        }

      videoState->audio_buf = videoState->audio_buf1;
      resampled_data_size = len2 * videoState->audio_tgt.channels * av_get_bytes_per_sample(videoState->audio_tgt.fmt);
      }

    else {
      videoState->audio_buf = af->frame->data[0];
      resampled_data_size = data_size;
      }

    // update the audio clock with the pts
    av_unused double audio_clock0 = videoState->audio_clock;
    if (!isnan(af->pts))
      videoState->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
      videoState->audio_clock = NAN;
    videoState->audio_clock_serial = af->serial;

    #ifdef DEBUG
      {
      static double last_clock;
      printf ("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
             videoState->audio_clock - last_clock, videoState->audio_clock, audio_clock0);
      last_clock = videoState->audio_clock;
      }
    #endif

    return resampled_data_size;
    }
  //}}}
  //{{{
  void sdl_audio_callback (void* opaque, Uint8* stream, int len) {
  // prepare a new audio buffer

    auto videoState = (sVideoState*)opaque;

    audio_callback_time = av_gettime_relative();

    while (len > 0) {
      if (videoState->audio_buf_index >= (int)videoState->audio_buf_size) {
        int audio_size = audio_decode_frame (videoState);
        if (audio_size < 0) {
          // if error, just output silence
          videoState->audio_buf = NULL;
          videoState->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / videoState->audio_tgt.frame_size * videoState->audio_tgt.frame_size;
          }
        else {
          if (videoState->show_mode != SHOW_MODE_VIDEO)
            update_sample_display (videoState, (int16_t *)videoState->audio_buf, audio_size);
          videoState->audio_buf_size = audio_size;
          }
        videoState->audio_buf_index = 0;
        }

      int len1 = videoState->audio_buf_size - videoState->audio_buf_index;
      if (len1 > len)
        len1 = len;
      if (!videoState->muted && videoState->audio_buf && videoState->audio_volume == SDL_MIX_MAXVOLUME)
        memcpy (stream, (uint8_t *)videoState->audio_buf + videoState->audio_buf_index, len1);
      else {
        memset (stream, 0, len1);
        if (!videoState->muted && videoState->audio_buf)
          SDL_MixAudioFormat (stream, (uint8_t *)videoState->audio_buf + videoState->audio_buf_index, AUDIO_S16SYS, len1, videoState->audio_volume);
        }
      len -= len1;
      stream += len1;
      videoState->audio_buf_index += len1;
      }

    videoState->audio_write_buf_size = videoState->audio_buf_size - videoState->audio_buf_index;

    // Let's assume the audio driver that is used by SDL has two periods
    if (!isnan (videoState->audio_clock)) {
      set_clock_at (&videoState->audclk, videoState->audio_clock - (double)(2 * videoState->audio_hw_buf_size + videoState->audio_write_buf_size) / videoState->audio_tgt.bytes_per_sec, videoState->audio_clock_serial, audio_callback_time / 1000000.0);
      sync_clock_to_slave (&videoState->extclk, &videoState->audclk);
      }
    }
  //}}}
  //{{{
  int audio_open (sVideoState* videoState, int64_t wanted_channel_layout, int wanted_nb_channels,
                         int wanted_sample_rate, sAudioParams* audio_hw_params) {

    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    const char* env = SDL_getenv ("SDL_AUDIO_CHANNELS");
    if (env) {
      wanted_nb_channels = atoi (env);
      wanted_channel_layout = av_get_default_channel_layout (wanted_nb_channels);
      }

    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels (wanted_channel_layout)) {
      wanted_channel_layout = av_get_default_channel_layout (wanted_nb_channels);
      wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
      }

    //{{{  wanted_spec
    SDL_AudioSpec wanted_spec;

    wanted_nb_channels = av_get_channel_layout_nb_channels (wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
      //{{{  error return
      av_log (NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
      return -1;
      }
      //}}}

    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
      next_sample_rate_idx--;

    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2 (wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = videoState;
    //}}}
    SDL_AudioSpec spec;
    while (!(audio_dev = SDL_OpenAudioDevice (NULL, 0, &wanted_spec, &spec,
                                              SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
      av_log (NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
              wanted_spec.channels, wanted_spec.freq, SDL_GetError());
      wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
      if (!wanted_spec.channels) {
        wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
        wanted_spec.channels = wanted_nb_channels;
        if (!wanted_spec.freq) {
          //{{{  error return
          av_log(NULL, AV_LOG_ERROR, "No more combinations to try, audio open failed\n");
          return -1;
          }
          //}}}
        }
      wanted_channel_layout = av_get_default_channel_layout (wanted_spec.channels);
      }

    if (spec.format != AUDIO_S16SYS) {
      //{{{  error return
      av_log (NULL, AV_LOG_ERROR, "SDL advised audio format %d is not supported!\n", spec.format);
      return -1;
      }
      //}}}

    if (spec.channels != wanted_spec.channels) {
      wanted_channel_layout = av_get_default_channel_layout (spec.channels);
      if (!wanted_channel_layout) {
        //{{{  error return
        av_log (NULL, AV_LOG_ERROR, "SDL advised channel count %d is not supported!\n", spec.channels);
        return -1;
        }
        //}}}
      }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size (NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size (NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
      //{{{  error return
      av_log (NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
      return -1;
      }
      //}}}

    return spec.size;
    }
  //}}}
  //{{{
  int audio_thread (void* arg) {

    auto videoState = (sVideoState*)arg;
    int ret = 0;

    AVFrame* frame = av_frame_alloc();
    if (!frame)
      return AVERROR(ENOMEM);

    do {
      int got_frame = decoder_decode_frame (&videoState->auddec, frame, NULL);
      if (got_frame < 0)
        goto the_end;

      if (got_frame) {
        AVRational tb = {1, frame->sample_rate};

        sFrame* af;
        if (!(af = frame_queue_peek_writable (&videoState->sampq)))
           goto the_end;

        af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        af->pos = frame->pkt_pos;
        af->serial = videoState->auddec.pkt_serial;
        af->duration = av_q2d ({frame->nb_samples, frame->sample_rate});

        av_frame_move_ref (af->frame, frame);
        frame_queue_push (&videoState->sampq);
        }
      } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

  the_end:
    av_frame_free (&frame);
    return ret;
    }
  //}}}

  // video
  //{{{
  void calculate_display_rect (SDL_Rect* rect,
                                      int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                      int pic_width, int pic_height, AVRational pic_sar) {

    AVRational aspect_ratio = pic_sar;
    int64_t width, height, x, y;

    if (av_cmp_q (aspect_ratio, av_make_q(0, 1)) <= 0)
      aspect_ratio = av_make_q(1, 1);
    aspect_ratio = av_mul_q (aspect_ratio, av_make_q (pic_width, pic_height));

    // XXX: we suppose the screen has a 1.0 pixel ratio
    height = scr_height;
    width = av_rescale (height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > scr_width) {
      width = scr_width;
      height = av_rescale (width, aspect_ratio.den, aspect_ratio.num) & ~1;
      }

    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;

    rect->x = int(scr_xleft + x);
    rect->y = int(scr_ytop  + y);
    rect->w = FFMAX((int)width,  1);
    rect->h = FFMAX((int)height, 1);
    }
  //}}}
  //{{{
  void set_default_window_size (int width, int height, AVRational sar) {

    SDL_Rect rect;

    int max_width  = screen_width  ? screen_width  : INT_MAX;
    int max_height = screen_height ? screen_height : INT_MAX;

    if (max_width == INT_MAX && max_height == INT_MAX)
      max_height = height;
    calculate_display_rect (&rect, 0, 0, max_width, max_height, width, height, sar);

    default_width  = rect.w;
    default_height = rect.h;
    }
  //}}}
  //{{{
  int decode_video_frame (sVideoState* videoState, AVFrame* frame) {

    int got_picture = decoder_decode_frame (&videoState->viddec, frame, NULL);
    if (got_picture < 0)
      return -1;

    if (got_picture) {
      double dpts = NAN;

      if (frame->pts != AV_NOPTS_VALUE)
        dpts = av_q2d (videoState->video_st->time_base) * frame->pts;

      frame->sample_aspect_ratio = av_guess_sample_aspect_ratio (videoState->ic, videoState->video_st, frame);

      if (framedrop>0 || (framedrop && get_master_sync_type (videoState) != AV_SYNC_VIDEO_MASTER)) {
        if (frame->pts != AV_NOPTS_VALUE) {
          double diff = dpts - get_master_clock (videoState);
          if (!isnan (diff) && fabs (diff) < AV_NOSYNC_THRESHOLD &&
            diff - videoState->frame_last_filter_delay < 0 &&
            videoState->viddec.pkt_serial == videoState->vidclk.serial &&
            videoState->videoq.nb_packets) {
            videoState->frame_drops_early++;
            av_frame_unref (frame);
            got_picture = 0;
            }
          }
        }
      }

    return got_picture;
    }
  //}}}
  //{{{
  int queue_video_frame (sVideoState* videoState, AVFrame* src_frame, double pts, double duration, int64_t pos, int serial) {


    #if defined(DEBUG_SYNC)
      printf ("frame_type=%c pts=%0.3f\n", av_get_picture_type_char (src_frame->pict_type), pts);
    #endif

    sFrame* vp = frame_queue_peek_writable (&videoState->pictq);
    if (!vp)
      return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    //set_default_window_size (vp->width, vp->height, vp->sar);

    av_frame_move_ref (vp->frame, src_frame);
    frame_queue_push (&videoState->pictq);
    return 0;
    }
  //}}}
  //{{{
  int video_open (sVideoState* videoState) {

    int w = screen_width ? screen_width : default_width;
    int h = screen_height ? screen_height : default_height;

    SDL_SetWindowTitle (window, input_filename);
    SDL_SetWindowSize (window, w, h);
    SDL_SetWindowPosition (window, screen_left, screen_top);

    if (is_full_screen)
      SDL_SetWindowFullscreen (window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    SDL_ShowWindow (window);

    videoState->width  = w;
    videoState->height = h;

    return 0;
    }
  //}}}
  //{{{
  int video_thread (void* arg) {

    auto videoState = (sVideoState*)arg;

    AVFrame* frame = av_frame_alloc();
    if (!frame)
      return AVERROR (ENOMEM);

    for (;;) {
      int ret = decode_video_frame (videoState, frame);
      if (ret < 0)
        goto the_end;
      if (!ret)
        continue;

      AVRational tb = videoState->video_st->time_base;
      AVRational frame_rate = av_guess_frame_rate (videoState->ic, videoState->video_st, NULL);
      double duration = (frame_rate.num && frame_rate.den ?
                           av_q2d ({ frame_rate.den, frame_rate.num } ) : 0);
      double pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d (tb);
      ret = queue_video_frame (videoState, frame, pts, duration, frame->pkt_pos, videoState->viddec.pkt_serial);
      av_frame_unref (frame);

      if (ret < 0)
        goto the_end;
      }

  the_end:
    av_frame_free (&frame);
    return 0;
    }
  //}}}

  //{{{
  int stream_component_open (sVideoState* videoState, int stream_index) {
  // open a given stream. Return 0 if OK

    AVFormatContext* ic = videoState->ic;
    int stream_lowres = lowres;
    AVDictionaryEntry* t = NULL;
    AVDictionary* opts = NULL;
    AVCodec* codec = NULL;
    const char* forced_codec_name = NULL;
    int ret = 0;

    if (stream_index < 0 || stream_index >= (int)ic->nb_streams)
      return -1;

    AVCodecContext* avctx = avcodec_alloc_context3 (NULL);
    if (!avctx)
      return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context (avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
      goto fail;
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder (avctx->codec_id);
    switch (avctx->codec_type){
      //{{{
      case AVMEDIA_TYPE_AUDIO:
        videoState->last_audio_stream = stream_index;
        forced_codec_name = audio_codec_name;
        break;
      //}}}
      //{{{
      case AVMEDIA_TYPE_SUBTITLE:
        videoState->last_subtitle_stream = stream_index;
        forced_codec_name = subtitle_codec_name;
        break;
      //}}}
      //{{{
      case AVMEDIA_TYPE_VIDEO:
        videoState->last_video_stream = stream_index;
        forced_codec_name = video_codec_name;
        break;
      //}}}
      default:;
      }

    if (forced_codec_name)
      codec = avcodec_find_decoder_by_name (forced_codec_name);
    if (!codec) {
      //{{{  error fail
      if (forced_codec_name)
        av_log (NULL, AV_LOG_WARNING, "No codec could be found with name '%s'\n", forced_codec_name);
      else
        av_log (NULL, AV_LOG_WARNING, "No decoder could be found for codec %s\n", avcodec_get_name (avctx->codec_id));
      ret = AVERROR(EINVAL);
      goto fail;
      }
      //}}}

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
      av_log (avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n", codec->max_lowres);
      stream_lowres = codec->max_lowres;
      }
    avctx->lowres = stream_lowres;

    if (fast)
      avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    opts = filter_codec_opts (codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get (opts, "threads", NULL, 0))
      av_dict_set (&opts, "threads", "auto", 0);
    if (stream_lowres)
      av_dict_set_int (&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
      av_dict_set (&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2 (avctx, codec, &opts)) < 0)
      goto fail;

    if ((t = av_dict_get (opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
      av_log (NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
      ret = AVERROR_OPTION_NOT_FOUND;
      goto fail;
      }

    videoState->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;

    int sample_rate, nb_channels;
    int64_t channel_layout;
    switch (avctx->codec_type) {
      //{{{
      case AVMEDIA_TYPE_AUDIO:
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;

        /* prepare audio output */
        if ((ret = audio_open (videoState, channel_layout, nb_channels, sample_rate, &videoState->audio_tgt)) < 0)
          goto fail;

        videoState->audio_hw_buf_size = ret;
        videoState->audio_src = videoState->audio_tgt;
        videoState->audio_buf_size  = 0;
        videoState->audio_buf_index = 0;

        /* init averaging filter */
        videoState->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        videoState->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        videoState->audio_diff_threshold = (double)(videoState->audio_hw_buf_size) / videoState->audio_tgt.bytes_per_sec;

        videoState->audio_stream = stream_index;
        videoState->audio_st = ic->streams[stream_index];

        decoder_init(&videoState->auddec, avctx, &videoState->audioq, videoState->continue_read_thread);
        if ((videoState->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !videoState->ic->iformat->read_seek) {
          videoState->auddec.start_pts = videoState->audio_st->start_time;
          videoState->auddec.start_pts_tb = videoState->audio_st->time_base;
          }
        if ((ret = decoder_start (&videoState->auddec, audio_thread, "audio_decoder", videoState)) < 0)
          goto out;
        SDL_PauseAudioDevice (audio_dev, 0);
        break;
      //}}}
      //{{{
      case AVMEDIA_TYPE_VIDEO:
        videoState->video_stream = stream_index;
        videoState->video_st = ic->streams[stream_index];

        decoder_init (&videoState->viddec, avctx, &videoState->videoq, videoState->continue_read_thread);
        if ((ret = decoder_start (&videoState->viddec, video_thread, "video_decoder", videoState)) < 0)
          goto out;

        videoState->queue_attachments_req = 1;
        break;
      //}}}
      //{{{
      case AVMEDIA_TYPE_SUBTITLE:
        videoState->subtitle_stream = stream_index;
        videoState->subtitle_st = ic->streams[stream_index];

        decoder_init(&videoState->subdec, avctx, &videoState->subtitleq, videoState->continue_read_thread);
        if ((ret = decoder_start (&videoState->subdec, subtitle_thread, "subtitle_decoder", videoState)) < 0)
          goto out;

        break;
      //}}}
      default:
        break;
      }
    goto out;

  fail:
    avcodec_free_context (&avctx);

  out:
    av_dict_free (&opts);

    return ret;
    }
  //}}}
  //{{{
  int decode_interrupt_cb (void* opaque) {

    auto videoState = (sVideoState*)opaque;
    return videoState->abort_request;
    }
  //}}}
  //{{{
  int stream_has_enough_packets (AVStream* st, int stream_id, sPacketQueue* queue) {

    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           (queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d (st->time_base) * queue->duration > 1.0));
    }
  //}}}
  //{{{
  int is_realtime (AVFormatContext* s) {

    if (!strcmp (s->iformat->name, "rtp") ||
        !strcmp (s->iformat->name, "rtsp") ||
        !strcmp (s->iformat->name, "sdp"))
      return 1;

    if (s->pb && (!strncmp (s->url, "rtp:", 4) || !strncmp(s->url, "udp:", 4)))
      return 1;

    return 0;
    }
  //}}}
  //{{{
  int read_thread (void* arg) {
  // this thread gets the stream from the disk or the network

    auto videoState = (sVideoState*)arg;
    AVFormatContext* ic = NULL;

    int err, i, ret;

    AVPacket pkt1;
    AVPacket* pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;

    AVDictionaryEntry* t;

    int scan_all_pmts_set = 0;
    int64_t pkt_ts;

    SDL_mutex* wait_mutex = SDL_CreateMutex();
    if (!wait_mutex) {
      //{{{  error fail
      av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
      ret = AVERROR(ENOMEM);
      goto fail;
      }
      //}}}

    int st_index[AVMEDIA_TYPE_NB];
    memset (st_index, -1, sizeof(st_index));
    videoState->eof = 0;

    ic = avformat_alloc_context();
    if (!ic) {
      //{{{  error fail
      av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
      ret = AVERROR(ENOMEM);
      goto fail;
      }
      //}}}

    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = videoState;
    if (!av_dict_get (format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set (&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
      }

    err = avformat_open_input (&ic, videoState->filename, videoState->iformat, &format_opts);
    if (err < 0) {
      //{{{  error fail
      print_error (videoState->filename, err);
      ret = -1;
      goto fail;
      }
      //}}}
    if (scan_all_pmts_set)
      av_dict_set (&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    if ((t = av_dict_get (format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
      //{{{  error fail
      av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
      ret = AVERROR_OPTION_NOT_FOUND;
      goto fail;
      }
      //}}}
    videoState->ic = ic;

    if (genpts)
      ic->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data (ic);

    if (find_stream_info) {
      AVDictionary** opts = setup_find_stream_info_opts (ic, codec_opts);
      int orig_nb_streams = ic->nb_streams;
      err = avformat_find_stream_info (ic, opts);
      for (i = 0; i < orig_nb_streams; i++)
        av_dict_free (&opts[i]);
      av_freep (&opts);
      if (err < 0) {
        //{{{  error fail
        av_log(NULL, AV_LOG_WARNING, "%s: could not find codec parameters\n", videoState->filename);
        ret = -1;
        goto fail;
        }
        //}}}
      }

    if (ic->pb)
      ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (seek_by_bytes < 0)
      seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

    videoState->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    // if seeking requested, we execute it
    if (start_time != AV_NOPTS_VALUE) {
      int64_t timestamp = start_time;
      // add the stream start time
      if (ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;
      ret = avformat_seek_file (ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
      if (ret < 0)
        av_log (NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n", videoState->filename, (double)timestamp / AV_TIME_BASE);
      }

    videoState->realtime = is_realtime (ic);
    if (show_status)
      av_dump_format (ic, 0, videoState->filename, 0);

    for (i = 0; i < (int)ic->nb_streams; i++) {
      AVStream *st = ic->streams[i];
      enum AVMediaType type = st->codecpar->codec_type;
      st->discard = AVDISCARD_ALL;
      if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
        if (avformat_match_stream_specifier (ic, st, wanted_stream_spec[type]) > 0)
          st_index[type] = i;
      }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
      if (wanted_stream_spec[i] && st_index[i] == -1) {
        av_log (NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n",
                                    wanted_stream_spec[i], av_get_media_type_string (AVMediaType(i)));
        st_index[i] = INT_MAX;
        }
      }

    if (!video_disable)
      st_index[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream (ic, AVMEDIA_TYPE_VIDEO, st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);

    if (!audio_disable)
      st_index[AVMEDIA_TYPE_AUDIO] =
        av_find_best_stream (ic, AVMEDIA_TYPE_AUDIO, st_index[AVMEDIA_TYPE_AUDIO], st_index[AVMEDIA_TYPE_VIDEO],
                             NULL, 0);

    if (!video_disable && !subtitle_disable)
      st_index[AVMEDIA_TYPE_SUBTITLE] =
        av_find_best_stream (ic, AVMEDIA_TYPE_SUBTITLE, st_index[AVMEDIA_TYPE_SUBTITLE],
                             (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ? st_index[AVMEDIA_TYPE_AUDIO] : st_index[AVMEDIA_TYPE_VIDEO]),
                             NULL, 0);

    videoState->show_mode = show_mode;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
      //AVStream* st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
      //AVCodecParameters* codecpar = st->codecpar;
      //AVRational sar = av_guess_sample_aspect_ratio (ic, st, NULL);
      //if (codecpar->width)
      //  set_default_window_size (codecpar->width, codecpar->height, sar);
      }

    // open the streams
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0)
      stream_component_open (videoState, st_index[AVMEDIA_TYPE_AUDIO]);

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
      ret = stream_component_open (videoState, st_index[AVMEDIA_TYPE_VIDEO]);
    if (videoState->show_mode == SHOW_MODE_NONE)
      videoState->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;
    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0)
      stream_component_open (videoState, st_index[AVMEDIA_TYPE_SUBTITLE]);

    if (videoState->video_stream < 0 && videoState->audio_stream < 0) {
      //{{{  error fail
      av_log (NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n", videoState->filename);
      ret = -1;
      goto fail;
      }
      //}}}

    if (infinite_buffer < 0 && videoState->realtime)
      infinite_buffer = 1;

    for (;;) {
      //{{{  loop
      if (videoState->abort_request)
        break;

      if (videoState->paused != videoState->last_paused) {
        videoState->last_paused = videoState->paused;
        if (videoState->paused)
          videoState->read_pause_return = av_read_pause(ic);
        else
          av_read_play(ic);
        }

      #if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (videoState->paused &&
            (!strcmp(ic->iformat->name, "rtsp") ||
            (ic->pb && !strncmp(input_filename, "mmsh:", 5)))) {
          /* wait 10 ms to avoid trying to get another packet */
          /* XXX: horrible */
          SDL_Delay(10);
          continue;
          }
      #endif

      if (videoState->seek_req) {
        int64_t seek_target = videoState->seek_pos;
        int64_t seek_min    = videoState->seek_rel > 0 ? seek_target - videoState->seek_rel + 2: INT64_MIN;
        int64_t seek_max    = videoState->seek_rel < 0 ? seek_target - videoState->seek_rel - 2: INT64_MAX;
        // FIXME the +-2 is due to rounding being not done in the correct direction in generation
        //      of the seek_pos/seek_rel variables

        ret = avformat_seek_file(videoState->ic, -1, seek_min, seek_target, seek_max, videoState->seek_flags);
        if (ret < 0)
          av_log(NULL, AV_LOG_ERROR, "%s: error while seeking\n", videoState->ic->url);
        else {
          if (videoState->audio_stream >= 0) {
            packet_queue_flush(&videoState->audioq);
            packet_queue_put(&videoState->audioq, &flush_pkt);
            }
          if (videoState->subtitle_stream >= 0) {
            packet_queue_flush(&videoState->subtitleq);
            packet_queue_put(&videoState->subtitleq, &flush_pkt);
            }
          if (videoState->video_stream >= 0) {
            packet_queue_flush(&videoState->videoq);
            packet_queue_put(&videoState->videoq, &flush_pkt);
            }
          if (videoState->seek_flags & AVSEEK_FLAG_BYTE)
            set_clock(&videoState->extclk, NAN, 0);
          else
            set_clock(&videoState->extclk, seek_target / (double)AV_TIME_BASE, 0);
          }

        videoState->seek_req = 0;
        videoState->queue_attachments_req = 1;
        videoState->eof = 0;
        if (videoState->paused)
          step_to_next_frame (videoState);
        }

      if (videoState->queue_attachments_req) {
        if (videoState->video_st && videoState->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
          AVPacket copy;
          if ((ret = av_packet_ref(&copy, &videoState->video_st->attached_pic)) < 0)
            goto fail;
          packet_queue_put(&videoState->videoq, &copy);
          packet_queue_put_nullpacket(&videoState->videoq, videoState->video_stream);
          }
        videoState->queue_attachments_req = 0;
        }

      // if the queue are full, no need to read more
      if (infinite_buffer<1 &&
          (videoState->audioq.size + videoState->videoq.size + videoState->subtitleq.size > MAX_QUEUE_SIZE
          || (stream_has_enough_packets(videoState->audio_st, videoState->audio_stream, &videoState->audioq) &&
          stream_has_enough_packets(videoState->video_st, videoState->video_stream, &videoState->videoq) &&
          stream_has_enough_packets(videoState->subtitle_st, videoState->subtitle_stream, &videoState->subtitleq)))) {
        // wait 10 ms
        SDL_LockMutex (wait_mutex);
        SDL_CondWaitTimeout (videoState->continue_read_thread, wait_mutex, 10);
        SDL_UnlockMutex (wait_mutex);
        continue;
        }

      ret = av_read_frame (ic, pkt);
      if (ret < 0) {
        if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !videoState->eof) {
          if (videoState->video_stream >= 0)
            packet_queue_put_nullpacket(&videoState->videoq, videoState->video_stream);
          if (videoState->audio_stream >= 0)
            packet_queue_put_nullpacket(&videoState->audioq, videoState->audio_stream);
          if (videoState->subtitle_stream >= 0)
            packet_queue_put_nullpacket(&videoState->subtitleq, videoState->subtitle_stream);
          videoState->eof = 1;
          }
        if (ic->pb && ic->pb->error)
          break;
        SDL_LockMutex (wait_mutex);
        SDL_CondWaitTimeout (videoState->continue_read_thread, wait_mutex, 10);
        SDL_UnlockMutex (wait_mutex);
        continue;
        }
      else
        videoState->eof = 0;

      // check if packet is in play range specified by user, then queue, otherwise discard
      stream_start_time = ic->streams[pkt->stream_index]->start_time;
      pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
      pkt_in_play_range = (duration == AV_NOPTS_VALUE) ||
                          (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                           av_q2d (ic->streams[pkt->stream_index]->time_base) -
                            (double)(start_time != AV_NOPTS_VALUE ? start_time : 0) / 1000000
                            <= ((double)duration / 1000000);
      if (pkt->stream_index == videoState->audio_stream && pkt_in_play_range)
        packet_queue_put(&videoState->audioq, pkt);
      else if (pkt->stream_index == videoState->video_stream && pkt_in_play_range
               && !(videoState->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))
        packet_queue_put (&videoState->videoq, pkt);
      else if (pkt->stream_index == videoState->subtitle_stream && pkt_in_play_range)
        packet_queue_put (&videoState->subtitleq, pkt);
      else
        av_packet_unref (pkt);
      }
      //}}}

    ret = 0;

  fail:
    if (ic && !videoState->ic)
      avformat_close_input (&ic);

    if (ret != 0) {
      SDL_Event event;
      event.type = FF_QUIT_EVENT;
      event.user.data1 = videoState;
      SDL_PushEvent (&event);
      }

    SDL_DestroyMutex (wait_mutex);
    return 0;
    }
  //}}}

  //{{{
  void fill_rectangle (int x, int y, int w, int h) {

    if (w && h) {
      SDL_Rect rect = { x, y, w, h};
      SDL_RenderFillRect (renderer, &rect);
      }
    }
  //}}}
  //{{{
  void set_sdl_yuv_conversion_mode (AVFrame* frame) {

    SDL_YUV_CONVERSION_MODE mode = SDL_YUV_CONVERSION_AUTOMATIC;

    if (frame &&
        (frame->format == AV_PIX_FMT_YUV420P ||
         frame->format == AV_PIX_FMT_YUYV422 ||
         frame->format == AV_PIX_FMT_UYVY422)) {

      if (frame->color_range == AVCOL_RANGE_JPEG)
        mode = SDL_YUV_CONVERSION_JPEG;

      else if (frame->colorspace == AVCOL_SPC_BT709)
        mode = SDL_YUV_CONVERSION_BT709;

      else if (frame->colorspace == AVCOL_SPC_BT470BG ||
               frame->colorspace == AVCOL_SPC_SMPTE170M ||
               frame->colorspace == AVCOL_SPC_SMPTE240M)
        mode = SDL_YUV_CONVERSION_BT601;
      }

    SDL_SetYUVConversionMode (mode);
    }
  //}}}
  //{{{
  void get_sdl_pix_fmt_and_blendmode (int format, Uint32* sdl_pix_fmt, SDL_BlendMode* sdl_blendmode) {

    *sdl_blendmode = SDL_BLENDMODE_NONE;
    if (format == AV_PIX_FMT_RGB32   ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32   ||
        format == AV_PIX_FMT_BGR32_1)
      *sdl_blendmode = SDL_BLENDMODE_BLEND;

    *sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
    for (int i = 0; i < (int)FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++) {
      if (format == sdl_texture_format_map[i].format) {
        *sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
        return;
        }
      }
    }
  //}}}
  //{{{
  int realloc_texture (SDL_Texture** texture, Uint32 new_format,
                              int new_width, int new_height, SDL_BlendMode blendmode, int init_texture) {

    Uint32 format;
    int access, w, h;
    if (!*texture ||
        SDL_QueryTexture (*texture, &format, &access, &w, &h) < 0 ||
        new_width != w || new_height != h || new_format != format) {
      if (*texture)
        SDL_DestroyTexture (*texture);

      if (!(*texture = SDL_CreateTexture (renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
        return -1;

      if (SDL_SetTextureBlendMode (*texture, blendmode) < 0)
        return -1;

      if (init_texture) {
        void* pixels;
        int pitch;
        if (SDL_LockTexture (*texture, NULL, &pixels, &pitch) < 0)
          return -1;

        memset (pixels, 0, pitch * new_height);
        SDL_UnlockTexture (*texture);
        }

      av_log (NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n",
                                    new_width, new_height, SDL_GetPixelFormatName(new_format));
      }

    return 0;
    }
  //}}}
  //{{{
  int upload_texture (SDL_Texture** texture, AVFrame* frame, struct SwsContext** img_convert_ctx) {

    int ret = 0;

    Uint32 sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    get_sdl_pix_fmt_and_blendmode (frame->format, &sdl_pix_fmt, &sdl_blendmode);

    if (realloc_texture (texture,
                         sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt,
                         frame->width, frame->height,
                         sdl_blendmode, 0) < 0)
      return -1;

    switch (sdl_pix_fmt) {
      //{{{
      case SDL_PIXELFORMAT_UNKNOWN: // This should only happen if we are not using avfilter
        *img_convert_ctx = sws_getCachedContext (*img_convert_ctx,
                                                 frame->width, frame->height,
                                                 (AVPixelFormat)frame->format,
                                                 frame->width, frame->height,
                                                 AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
        if (*img_convert_ctx != NULL) {
          uint8_t *pixels[4];
          int pitch[4];
          if (!SDL_LockTexture (*texture, NULL, (void **)pixels, pitch)) {
            sws_scale (*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
                       0, frame->height, pixels, pitch);
            SDL_UnlockTexture (*texture);
            }
          }

        else {
          av_log (NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
          ret = -1;
          }

        break;
      //}}}
      //{{{
      case SDL_PIXELFORMAT_IYUV:
        if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0)
          ret = SDL_UpdateYUVTexture (*texture, NULL, frame->data[0], frame->linesize[0],
                                      frame->data[1], frame->linesize[1],
                                      frame->data[2], frame->linesize[2]);

        else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0)
          ret = SDL_UpdateYUVTexture (*texture, NULL, frame->data[0] + frame->linesize[0] * (frame->height                    - 1), -frame->linesize[0],
                                      frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                                      frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);

        else {
          av_log (NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
          return -1;
          }

        break;
      //}}}
      //{{{
      default:
        if (frame->linesize[0] < 0)
          ret = SDL_UpdateTexture (*texture, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
        else
          ret = SDL_UpdateTexture (*texture, NULL, frame->data[0], frame->linesize[0]);
        break;
      //}}}
      }

    return ret;
    }
  //}}}
  //{{{
  void video_audio_display (sVideoState* videoState) {

    int rdft_bits;
    for (rdft_bits = 1; (1 << rdft_bits) < 2 * videoState->height; rdft_bits++) {}
    int nb_freq = 1 << (rdft_bits - 1);

    // compute display index : center on currently output samples
    int i_start;
    int channels = videoState->audio_tgt.channels;
    int nb_display_channels = channels;
    if (!videoState->paused) {
      int data_used = videoState->show_mode == SHOW_MODE_WAVES ? videoState->width : (2*nb_freq);
      int n = 2 * channels;
      int delay = videoState->audio_write_buf_size;
      delay /= n;

      // to be more precise, we take into account the time spent since last buffer computation
      if (audio_callback_time) {
        int64_t time_diff = av_gettime_relative() - audio_callback_time;
        delay -= int((time_diff * videoState->audio_tgt.freq) / 1000000);
        }

      delay += 2 * data_used;
      if (delay < data_used)
        delay = data_used;

      int x = compute_mod (videoState->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
      i_start = x;
      if (videoState->show_mode == SHOW_MODE_WAVES) {
        int h = INT_MIN;
        for (int i = 0; i < 1000; i += channels) {
          int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
          int a = videoState->sample_array[idx];
          int b = videoState->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
          int c = videoState->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
          int d = videoState->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
          int score = a - d;
          if (h < score && (b ^ c) < 0) {
            h = score;
            i_start = idx;
            }
          }
        }
      videoState->last_i_start = i_start;
      }
    else
      i_start = videoState->last_i_start;

    if (videoState->show_mode == SHOW_MODE_WAVES) {
      //{{{  show waves
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

      // total height for one channel
      int h = videoState->height / nb_display_channels;

      // graph height / 2
      int h2 = (h * 9) / 20;
      for (int ch = 0; ch < nb_display_channels; ch++) {
        int i = i_start + ch;
        int y1 = videoState->ytop + ch * h + (h / 2); /* position of center line */
        int ys;
        for (int x = 0; x < videoState->width; x++) {
          int y = (videoState->sample_array[i] * h2) >> 15;
          if (y < 0) {
            y = -y;
            ys = y1 - y;
            }
          else
            ys = y1;

          fill_rectangle (videoState->xleft + x, ys, 1, y);
          i += channels;
          if (i >= SAMPLE_ARRAY_SIZE)
            i -= SAMPLE_ARRAY_SIZE;
          }
        }

      SDL_SetRenderDrawColor (renderer, 0, 0, 255, 255);

      for (int ch = 1; ch < nb_display_channels; ch++)
        fill_rectangle (videoState->xleft, videoState->ytop + ch * h, videoState->width, 1);
      }
      //}}}
    else {
      //{{{  show spect
      if (realloc_texture (&videoState->vis_texture, SDL_PIXELFORMAT_ARGB8888, videoState->width, videoState->height, SDL_BLENDMODE_NONE, 1) < 0)
        return;

      nb_display_channels= FFMIN(nb_display_channels, 2);
      if (rdft_bits != videoState->rdft_bits) {
        av_rdft_end(videoState->rdft);
        av_free(videoState->rdft_data);
        videoState->rdft = av_rdft_init (rdft_bits, DFT_R2C);
        videoState->rdft_bits = rdft_bits;
        videoState->rdft_data = (FFTSample*)av_malloc_array (nb_freq, 4 *sizeof(*videoState->rdft_data));
        }

      if (!videoState->rdft || !videoState->rdft_data){
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
        videoState->show_mode = SHOW_MODE_WAVES;
        }

      else {
        FFTSample *data[2];
        SDL_Rect rect = {.x = videoState->xpos, .y = 0, .w = 1, .h = videoState->height};
        uint32_t* pixels;
        int pitch;
        for (int ch = 0; ch < nb_display_channels; ch++) {
          data[ch] = videoState->rdft_data + 2 * nb_freq * ch;
          int i = i_start + ch;
          for (int x = 0; x < 2 * nb_freq; x++) {
            double w = (x-nb_freq) * (1.0 / nb_freq);
            data[ch][x] = (FFTSample)(videoState->sample_array[i] * (1.0f - w * w));
            i += channels;
            if (i >= SAMPLE_ARRAY_SIZE)
              i -= SAMPLE_ARRAY_SIZE;
            }
          av_rdft_calc(videoState->rdft, data[ch]);
          }

        // Least efficient way to do this, we should of course
        // directly access it but it is more than fast enough
        if (!SDL_LockTexture (videoState->vis_texture, &rect, (void **)&pixels, &pitch)) {
          pitch >>= 2;
          pixels += pitch * videoState->height;
          for (int y = 0; y < videoState->height; y++) {
            double w = 1 / sqrt(nb_freq);
            int a = (int)(sqrt (w * sqrt (data[0][2 * y + 0] * data[0][2 * y + 0] + data[0][2 * y + 1] * data[0][2 * y + 1])));
            int b = (nb_display_channels == 2 ) ? (int)(sqrt (w * hypot(data[1][2 * y + 0], data[1][2 * y + 1]))) : a;
            a = FFMIN (a, 255);
            b = FFMIN (b, 255);
            pixels -= pitch;
            *pixels = (a << 16) + (b << 8) + ((a+b) >> 1);
            }
          SDL_UnlockTexture (videoState->vis_texture);
          }

        SDL_RenderCopy (renderer, videoState->vis_texture, NULL, NULL);
        }

      if (!videoState->paused)
        videoState->xpos++;
      if (videoState->xpos >= videoState->width)
        videoState->xpos= videoState->xleft;
      }
      //}}}
    }
  //}}}
  //{{{
  void video_image_display (sVideoState* videoState) {

    sFrame* vp = frame_queue_peek_last (&videoState->pictq);

    sFrame* sp = NULL;
    if (videoState->subtitle_st) {
      //{{{  make subtitle texture
      if (frame_queue_nb_remaining (&videoState->subpq) > 0) {
        sp = frame_queue_peek (&videoState->subpq);

        if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
          if (!sp->uploaded) {
            if (!sp->width || !sp->height) {
              sp->width = vp->width;
              sp->height = vp->height;
              }

            if (realloc_texture (&videoState->sub_texture, SDL_PIXELFORMAT_ARGB8888,
                                 sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
              return;

            for (int i = 0; i < (int)sp->sub.num_rects; i++) {
              AVSubtitleRect* subRect = sp->sub.rects[i];
              subRect->x = av_clip (subRect->x, 0, sp->width );
              subRect->y = av_clip (subRect->y, 0, sp->height);
              subRect->w = av_clip (subRect->w, 0, sp->width  - subRect->x);
              subRect->h = av_clip (subRect->h, 0, sp->height - subRect->y);

              videoState->sub_convert_ctx = sws_getCachedContext (videoState->sub_convert_ctx,
                                                                  subRect->w, subRect->h, AV_PIX_FMT_PAL8,
                                                                  subRect->w, subRect->h, AV_PIX_FMT_BGRA,
                                                                  0, NULL, NULL, NULL);
              if (!videoState->sub_convert_ctx) {
                //{{{  error return
                av_log (NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                return;
                }
                //}}}

              uint8_t* pixels[4];
              int pitch[4];
              if (!SDL_LockTexture (videoState->sub_texture, (SDL_Rect*)subRect, (void**)pixels, pitch)) {
                sws_scale (videoState->sub_convert_ctx, (const uint8_t* const*)subRect->data, subRect->linesize,
                           0, subRect->h, pixels, pitch);
                SDL_UnlockTexture (videoState->sub_texture);
                }
              }
            sp->uploaded = 1;
            }
          }
        else
          sp = NULL;
        }
      }
      //}}}

    SDL_Rect rect;
    calculate_display_rect (&rect, videoState->xleft, videoState->ytop, videoState->width, videoState->height, vp->width, vp->height, vp->sar);

    if (!vp->uploaded) {
      if (upload_texture (&videoState->vid_texture, vp->frame, &videoState->img_convert_ctx) < 0)
        return;
      vp->uploaded = 1;
      vp->flip_v = vp->frame->linesize[0] < 0;
      }

    set_sdl_yuv_conversion_mode (vp->frame);
    SDL_RenderCopyEx (renderer, videoState->vid_texture, NULL, &rect, 0, NULL,
                      SDL_RendererFlip (vp->flip_v ? SDL_FLIP_VERTICAL : 0));
    set_sdl_yuv_conversion_mode (NULL);

    if (sp)
      SDL_RenderCopy (renderer, videoState->sub_texture, NULL, &rect);
    }
  //}}}
  //{{{
  void video_display (sVideoState* videoState) {
  // display the current picture, if any

    if (!videoState->width)
      video_open (videoState);

    SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
    SDL_RenderClear (renderer);

    if (videoState->audio_st && (videoState->show_mode != SHOW_MODE_VIDEO))
      video_audio_display (videoState);
    else if (videoState->video_st)
      video_image_display (videoState);

    SDL_RenderPresent (renderer);
    }
  //}}}

  //{{{
  double vp_duration (sVideoState* videoState, sFrame* vp, sFrame* nextvp) {

    if (vp->serial == nextvp->serial) {
      double duration = nextvp->pts - vp->pts;
      if (isnan (duration) || duration <= 0 || duration > videoState->max_frame_duration)
        return vp->duration;
      else
        return duration;
      }
    else
      return 0.0;
    }
  //}}}
  //{{{
  double compute_target_delay (double delay, sVideoState* videoState) {

    // update delay to follow master synchronisation source
    double diff = 0;
    if (get_master_sync_type (videoState) != AV_SYNC_VIDEO_MASTER) {
      // if video is slave, we try to correct big delays by duplicating or deleting a frame
      diff = get_clock(&videoState->vidclk) - get_master_clock (videoState);

      // skip or repeat frame. We take into account the
      // delay to compute the threshold. I still don't know if it is the best guess
      double sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
      if (!isnan(diff) && fabs(diff) < videoState->max_frame_duration) {
        if (diff <= -sync_threshold)
          delay = FFMAX(0, delay + diff);
        else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
          delay = delay + diff;
        else if (diff >= sync_threshold)
          delay = 2 * delay;
        }
      }

    av_log (NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);

    return delay;
    }
  //}}}
  //{{{
  void update_video_pts (sVideoState* videoState, double pts, int64_t pos, int serial) {
  // update current video pts

    set_clock (&videoState->vidclk, pts, serial);
    sync_clock_to_slave (&videoState->extclk, &videoState->vidclk);
    }
  //}}}
  //{{{
  void video_refresh (sVideoState* videoState, double* remaining_time) {

    if (!videoState->paused &&
        ((get_master_sync_type (videoState) == AV_SYNC_EXTERNAL_CLOCK) && videoState->realtime))
      check_external_clock_speed (videoState);

    if (!display_disable && videoState->show_mode != SHOW_MODE_VIDEO && videoState->audio_st) {
      double time = av_gettime_relative() / 1000000.0;
      if (videoState->force_refresh || (videoState->last_vis_time + rdftspeed < time)) {
        video_display (videoState);
        videoState->last_vis_time = time;
        }
      *remaining_time = FFMIN(*remaining_time, videoState->last_vis_time + rdftspeed - time);
      }

    if (videoState->video_st) {
  retry:
      if (frame_queue_nb_remaining (&videoState->pictq) == 0) {
        // no picture to display in queue
        }
      else {
        //{{{  dequeue the picture
        sFrame* lastvp = frame_queue_peek_last(&videoState->pictq);
        sFrame* vp = frame_queue_peek(&videoState->pictq);
        if (vp->serial != videoState->videoq.serial) {
          frame_queue_next (&videoState->pictq);
          goto retry;
          }

        if (lastvp->serial != vp->serial)
          videoState->frame_timer = av_gettime_relative() / 1000000.0;

        if (videoState->paused)
          goto display;

        // compute nominal last_duration
        double last_duration = vp_duration (videoState, lastvp, vp);
        double delay = compute_target_delay (last_duration, videoState);

        double time = av_gettime_relative()/1000000.0;
        if (time < videoState->frame_timer + delay) {
          *remaining_time = FFMIN(videoState->frame_timer + delay - time, *remaining_time);
          goto display;
          }

        videoState->frame_timer += delay;
        if (delay > 0 && time - videoState->frame_timer > AV_SYNC_THRESHOLD_MAX)
          videoState->frame_timer = time;

        SDL_LockMutex (videoState->pictq.mutex);
        if (!isnan (vp->pts))
          update_video_pts (videoState, vp->pts, vp->pos, vp->serial);
        SDL_UnlockMutex (videoState->pictq.mutex);

        if (frame_queue_nb_remaining (&videoState->pictq) > 1) {
          sFrame *nextvp = frame_queue_peek_next (&videoState->pictq);
          double duration = vp_duration (videoState, vp, nextvp);
          if (!videoState->step &&
              (framedrop > 0 || (framedrop && get_master_sync_type (videoState) != AV_SYNC_VIDEO_MASTER)) &&
              time > videoState->frame_timer + duration) {
            videoState->frame_drops_late++;
            frame_queue_next (&videoState->pictq);
            goto retry;
            }
          }

        if (videoState->subtitle_st) {
          //{{{  subtitle
          while (frame_queue_nb_remaining (&videoState->subpq) > 0) {
            sFrame* sp = frame_queue_peek (&videoState->subpq);
            sFrame* sp2;
            if (frame_queue_nb_remaining (&videoState->subpq) > 1)
              sp2 = frame_queue_peek_next (&videoState->subpq);
            else
              sp2 = NULL;

            if (sp->serial != videoState->subtitleq.serial
                || (videoState->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                || (sp2 && videoState->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000)))) {
              if (sp->uploaded) {
                int i;
                for (i = 0; i < (int)sp->sub.num_rects; i++) {
                  AVSubtitleRect *sub_rect = sp->sub.rects[i];
                  uint8_t *pixels;
                  int pitch, j;

                  if (!SDL_LockTexture(videoState->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                       memset(pixels, 0, sub_rect->w << 2);
                    SDL_UnlockTexture(videoState->sub_texture);
                    }
                  }
                }
              frame_queue_next(&videoState->subpq);
              }
            else {
              break;
              }
            }
          }
          //}}}

        frame_queue_next(&videoState->pictq);
        videoState->force_refresh = 1;

        if (videoState->step && !videoState->paused)
          stream_toggle_pause (videoState);
        }
        //}}}
  display:
      // display picture
      if (!display_disable && videoState->force_refresh && videoState->show_mode == SHOW_MODE_VIDEO && videoState->pictq.rindex_shown)
        video_display (videoState);
      }

    videoState->force_refresh = 0;
    if (show_status) {
      //{{{  show status
      AVBPrint buf;
      int64_t cur_time;
      int aqsize, vqsize, sqsize;
      double av_diff;

      cur_time = av_gettime_relative();
      if (!last_time || (cur_time - last_time) >= 30000) {
        aqsize = 0;
        vqsize = 0;
        sqsize = 0;
        if (videoState->audio_st)
          aqsize = videoState->audioq.size;
        if (videoState->video_st)
          vqsize = videoState->videoq.size;
        if (videoState->subtitle_st)
          sqsize = videoState->subtitleq.size;

        av_diff = 0;
        if (videoState->audio_st && videoState->video_st)
          av_diff = get_clock (&videoState->audclk) - get_clock (&videoState->vidclk);
        else if (videoState->video_st)
          av_diff = get_master_clock (videoState) - get_clock (&videoState->vidclk);
        else if (videoState->audio_st)
          av_diff = get_master_clock (videoState) - get_clock (&videoState->audclk);

        av_bprint_init (&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
        av_bprintf (&buf,
                    "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%" PRId64"/%" PRId64"   \r",
                    get_master_clock (videoState),
                    (videoState->audio_st && videoState->video_st) ? "A-V" : (videoState->video_st ? "M-V" : (videoState->audio_st ? "M-A" : "   ")),
                    av_diff,
                    videoState->frame_drops_early + videoState->frame_drops_late,
                    aqsize / 1024,
                    vqsize / 1024,
                    sqsize,
                    videoState->video_st ? videoState->viddec.avctx->pts_correction_num_faulty_dts : 0,
                    videoState->video_st ? videoState->viddec.avctx->pts_correction_num_faulty_pts : 0);

        if (show_status == 1 && AV_LOG_INFO > av_log_get_level())
          fprintf (stderr, "%s", buf.str);
        else
          av_log (NULL, AV_LOG_INFO, "%s", buf.str);

        fflush (stderr);
        av_bprint_finalize (&buf, NULL);

        last_time = cur_time;
        }
      }
      //}}}
    }
  //}}}

  // key actions
  //{{{
  void stream_cycle_channel (sVideoState* videoState, int codec_type) {

    AVFormatContext* ic = videoState->ic;

    int start_index;
    int old_index;
    if (codec_type == AVMEDIA_TYPE_VIDEO) {
      start_index = videoState->last_video_stream;
      old_index = videoState->video_stream;
      }
    else if (codec_type == AVMEDIA_TYPE_AUDIO) {
      start_index = videoState->last_audio_stream;
      old_index = videoState->audio_stream;
      }
    else {
      start_index = videoState->last_subtitle_stream;
      old_index = videoState->subtitle_stream;
      }
    int stream_index = start_index;

    AVProgram* p = NULL;
    int nb_streams = videoState->ic->nb_streams;
    if (codec_type != AVMEDIA_TYPE_VIDEO && videoState->video_stream != -1) {
      //{{{  video
      p = av_find_program_from_stream (ic, NULL, videoState->video_stream);
      if (p) {
        nb_streams = p->nb_stream_indexes;
        for (start_index = 0; start_index < nb_streams; start_index++)
          if ((int)p->stream_index[start_index] == stream_index)
            break;
        if (start_index == nb_streams)
          start_index = -1;
        stream_index = start_index;
        }
      }
      //}}}

    for (;;) {
      if (++stream_index >= nb_streams) {
        if (codec_type == AVMEDIA_TYPE_SUBTITLE) {
          //{{{  subtitle
          stream_index = -1;
          videoState->last_subtitle_stream = -1;
          goto the_end;
          }
          //}}}
        if (start_index == -1)
          return;
        stream_index = 0;
        }
      if (stream_index == start_index)
        return;

      AVStream* st = videoState->ic->streams[p ? p->stream_index[stream_index] : stream_index];
      if (st->codecpar->codec_type == codec_type) {
        // check that parameters are OK
        switch (codec_type) {
          case AVMEDIA_TYPE_AUDIO:
            //{{{  audio
            if (st->codecpar->sample_rate != 0 &&
                st->codecpar->channels != 0)
              goto the_end;
            break;
            //}}}
          case AVMEDIA_TYPE_VIDEO:
          case AVMEDIA_TYPE_SUBTITLE:
             goto the_end;
          default:
            break;
          }
        }
      }
  the_end:
    if (p && stream_index != -1)
      stream_index = p->stream_index[stream_index];
    av_log (NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
            av_get_media_type_string ((AVMediaType)codec_type), old_index, stream_index);

    stream_component_close (videoState, old_index);
    stream_component_open (videoState, stream_index);
    }
  //}}}
  //{{{
  void toggle_pause (sVideoState* videoState) {

    stream_toggle_pause (videoState);
    videoState->step = 0;
    }
  //}}}
  //{{{
  void toggle_mute (sVideoState* videoState) {
    videoState->muted = !videoState->muted;
    }
  //}}}
  //{{{
  void update_volume (sVideoState* videoState, int sign, double step) {

    double volume_level = videoState->audio_volume ?
      (20 * log(videoState->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint (SDL_MIX_MAXVOLUME * pow (10.0, (volume_level + sign * step) / 20.0));

    videoState->audio_volume =
      av_clip (videoState->audio_volume == new_volume ? (videoState->audio_volume + sign) : new_volume,
               0, SDL_MIX_MAXVOLUME);
    }
  //}}}
  //{{{
  void toggle_audio_display (sVideoState* videoState) {

    int next = videoState->show_mode;
    do {
      next = (next + 1) % SHOW_MODE_NB;
      } while ((next != videoState->show_mode) &&
               (next == SHOW_MODE_VIDEO) &&
               (!videoState->video_st || (next != SHOW_MODE_VIDEO && !videoState->audio_st)));

    if (videoState->show_mode != next) {
      videoState->force_refresh = 1;
      videoState->show_mode = (ShowMode)next;
      }
    }
  //}}}
  //{{{
  void toggle_full_screen (sVideoState* videoState) {

    is_full_screen = !is_full_screen;
    SDL_SetWindowFullscreen (window, is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
  //}}}

  // event loop
  //{{{
  sVideoState* stream_open (const char* filename, AVInputFormat* iformat) {

    sVideoState* videoState = (sVideoState*)av_mallocz (sizeof(sVideoState));
    if (!videoState)
      return NULL;

    videoState->last_video_stream = videoState->video_stream = -1;
    videoState->last_audio_stream = videoState->audio_stream = -1;
    videoState->last_subtitle_stream = videoState->subtitle_stream = -1;
    videoState->filename = av_strdup (filename);
    if (!videoState->filename)
      goto fail;

    videoState->iformat = iformat;
    videoState->ytop    = 0;
    videoState->xleft   = 0;

    // start video display
    if (frame_queue_init (&videoState->pictq, &videoState->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
      goto fail;
    if (frame_queue_init (&videoState->subpq, &videoState->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
      goto fail;
    if (frame_queue_init (&videoState->sampq, &videoState->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
      goto fail;

    if (packet_queue_init (&videoState->videoq) < 0 ||
        packet_queue_init (&videoState->audioq) < 0 ||
        packet_queue_init (&videoState->subtitleq) < 0)
      goto fail;

    if (!(videoState->continue_read_thread = SDL_CreateCond())) {
      av_log (NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
      goto fail;
      }

    init_clock (&videoState->vidclk, &videoState->videoq.serial);
    init_clock (&videoState->audclk, &videoState->audioq.serial);
    init_clock (&videoState->extclk, &videoState->extclk.serial);
    videoState->audio_clock_serial = -1;

    if (startup_volume < 0)
      av_log (NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", startup_volume);
    if (startup_volume > 100)
      av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", startup_volume);
    startup_volume = av_clip (startup_volume, 0, 100);
    startup_volume = av_clip (SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);

    videoState->audio_volume = startup_volume;
    videoState->muted = 0;
    videoState->av_sync_type = av_sync_type;
    videoState->read_tid     = SDL_CreateThread (read_thread, "read_thread", videoState);

    if (!videoState->read_tid) {
      av_log (NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
  fail:
      stream_close (videoState);
      return NULL;
      }

    return videoState;
    }
  //}}}
  //{{{
  void do_exit (sVideoState* videoState) {

    if (videoState)
      stream_close (videoState);

    if (renderer)
      SDL_DestroyRenderer (renderer);

    if (window)
      SDL_DestroyWindow (window);
    uninit_opts();

    avformat_network_deinit();

    if (show_status)
      printf ("\n");

    SDL_Quit();

    av_log (NULL, AV_LOG_QUIET, "%s", "");
    exit (0);
    }
  //}}}
  //{{{
  void event_loop (sVideoState* cur_stream) {
  // handle GUI event

    double incr, pos;

    for (;;) {
      double x;
      SDL_PumpEvents();

      double remaining_time = 0.0;
      SDL_Event event;
      while (!SDL_PeepEvents (&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
          SDL_ShowCursor (0);
          cursor_hidden = 1;
          }
        if (remaining_time > 0.0)
          av_usleep (int(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;

        if (cur_stream->show_mode != SHOW_MODE_NONE && (!cur_stream->paused || cur_stream->force_refresh))
          video_refresh (cur_stream, &remaining_time);
        SDL_PumpEvents();
        }

      switch (event.type) {
        case SDL_KEYDOWN:
          if (event.key.keysym.sym == SDLK_ESCAPE) {
            do_exit (cur_stream);
            break;
            }

          // If we don't yet have a window, skip all key events, because read_thread might still be initializing...
          if (!cur_stream->width)
            continue;

          switch (event.key.keysym.sym) {
            //{{{
            case SDLK_f:
              toggle_full_screen (cur_stream);
              cur_stream->force_refresh = 1;
              break;
            //}}}
            //{{{
            case SDLK_m:
              toggle_mute (cur_stream);
              break;
            //}}}
            //{{{
            case SDLK_s:
              step_to_next_frame (cur_stream);
              break;
            //}}}
            //{{{
            case SDLK_a:
              stream_cycle_channel (cur_stream, AVMEDIA_TYPE_AUDIO);
              break;
            //}}}
            //{{{
            case SDLK_v:
              stream_cycle_channel (cur_stream, AVMEDIA_TYPE_VIDEO);
              break;
            //}}}
            //{{{
            case SDLK_c:
              stream_cycle_channel (cur_stream, AVMEDIA_TYPE_VIDEO);
              stream_cycle_channel (cur_stream, AVMEDIA_TYPE_AUDIO);
              stream_cycle_channel (cur_stream, AVMEDIA_TYPE_SUBTITLE);
              break;
            //}}}
            //{{{
            case SDLK_t:
              stream_cycle_channel (cur_stream, AVMEDIA_TYPE_SUBTITLE);
              break;
            //}}}
            //{{{
            case SDLK_w:
              toggle_audio_display (cur_stream);
              break;
            //}}}
            //{{{
            case SDLK_PAGEUP:
              break;
            //}}}
            //{{{
            case SDLK_PAGEDOWN:
              break;
            //}}}
            //{{{
            case SDLK_LEFT:
              incr = seek_interval ? -seek_interval : -10.0;
              goto do_seek;
            //}}}
            //{{{
            case SDLK_RIGHT:
              incr = seek_interval ? seek_interval : 10.0;
              goto do_seek;
            //}}}
            //{{{
            case SDLK_UP:
              incr = 60.0;
              goto do_seek;
            //}}}
            //{{{
            case SDLK_DOWN:
              incr = -60.0;

            do_seek:
              if (seek_by_bytes) {
                pos = -1;
                if (pos < 0 && cur_stream->video_stream >= 0)
                  pos = (double)frame_queue_last_pos (&cur_stream->pictq);
                if (pos < 0 && cur_stream->audio_stream >= 0)
                  pos = (double)frame_queue_last_pos (&cur_stream->sampq);
                if (pos < 0)
                  pos = (double)avio_tell (cur_stream->ic->pb);
                if (cur_stream->ic->bit_rate)
                  incr *= cur_stream->ic->bit_rate / 8.0;
                else
                  incr *= 180000.0;

                pos += incr;
                stream_seek (cur_stream, (int64_t)pos, (int64_t)incr, 1);
                }

              else {
                pos = get_master_clock(cur_stream);
                if (isnan(pos))
                  pos = (double)cur_stream->seek_pos / AV_TIME_BASE;
                pos += incr;
                if (cur_stream->ic->start_time != AV_NOPTS_VALUE && pos < cur_stream->ic->start_time / (double)AV_TIME_BASE)
                  pos = cur_stream->ic->start_time / (double)AV_TIME_BASE;

                stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                }

            break;
            //}}}
            //{{{
            case SDLK_SPACE:
              toggle_pause (cur_stream);
              break;
            //}}}
            //{{{
            case SDLK_0:
              update_volume (cur_stream, 1, SDL_VOLUME_STEP);
              break;
            //}}}
            //{{{
            case SDLK_9:
              update_volume (cur_stream, -1, SDL_VOLUME_STEP);
              break;
            //}}}
            default:
              break;
            }
          break;

        //{{{
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.button == SDL_BUTTON_LEFT) {
            toggle_full_screen(cur_stream);
            cur_stream->force_refresh = 1;
            }
        //}}}
        //{{{
        case SDL_MOUSEMOTION:

          if (cursor_hidden) {
            SDL_ShowCursor(1);
            cursor_hidden = 0;
            }

          cursor_last_shown = av_gettime_relative();
          if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button != SDL_BUTTON_RIGHT)
              break;
            x = event.button.x;
            }
          else {
            if (!(event.motion.state & SDL_BUTTON_RMASK))
              break;
            x = event.motion.x;
            }
          if (seek_by_bytes || cur_stream->ic->duration <= 0) {
            uint64_t size =  avio_size (cur_stream->ic->pb);
            stream_seek (cur_stream,int64_t(size * x /cur_stream->width), 0, 1);
            }
          else {
            int tns  = int(cur_stream->ic->duration / 1000000LL);
            int thh  = tns / 3600;
            int tmm  = (tns % 3600) / 60;
            int tss  = (tns % 60);
            float frac = int(x / cur_stream->width);
            int ns   = int(frac * tns);
            int hh   = ns / 3600;
            int mm   = (ns % 3600) / 60;
            int ss   = (ns % 60);
            av_log (NULL, AV_LOG_INFO,
                    "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n",
                    frac*100.f, hh, mm, ss, thh, tmm, tss);

            int64_t ts = int64_t(frac * cur_stream->ic->duration);
            if (cur_stream->ic->start_time != AV_NOPTS_VALUE)
              ts += cur_stream->ic->start_time;
            stream_seek (cur_stream, ts, 0, 0);
            }

          break;
        //}}}
        //{{{
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
              case SDL_WINDOWEVENT_SIZE_CHANGED:
                screen_width  = cur_stream->width  = event.window.data1;
                screen_height = cur_stream->height = event.window.data2;
                if (cur_stream->vis_texture) {
                  SDL_DestroyTexture (cur_stream->vis_texture);
                  cur_stream->vis_texture = NULL;
                  }
              case SDL_WINDOWEVENT_EXPOSED:
                cur_stream->force_refresh = 1;
              }
            break;
        //}}}
        case SDL_QUIT:
        //{{{
        case FF_QUIT_EVENT:
          do_exit (cur_stream);
          break;
        //}}}
        //{{{
        default:
            break;
        //}}}
        }
      }
    }
  //}}}

  // opts
  //{{{
  int opt_width (void* optctx, const char* opt, const char* arg) {

    screen_width = (int)parse_number_or_die (opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
    }
  //}}}
  //{{{
  int opt_height (void* optctx, const char* opt, const char* arg) {

    screen_height = (int)parse_number_or_die (opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
    }
  //}}}
  //{{{
  int opt_show_mode (void* optctx, const char* opt, const char* arg) {

    show_mode = !strcmp (arg, "video") ? SHOW_MODE_VIDEO :
                  !strcmp (arg, "waves") ? SHOW_MODE_WAVES :
                    !strcmp (arg, "rdft" ) ? SHOW_MODE_RDFT  :
                      (ShowMode)parse_number_or_die (opt, arg, OPT_INT, 0, SHOW_MODE_NB-1);
    return 0;
    }
  //}}}
  //{{{
  void opt_input_file (void* optctx, const char* filename) {

    if (input_filename) {
      av_log (NULL, AV_LOG_FATAL,
              "Argument '%s' provided as input filename, but '%s' was already specified.\n",
              filename, input_filename);
      exit(1);
      }

    if (!strcmp (filename, "-"))
      filename = "pipe:";

    input_filename = filename;
    }
  //}}}
  //{{{
  int opt_format (void* optctx, const char* opt, const char* arg) {

    file_iformat = av_find_input_format (arg);
    if (!file_iformat) {
      av_log (NULL, AV_LOG_FATAL, "Unknown input format: %s\n", arg);
      return AVERROR (EINVAL);
      }

    return 0;
    }
  //}}}
  //{{{
  int opt_codec (void* optctx, const char* opt, const char* arg) {

    const char* spec = strchr (opt, ':');
    if (!spec) {
      av_log (NULL, AV_LOG_ERROR, "No media specifier was specified in '%s' in option '%s'\n", arg, opt);
      return AVERROR(EINVAL);
      }

    spec++;
    switch (spec[0]) {
      case 'a' :
        audio_codec_name = arg;
        break;

      case 's' :
        subtitle_codec_name = arg;
        break;

      case 'v' :
        video_codec_name = arg;
        break;

      default:
        av_log (NULL, AV_LOG_ERROR, "Invalid media specifier '%s' in option '%s'\n", spec, opt);
        return AVERROR(EINVAL);
      }


    return 0;
    }
  //}}}
  //{{{
  const OptionDef kOptions[] = {
    CMDUTILS_COMMON_OPTIONS
    { "nod",     OPT_BOOL, { &display_disable },  "disable graphical display" },
    { "an",      OPT_BOOL, { &audio_disable },    "disable audio" },
    { "vn",      OPT_BOOL, { &video_disable },    "disable video" },
    { "sn",      OPT_BOOL, { &subtitle_disable }, "disable subtitling" },
    { "rotate",  OPT_BOOL, { &autorotate },       "automatically rotate video", "" },
    { "nob",     OPT_BOOL, { &borderless },       "borderless window" },
    { "top",     OPT_BOOL, { &alwaysontop },      "window always on top" },
    { "fs",      OPT_BOOL, { &is_full_screen },   "force full screen" },

    { "stats",   OPT_BOOL | OPT_EXPERT, { &show_status },       "show status", "" },
    { "fast",    OPT_BOOL | OPT_EXPERT, { &fast },              "non spec compliant optimizations", "" },
    { "genpts",  OPT_BOOL | OPT_EXPERT, { &genpts },            "generate pts", "" },
    { "drop",    OPT_BOOL | OPT_EXPERT, { &framedrop },         "drop frames when cpu is too slow", "" },
    { "infbuf",  OPT_BOOL | OPT_EXPERT, { &infinite_buffer },   "no limit input buffer size", "" },

    { "info",    OPT_BOOL | OPT_INPUT | OPT_EXPERT, { &find_stream_info }, "read decode streams fill missing infor" },

    { "f",       HAS_ARG,  { .func_arg = opt_format },    "force format", "fmt" },
    { "x",       HAS_ARG,  { .func_arg = opt_width },     "force displayed width", "width" },
    { "y",       HAS_ARG,  { .func_arg = opt_height },    "force displayed height", "height" },
    { "show",    HAS_ARG,  { .func_arg = opt_show_mode }, "set show 0:video 1:waves, 2:RDFT", "mode" },
    { "codec",   HAS_ARG,  { .func_arg = opt_codec},      "force decoder", "decoder_name" },

    { "b",       HAS_ARG | OPT_INT, { &seek_by_bytes }, "seek by bytes 0=off 1=on -1=auto", "val" },
    { "vol",     HAS_ARG | OPT_INT, { &startup_volume}, "set startup volume 0=min 100=max", "volume" },

    { "left",    HAS_ARG | OPT_INT | OPT_EXPERT, { &screen_left },         "set xleft window", "x pos" },
    { "top",     HAS_ARG | OPT_INT | OPT_EXPERT, { &screen_top },          "set ytop  window", "y pos" },
    { "drp",     HAS_ARG | OPT_INT | OPT_EXPERT, { &decoder_reorder_pts }, "decoder reorder pts 0=off 1=on -1=auto", ""},
    { "lowres",  HAS_ARG | OPT_INT | OPT_EXPERT, { &lowres },              "", "" },
    { "thread",  HAS_ARG | OPT_INT | OPT_EXPERT, { &filter_nbthreads },    "set filter threads per graph" },

    { "acodec",  HAS_ARG | OPT_STRING | OPT_EXPERT, { &audio_codec_name },    "force audDecode", "decoder_name" },
    { "scodec",  HAS_ARG | OPT_STRING | OPT_EXPERT, { &subtitle_codec_name }, "force subDecode", "decoder_name" },
    { "vcodec",  HAS_ARG | OPT_STRING | OPT_EXPERT, { &video_codec_name },    "force vidDecode", "decoder_name" },

    { "ast",     HAS_ARG | OPT_STRING | OPT_EXPERT, { &wanted_stream_spec[AVMEDIA_TYPE_AUDIO] },    "audio stream", "stream_specifier" },
    { "vst",     HAS_ARG | OPT_STRING | OPT_EXPERT, { &wanted_stream_spec[AVMEDIA_TYPE_VIDEO] },    "video stream", "stream_specifier" },
    { "sst",     HAS_ARG | OPT_STRING | OPT_EXPERT, { &wanted_stream_spec[AVMEDIA_TYPE_SUBTITLE] }, "subtitle stream", "stream_specifier" },

    { "rdft",    HAS_ARG | OPT_INT | OPT_AUDIO | OPT_EXPERT, { &rdftspeed }, "rdft speed", "msecs" },
    { "def",     HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, { .func_arg = opt_default }, "catch all option", "" },

    { NULL, },
    };
  //}}}

  //{{{
  void sigterm_handler (int sig) {
    exit(123);
    }
  //}}}
  }

//{{{
void show_help_default (const char* opt, const char* arg) {

  av_log_set_callback (log_callback_help);
  av_log (NULL, AV_LOG_INFO, "play\n");
  av_log (NULL, AV_LOG_INFO, "usage: %s [options] input_file\n", program_name);

  show_help_options (kOptions, "Main options:", 0, OPT_EXPERT, 0);
  show_help_options (kOptions, "Advanced options:", OPT_EXPERT, 0, 0);
  show_help_children (avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
  show_help_children (avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
  show_help_children (sws_get_class(), AV_OPT_FLAG_ENCODING_PARAM);

  printf ("ESC         quit\n"
          "f           toggle fullScreen\n"
          "SPC         pause\n"
          "m           toggle mute\n"
          "9, 0        decrease and increase volume\n"
          "a           cycle audio channel in current program\n"
          "v           cycle video channel\n"
          "t           cycle subtitle channel in current program\n"
          "c           cycle program\n"
          "w           cycle video filters or show modes\n"
          "s           frameStep\n"
          "left,right  seek 10 seconds\n"
          "down,up     seek 1 minute\n"
          "rightClick  seek to percentage in file\n"
          "leftClick   toggle full screen\n"
          );
  }
//}}}
//{{{
int main (int argc, char** argv) {

  init_dynload();
  av_log_set_flags (AV_LOG_SKIP_REPEATED);
  parse_loglevel (argc, argv, kOptions);

  #if CONFIG_AVDEVICE
    avdevice_register_all();
  #endif
  avformat_network_init();

  init_opts();

  signal (SIGINT , sigterm_handler); // Interrupt (ANSI)
  signal (SIGTERM, sigterm_handler); // Termination (ANSI)

  show_banner (argc, argv, kOptions);
  parse_options (NULL, argc, argv, kOptions, opt_input_file);

  if (!input_filename) {
     //{{{  error exit
     av_log (NULL, AV_LOG_INFO, "no file, -h for help\n");
     av_log (NULL, AV_LOG_INFO, "usage: %s [options] input_file\n", program_name);
     exit (1);
     }
     //}}}

  if (display_disable)
    video_disable = 1;

  int flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
  if (audio_disable)
    flags &= ~SDL_INIT_AUDIO;
  else if (!SDL_getenv ("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
    SDL_setenv ("SDL_AUDIO_ALSA_SET_BUFFER_SIZE","1", 1);

  if (display_disable)
    flags &= ~SDL_INIT_VIDEO;
  if (SDL_Init (flags)) {
    //{{{  error exit
    av_log (NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
    exit (1);
    }
    //}}}

  SDL_EventState (SDL_SYSWMEVENT, SDL_IGNORE);
  SDL_EventState (SDL_USEREVENT, SDL_IGNORE);

  av_init_packet (&flush_pkt);
  flush_pkt.data = (uint8_t*)&flush_pkt;

  if (!display_disable) {
    int flags = SDL_WINDOW_HIDDEN;
    if (alwaysontop)
      flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    if (borderless)
      flags |= SDL_WINDOW_BORDERLESS;
    else
      flags |= SDL_WINDOW_RESIZABLE;
    window = SDL_CreateWindow (program_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               default_width, default_height, flags);

    SDL_SetHint (SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (window) {
      renderer = SDL_CreateRenderer (window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
      if (!renderer) {
        av_log (NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
        renderer = SDL_CreateRenderer (window, -1, 0);
        }
      if (renderer)
        if (!SDL_GetRendererInfo (renderer, &renderer_info))
          av_log (NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", renderer_info.name);
      }
    if (!window || !renderer || !renderer_info.num_texture_formats) {
      //{{{  error exit
      av_log (NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
      do_exit (NULL);
      }
      //}}}
    }

  sVideoState* videoState = stream_open (input_filename, file_iformat);
  if (!videoState) {
    //{{{  error exit
    av_log (NULL, AV_LOG_FATAL, "Failed to initialize videoState!\n");
    do_exit (NULL);
    }
    //}}}

  event_loop (videoState);
  return 0;
  }
//}}}
