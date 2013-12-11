#include "id3as_libav.h"
#include <libavfilter/avfilter.h>

#define CUSTOM_VARARGS_READ_PROCESSOR NULL

static ID3ASFilterContext *build_graph(char *buffer);
static ID3ASFilterContext *read_filter(char *buf, int *index);
static AVDictionary *read_params(char *buf, int *index);

ID3ASFilterContext *input;
volatile int sync_mode;

void initialise(char *mode, void *initialisation_data, int length) 
{
  sync_mode = (strncmp(mode, "async", 5) != 0);

  input = build_graph((char *) initialisation_data);
}

void process_frame(void *metadata, int metadata_size, void *frame_info, int frame_info_size) 
{
  static unsigned char *data = NULL;
  static unsigned int data_buffer_size = 0;

  int data_size = read_port(PACKET_SIZE, &data, &data_buffer_size);

  input->filter->execute(input, 
			 metadata, metadata_size,
			 frame_info, frame_info_size,
			 data, data_size);

  if (sync_mode) {
    write_done("frame_done");
  }
}

void flush() 
{
  input->filter->flush(input);

  write_done("flush_done");
}

void command_loop() 
{
  char *buf = NULL;
  char *command;
  int index = 0;

  while (read_port_command(PACKET_SIZE, SUBSYSTEM, (unsigned char **) &buf, &command, &index) > 0) 
    {
      void *initialisation_data = NULL, *metadata = NULL, *frame_info = NULL;
      char *mode = NULL;
      long length1, length2, length3;

      START_MATCH()
	HANDLE_MATCH3(initialise, "~a~b", mode, initialisation_data, length1)
	HANDLE_MATCH4(process_frame, "~b~b", metadata, length2, frame_info, length3)
	HANDLE_MATCH0(flush)

	HANDLE_UNMATCHED()
	free(command);
      index = 0;
    }
}

int main(int argc, char **argv) 
{

  //av_log_set_level(AV_LOG_DEBUG);
  avcodec_register_all();
  avfilter_register_all();

  id3as_filters_register_all();

  command_loop();

  return 0;
}

static ID3ASFilterContext *build_graph(char *buf) 
{
  int index = 0;
  int version;

  ei_decode_version(buf, &index, &version);

  return read_filter(buf, &index);
}

static ID3ASFilterContext *read_filter(char *buf, int *index)
{
  int arity;
  char *name = NULL;
  AVDictionary *params = NULL;
  AVDictionary *codec_params = NULL;
  int num_downstream_filters;
  ID3ASFilterContext **downstream_filters;

  I_DECODE_TUPLE_HEADER(buf, index, &arity);

  I_DECODE_STRING(buf, index, &name);

  params = read_params(buf, index);

  codec_params = read_params(buf, index);

  I_DECODE_LIST_HEADER(buf, index, &num_downstream_filters);

  downstream_filters = malloc(sizeof(ID3ASFilterContext*) * num_downstream_filters);

  for (int i = 0; i < num_downstream_filters; i++)
    {
      downstream_filters[i] = read_filter(buf, index);
    }

  I_SKIP_NULL(buf, index);

  ID3ASFilter *filter = find_filter(name);

  return allocate_instance(filter, params, codec_params, downstream_filters, num_downstream_filters);
}

static AVDictionary *read_params(char *buf, int *index)
{
  int num_params;
  AVDictionary *dict = NULL;

  I_DECODE_LIST_HEADER(buf, index, &num_params);

  for (int i = 0; i < num_params; i++)
    {
      char *name;
      char *value;
      int arity;

      I_DECODE_TUPLE_HEADER(buf, index, &arity);

      I_DECODE_STRING(buf, index, &name);
      I_DECODE_STRING(buf, index, &value);

      av_dict_set(&dict, name, value, AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
    }
  
  I_SKIP_NULL(buf, index);

  return dict;
}

