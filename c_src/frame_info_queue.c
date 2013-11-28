#include "id3as_libav.h"

typedef struct _frame_info_queue_item 
{
  int64_t pts;
  frame_info *frame_info;
  struct _frame_info_queue_item *next;
} frame_info_queue_item;

struct _frame_info_queue
{
  frame_info_queue_item *inbound_list_head;
  frame_info_queue_item *inbound_list_tail;
};

static void add_frame_info_to_queue(frame_info_queue *queue, frame_info *frame_info, int64_t pts) 
{
  frame_info_queue_item *list_entry = malloc(sizeof(frame_info_queue_item));
  list_entry->pts = pts;
  list_entry->frame_info = frame_info;
  list_entry->next = NULL;

  if (queue->inbound_list_head) {
    queue->inbound_list_tail->next = list_entry;
  }
  else {
    queue->inbound_list_head = list_entry;
  }

  queue->inbound_list_tail = list_entry;
}

void init_frame_info_queue(frame_info_queue **queue)
{
  *queue = malloc(sizeof(frame_info_queue));
  (*queue)->inbound_list_head = NULL;
  (*queue)->inbound_list_tail = NULL;
}

void queue_frame_info_from_frame(frame_info_queue *queue, AVFrame *frame) 
{
  AVFrameSideData *side_data = av_frame_get_side_data(frame, FRAME_INFO_SIDE_DATA_TYPE);

  frame_info *in_frame = (frame_info *)side_data->data;

  frame_info *info = malloc(sizeof(frame_info) + in_frame->buffer_size);
  memcpy(info, in_frame, sizeof(frame_info) + in_frame->buffer_size);

  add_frame_info_to_queue(queue, info, frame->pts);
}

void queue_frame_info(frame_info_queue *queue, unsigned char *frame_info_data, unsigned int frame_info_size, int64_t pts) 
{
  frame_info *info = malloc(sizeof(frame_info) + frame_info_size);
  info->flags = 0;
  info->buffer_size = frame_info_size;
  memcpy(info->buffer, frame_info_data, frame_info_size);

  add_frame_info_to_queue(queue, info, pts);
}

void add_frame_info_to_frame(frame_info_queue *queue, AVFrame *frame) 
{
  frame_info *frame_inf = get_frame_info(queue, frame->pkt_pts);

  AVFrameSideData *side_data = av_frame_new_side_data(frame, FRAME_INFO_SIDE_DATA_TYPE, sizeof(frame_info) + frame_inf->buffer_size);

  memcpy(side_data->data, frame_inf, sizeof(frame_info) + frame_inf->buffer_size);
}

frame_info *get_frame_info(frame_info_queue *queue, int64_t pts) 
{
  frame_info_queue_item *prev = NULL;
  frame_info_queue_item *current = queue->inbound_list_head;
  frame_info *frame_info;

  while (current) 
    {
      if (current->pts == pts) 
	{
	  if (queue->inbound_list_head == current) {
	    queue->inbound_list_head = current->next;
	  } 
	  else {
	    prev->next = current->next;
	  }
	  
	  if (queue->inbound_list_tail == current) {
	    queue->inbound_list_tail = prev;
	  }

	  frame_info = current->frame_info;
	  free(current);

	  return frame_info;
	}

      prev = current;
      current = current->next;
    }

  ERRORFMT("Failed to find frame_info for %ld", pts);
  exit(-2);
}

