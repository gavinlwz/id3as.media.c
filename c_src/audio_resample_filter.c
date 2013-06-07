#include "id3as_libav.h"

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

typedef struct _codec_t
{
  AVClass *av_class;

  int input_sample_rate;
  int input_channel_layout;
  int input_sample_format;

  int output_sample_rate;
  int output_channel_layout;
  int output_sample_format;
  int output_num_channels;
  int output_bytes_per_sample;

  AVFrame *frame;
  int max_samples;

  AVAudioResampleContext *resample_context;

} codec_t;

static void process(ID3ASFilterContext *context, AVFrame *frame)
{
  codec_t *this = context->priv_data;

  int nb_samples = avresample_convert(this->resample_context,
				      this->frame->data, this->frame->linesize[0], this->max_samples, 
				      frame->data, frame->linesize[0], frame->nb_samples); 
  if (nb_samples < 0) { 
    ERRORFMT("avresample_convert failed with %d\n", nb_samples);
    exit(-1);
  } 

  this->frame->nb_samples = nb_samples;
  
  av_samples_get_buffer_size(&this->frame->linesize[0], 
			     this->output_num_channels,
			     nb_samples,
			     this->output_sample_format, 
			     0);

  this->frame->pts = frame->pts;
  this->frame->opaque = frame->opaque;

  send_to_graph(context, this->frame);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options)
{
  codec_t *this = context->priv_data;

  this->resample_context = avresample_alloc_context();

  av_opt_set_int(this->resample_context, "in_channel_layout", this->input_channel_layout, 0);
  av_opt_set_int(this->resample_context, "out_channel_layout", this->output_channel_layout, 0);
  av_opt_set_int(this->resample_context, "in_sample_fmt", this->input_sample_format, 0);
  av_opt_set_int(this->resample_context, "out_sample_fmt", this->output_sample_format, 0);
  av_opt_set_int(this->resample_context, "in_sample_rate", this->input_sample_rate, 0);
  av_opt_set_int(this->resample_context, "out_sample_rate", this->output_sample_rate, 0);

  avresample_open(this->resample_context);

  // Get the frame
  this->frame = avcodec_alloc_frame();
  this->frame->format = this->output_sample_format;
  this->frame->channel_layout = this->output_channel_layout;
  this->frame->linesize[0] = MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE;
  for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
    {
      this->frame->data[i] = av_malloc(this->frame->linesize[0]);
    }
  
  this->output_num_channels = av_get_channel_layout_nb_channels(this->output_channel_layout);
  this->output_bytes_per_sample = av_get_bytes_per_sample(this->frame->format);

  this->max_samples = this->frame->linesize[0] / 
    (av_get_channel_layout_nb_channels(this->output_channel_layout) * av_get_bytes_per_sample(this->frame->format));
}

static const AVOption options[] = {
  { "input_sample_rate", "the input sample rate", offsetof(codec_t, input_sample_rate), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "input_channel_layout", "the input channel layout", offsetof(codec_t, input_channel_layout), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "input_sample_format", "the input sample format", offsetof(codec_t, input_sample_format), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "output_sample_rate", "the output sample rate", offsetof(codec_t, output_sample_rate), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "output_channel_layout", "the output channel layout", offsetof(codec_t, output_channel_layout), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "output_sample_format", "the output sample format", offsetof(codec_t, output_sample_format), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { NULL }
};

static const AVClass class = {
  .class_name = "audio resampler options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_resample_audio_filter = {
  .name = "audio resampler",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
