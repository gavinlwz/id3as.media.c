#include "id3as_libav.h"
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

typedef struct _codec_t {
  AVClass *av_class;
  int initialised;
  
  AVRational time_base;
  char *filter_graph_desc;
  
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;
  AVFrame *output_frame;
  
} codec_t;

static void do_init(codec_t *this, AVFrame *frame);

static void process(ID3ASFilterContext *context, AVFrame *frame)
{
  codec_t *this = context->priv_data;
  int ret;

  do_init(this, frame);

  if (av_buffersrc_add_frame(this->buffersrc_ctx, frame) < 0) {
    ERROR("Error while feeding the filtergraph\n");
    exit(-1);
  }

  while (1) {
    ret = av_buffersink_get_frame(this->buffersink_ctx, this->output_frame);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;

    if (ret < 0) {
      ERROR("Error from get_frame");
      exit(-1);
    }

    send_to_graph(context, this->output_frame);
  }
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;
  int ret;

  this->initialised = 0;
  this->time_base = NINETY_KHZ;
  this->filter_graph = avfilter_graph_alloc();
  this->output_frame = av_frame_alloc();

  ret = avfilter_graph_create_filter(&this->buffersink_ctx, avfilter_get_by_name("buffersink"), "out", NULL, NULL, this->filter_graph);

  if (ret < 0) {
    ERROR("Cannot create buffer sink\n");
    exit(-1);
  }
}

static void do_init(codec_t *this, AVFrame *frame) 
{
  if (this->initialised) {
    return;
  }

  char args[512];
  int ret;
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *inputs  = avfilter_inout_alloc();

  snprintf(args, sizeof(args),
	   "%d:%d:%d:%d:%d:%d:%d",
	   //"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
	   frame->width, frame->height, frame->format,
	   this->time_base.num, this->time_base.den,
	   frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);
  TRACEFMT("ARG ARE %s, GRAPH IS %s", args, this->filter_graph_desc),
  ret = avfilter_graph_create_filter(&this->buffersrc_ctx, avfilter_get_by_name("buffer"), "in", args, NULL, this->filter_graph);

  if (ret < 0) {
    ERROR("Cannot create buffer source\n");
    exit(-1);
  }

  /* Endpoints for the filter graph. */
  outputs->name       = av_strdup("in");
  outputs->filter_ctx = this->buffersrc_ctx;
  outputs->pad_idx    = 0;
  outputs->next       = NULL;

  inputs->name       = av_strdup("out");
  inputs->filter_ctx = this->buffersink_ctx;
  inputs->pad_idx    = 0;
  inputs->next       = NULL;

  if ((ret = avfilter_graph_parse(this->filter_graph, this->filter_graph_desc, inputs, outputs, NULL)) < 0)
    {
      ERROR("Failed to parse graph");
      exit(-1);
    }

  if ((ret = avfilter_graph_config(this->filter_graph, NULL)) < 0) {
    ERROR("avfilter_graph_config failed");
    exit(-1);
  }

  this->initialised = 1;
}

static const AVOption options[] = {
  { "graph", "The filter graph description", offsetof(codec_t, filter_graph_desc), AV_OPT_TYPE_STRING },
  { NULL }
};

static const AVClass class = {
  .class_name = "effects processor options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_effects_processor_filter = {
  .name = "effects processor",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
