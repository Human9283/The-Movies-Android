#ifndef TM_DSHOW_READER_H
#define TM_DSHOW_READER_H

#include <stdint.h>

/* Register the Windows Media ASF Reader class with the HLE COM runtime. */
void tm_dshow_reader_init(void);

/* Create, load, and add a WMAsfReader source filter to an existing graph.
 * The returned IBaseFilter pointer carries one caller-owned reference. */
uint32_t tm_dshow_reader_add_source_filter(uint32_t graph, uint32_t file_name,
                                           uint32_t filter_name, uint32_t *out_filter);
/* Read the loaded source's media properties without exposing decoder state. */
int tm_dshow_reader_media_info(uint32_t filter, double *duration_seconds,
                               double *frame_seconds, int *width, int *height);
/* Return the last successfully delivered source position in seconds. */
int tm_dshow_reader_current_position(uint32_t filter, double *position_seconds);


#endif
