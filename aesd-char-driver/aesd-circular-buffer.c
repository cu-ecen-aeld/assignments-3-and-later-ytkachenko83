/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

#define LENGTH(entries)\
    sizeof(entries)/sizeof(entries[0])

#define CB_POINTER_INC(pointer,entries)\
    pointer = (pointer + 1)%(LENGTH(entries))
#define CB_POINTER_CAST(pointer,entries)\
    pointer%(LENGTH(entries))
/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    uint8_t i = buffer->out_offs;
    uint8_t upper_boundary = buffer->in_offs;
    if (buffer->full) {
        upper_boundary += LENGTH(buffer->entry);
    }
    for(; i < upper_boundary; i++) {
        size_t ind = CB_POINTER_CAST(i, buffer->entry);
        size_t block_len = buffer->entry[ind].size;
        if (char_offset < block_len) {
            *entry_offset_byte_rtn = char_offset;
            return &(buffer->entry[ind]);
        } else {
            char_offset -= block_len;
        }
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
char * aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    char *old_buf = NULL;
    if (buffer->full) {
        old_buf = buffer->entry[buffer->in_offs].buffptr;
        CB_POINTER_INC(buffer->out_offs, buffer->entry);
    }
    buffer->entry[buffer->in_offs] = *add_entry;
    CB_POINTER_INC(buffer->in_offs, buffer->entry);
    buffer->full = buffer->in_offs == buffer->out_offs;

    return old_buf;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
