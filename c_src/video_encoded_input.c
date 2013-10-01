#include "id3as_libav.h"

typedef struct _item 
{
  int64_t pts;
  sized_buffer opaque;
  struct _item *next;
} item;

typedef struct _codec_t
{
  AVClass *av_class;

  AVCodec *codec;
  AVCodecContext *context;
  AVFrame *frame;
  item *inbound_list_head;
  item *inbound_list_tail;

  int width;
  int height;
  char *codec_name;
  enum PixelFormat pixfmt;

} codec_t;

static void queue_opaque(codec_t *this, unsigned char *opaque, unsigned int opaque_size, int64_t pts) 
{
  item *list_entry = (item *) malloc(sizeof(item));
  list_entry->pts = pts;
  list_entry->opaque.data = malloc(opaque_size);
  list_entry->opaque.size = opaque_size;
  list_entry->next = NULL;
  memcpy(list_entry->opaque.data, opaque, opaque_size);

  if (this->inbound_list_head) {
    this->inbound_list_tail->next = list_entry;
  }
  else {
    this->inbound_list_head = list_entry;
  }

  this->inbound_list_tail = list_entry;
}

static item *get_opaque(codec_t *this, int64_t pts) 
{
  item *prev = NULL;
  item *current = this->inbound_list_head;

  while (current) 
    {
      if (current->pts == pts) 
	{
	  if (this->inbound_list_head == current) {
	    this->inbound_list_head = current->next;
	  } 
	  else {
	    prev->next = current->next;
	  }
	  
	  if (this->inbound_list_tail == current) {
	    this->inbound_list_tail = prev;
	  }

	  return current;
	}

      prev = current;
      current = current->next;
    }

  return NULL;
}

int decode(ID3ASFilterContext *context, AVPacket *pkt) 
{
  codec_t *this = context->priv_data;
  int got_frame;
  int len = avcodec_decode_video2(this->context, this->frame, &got_frame, pkt);
  
  if (len < 0)
    {
      TRACEFMT("avcodec_decode_video2 failed with %d", len);
      got_frame = 0;
      //exit(-1);
    }
  else if (got_frame && len > 0)
    {
      item *item = get_opaque(this, this->frame->pkt_pts);
      
      if (item == NULL) {
	ERRORFMT("Failed to find opaque for %ld", this->frame->pkt_pts);
	exit(-2);
      }
      
      sized_buffer o = item->opaque;
      
      this->frame->opaque = &o; // TODO - should be &(item->opaque);
      this->frame->pts = this->frame->pkt_pts;
      
      send_to_graph(context, this->frame, NINETY_KHZ);
      
      free(item->opaque.data);
      free(item);
      
      av_frame_unref(this->frame);
    }

  return got_frame;
}

static void process(ID3ASFilterContext *context,
		    unsigned char *metadata, unsigned int metadata_size, 
		    unsigned char *opaque, unsigned int opaque_size, 
		    unsigned char *data, unsigned int data_size)
{
  codec_t *this = context->priv_data;
  AVPacket pkt;

  av_init_packet(&pkt);
  pkt.data = data;
  pkt.size = data_size;

  set_packet_metadata(&pkt, metadata);
  
  queue_opaque(this, opaque, opaque_size, pkt.pts);

  decode(context, &pkt);
}

static void flush(ID3ASFilterContext *context) 
{
  AVPacket pkt;

  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  while (1) 
    {
      if (!decode(context, &pkt)) {
	break;
      }
    }
  
  flush_graph(context);
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  this->codec = get_decoder(this->codec_name);

  this->context = allocate_video_context(this->codec, this->width, this->height, this->pixfmt, codec_options);

  this->frame = av_frame_alloc();
  this->inbound_list_head = NULL;
  this->inbound_list_tail = NULL;
}

static const AVOption options[] = {
  { "width", "the width", offsetof(codec_t, width), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "height", "the height", offsetof(codec_t, height), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "pixel_format", "the pixel format", offsetof(codec_t, pixfmt), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX },
  { "codec", "the codec name", offsetof(codec_t, codec_name), AV_OPT_TYPE_STRING },
  { NULL }
};

static const AVClass class = {
  .class_name = "encoded video input options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_encoded_video_input = {
  .name = "encoded video input",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
