#include "id3as_libav.h"
#include "i_utils.h"
#include <pthread.h>

struct _codec_t;

#define INIT_QUEUE(QUEUE)\
  (QUEUE).head = NULL;\
	 (QUEUE).tail = NULL;\
	 INITIALISE_MUTEX(&(QUEUE).mutex);\
	 INITIALISE_COND(&(QUEUE).cond);\
	 (QUEUE).active = 1;

#define DESTROY_QUEUE(QUEUE, FUN)\
  i_mutex_lock(&(QUEUE).mutex);\
  (QUEUE).active = 0;\
  i_mutex_signal(&(QUEUE).cond);\
  while ((QUEUE).head != NULL)\
    {\
  queue_entry_t *t = (QUEUE).head;\
  (QUEUE).head = t->next;\
	 FUN((void *)t);\
    }\
  i_mutex_unlock(&(QUEUE).mutex);

#define ADD_TO_QUEUE(QUEUE, ITEM)\
  i_mutex_lock(&(QUEUE).mutex);\
  (ITEM)->queue_entry.next = NULL;\
  if ((QUEUE).head == NULL)\
    {\
  (QUEUE).head = &(ITEM)->queue_entry;\
    }\
  else\
    {\
  (QUEUE).tail->next = &(ITEM)->queue_entry;\
    }\
  (QUEUE).tail = &(ITEM)->queue_entry;\
  i_mutex_signal(&(QUEUE).cond);\
  i_mutex_unlock(&(QUEUE).mutex);

#define WAIT_FOR_QUEUE(QUEUE)\
  i_mutex_lock(&(QUEUE).mutex);\
  i_mutex_wait(&(QUEUE).cond, &(QUEUE).mutex);\
  i_mutex_unlock(&(QUEUE).mutex);

#define INACTIVE(QUEUE)\
  (!&(QUEUE).active)

#define REMOVE_FROM_QUEUE(QUEUE, ITEM)\
  {\
  i_mutex_lock(&(QUEUE).mutex);\
  if ((QUEUE).head == NULL)\
    {\
  ITEM = NULL;\
    }\
  else\
    {\
  queue_entry_t *t = (QUEUE).head;\
  if (t->next == NULL)\
    {\
  (QUEUE).head = NULL;\
  (QUEUE).tail = NULL;\
    }\
  else\
    {\
  (QUEUE).head = t->next;\
    }\
  ITEM = (void *)t;\
    }\
  i_mutex_unlock(&(QUEUE).mutex);\
  }

typedef struct _queue_entry_t
{
  struct _queue_entry_t *next;
  
} queue_entry_t;

typedef struct _queue_root_t
{
  i_mutex_t mutex;
  i_cond_t cond;
  int active;
  struct _queue_entry_t *head;
  struct _queue_entry_t *tail;
  
} queue_root_t;

typedef struct _frame_entry_t
{
  queue_entry_t queue_entry;
  AVFrame *frame;
  AVRational timebase;

} frame_entry_t;

static int thread_id;

typedef struct _thread_struct_t
{
  int thread_id;
  pthread_t thread;
  struct _codec_t *codec_t;
  ID3ASFilterContext *context;
  ID3ASFilterContext *downstream_filter;
  queue_root_t inbound_frame_queue;

} thread_struct;

typedef struct _codec_t
{
  AVClass *av_class;
  int pass_through;
  thread_struct *threads;

} codec_t;

static void process(ID3ASFilterContext *context, AVFrame *frame, AVRational timebase)
{
  codec_t *this = context->priv_data;

  if (this->pass_through) {
    send_to_graph(context, frame, timebase);
  }
  else {

    for (int i = 0; i < context->num_downstream_filters; i++) {

      frame_entry_t *frame_entry = (frame_entry_t *) malloc(sizeof(frame_entry_t));
      frame_entry->timebase = timebase;
      frame_entry->frame = av_frame_clone(frame);

      sized_buffer *opaque = (sized_buffer *) malloc(sizeof(sized_buffer));
      opaque->size = ((sized_buffer *) frame->opaque)->size;
      opaque->data = malloc(opaque->size);
      memcpy(opaque->data, ((sized_buffer *) frame->opaque)->data, opaque->size);
      frame_entry->frame->opaque = opaque;

      ADD_TO_QUEUE(this->threads[i].inbound_frame_queue, frame_entry);
    }
  }
}

static void *thread_proc(void *data) 
{
  thread_struct *this = (thread_struct *) data;

  do {
    WAIT_FOR_QUEUE(this->inbound_frame_queue);

    frame_entry_t *inbound;

    do {

      REMOVE_FROM_QUEUE(this->inbound_frame_queue, inbound);
     
      if (inbound != NULL) {
	this->downstream_filter->filter->execute(this->downstream_filter, inbound->frame, inbound->timebase);

	free(((sized_buffer *)inbound->frame->opaque)->data);
	free(inbound->frame->opaque);
	av_frame_free(&inbound->frame);
	free(inbound);
      }
    } while (inbound != NULL);

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
	INIT_QUEUE(this->threads[i].inbound_frame_queue);

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

ID3ASFilter id3as_async_parallel_filter = {
  .name = "async_parallel",
  .init = init,
  .execute = process,
  .priv_data_size = sizeof(codec_t),
  .priv_class = &class
};