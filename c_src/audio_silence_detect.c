#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  int initialised;

  double noise_threshold;
  
  double silence_duration;
  double noise_duration;

  int silence_counter;
  int noise_counter;
  int silence_sample_count_threshold;
  int noise_sample_count_threshold;
  int silent;
  
  double (*calc_avg)(AVFrame *insamples, int nb_samples);

} codec_t;

static void do_init(codec_t *this, AVFrame *frame);

#define CALC_AVG(name, type)						\
  static double calc_avg_##name(AVFrame *insamples, int nb_samples)	\
  {									\
  const type *p = (const type *)insamples->data[0];			\
  int i;								\
  type sum = 0;								\
									\
  for (i = 0; i < nb_samples; i++, p++)					\
    if (*p < 0)								\
      sum -= *p++;							\
    else								\
      sum += *p++;							\
									\
  return (sum / nb_samples);						\
  }

CALC_AVG(dbl, double)
CALC_AVG(flt, float)
CALC_AVG(s32, int32_t)
CALC_AVG(s16, int16_t)

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  codec_t *this = context->priv_data;

  do_init(this, frame);

  const int nb_channels = av_get_channel_layout_nb_channels(frame->channel_layout);
  const int nb_samples = frame->nb_samples * nb_channels;
  double level;
  int is_silence;

  level = this->calc_avg(frame, nb_samples);

  is_silence = level < this->noise_threshold;

  if (is_silence) 
    {
      this->silence_counter += nb_samples;

      if (this->silence_counter > this->silence_sample_count_threshold) 
	{
	  this->silence_counter = 0;
	  this->noise_counter = 0;
	  this->silent = 1;
	}
    }
  else 
    {
      this->noise_counter += nb_samples;

      if (this->noise_counter > this->noise_sample_count_threshold)
	{
	  this->noise_counter = 0;
	  this->silence_counter = 0;
	  this->silent = 0;
	}
    }

  ((frame_info *) frame->opaque)->flags |= (this->silent ? SILENT : 0);

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
      switch (frame->format) {
      case AV_SAMPLE_FMT_DBL: 
	this->calc_avg = calc_avg_dbl; 
	break;
      case AV_SAMPLE_FMT_FLT: 
	this->calc_avg = calc_avg_flt; 
	break;
      case AV_SAMPLE_FMT_FLTP: 
	this->calc_avg = calc_avg_flt; 
	break;
      case AV_SAMPLE_FMT_S32:
        this->noise_threshold *= INT32_MAX;
        this->calc_avg = calc_avg_s32;
        break;
      case AV_SAMPLE_FMT_S16:
        this->noise_threshold *= INT16_MAX;
        this->calc_avg = calc_avg_s16;
        break;
      default:
	ERRORFMT("Silence detect unable to handle format %d\n", frame->format);
	exit(1);
      }

      const int nb_channels = av_get_channel_layout_nb_channels(frame->channel_layout);
      const int sample_rate = frame->sample_rate;

      this->silence_sample_count_threshold = this->silence_duration * sample_rate * nb_channels;
      this->noise_sample_count_threshold = this->noise_duration * sample_rate * nb_channels;

      this->initialised = 1;
    }
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->initialised = 0;
  this->silence_counter = 0;
  this->noise_counter = 0;
  this->silent = 1;
}

#define OFFSET(x) offsetof(codec_t, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
    { "noise", "set noise threshold", OFFSET(noise_threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.001}, 0, 1 },
    { "silence_duration", "set silence duration", OFFSET(silence_duration), AV_OPT_TYPE_DOUBLE, {.dbl = 2.25}, 0, 24*60*60 },
    { "noise_duration", "set noise duration", OFFSET(noise_duration), AV_OPT_TYPE_DOUBLE, {.dbl = 0.025}, 0, 24*60*60 },
    { NULL }
};


static const AVClass class = {
  .class_name = "silence detect options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_silence_detect_filter = {
  .name = "silence detect",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
