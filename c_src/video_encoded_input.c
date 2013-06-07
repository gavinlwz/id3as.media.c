#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;

  int width;
  int height;
  char *codec_name;
  enum PixelFormat pixfmt;

} codec_t;

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;
  AVPacket pkt;
  int got_frame;

  av_init_packet(&pkt);
  pkt.data = data;
  pkt.size = data_size;

  set_packet_metadata(&pkt, metadata);

  // Rescales pts / dts to this->time_base.  Or just count...

  int len = avcodec_decode_video2(this->context, this->frame, &got_frame, &pkt);
  
  if (len < 0)
    {
      TRACEFMT("avcodec_decode_video2 failed with %d", len);
      //exit(-1);
    }
  else if (got_frame && len > 0)
    {
      sized_buffer o = {.data = opaque, .size = opaque_size};

      this->frame->pts = pkt.dts;
      this->frame->opaque = &o;

      send_to_graph(context, this->frame);
    }
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->codec = get_decoder(this->codec_name);

  this->context = allocate_video_context(this->codec, this->width, this->height, this->pixfmt, codec_options);

  this->frame = avcodec_alloc_frame();
}

static const AVOption options[] = {
  { "width", "the width", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "the height", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pixel_format", "the pixel format", offsetof(codec_t, pixfmt), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "codec", "the codec name", offsetof(codec_t, codec_name), AV_OPT_TYPE_STRING },
  { NULL }
};

static const AVClass class = {
  .class_name = "encoded video input options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_encoded_video_input = {
  .name = "encoded video input",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
