#include "hlmediacodec_codec.h"
#include "h264_parse.h"
#include "hevc_parse.h"
#include <string.h>
#include <sys/types.h>
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"
#include "libavcodec/mpeg4audio.h"

static int hlmediacodec_encode_fill_format(AVCodecContext *avctx, AMediaFormat *mediaformat)
{
  hi_logd(avctx, "%s %d", __FUNCTION__, __LINE__);

  int ret = AVERROR_EXTERNAL;
  HLMediaCodecEncContext *ctx = avctx->priv_data;

  do
  {
    const char *mime = ff_hlmediacodec_get_mime(avctx->codec_id);
    if (!mime)
    {
      hi_loge(avctx, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
      break;
    }

    // int color_format = ff_hlmediacodec_get_color_format(avctx->pix_fmt);
    int color_format = ctx->color_format;
    hi_logi(avctx, "%s %d color_format: %d", __FUNCTION__, __LINE__, color_format);
    // int color_format = 21;

    AMediaFormat_setString(mediaformat, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_HEIGHT, avctx->height);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_WIDTH, avctx->width);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_BIT_RATE, avctx->bit_rate);
    AMediaFormat_setFloat(mediaformat, AMEDIAFORMAT_KEY_FRAME_RATE, av_q2d(avctx->framerate));
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_COLOR_FORMAT, color_format);
    AMediaFormat_setInt32(mediaformat, "bitrate-mode", ctx->rc_mode); //质量优先

    AMediaFormat_setInt32(mediaformat, "profile", 0x08); // High
    AMediaFormat_setInt32(mediaformat, "level", 0x200);  // Level31

    hi_logi(avctx, "%s %d mime: %s timeout: (in: %d ou: %d) times: (in: %d ou: %d) "
                   "width: %d height: %d pix_fmt: %d color_format: %d bit_rate: %d",
            __FUNCTION__, __LINE__, mime,
            ctx->in_timeout, ctx->ou_timeout, ctx->in_timeout_times, ctx->ou_timeout_times,
            avctx->width, avctx->height, avctx->pix_fmt, color_format, avctx->bit_rate);

    ret = 0;
  } while (false);

  hi_logi(avctx, "%s %d end (%d)", __FUNCTION__, __LINE__, ret);

  return ret;
}

int hlmediacodec_fill_format(AVCodecContext *avctx, AMediaFormat *mediaformat)
{
  if (av_codec_is_encoder(avctx->codec))
  {
    return hlmediacodec_encode_fill_format(avctx, mediaformat);
  }
  else
  {
    return AVERROR_PROTOCOL_NOT_FOUND;
  }
}

