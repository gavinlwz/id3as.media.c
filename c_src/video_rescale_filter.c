#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;
  int initialised;

  struct SwsContext *convert_context;

  enum PixelFormat output_pixfmt;
  int output_width;
  int output_height;

  AVFrame *output_frame;

} codec_t;

static void do_init(codec_t *this, AVFrame *frame);

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  codec_t *this = context->priv_data;

  do_init(this, frame);

  if (this->convert_context) {
    
    sws_scale(this->convert_context,
	      (const uint8_t * const *) frame->data, frame->linesize, 0, frame->height, 
	      this->output_frame->data, this->output_frame->linesize);  

    // This copies the properties, including allocating a copy of the side_data.
    // There's no need to free since a) there's no API to do so and b) copy_props
    // checks if there is already side_data on the dest and free it itself.
    av_frame_copy_props(this->output_frame, frame);
    
    send_to_graph(context, this->output_frame, timebase);
  }
  else {
    send_to_graph(context, frame, timebase);
  }
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->output_frame = av_frame_alloc();
  this->output_frame->format = this->output_pixfmt;
  this->output_frame->width = this->output_width;
  this->output_frame->height = this->output_height;

  av_frame_get_buffer(this->output_frame, 32);

  this->initialised = 0;
}

static void do_init(codec_t *this, AVFrame *frame)
{
  if (this->initialised) {
    return;
  }

  if ((frame->width != this->output_width) ||
      (frame->height != this->output_height) ||
      (frame->format != this->output_pixfmt)) {
    this->convert_context = sws_getContext(frame->width,
					   frame->height,
					   frame->format,
					   this->output_width,
					   this->output_height,
					   this->output_pixfmt,
					   SWS_BICUBIC, NULL, NULL, NULL);
  }
  else {
    this->convert_context = 0;
  }

  this->initialised = 1;
}

static const AVOption options[] = {
  { "output_width", "the output width", offsetof(codec_t, output_width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "output_height", "the output height", offsetof(codec_t, output_height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "output_pixel_format", "the output pixel format", offsetof(codec_t, output_pixfmt), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { NULL }
};

static const AVClass class = {
  .class_name = "video rescaler options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_rescale_video_filter = {
  .name = "video rescaler",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
