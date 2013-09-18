#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include <libavutil/mem.h>

#include "id3as_libav.h"
#include "i_port.h" 
#include "i_utils.h" 

typedef struct _metadata_t {

  enum AVMediaType type;

  char *pin_name;
  int stream_id;

  int64_t pts;
  int64_t dts;
  int duration;
  
  const char *codec_name;
  
  int sample_rate;
  char *sample_format_name;
  char *channel_layout_name;

  int keyframe;
  int width;
  int height;
  int interlaced;
  char *pixel_format_name;

  uint8_t *extradata;
  int extradata_size;


} metadata_t;

static void write_header(metadata_t *metadata);
static void write_data(char *data, int size);
static void resize_buffer(int bytes_required, char **output_buffer, int *buffer_size);
static char *get_pixel_format_name(enum PixelFormat pixel_format);
static char *get_sample_format_name(int sample_format);
static char *get_channel_layout_name(int channel_layout);
static int encode_header(char *output_buffer, metadata_t *metadata);
static void encode_audio_header(char *output_buffer, int *i, metadata_t *metadata);
static void encode_video_header(char *output_buffer, int *i, metadata_t *metadata);
static void encode_timestamp(char *output_buffer, int *i, int64_t timestamp);

static i_mutex_t mutex = INITIALISE_STATIC_MUTEX();

void send_to_graph(ID3ASFilterContext *this, AVFrame *frame, AVRational timebase)
{
  for (int i = 0; i < this->num_downstream_filters; i++)
    {
      this->downstream_filters[i]->filter->execute(this->downstream_filters[i], frame, timebase);
    }
}

AVCodec *get_encoder(char *codec_name)
{
  AVCodec *codec = NULL;

  codec = avcodec_find_encoder_by_name(codec_name);

  if (!codec) {
    ERRORFMT("codec not found %s", codec_name);
    exit(1);
  }

  return codec;
}

AVCodec *get_decoder(char *codec_name)
{
  AVCodec *codec = NULL;

  codec = avcodec_find_decoder_by_name(codec_name);

  if (!codec) {
    ERRORFMT("codec not found %s", codec_name);
    exit(1);
  }

  return codec;
}

static char *get_sample_format_name(int sample_format) 
{
  if (sample_format == AV_SAMPLE_FMT_U8)
    {
      return "u8";
    }
  else if (sample_format == AV_SAMPLE_FMT_S16)
    {
      return "s16";
    }
  else if (sample_format == AV_SAMPLE_FMT_S16P)
    {
      return "s16p";
    }
  else if (sample_format == AV_SAMPLE_FMT_FLT)
    {
      return "flt";
    }
  else if (sample_format == AV_SAMPLE_FMT_FLTP)
    {
      return "fltp";
    }

  ERRORFMT("Invalid sample format %d", sample_format);
  exit(1);
}

static char *get_channel_layout_name(int channel_layout)
{
  switch (channel_layout) {
  case AV_CH_LAYOUT_MONO:
    return "mono";
  case AV_CH_LAYOUT_STEREO:
    return "stereo";
  default:
    ERRORFMT("Invalid channel layout %d", channel_layout);
    exit(1);
  }
}

static char * get_pixel_format_name(enum PixelFormat pixel_format) 
{
  if (pixel_format == PIX_FMT_YUV420P)
    {
      return "yuv420p";
    }
  else if (pixel_format == PIX_FMT_BGR24)
    {
      return "bgr24";
    }
  else if (pixel_format == AV_PIX_FMT_YUVJ420P) 
    {
      return "yuvj420p";
    }

  ERRORFMT("unknown pixel format %d", pixel_format);
  exit(1);
}

AVCodecContext *allocate_audio_context(AVCodec *codec, int sample_rate, int channel_layout, enum AVSampleFormat sample_format, AVDictionary *codec_options) 
{
  AVCodecContext *c = avcodec_alloc_context3(codec);

  c->sample_fmt = sample_format;
  c->sample_rate = sample_rate;
  c->channel_layout = channel_layout;
  c->channels = av_get_channel_layout_nb_channels(channel_layout);

  int rc = avcodec_open2(c, codec, &codec_options); 
  if (rc < 0) {
    ERRORFMT("could not open codec %s - rc = %d", codec->name, rc);
    exit(1);
  }

  if (av_dict_count(codec_options) != 0)
    {
      TRACE("Unused options:");

      AVDictionaryEntry *t = NULL;
      while ((t = av_dict_get(codec_options, "", t, AV_DICT_IGNORE_SUFFIX))) 
	{
	  TRACEFMT("Unused key: %s\n", t->key);
	}
      exit(-1);
    }

  return c;
}

