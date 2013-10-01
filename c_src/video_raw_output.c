#include "id3as_libav.h"

static void frame_to_array(AVFrame *frame, unsigned char **output_data, unsigned int *output_data_size, unsigned int *output_size);

typedef struct _codec_t
{
  AVClass *av_class;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;

  char *pin_name;
  int width;
  int height;
  char *pixel_format_name;
  enum PixelFormat pixfmt;

  unsigned char *output_data;
  unsigned int output_data_buffer_size;
  unsigned int output_data_size;

} codec_t;

static void process(ID3ASFilterContext *context, AVFrame *frame)
{
  codec_t *this = context->priv_data;

  frame_to_array(frame, &this->output_data, &this->output_data_buffer_size, &this->output_data_size);

  // write_output_from_frame(this->pin_name, this->context, frame, this->output_data, this->output_data_size);
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->output_data = NULL;
  this->output_data_buffer_size = 0;
}

static void frame_to_array(AVFrame *frame, unsigned char **output_data, unsigned int *output_data_size, unsigned int *output_size) 
{
  if (frame->format == PIX_FMT_YUV420P) 
    {
      int size = frame->width * frame->height * 12 / 8;

      if (*output_data_size < size)
	{
	  if (*output_data) 
	    {
	      free(*output_data);
	    }

	  *output_data = (unsigned char *) malloc (size);
	  *output_data_size = size;
	}

      *output_size = size;
      
      unsigned char *p = *output_data;
      
      for (int i = 0; i < frame->height; i++)
	{
	  memcpy(p, frame->data[0] + (i * frame->linesize[0]), frame->width);
	  p += frame->width;
	}
      for (int i = 0; i < frame->height / 2; i++)
	{
	  memcpy(p, frame->data[1] + (i * frame->linesize[1]), frame->width / 2);
	  p += frame->width / 2;
	}
      for (int i = 0; i < frame->height / 2; i++)
	{
	  memcpy(p, frame->data[2] + (i * frame->linesize[2]), frame->width / 2);
	  p += frame->width / 2;
	}
    }
  else if (frame->format == PIX_FMT_BGR24) 
    {
      *output_data = frame->data[0];
      *output_size = frame->width * frame->height * 3;
    }
  else 
    {
      ERRORFMT("Unable to convert pixel format %d to array", frame->format);
      exit(-1);
    }
}

static const AVOption options[] = {
  { "pin_name", "The pin name for the output stream", offsetof(codec_t, pin_name), AV_OPT_TYPE_STRING },
  { NULL },
};

static const AVClass class = {
  .class_name = "raw video output options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_output_raw_video_filter = {
  .name = "raw video output",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
