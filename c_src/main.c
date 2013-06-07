#include "id3as_libav.h"
#include <libavfilter/avfilter.h>

static ID3ASFilterContext *initialise(char *buffer);
static ID3ASFilterContext *read_filter(char *buf, int *index);
static AVDictionary *read_params(char *buf, int *index);

int main(int argc, char **argv) 
{
  unsigned char *metadata = NULL;
  unsigned int metadata_buffer_size = 0;

  unsigned char *opaque = NULL;
  unsigned int opaque_buffer_size = 0;

  unsigned char *data = NULL;
  unsigned int data_buffer_size = 0;

  //av_log_set_level(AV_LOG_DEBUG);
  avcodec_register_all();
  avfilter_register_all();

  id3as_filters_register_all();

  if (!read_port(PACKET_SIZE, &data, &data_buffer_size))
    {
      fprintf(stderr, "Failed to read initialisation packet");
      exit(-1);
    }

  ID3ASFilterContext *input = initialise((char *)data);

  do 
    {

      int metadata_size = read_port(PACKET_SIZE, &metadata, &metadata_buffer_size);
      int opaque_size = read_port(PACKET_SIZE, &opaque, &opaque_buffer_size);
      int data_size = read_port(PACKET_SIZE, &data, &data_buffer_size);

      if (metadata_size < 0 || data_size < 0 || opaque_size < 0)
	{
	  break;
	}

      input->filter->execute(input, 
			     metadata, metadata_size,
			     opaque, opaque_size,
			     data, data_size);
    }
  while (1);

  return 0;
}

static ID3ASFilterContext *initialise(char *buf) 
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