AVCodecContext *allocate_video_context(AVCodec *codec, int width, int height, enum PixelFormat pixfmt, AVDictionary *codec_options) 
{
  AVCodecContext *c = avcodec_alloc_context3(codec);

  c->pix_fmt = pixfmt;
  c->width = width;
  c->height = height;

  if (avcodec_open2(c, codec, &codec_options) < 0) {
    ERROR("could not open codec");
    exit(1);
  }

  if (av_dict_count(codec_options) != 0)
    {
      TRACE("Unused options:");

      AVDictionaryEntry *t = NULL;
      while ((t = av_dict_get(codec_options, "", t, AV_DICT_IGNORE_SUFFIX))) 
	{
	  TRACEFMT("Unused key: %s\n", t->key);
	}
      exit(-1);
    }

  return c;
}

void set_packet_metadata(AVPacket *pkt, unsigned char *metadata) 
{
  char *buf = (char *) metadata;
  int index = 0;
  int version;
  int arity;

  // Packets have PTS / DTS
  ei_decode_version(buf, &index, &version);
  ei_decode_tuple_header(buf, &index, &arity);
  I_DECODE_LONGLONG(buf, &index, &pkt->pts);
  I_DECODE_LONGLONG(buf, &index, &pkt->dts);
}

void set_frame_metadata(AVFrame *frame, unsigned char *metadata) 
{
  char *buf = (char *) metadata;
  int index = 0;
  int version;
  int arity;

  // Frames just have PTS
  ei_decode_version(buf, &index, &version);
  ei_decode_tuple_header(buf, &index, &arity);
  I_DECODE_LONGLONG(buf, &index, &frame->pts);
}

void write_output_from_frame(char *pin_name, int stream_id, AVFrame *frame)
{
  i_mutex_lock(&mutex);

  metadata_t metadata = {
    .type = frame->pict_type == 0 ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO,
    .pin_name = pin_name,
    .stream_id = stream_id,
    .pts = frame->pts,
    .dts = frame->pts,
    .duration = 0,
    .codec_name = "raw",
    .extradata = NULL,
    .extradata_size = 0
  };

  switch (metadata.type) {

  case AVMEDIA_TYPE_VIDEO:
    metadata.keyframe = frame->key_frame;
    metadata.width = frame->width;
    metadata.height = frame->height;
    metadata.pixel_format_name = get_pixel_format_name(frame->format);
    break;
    
  case AVMEDIA_TYPE_AUDIO:
    metadata.sample_rate = frame->sample_rate;
    metadata.sample_format_name = get_sample_format_name(frame->format);
    metadata.channel_layout_name = get_channel_layout_name(frame->channel_layout);
    break;
    
  default:
    ERRORFMT("Unsupported media type %d\n", metadata.type);
    exit(-1);
  }

  write_header(&metadata);
  write_data((char *)frame->data[0], frame->linesize[0]);
  i_mutex_unlock(&mutex);
}

void write_output_from_packet(char *pin_name, int stream_id, AVCodecContext *codec_context, AVPacket *pkt, sized_buffer *opaque)
{
  i_mutex_lock(&mutex);

  metadata_t metadata = {
    .type = codec_context->codec_type,
    .pin_name = pin_name,
    .stream_id = stream_id,
    .pts = pkt->pts,
    .dts = pkt->dts,
    .duration = pkt->duration,
    .codec_name = codec_context->codec->name,
    .extradata = codec_context->extradata,
    .extradata_size = codec_context->extradata_size
  };

  switch (metadata.type) {

  case AVMEDIA_TYPE_VIDEO:
    metadata.keyframe = pkt->flags & AV_PKT_FLAG_KEY;
    metadata.width = codec_context->width;
    metadata.height = codec_context->height;
    metadata.interlaced = codec_context->flags & CODEC_FLAG_INTERLACED_DCT ? 1 : 0;
    metadata.pixel_format_name= get_pixel_format_name(codec_context->pix_fmt);
    break;
    
  case AVMEDIA_TYPE_AUDIO:
    metadata.sample_rate = codec_context->sample_rate;
    metadata.sample_format_name = get_sample_format_name(codec_context->sample_fmt);
    metadata.channel_layout_name = get_channel_layout_name(codec_context->channel_layout);
    break;
    
  default:
    ERRORFMT("Unsupported codec type %d\n", codec_context->codec_type);
    exit(-1);
  }

  write_header(&metadata);
  write_data((char *)opaque->data, opaque->size);
  write_data((char *)pkt->data, pkt->size);

  i_mutex_unlock(&mutex);
}

