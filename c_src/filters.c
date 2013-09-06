#include <string.h>
#include <stdlib.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include "i_port.h"
#include "id3as_libav.h"

#define REGISTER_INPUT(name) {				\
    extern ID3ASFilter id3as_##name##_input;		\
    register_filter(&id3as_##name##_input); }

#define REGISTER_FILTER(name) {				\
    extern ID3ASFilter id3as_##name##_filter;		\
    register_filter(&id3as_##name##_filter); }


static ID3ASFilter *first_filter = NULL;

static void register_filter(ID3ASFilter *filter);

void id3as_filters_register_all() 
{
  REGISTER_INPUT(raw_audio);
  REGISTER_INPUT(encoded_audio);
  REGISTER_INPUT(raw_video);
  REGISTER_INPUT(encoded_video);

  REGISTER_FILTER(resample_audio);
  REGISTER_FILTER(rescale_video);
  REGISTER_FILTER(output_raw_audio);
  REGISTER_FILTER(output_encoded_audio);
  REGISTER_FILTER(output_raw_video);
  REGISTER_FILTER(output_encoded_video);
  REGISTER_FILTER(stereo_splitter);
  REGISTER_FILTER(effects_processor);
  REGISTER_FILTER(parallel);
  REGISTER_FILTER(async_parallel);
}

ID3ASFilter *find_filter(char *name) 
{
  ID3ASFilter *p = NULL;
  p = first_filter;
  while (p) 
    {
      if (strcmp(p->name, name) == 0) {
	return p;
      }
      p = p->next;
    }

  ERRORFMT("Failed to find filter %s\n", name);
  exit(-1);
}

ID3ASFilterContext *allocate_instance(ID3ASFilter *filter, 
				      AVDictionary *options, 
				      AVDictionary *codec_options,
				      ID3ASFilterContext **downstream_filters, 
				      int num_downstream_filters) 
{
  ID3ASFilterContext *instance = av_mallocz(sizeof(ID3ASFilterContext));

  instance->filter = filter;
  instance->priv_data = av_mallocz(filter->priv_data_size);
  instance->downstream_filters = downstream_filters;
  instance->num_downstream_filters = num_downstream_filters;

  *(AVClass**)instance->priv_data = (AVClass *) filter->priv_class;

  av_opt_set_defaults(instance->priv_data);
  
  av_opt_set_dict(instance->priv_data, &options);

  filter->init(instance, codec_options);

  return instance;
}

static void register_filter(ID3ASFilter *filter) 
{
  ID3ASFilter **p;

  p = &first_filter;
  while (*p != NULL) p = &(*p)->next;
  *p = filter;
  filter->next = NULL;
}
