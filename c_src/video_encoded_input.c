#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;
  frame_info_queue frame_info_queue;

  int width;
  int height;
  char *codec_name;
  enum PixelFormat pixfmt;

} codec_t;

static int decode(ID3ASFilterContext *context, AVPacket *pkt) 
{
  codec_t *this = context->priv_data;
  int got_frame;
  int len = avcodec_decode_video2(this->context, this->frame, &got_frame, pkt);
  
  if (len < 0)
    {
      TRACEFMT("avcodec_decode_video2 failed with %d", len);
      got_frame = 0;
      //exit(-1);
    }
  else if (got_frame && len > 0)
    {
      add_frame_info_to_frame(&this->frame_info_queue, this->frame);

      this->frame->pts = this->frame->pkt_pts;
      
      send_to_graph(context, this->frame, NINETY_KHZ);
      
      av_frame_unref(this->frame);
    }

  return got_frame;
}

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *frame_info, unsigned int frame_info_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;
  AVPacket pkt;

  av_init_packet(&pkt);
  pkt.data = data;
  pkt.size = data_size;

  set_packet_metadata(&pkt, metadata);
  
  queue_frame_info(&this->frame_info_queue, frame_info, frame_info_size, pkt.pts);

  decode(context, &pkt);
}

static void flush(ID3ASFilterContext *context) 
{
  AVPacket pkt;

  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  while (1) 
    {
      if (!decode(context, &pkt)) {
	break;
      }
    }
  
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->codec = get_decoder(this->codec_name);

  this->context = allocate_video_context(this->codec, this->width, this->height, this->pixfmt, codec_options);

  this->frame = av_frame_alloc();
  init_frame_info_queue(&this->frame_info_queue);
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
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
