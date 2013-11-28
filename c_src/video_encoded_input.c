#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;
  frame_info_queue *frame_info_queue;

  int width;
  int height;
  char *codec_name;
  enum PixelFormat pixfmt;

  char *extradata;

} codec_t;

static int decode(ID3ASFilterContext *context, AVPacket *pkt) 
{

  codec_t *this = context->priv_data;
  int got_frame = 0;
  int len = avcodec_decode_video2(this->context, this->frame, &got_frame, pkt);
  
  if (len < 0)
    {
      TRACEFMT("avcodec_decode_video2 failed with %d", len);
      got_frame = 0;
      //exit(-1);
    }
  else if (got_frame)
    {
      add_frame_info_to_frame(this->frame_info_queue, this->frame);

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
  
  queue_frame_info(this->frame_info_queue, frame_info, frame_info_size, pkt.pts);

  decode(context, &pkt);
}

static void flush(ID3ASFilterContext *context) 
{
  codec_t *this = context->priv_data;

  if (this->codec->capabilities & CODEC_CAP_DELAY) 
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
    }
  
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;
  uint8_t *extradata = NULL;
  int extradata_size = 0;

  this->codec = get_decoder(this->codec_name);

  if (this->extradata) 
    {
      extradata = malloc(strlen(this->extradata));
      extradata_size = av_base64_decode(extradata, this->extradata, strlen(this->extradata));
    }

  this->context = allocate_video_context(this->codec, this->width, this->height, this->pixfmt, extradata, extradata_size, codec_options);

  this->frame = av_frame_alloc();
  init_frame_info_queue(&this->frame_info_queue);
}

static const AVOption options[] = {
  { "width", "the width", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "the height", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pixel_format", "the pixel format", offsetof(codec_t, pixfmt), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "codec", "the codec name", offsetof(codec_t, codec_name), AV_OPT_TYPE_STRING },
  { "extradata", "codec extradata", offsetof(codec_t, extradata), AV_OPT_TYPE_STRING, {.str = NULL} },
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
