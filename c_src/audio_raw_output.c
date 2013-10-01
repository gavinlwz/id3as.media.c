#include "id3as_libav.h"

typedef struct _codec_t
{
  AVClass *av_class;
  char *pin_name;
  int stream_id;

} codec_t;

static void process(ID3ASFilterContext *context, AVFrame *frame)
{
  codec_t *this = context->priv_data;

  write_output_from_frame(this->pin_name, this->stream_id, frame);
}

static void flush(ID3ASFilterContext *context) 
{
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options)
{
}

static const AVOption options[] = {
  { "stream_id", "The stream id for the output stream", offsetof(codec_t, stream_id), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pin_name", "The pin name for the output stream", offsetof(codec_t, pin_name), AV_OPT_TYPE_STRING },
  { NULL }
};

static const AVClass class = {
  .class_name = "audio resampler options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_output_raw_audio_filter = {
  .name = "raw audio output",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
