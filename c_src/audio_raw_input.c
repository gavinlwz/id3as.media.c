#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  int sample_rate;
  int channel_layout;
  int num_channels;
  int sample_format;
  int bytes_per_sample;

  AVFrame *frame;

} codec_t;

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;

  this->frame->nb_samples = data_size / this->bytes_per_sample;
  set_frame_metadata(this->frame, metadata);

  if (avcodec_fill_audio_frame(this->frame, this->num_channels, this->sample_format, data, data_size, 1) < 0) 
    { 
      ERROR("Failed to fill audio frame"); 
      exit(-1); 
    } 

  frame_info *info = malloc(sizeof(frame_info) + opaque_size);
  info->flags = 0;
  info->buffer_size = opaque_size;
  memcpy(info->buffer, opaque, opaque_size);
  this->frame->opaque = info;

  send_to_graph(context, this->frame, NINETY_KHZ);

  free(info);
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = (codec_t *) context->priv_data;

  this->num_channels = av_get_channel_layout_nb_channels(this->channel_layout);
  this->bytes_per_sample = this->num_channels * av_get_bytes_per_sample(this->sample_format);

  this->frame = avcodec_alloc_frame();
  this->frame->format = this->sample_format;
  this->frame->channel_layout = this->channel_layout;
}

static const AVOption options[] = {
  { "sample_rate", "sample rate", offsetof(codec_t, sample_rate), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "channel_layout", "channel layout", offsetof(codec_t, channel_layout), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "sample_format", "The sample format", offsetof(codec_t, sample_format), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { NULL },
};

static const AVClass class = {
  .class_name = "raw audio input options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_raw_audio_input = {
  .name = "raw audio input",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
