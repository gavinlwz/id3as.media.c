#include "id3as_libav.h"

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

enum SplitMode {
  LEFT_ONLY,
  RIGHT_ONLY,
  LEFT_RIGHT
};

typedef struct _codec_t
{
  AVClass *av_class;

  enum AVSampleFormat sample_format;
  int sample_rate;
  int channel_layout;
  int num_channels;
  int bytes_per_sample;
  char *split_mode;
  enum SplitMode split_mode_enum;

  void (*convert_fun)(AVFrame *src, AVFrame *left, AVFrame *right);

  AVFrame *left_frame;
  AVFrame *right_frame;

} codec_t;

static void split_s16p(AVFrame *src, AVFrame *left, AVFrame *right);
static void split_fltp(AVFrame *src, AVFrame *left, AVFrame *right);

static void process(ID3ASFilterContext *context, AVFrame *frame)
{
  codec_t *this = context->priv_data;

  this->convert_fun(frame, this->left_frame, this->right_frame);

  this->left_frame->opaque = frame->opaque;
  this->right_frame->opaque = frame->opaque;

  switch (this->split_mode_enum) 
    {
    case LEFT_ONLY:
      for (int i = 0; i < context->num_downstream_filters; i++) {
	context->downstream_filters[i]->filter->execute(context->downstream_filters[i], this->left_frame);
      }
      break;
    case RIGHT_ONLY:
      for (int i = 0; i < context->num_downstream_filters; i++) {
	context->downstream_filters[i]->filter->execute(context->downstream_filters[i], this->right_frame);
      }
      break;
    case LEFT_RIGHT:
      for (int i = 0; i < context->num_downstream_filters; i += 2) {
	context->downstream_filters[i]->filter->execute(context->downstream_filters[i], this->left_frame);
	context->downstream_filters[i + 1]->filter->execute(context->downstream_filters[i + 1], this->right_frame);
      }
      break;
    }
}


static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = (codec_t *) context->priv_data;

  this->num_channels = av_get_channel_layout_nb_channels(this->channel_layout);
  this->bytes_per_sample = this->num_channels * av_get_bytes_per_sample(this->sample_format);

  this->left_frame = avcodec_alloc_frame();
  this->left_frame->format = this->sample_format;
  this->left_frame->channel_layout = AV_CH_LAYOUT_MONO;
  this->left_frame->linesize[0] = MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE;
  for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
    {
      this->left_frame->data[i] = av_malloc(this->left_frame->linesize[0]);
    }

  this->right_frame = avcodec_alloc_frame();
  this->right_frame->format = this->sample_format;
  this->right_frame->channel_layout = AV_CH_LAYOUT_MONO;
  this->right_frame->linesize[0] = MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE;
  for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
    {
      this->right_frame->data[i] = av_malloc(this->right_frame->linesize[0]);
    }

  if (strcmp(this->split_mode, "left_only") == 0) {
    this->split_mode_enum = LEFT_ONLY;
  } 
  else if (strcmp(this->split_mode, "right_only") == 0) {
    this->split_mode_enum = RIGHT_ONLY;
  }
  else if (strcmp(this->split_mode, "left_right") == 0) {
    this->split_mode_enum = LEFT_RIGHT;
  } 
  else {
    ERRORFMT("Invalid split mode %s\n", this->split_mode);
    exit(1);
  }

  switch (this->sample_format) 
    {
    case AV_SAMPLE_FMT_S16P:
      this->convert_fun = split_s16p;
      break;

    case AV_SAMPLE_FMT_FLTP:
      this->convert_fun = split_fltp;
      break;

    default:
      ERRORFMT("Unsupported format for stereo split: %d\n", this->sample_format);
      exit(1);
      break;
    }
}

static void split_s16p(AVFrame *src, AVFrame *left, AVFrame *right) {
}

static void split_fltp(AVFrame *src, AVFrame *left, AVFrame *right) {
  left->data[0] = src->data[0];
  left->linesize[0] = src->linesize[0];
  left->nb_samples = src->nb_samples;
  left->pts = src->pts;
  left->pkt_pts = src->pkt_pts;
  left->pkt_dts = src->pkt_dts;

  right->data[0] = src->data[1];
  right->linesize[0] = src->linesize[1];
  right->nb_samples = src->nb_samples;
  right->pts = src->pts;
  right->pkt_pts = src->pkt_pts;
  right->pkt_dts = src->pkt_dts;
}

static const AVOption options[] = {
  { "sample_rate", "sample rate", offsetof(codec_t, sample_rate), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "sample_format", "the sample format", offsetof(codec_t, sample_format), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "channel_layout", "channel layout", offsetof(codec_t, channel_layout), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "split_mode", "split mode", offsetof(codec_t, split_mode), AV_OPT_TYPE_STRING },
  { NULL },
};

static const AVClass class = {
  .class_name = "stero splitter options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_stereo_splitter_filter = {
  .name = "stereo splitter",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
