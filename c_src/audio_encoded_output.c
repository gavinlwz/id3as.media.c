#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  enum AVSampleFormat sample_format;
  int sample_rate;
  int channel_layout;
  char *codec_name;
  char *pin_name;
  int stream_id;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;

  int current_sample_offset;
  int operating_buffer_size_in_samples;
  int operating_buffer_size_in_bytes;
  unsigned char *operating_buffer[AV_NUM_DATA_POINTERS];
  int64_t operating_timestamp;

  int resample_size;
  unsigned char *resample_buffer;
  int output_size;
  unsigned char *output_buf;

  int using_libfdk;
  int libfdk_bad_frame_counter;

} codec_t;

static int should_send(codec_t *this, AVFrame *frame);

static void resize_buffer(codec_t *this, int nb_samples)
{
  if (this->operating_buffer_size_in_samples - this->current_sample_offset < nb_samples)
    {
      unsigned char *tmp[AV_NUM_DATA_POINTERS];

      for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
	{
	  tmp[i] = this->operating_buffer[i];
	}

      av_samples_alloc(this->operating_buffer, &this->operating_buffer_size_in_bytes,
		       this->context->channels, this->operating_buffer_size_in_samples + nb_samples,
		       this->sample_format, 1);

      av_samples_copy(this->operating_buffer, tmp, 0, 0, this->current_sample_offset,
		      this->context->channels, this->sample_format);

      // Memory leak here - how to free the samples in tmp?

      this->operating_buffer_size_in_samples = this->operating_buffer_size_in_samples + nb_samples;
    }
}

static void copy_frame_to_operating_buffer(codec_t *this, AVFrame *frame) 
{
  resize_buffer(this, frame->nb_samples);

  av_samples_copy(this->operating_buffer, frame->data, this->current_sample_offset, 0,
		  frame->nb_samples, this->context->channels, this->sample_format);

  this->operating_timestamp = frame->pts - (90000 * this->current_sample_offset / this->sample_rate);
  /*
  if (this->current_sample_offset == 0)
    {
      TRACE("SYNC POINT");
      this->operating_timestamp = frame->pts;
    }
  */
  this->current_sample_offset += frame->nb_samples;
}

static void process(ID3ASFilterContext *context, AVFrame *frame)
{
  codec_t *this = context->priv_data;
  AVPacket pkt;
  int got_packet_ptr;
  int ret = 0;
  int samples_used = 0;

  if (frame->format != this->context->sample_fmt)
    {
      ERRORFMT("Input frame sample format %d does not match required codec format %d\n", frame->format, this->context->sample_fmt);
      exit(-1);
    }

  copy_frame_to_operating_buffer(this, frame);

  while (this->current_sample_offset - samples_used >= this->context->frame_size) {
    
    av_init_packet(&pkt);
    pkt.data = this->output_buf;
    pkt.size = this->output_size;

    av_samples_copy(this->frame->data, this->operating_buffer, 0, samples_used,
		    this->context->frame_size, this->context->channels, this->sample_format);

    this->frame->pts = this->operating_timestamp;
    this->frame->nb_samples = this->context->frame_size;

    ret = avcodec_encode_audio2(this->context, &pkt, this->frame, &got_packet_ptr);

    if (ret != 0)
      {
	ERRORFMT("avcodec_encode_audio2 failed with %d for codec %s", ret, this->context->codec->name);
	exit(-1);
      }
    else
      {
	if (got_packet_ptr && should_send(this, frame))
	  {
	    pkt.duration = av_rescale_q(pkt.duration, this->context->time_base, (AVRational) {1, 90000});

	    write_output_from_packet(this->pin_name, this->stream_id, this->context, &pkt);
	  }
      }

    samples_used += this->context->frame_size;

    int time_delta = 90000 * this->frame->nb_samples / this->sample_rate;

    this->operating_timestamp += time_delta;
  }

  av_samples_copy(this->operating_buffer, this->operating_buffer, 0, samples_used, this->current_sample_offset - samples_used,
		  this->context->channels, this->sample_format);

  this->current_sample_offset = this->current_sample_offset - samples_used;
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options)
{
  codec_t *this = context->priv_data;

  // Get the codec and context
  this->codec = get_encoder(this->codec_name);
  this->context = allocate_audio_context(this->codec, this->sample_rate, this->channel_layout, this->sample_format, codec_options);

  // Get the frame
  this->frame = avcodec_alloc_frame();
  this->frame->nb_samples     = this->context->frame_size;
  this->frame->format         = this->context->sample_fmt;
  this->frame->channel_layout = this->context->channel_layout;

  av_samples_alloc(this->frame->data, &this->frame->linesize[0],
		   this->context->channels,
		   this->context->frame_size,
		   this->sample_format,
		   1);

  av_samples_alloc(this->operating_buffer, 
		   &this->operating_buffer_size_in_bytes,
		   this->context->channels,
		   this->context->frame_size,
		   this->sample_format,
		   1);

  this->operating_buffer_size_in_samples = this->context->frame_size;
  this->current_sample_offset = 0;
  this->operating_timestamp = 0;

  // And get output buffers for the results - we don't know how much, but this seems reasonable for audio...
  this->output_size = 32768;
  this->output_buf = av_malloc(this->output_size);

  if (strcmp(this->codec_name, "libfdk_aac") == 0) {
    this->using_libfdk = 1;
    this->libfdk_bad_frame_counter = 0;
  }
  else {
    this->using_libfdk = 0;
  }
}

static int should_send(codec_t *this, AVFrame *frame) 
{
  // libfdk seems to create 2 frames at the start of an encode with timestamps that are too small.  Rather
  // than try to handle them, we just drop them here...

  if (this->using_libfdk && this->libfdk_bad_frame_counter < 2) 
    {
      this->libfdk_bad_frame_counter++;
      return 0;
    } 

  return 1;
}


static const AVOption options[] = {
  { "stream_id", "The stream id for the output stream", offsetof(codec_t, stream_id), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pin_name", "The pin name for the output stream", offsetof(codec_t, pin_name), AV_OPT_TYPE_STRING },
  { "sample_rate", "the sample rate", offsetof(codec_t, sample_rate), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "sample_format", "the sample format", offsetof(codec_t, sample_format), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "channel_layout", "the number of channels", offsetof(codec_t, channel_layout), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "codec", "the codec name", offsetof(codec_t, codec_name), AV_OPT_TYPE_STRING },
  { NULL }
};

static const AVClass class = {
  .class_name = "audio resampler options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_output_encoded_audio_filter = {
  .name = "encoded audio output",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
