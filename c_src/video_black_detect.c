#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  int percentage_below_threshold;
  int threshold;

} codec_t;

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  codec_t *this = context->priv_data;

  uint8_t *p = frame->data[0];
  int i, j;
  int pblack = 0;
  int nblack = 0;

  for (i = 0; i < frame->height; i++) 
    {
      for (j = 0; j < frame->width; j++)
	{
	  nblack += p[j] < this->threshold;
	}
      p += frame->linesize[0];
    }

  pblack = nblack * 100 / (frame->width * frame->height);

  AVFrameSideData *side_data = av_frame_get_side_data(frame, FRAME_INFO_SIDE_DATA_TYPE);

  ((frame_info *)side_data->data)->flags |= (pblack >= this->percentage_below_threshold ? BLACK : 0);

  send_to_graph(context, frame, timebase);
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
}

static const AVOption options[] = {
  { "percentage_below_threshold", "Percentage of the pixels that have to be below the threshold for the frame to be considered black.", offsetof(codec_t, percentage_below_threshold), AV_OPT_TYPE_INT, { .i64 = 98 }, 0, 100 },
  { "threshold", "threshold below which a pixel value is considered black", offsetof(codec_t, threshold), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, INT_MAX },
    { NULL },
};

static const AVClass class = {
  .class_name = "black detect options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_black_detect_filter = {
  .name = "black detect",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
