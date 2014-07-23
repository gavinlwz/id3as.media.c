#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  int initialised;

  int percentage_below_threshold;
  int threshold;
  AVRational frame_rate;

  double black_duration;
  double non_black_duration;

  int black_frame_counter;
  int non_black_frame_counter;
  int black_frame_count_threshold;
  int non_black_frame_count_threshold;
  int black;

} codec_t;

static void do_init(codec_t *this, AVFrame *frame);

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  codec_t *this = context->priv_data;

  do_init(this, frame);

  uint8_t *p = frame->data[0];
  int i, j;
  int pblack = 0;
  int nblack = 0;
  int is_black;

  for (i = 0; i < frame->height; i++)
    {
      for (j = 0; j < frame->width; j++)
	{
	  nblack += p[j] < this->threshold;
	}
      p += frame->linesize[0];
    }

  pblack = nblack * 100 / (frame->width * frame->height);

  is_black = pblack >= this->percentage_below_threshold;

  if (is_black)
    {
      this->black_frame_counter++;

      if (this->black_frame_counter > this->black_frame_count_threshold)
	{
	  this->black_frame_counter = 0;
	  this->non_black_frame_counter = 0;
	  this->black = 1;
	}
    }
  else
    {
      this->non_black_frame_counter++;

      if (this->non_black_frame_counter > this->non_black_frame_count_threshold)
	{
	  this->black_frame_counter = 0;
	  this->non_black_frame_counter = 0;
	  this->black = 0;
	}
    }

  AVFrameSideData *side_data = av_frame_get_side_data(frame, FRAME_INFO_SIDE_DATA_TYPE);

  ((frame_info *)side_data->data)->flags |= (this->black ? BLACK : 0);

  send_to_graph(context, frame, timebase);
}

static void flush(ID3ASFilterContext *context)
{
  flush_graph(context);
}

static void do_init(codec_t *this, AVFrame *frame)
{
  if (!this->initialised)
    {
      this->black_frame_count_threshold = (this->black_duration * this->frame_rate.num) / this->frame_rate.den;
      this->non_black_frame_count_threshold = (this->non_black_duration * this->frame_rate.num) / this->frame_rate.den;

      this->initialised = 1;
    }
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options)
{
  codec_t *this = context->priv_data;

  this->black_frame_counter = 0;
  this->non_black_frame_counter = 0;
  this->black = 1;
  this->initialised = 0;
}

#define OFFSET(x) offsetof(codec_t, x)
static const AVOption options[] = {
  { "percentage_below_threshold", "Percentage of the pixels that have to be below the threshold for the frame to be considered black.", offsetof(codec_t, percentage_below_threshold), AV_OPT_TYPE_INT, { .i64 = 98 }, 0, 100 },
  { "threshold", "threshold below which a pixel value is considered black", offsetof(codec_t, threshold), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, INT_MAX },
  { "black_duration", "set black duration", OFFSET(black_duration), AV_OPT_TYPE_DOUBLE, {.dbl = 2.25}, 0, 24*60*60 },
  { "non_black_duration", "set non-black duration", OFFSET(non_black_duration), AV_OPT_TYPE_DOUBLE, {.dbl = 0.025}, 0, 24*60*60 },
  {"frame_rate", "frame rate", OFFSET(frame_rate), AV_OPT_TYPE_RATIONAL, {.dbl = 0}, INT_MIN, INT_MAX},

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