int hlmediacodec_encode_header(AVCodecContext *avctx)
{
  HLMediaCodecEncContext *ctx = avctx->priv_data;
  AMediaCodec *codec = NULL;
  int ret = AVERROR_BUG;

  do
  {
    hi_logi(avctx, "%s %d start", __FUNCTION__, __LINE__);

    const char *mime = ff_hlmediacodec_get_mime(avctx->codec_id);
    if (!mime)
    {
      hi_loge(avctx, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
      break;
    }

    hi_logi(avctx, "%s %d AMediaCodec_createEncoderByType %s", __FUNCTION__, __LINE__, mime);

    if (!(codec = AMediaCodec_createEncoderByType(mime)))
    {
      hi_loge(avctx, "%s %d AMediaCodec_createEncoderByType (%s) failed!", __FUNCTION__, __LINE__, mime);
      break;
    }

    hi_logi(avctx, "%s %d AMediaCodec_configure %s format %s", __FUNCTION__, __LINE__, mime, AMediaFormat_toString(ctx->mediaformat));

    media_status_t status = AMEDIA_OK;
    if ((status = AMediaCodec_configure(codec, ctx->mediaformat, NULL, 0, HLMEDIACODEC_CONFIGURE_FLAG_ENCODE)) != AMEDIA_OK)
    {
      hi_loge(avctx, "%s %d AMediaCodec_configure failed (%d)!", __FUNCTION__, __LINE__, status);
      break;
    }

    if ((status = AMediaCodec_start(codec)))
    {
      hi_loge(avctx, "%s %d AMediaCodec_start failed (%d)!", __FUNCTION__, __LINE__, status);
      break;
    }

    int in_times = ctx->in_timeout_times;
    while (true)
    {
      // input buff
      ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(codec, ctx->in_timeout);
      if (bufferIndex < 0)
      {
        hi_loge(avctx, "%s %d AMediaCodec_dequeueInputBuffer failed (%d) times: %d!", __FUNCTION__, __LINE__, bufferIndex, in_times);
        if (in_times-- <= 0)
        {
          hi_loge(avctx, "%s %d AMediaCodec_dequeueInputBuffer timeout ", __FUNCTION__, __LINE__);
          break;
        }
        continue;
      }

      size_t bufferSize = 0;
      uint8_t *buffer = AMediaCodec_getInputBuffer(codec, bufferIndex, &bufferSize);
      if (!buffer)
      {
        hi_loge(avctx, "%s %d AMediaCodec_getInputBuffer failed!", __FUNCTION__, __LINE__);
        break;
      }

      int status = AMediaCodec_queueInputBuffer(codec, bufferIndex, 0, bufferSize, 0, 0);
      hi_logi(avctx, "%s %d AMediaCodec_queueInputBuffer status (%d)!", __FUNCTION__, __LINE__, status);
      break;
    }

    int ou_times = ctx->ou_timeout_times;
    bool got_config = false;

    while (!got_config)
    {
      AMediaCodecBufferInfo bufferInfo;
      int bufferIndex = AMediaCodec_dequeueOutputBuffer(codec, &bufferInfo, ctx->ou_timeout);
      hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer stats (%d) size: %u offset: %u flags: %u pts: %lld", __FUNCTION__, __LINE__,
              bufferIndex, bufferInfo.size, bufferInfo.offset, bufferInfo.flags, bufferInfo.presentationTimeUs);
      if (bufferIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED || bufferIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
      {
        continue;
      }
      else if (bufferIndex < 0)
      {
        if (ou_times-- <= 0)
        {
          hi_loge(avctx, "%s %d Got extradata timeout ", __FUNCTION__, __LINE__);
          break;
        }

        continue;
      }

      size_t out_size = 0;
      uint8_t *out_buffer = AMediaCodec_getOutputBuffer(codec, bufferIndex, &out_size);
      if (!out_buffer)
      {
        hi_loge(avctx, "%s %d AMediaCodec_getOutputBuffer failed!", __FUNCTION__, __LINE__);
        AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
        break;
      }

      if (bufferInfo.flags & HLMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
      {
        hi_logi(avctx, "%s %d Got extradata of size (%d %d)", __FUNCTION__, __LINE__, out_size, bufferInfo.size);

        if (avctx->extradata)
        {
          av_freep(&avctx->extradata);
          avctx->extradata = NULL;
          avctx->extradata_size = 0;
        }

        avctx->extradata = av_mallocz(bufferInfo.size + AV_INPUT_BUFFER_PADDING_SIZE);
        avctx->extradata_size = bufferInfo.size;
        memcpy(avctx->extradata, out_buffer, avctx->extradata_size);

        got_config = true;
      }

      AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
    }

    if (!got_config)
    {
      hi_loge(avctx, "%s %d get config fail!", __FUNCTION__, __LINE__);
      break;
    }

    ret = 0;
  } while (false);

  if (codec)
  {
    AMediaCodec_flush(codec);
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);

    hi_logi(avctx, "%s %d AMediaCodec_delete!", __FUNCTION__, __LINE__);
  }

  hi_logi(avctx, "%s %d ret: %d", __FUNCTION__, __LINE__, ret);
  return ret;
}

void hlmediacodec_show_stats(AVCodecContext *avctx, HLMediaCodecStats stats)
{
  hi_logi(avctx, "%s %d alive: %lld get: (succ: %d fail: %d) in: (succ: %d fail: [%d %d]) ou: [succ: (%d frame: [%d %d %d %d]) fail: (%d %d %d %d %d)]", __FUNCTION__, __LINE__,
          stats.uint_stamp - stats.init_stamp, stats.get_succ_cnt, stats.get_fail_cnt, stats.in_succ_cnt, stats.in_fail_cnt, stats.in_fail_again_cnt,
          stats.ou_succ_cnt, stats.ou_succ_frame_cnt, stats.ou_succ_conf_cnt, stats.ou_succ_idr_cnt, stats.ou_succ_end_cnt,
          stats.ou_fail_cnt, stats.ou_fail_again_cnt, stats.ou_fail_format_cnt, stats.ou_fail_buffer_cnt, stats.ou_fail_oth_cnt);
}