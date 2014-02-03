#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  AVFrame *frame;

  char *device_name;
  int device_fd;
  int width;
  int height;
  int interlaced;
  int frame_rate;
  enum PixelFormat pixfmt;

  int data_size;
  uint8_t *data;

} codec_t;

static int read_exact(int fd, unsigned char *buf, int len)
{
  int i, got = 0;

  do {
    if ((i = read(fd, buf + got, len - got)) <= 0)
      {
	return i;
      }

    got += i;

  } while (got < len);

  return len;
}

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;

  read_exact(this->device_fd, this->data, this->data_size);

  avpicture_fill((AVPicture *) this->frame, this->data, this->pixfmt, this->width, this->height);

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
  this->frame->format = this->pixfmt;
  this->frame->width = this->width;
  this->frame->height = this->height;
  this->frame->interlaced_frame = this->interlaced ? 1 : 0;

  this->data_size = avpicture_fill((AVPicture *) this->frame, NULL, this->pixfmt, this->width, this->height);
  this->data = malloc(this->data_size);

  this->device_fd = open(this->device_name, O_RDONLY);
}

static const AVOption options[] = {
  { "device", "The codec for encoding", offsetof(codec_t, device_name), AV_OPT_TYPE_STRING },
  { "width", "the width", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "the height", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "interlaced", "is input interlaced format", offsetof(codec_t, interlaced), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 1 },
  { "frame_rate", "frame rate", offsetof(codec_t, frame_rate), AV_OPT_TYPE_INT, { .i64 = 25 }, INT_MIN, INT_MAX },
  { "pixel_format", "the pixel format", offsetof(codec_t, pixfmt), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
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
