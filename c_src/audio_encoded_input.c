#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;

  enum AVSampleFormat sample_format;
  int sample_rate;
  int channel_layout;
  char *codec_name;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;

} codec_t;

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;
  AVPacket pkt;
  int got_frame;

  av_init_packet(&pkt);
  pkt.data = data;
  pkt.size = data_size;

  set_packet_metadata(&pkt, metadata);

  while (pkt.size > 0)
    {
      int len = avcodec_decode_audio4(this->context, this->frame, &got_frame, &pkt);

      if (len < 0) 
	{
	  TRACEFMT("avcodec_decode_audio4 failed with %d", len);
	  break;
	  //exit(-1);
	}
      else if (got_frame && len > 0)
	{
	  sized_buffer o = {.data = opaque, .size = opaque_size};

	  this->frame->pts = this->frame->pkt_pts;
	  this->frame->opaque = &o;

	  send_to_graph(context, this->frame, NINETY_KHZ);

	  pkt.size -= len;
	  pkt.data += len;

	  int time_delta = 90000 * this->frame->nb_samples / this->sample_rate;

	  pkt.pts += time_delta;
	  pkt.dts += time_delta;

	  av_frame_unref(this->frame);
	}

      // Note: the docs suggest (http://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga834bb1b062fbcc2de4cf7fb93f154a3e) that
      // you can get a non-negative return from avcodec_decode_audio4 and a got_frame flag of false, and that in this case
      // you should feed the next chunk of data to the decoder.  We don't do that, so this might be a bug.  But don't want to change
      // it until we find some input that allows it to be tested.
    }
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options)
{
  codec_t *this = context->priv_data;

  this->codec = get_decoder(this->codec_name);
  this->context = allocate_audio_context(this->codec, this->sample_rate, this->channel_layout, this->sample_format, codec_options);

  this->frame = avcodec_alloc_frame();
}

static const AVOption options[] = {
  { "sample_rate", "sample rate", offsetof(codec_t, sample_rate), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "sample_format", "the sample format", offsetof(codec_t, sample_format), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "channel_layout", "channel layout", offsetof(codec_t, channel_layout), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "codec", "The name of the codec", offsetof(codec_t, codec_name), AV_OPT_TYPE_STRING },
  { NULL },
};

static const AVClass class = {
  .class_name = "encoded audio input options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_encoded_audio_input = {
  .name = "encoded audio input",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
