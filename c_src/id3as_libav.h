#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ei.h>
#include <inttypes.h>
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
#define FRAME_INFO_SIDE_DATA_TYPE 99 // Must not match anything in libavutil/frame.h:AVFrameSideDataType

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
  void (*flush)();
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

typedef struct _frame_info_queue_item 
{
  int64_t pts;
  sized_buffer frame_info;
  struct _frame_info_queue_item *next;
} frame_info_queue_item;

typedef struct _frame_info_queue
{
  frame_info_queue_item *inbound_list_head;
  frame_info_queue_item *inbound_list_tail;
} frame_info_queue;

extern volatile int sync_mode;

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
void flush_graph(ID3ASFilterContext *this);

void set_packet_metadata(AVPacket *pkt, unsigned char *metadata);
void set_frame_metadata(AVFrame *frame, unsigned char *metadata);

void write_done(char *type);
void write_output_from_frame(char *pin_name, int stream_id, AVFrame *frame);
void write_output_from_packet(char *pin_name, int stream_id, AVCodecContext *codec_context, AVPacket *pkt, sized_buffer *frame_info);

AVCodec *get_encoder(char *codec_name);
AVCodec *get_decoder(char *codec_name);
AVCodecContext *allocate_audio_context(AVCodec *codec, int sample_rate, int channel_layout, enum AVSampleFormat sample_format, AVDictionary *codec_options);
AVCodecContext *allocate_video_context(AVCodec *codec, int width, int height, enum PixelFormat pixfmt, AVDictionary *codec_options);

void queue_frame_info_from_frame(frame_info_queue *queue, AVFrame *frame);
void queue_frame_info(frame_info_queue *queue, unsigned char *frame_info, unsigned int frame_info_size, int64_t pts);
void add_frame_info_to_frame(frame_info_queue *queue, AVFrame *frame);
void init_frame_info_queue(frame_info_queue *queue);
frame_info_queue_item *get_frame_info(frame_info_queue *queue, int64_t pts);
void free_frame_info(frame_info_queue_item *frame_info_queue_item);
