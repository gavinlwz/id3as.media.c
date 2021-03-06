#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  AVFrame *frame;

  int width;
  int height;
  enum PixelFormat input_pixfmt;
  int interlaced;

} codec_t;

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;

  avpicture_fill((AVPicture *) this->frame, data, this->input_pixfmt, this->width, this->height);

  this->frame->interlaced_frame = this->interlaced;

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
  this->frame->format = this->input_pixfmt;
  this->frame->width = this->width;
  this->frame->height = this->height;
}

static const AVOption options[] = {
  { "width", "the width", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "the height", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pixel_format", "the pixel format", offsetof(codec_t, input_pixfmt), AV_OPT_TYPE_INT },
  { "interlaced", "interlaced", offsetof(codec_t, interlaced), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX },
  { NULL }
};

static const AVClass class = {
  .class_name = "raw video input options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_raw_video_input = {
  .name = "raw video input",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
