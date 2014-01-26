#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  AVFrame *frame;

  char *device_name;
  int width;
  int height;
  int interlaced;
  int frame_rate;
  enum PixelFormat pixfmt;

} codec_t;

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;

  avpicture_fill((AVPicture *) this->frame, data, this->pixfmt, this->width, this->height);

  set_frame_metadata(this->frame, metadata);

  send_to_graph(context, this->frame, NINETY_KHZ);
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->frame = av_frame_alloc();
  this->frame->format = this->pixfmt;
  this->frame->width = this->width;
  this->frame->height = this->height;
  this->frame->interlaced_frame = this->interlaced ? 1 : 0;
}

static const AVOption options[] = {
  { "device", "The codec for encoding", offsetof(codec_t, device_name), AV_OPT_TYPE_STRING },
  { "width", "the width", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "the height", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "interlaced", "is input interlaced format", offsetof(codec_t, interlaced), AV_OPT_TYPE_INT },
  { "frame_rate", "frame rate", offsetof(codec_t, frame_rate), AV_OPT_TYPE_INT },
  { "pixel_format", "the pixel format", offsetof(codec_t, pixfmt), AV_OPT_TYPE_INT },
  { NULL }
};

static const AVClass class = {
  .class_name = "raw video input options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_raw_video_generator_input = {
  .name = "raw video generator",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
