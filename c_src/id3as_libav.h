#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ei.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libavresample/avresample.h>
#include "i_port.h" 

#define SUBSYSTEM "id3as_codecs"
#define PACKET_SIZE 4

#define NINETY_KHZ (AVRational){1, 90000}

#define FRAMES_TO_BYTES(frames, sample_format, num_channels) (frames) * (av_get_bytes_per_sample(sample_format)) * (num_channels)
#define BYTES_TO_FRAMES(bytes, sample_format, num_channels) (bytes) / (av_get_bytes_per_sample(sample_format)) / (num_channels)

typedef struct _ID3ASFilterContext ID3ASFilterContext;
typedef struct _ID3ASFilter ID3ASFilter;
typedef struct _sized_buffer sized_buffer;

struct _ID3ASFilterContext
{
  ID3ASFilter *filter;
  ID3ASFilterContext** downstream_filters;
  int num_downstream_filters;
  void *priv_data;
};

struct _ID3ASFilter
{
  char *name;
  void (*execute)();
  void (*init)(ID3ASFilterContext *context, AVDictionary *codec_options);
  int priv_data_size;
  const AVClass *priv_class;

  ID3ASFilter *next;
};

struct _sized_buffer
{
  void *data;
  int size;
};

extern int sync_mode;

//******************************************************************************
// Utility functions
//******************************************************************************
void id3as_filters_register_all();
ID3ASFilter *find_filter(char *name);
ID3ASFilterContext *allocate_instance(ID3ASFilter *filter, 
				      AVDictionary *options, 
				      AVDictionary *codec_options, 
				      ID3ASFilterContext **downstream_filters, 
				      int num_downstream_filters);

void send_to_graph(ID3ASFilterContext *processor, AVFrame *frame, AVRational timebase);

void set_packet_metadata(AVPacket *pkt, unsigned char *metadata);
void set_frame_metadata(AVFrame *frame, unsigned char *metadata);

void write_done();
void write_output_from_frame(char *pin_name, int stream_id, AVFrame *frame);
void write_output_from_packet(char *pin_name, int stream_id, AVCodecContext *codec_context, AVPacket *pkt, sized_buffer *opaque);

AVCodec *get_encoder(char *codec_name);
AVCodec *get_decoder(char *codec_name);
AVCodecContext *allocate_audio_context(AVCodec *codec, int sample_rate, int channel_layout, enum AVSampleFormat sample_format, AVDictionary *codec_options);
AVCodecContext *allocate_video_context(AVCodec *codec, int width, int height, enum PixelFormat pixfmt, AVDictionary *codec_options);