static void write_data(char *data, int size) 
{
  // TODO - check memory leak in port.c:read_port
  write_buffer_to_port(PACKET_SIZE, (unsigned char *)data, size);
}

static void write_header(metadata_t *metadata) 
{
  static char *output_buffer = NULL;
  static int buffer_size = 0;

  int bytes_required = encode_header(NULL, metadata);

  resize_buffer(bytes_required, &output_buffer, &buffer_size);

  encode_header(output_buffer, metadata);

  write_data(output_buffer, bytes_required);
}

static int encode_header(char *output_buffer, metadata_t *metadata)
{ 
  int i = 0;

  ei_encode_version(output_buffer, &i);
  
  ei_encode_tuple_header(output_buffer, &i, 3);
  ei_encode_atom(output_buffer, &i, "output_frame");
  ei_encode_atom(output_buffer, &i, metadata->pin_name);
  ei_encode_tuple_header(output_buffer, &i, 8);
  ei_encode_atom(output_buffer, &i, "frame");
  ei_encode_atom(output_buffer, &i, "undefined"); // info
  encode_timestamp(output_buffer, &i, metadata->pts); // pts
  encode_timestamp(output_buffer, &i, metadata->dts); // dts
  ei_encode_long(output_buffer, &i, metadata->duration);       // duration
  ei_encode_atom(output_buffer, &i, "undefined"); // flags
  ei_encode_atom(output_buffer, &i, "undefined"); // data

  switch (metadata->type) {
  case AVMEDIA_TYPE_VIDEO:
    encode_video_header(output_buffer, &i, metadata);
    break;
  case AVMEDIA_TYPE_AUDIO:
    encode_audio_header(output_buffer, &i, metadata);
    break;
  default:
    ERRORFMT("Unsupported codec type %d\n", metadata->type);
    exit(-1);
  }

  return i;
}

static void encode_audio_header(char *output_buffer, int *i, metadata_t *metadata)
{
  ei_encode_tuple_header(output_buffer, i, 6);
  ei_encode_atom(output_buffer, i, "audio_frame");
  ei_encode_atom(output_buffer, i, metadata->codec_name); // format
  ei_encode_long(output_buffer, i, metadata->sample_rate);  // sample_rate
  ei_encode_atom(output_buffer, i, metadata->sample_format_name); // sample_fmt
  ei_encode_atom(output_buffer, i, metadata->channel_layout_name); // channel_layout
  ei_encode_binary(output_buffer, i, metadata->extradata, metadata->extradata_size); // extradata
}



static void encode_video_header(char *output_buffer, int *i, metadata_t *metadata)
{
  ei_encode_tuple_header(output_buffer, i, 11);
  ei_encode_atom(output_buffer, i, "video_frame");
  ei_encode_atom(output_buffer, i, metadata->codec_name); // format
  ei_encode_atom(output_buffer, i, metadata->keyframe ? "iframe" : "bp_frame");  // frame_type
  ei_encode_atom(output_buffer, i, metadata->interlaced ? "true" : "false"); // interlaced
  ei_encode_long(output_buffer, i, metadata->width); // width
  ei_encode_long(output_buffer, i, metadata->height); // height
  ei_encode_atom(output_buffer, i, metadata->pixel_format_name); // pixel_format
  ei_encode_atom(output_buffer, i, "undefined"); // pixel_aspect_ratio
  ei_encode_atom(output_buffer, i, "undefined"); // display_aspect_ratio
  ei_encode_atom(output_buffer, i, "undefined"); // frame_rate
  ei_encode_binary(output_buffer, i, metadata->extradata, metadata->extradata_size); // extradata
}

static void encode_timestamp(char *output_buffer, int *i, int64_t timestamp) 
{
  ei_encode_long(output_buffer, i, timestamp);
}

static void resize_buffer(int bytes_required, char **output_buffer, int *buffer_size)
{
  if (bytes_required > *buffer_size) {
    if (*output_buffer) {
      free(*output_buffer);
    }
    *output_buffer = av_malloc(bytes_required);
    *buffer_size = bytes_required;
  }
}
