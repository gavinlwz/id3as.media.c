#include "id3as_libav.h"
#include "i_utils.h" 
#include <inttypes.h>

typedef struct _codec_t
{
  AVClass *av_class;
  int initialised;

  AVDictionary *codec_options;
  AVCodec *codec;
  AVCodecContext *context;
  void *pkt_buffer;
  int pkt_size;

  int width;
  int height;
  int stream_id;
  char *pin_name;
  char *codec_name;
  enum PixelFormat input_pixfmt;

} codec_t;

static i_mutex_t mutex = INITIALISE_STATIC_MUTEX();

static void do_init(codec_t *this, AVFrame *frame);

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  AVPacket pkt;
  int got_packet_ptr = 0;
  int ret = 0;
  codec_t *this = context->priv_data;

  do_init(this, frame);

  do
    {
      AVFrame local_frame = *frame;

      av_init_packet(&pkt);
      pkt.size = this->pkt_size;
      pkt.data = this->pkt_buffer;

      // Rescale PTS from the frame timebase to the codec timebase
      local_frame.pts = av_rescale_q(local_frame.pts, timebase, this->context->time_base);
      local_frame.pict_type = 0;

      ret = avcodec_encode_video2(this->context, &pkt, &local_frame, &got_packet_ptr);
      
      if (ret != 0)
	{
	  ERRORFMT("avcodec_encode_video2 failed with %d", ret);
	  // exit(-1);
	}
      
      if (got_packet_ptr)
	{
	  // And rescale back to "erlang time"
	  pkt.pts = av_rescale_q(pkt.pts, this->context->time_base, NINETY_KHZ);
	  pkt.dts = av_rescale_q(pkt.dts, this->context->time_base, NINETY_KHZ);
	  pkt.duration = av_rescale_q(pkt.duration, this->context->time_base, NINETY_KHZ);

	  write_output_from_packet(this->pin_name, this->stream_id, this->context, &pkt, local_frame.opaque);
	}

    } while (0); // currently we just "loop" once - when we need to flush (not yet implemented), we need to loop
                 // on got_packet_ptr
}

static void do_init(codec_t *this, AVFrame *frame) 
{
  if (this->initialised) {
    return;
  }

  AVDictionaryEntry *flagsEntry = av_dict_get(this->codec_options, "flags", NULL, 0);
  char flags[255];
  strcpy(flags, flagsEntry ? flagsEntry-> value : "");

  if (frame->interlaced_frame) {
    strcat(flags, "+ildct");
  }
  else {
    strcat(flags, "-ildct");
  }
  av_dict_set(&this->codec_options, "flags", flags, 0);

  i_mutex_lock(&mutex);

  this->context = allocate_video_context(this->codec, this->width, this->height, this->input_pixfmt, this->codec_options);
  this->initialised = 1;

  i_mutex_unlock(&mutex);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;
  this->initialised = 0;

  this->codec = get_encoder(this->codec_name);
  this->codec_options = codec_options;

  this->pkt_size = this->width * this->height * 10;  // Should be sufficient space! TODO - bit ugly though :(
  this->pkt_buffer = malloc(this->pkt_size);
}

static const AVOption options[] = {
  { "stream_id", "The stream id for the output stream", offsetof(codec_t, stream_id), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pin_name", "The pin name for the output stream", offsetof(codec_t, pin_name), AV_OPT_TYPE_STRING },
  { "codec", "The codec for encoding", offsetof(codec_t, codec_name), AV_OPT_TYPE_STRING },
  { "width", "The width of the frame", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "The height of the frame", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pixel_format", "The pixel format", offsetof(codec_t, input_pixfmt), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { NULL },
};

static const AVClass class = {
  .class_name = "video encoder options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_output_encoded_video_filter = {
  .name = "encoded video output",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
