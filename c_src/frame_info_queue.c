#include "id3as_libav.h"

void init_frame_info_queue(frame_info_queue *queue)
{
  queue->inbound_list_head = NULL;
  queue->inbound_list_tail = NULL;
}

void queue_frame_info_from_frame(frame_info_queue *queue, AVFrame *frame) 
{
  AVFrameSideData *side_data = av_frame_get_side_data(frame, FRAME_INFO_SIDE_DATA_TYPE);

  queue_frame_info(queue, side_data->data, side_data->size, frame->pts);
}

void queue_frame_info(frame_info_queue *queue, unsigned char *frame_info, unsigned int frame_info_size, int64_t pts) 
{
  frame_info_queue_item *list_entry = (frame_info_queue_item *) malloc(sizeof(frame_info_queue_item));
  list_entry->pts = pts;
  list_entry->frame_info.data = malloc(frame_info_size);
  list_entry->frame_info.size = frame_info_size;
  list_entry->next = NULL;
  memcpy(list_entry->frame_info.data, frame_info, frame_info_size);

  if (queue->inbound_list_head) {
    queue->inbound_list_tail->next = list_entry;
  }
  else {
    queue->inbound_list_head = list_entry;
  }

  queue->inbound_list_tail = list_entry;
}

void add_frame_info_to_frame(frame_info_queue *queue, AVFrame *frame) 
{
  frame_info_queue_item *frame_info_queue_item = get_frame_info(queue, frame->pkt_pts);

  AVFrameSideData *side_data = av_frame_new_side_data(frame, FRAME_INFO_SIDE_DATA_TYPE, frame_info_queue_item->frame_info.size);
  memcpy(side_data->data, frame_info_queue_item->frame_info.data, frame_info_queue_item->frame_info.size);

  free_frame_info(frame_info_queue_item);
}

void free_frame_info(frame_info_queue_item *frame_info_queue_item) 
{
  free(frame_info_queue_item->frame_info.data);
  free(frame_info_queue_item);
}

frame_info_queue_item *get_frame_info(frame_info_queue *queue, int64_t pts) 
{
  frame_info_queue_item *prev = NULL;
  frame_info_queue_item *current = queue->inbound_list_head;

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

	  return current;
	}

      prev = current;
      current = current->next;
    }

  ERRORFMT("Failed to find frame_info for %ld", pts);
  exit(-2);
}

