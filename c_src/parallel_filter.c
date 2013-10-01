#include "id3as_libav.h"
#include <pthread.h>

struct _codec_t;

static int thread_id;

typedef struct _thread_struct_t
{
  int thread_id;
  pthread_t thread;
  pthread_cond_t trigger;
  pthread_mutex_t trigger_mutex;
  pthread_cond_t complete;
  pthread_mutex_t complete_mutex;
  struct _codec_t *codec_t;
  ID3ASFilterContext *context;
  ID3ASFilterContext *downstream_filter;

} thread_struct;

typedef struct _codec_t
{
  AVClass *av_class;
  int pass_through;
  thread_struct *threads;
  AVFrame *inbound_frame;
  AVRational inbound_timebase;

} codec_t;

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  codec_t *this = context->priv_data;

  if (this->pass_through) {
    send_to_graph(context, frame, timebase);
  }
  else {

    this->inbound_frame = frame;
    this->inbound_timebase = timebase;

    for (int i = 0; i < context->num_downstream_filters; i++) {
      pthread_mutex_lock(&this->threads[i].trigger_mutex);
      pthread_cond_signal(&this->threads[i].trigger);
      pthread_mutex_unlock(&this->threads[i].trigger_mutex);
    }

    for (int i = 0; i < context->num_downstream_filters; i++) {
      pthread_cond_wait(&this->threads[i].complete, &this->threads[i].complete_mutex);
    }
  }
}

static void flush(ID3ASFilterContext *context) 
{
  flush_graph(context);
}

static void *thread_proc(void *data) 
{
  thread_struct *this = (thread_struct *) data;

  pthread_mutex_lock(&this->trigger_mutex);

  do {
    pthread_cond_wait(&this->trigger, &this->trigger_mutex);

    this->downstream_filter->filter->execute(this->downstream_filter, this->codec_t->inbound_frame, this->codec_t->inbound_timebase);

    pthread_mutex_lock(&this->complete_mutex);
    pthread_cond_signal(&this->complete);
    pthread_mutex_unlock(&this->complete_mutex);

  } while(1);

  return 0;
}

static void init(ID3ASFilterContext *context, AVDictionary *codec_options) 
{
  codec_t *this = context->priv_data;

  if (context->num_downstream_filters < 2) {
    this->pass_through = 1;
  }
  else {
    this->pass_through = 0;
    this->threads = (thread_struct *) malloc(sizeof(thread_struct) * context->num_downstream_filters);


    for (int i = 0; i < context->num_downstream_filters; i++)
      {
	this->threads[i].thread_id = thread_id++;
	this->threads[i].context = context;
	this->threads[i].downstream_filter = context->downstream_filters[i];
	this->threads[i].codec_t = this;
	pthread_cond_init(&this->threads[i].trigger, NULL);
	pthread_mutex_init(&this->threads[i].trigger_mutex, NULL);
	pthread_cond_init(&this->threads[i].complete, NULL);
	pthread_mutex_init(&this->threads[i].complete_mutex, NULL);

	pthread_mutex_lock(&this->threads[i].complete_mutex);

	pthread_create(&this->threads[i].thread, NULL, &thread_proc, &this->threads[i]);
      }

  }
}

static const AVOption options[] = {
  { NULL },
};

static const AVClass class = {
  .class_name = "parallel options",
  .item_name  = av_default_item_name,
  .option     = options,
  .version    = LIBAVUTIL_VERSION_INT,
};

ID3ASFilter id3as_parallel_filter = {
  .name = "parallel",
  .init = init,
  .execute = process,
  .flush = flush,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};
