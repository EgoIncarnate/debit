/*
 * Copyright (C) 2006, 2007 Jean-Baptiste Note <jean-baptiste.note@m4x.org>
 *
 * This file is part of debit.
 *
 * Debit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Debit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with debit.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>

#include "bitstream_header.h"
#include "bitstream.h"
#include "bitarray.h"

/* the meat */

/* Altera bitstream files are a
   three-dimentional array.
   First dimention is X, then slicing, then Y
*/

/* 0 - 65 */
#define EP35_X_SITES 66
/* 0 - 36, but we restrict here to 1-35
   The border causes problems and we don't want it */
#define EP35_Y_SITES 35
#define EP35_LAB_SITES 32
#define EP35_SLICE_SITES 70

typedef struct _geo_stride {
  unsigned offset;
  unsigned length;
} geo_stride_t;

static const geo_stride_t x_offsets_descr[] = {
  /* column 0 is IO */
  { .offset = 34,
    .length = 1 },
  { .offset = 35,
    .length = 12 },
  /* don't know what this is, X = 13 */
  { .offset = 166,
    .length = 1 },
  { .offset = 35,
    .length = 1 },
  /* small thing: interconnect site ? */
  { .offset = 4 + 35,
    .length = 1 },
  /* standard thing */
  { .offset = 35,
    .length = 10 },
  /* another @X = 26 */
  { .offset = 166,
    .length = 1 },
  { .offset = 35,
    .length = 12 },
  /* again strange, @X = 39 */
  { .offset = 27,
    .length = 1 },
  /* event 49->50 */
  { .offset = 35,
    .length = 10, },
  { .offset = 4 + 35,
    .length = 1, },
  { .offset = 35,
    .length = 1, },
  /* last strange @X = 52 */
  { .offset = 166,
    .length = 1 },
  { .offset = 35,
    .length = 12, },
  { .offset = 34,
    .length = 1, },
  /* NULL-terminate it */
  { .offset = 0,
    .length = 0, },
};

#define y_max 35
#define x_max 65

/* the absolute coordinate of (1,1,30) -- adjusted for various offsets */
gint base_off = 6479737 + 34 - 0*2701 - (y_max - 1) * 70 * 2701 - (2701 - 34);
/* this is in [0, 30] */
gint slice_off = 24;

typedef struct _coord_descr_t {
  unsigned max;
  unsigned offset;
} coord_descr_t;

typedef enum coords_types {
  Y = 0, SLICE, X, COORD_NUM,
} coord_type_t;

static const coord_descr_t coords[COORD_NUM] = {
  [Y] = { .max = 35, .offset = 70 * 2701 },
  [SLICE] = { .max = 69, .offset = 2701 /*could be computed from above
					  too, this is the offset_total*/ },
  /* For X, a bit complicated, see above */
  [X] = { .max = 65, .offset = 0 /* computed normally from above */},
};

static inline unsigned
width_total(const geo_stride_t *strides) {
  unsigned offset_tot = 0;
  unsigned length;

  do {
    length = strides->length;
    offset_tot += length;
    strides++;
  } while (length != 0);
  return offset_tot;
}

static inline unsigned
offset_total(const geo_stride_t *strides) {
  unsigned offset_tot = 0;
  unsigned length, offset;

  do {
    length = strides->length;
    offset = strides->offset;
    offset_tot += length * offset;
    strides++;
  } while (length != 0);
  return offset_tot;
}

static inline void
check_consistency(const altera_bitstream_t *bit) {
  unsigned offset = offset_total(x_offsets_descr);
/*   unsigned nx = width_total(x_offsets_descr); */
/*   g_print("offset total is %i for %i sites\n", offset, nx); */
  g_assert( offset == coords[SLICE].offset );
  (void) offset;
}

/* accumulate the x_offsets_descr and build corresponding
   offset array */
static unsigned *
offset_array(const geo_stride_t *strides) {
  unsigned total = width_total(strides);
  unsigned *array = g_new(unsigned, total);
  unsigned *posarray = array;
  unsigned offset = 0;
  unsigned steps, stepping;

  do {
    unsigned i;

    steps = strides->length;
    stepping = strides->offset;

    for (i = 0; i < steps; i++) {
      posarray[i] = offset;
/*       g_print("posarray[%i] = %i\n",i,offset); */
      offset += stepping;
    }

    posarray += steps;
    strides++;

  } while (steps != 0);

  return array;
}

/*
  NB: slices seem ordered in a bizarre fashion. Reordering them would
  take the following permutation (which is expressed in pure code
  instead of data in get_truth_table)
  there would be something like this
  1 extremity
  32 (4*8) standard, containing luts
  4 central
  32 (4*8) standard, containing luts
  1 extremity
 */

static inline unsigned
get_bit_offset(const altera_bitstream_t *bitstream,
	       unsigned y, unsigned slice, unsigned x) {
  /* start from beginning */
  unsigned y_offset = coords[Y].offset * (y_max - y);
  unsigned slice_offset = coords[SLICE].offset * slice;
  unsigned x_offset = (2667 - bitstream->xoffsets[x]);

  /* yes. Don't ask. Another intern. */
  unsigned ret = (base_off - slice_off) + y_offset + slice_offset + x_offset;

/*   g_print("offset is %i, y %i, x %i, slice %i\n", */
/*  	  ret, y_offset, x_offset, slice_offset); */

  g_assert(y_offset >= 0);
  //  g_assert(y_offset < 35 * 70 * 2701);
  g_assert(slice_offset >= 0);
  g_assert(slice_offset < (2701 * 70));
  g_assert(x_offset >= 0);
  g_assert(x_offset < 2701);

  return ret;
}

/* These are the reverse functions from the above. This allows us to
   check that the function is indeed a bijection ! */
#define GET_NUM(x,y) ( (x) / (y) )

static inline unsigned
get_y_from_bit_offset(const altera_bitstream_t *bit,
		      const unsigned off) {
  unsigned width = coords[Y].offset;
  unsigned basebits = get_bit_offset(bit, y_max, 0, x_max);
  unsigned remains = off - basebits;
  unsigned y = 35 - GET_NUM(remains, width);
/*   g_print("y is supposed to be %i\n", y); */
  return y;
}

static inline unsigned
get_slice_from_bit_offset(const altera_bitstream_t *bit,
			  const unsigned off) {
  unsigned y = get_y_from_bit_offset(bit,off);
  unsigned basebits = get_bit_offset(bit, y, 0, x_max);
  unsigned width = coords[SLICE].offset;
  unsigned remains = off - basebits;
  unsigned slice = GET_NUM (remains, width);
/*   g_print("remains %i bits for slice coord, slice is %i\n", remains, slice); */
  g_assert(remains < 70 * 2701);
  return slice;
}

static inline unsigned
get_x_from_bit_offset(const altera_bitstream_t *bit,
		      const unsigned off) {
  unsigned y = get_y_from_bit_offset(bit,off);
  unsigned slice = get_slice_from_bit_offset(bit,off);
  unsigned basebits = get_bit_offset(bit, y, slice, x_max);
  /* the term with remains here is x_offset - offset remaints */
  unsigned remains = off - basebits;
  unsigned *xoffsets = bit->xoffsets;
  unsigned x;

  for (x = 0; x < coords[X].max && (2667 - xoffsets[x]) > remains; x++)
    ;

/*   g_print("remains %i bits for x coord, returning %i\n", remains, x); */
  g_assert(remains >= 0);
  g_assert(remains < coords[SLICE].offset);

  return x;
}

static inline unsigned
get_offset_from_bit_offset(const altera_bitstream_t *bit,
			   unsigned off) {
  unsigned y = get_y_from_bit_offset(bit,off);
  unsigned slice = get_slice_from_bit_offset(bit,off);
  unsigned x = get_x_from_bit_offset(bit,off);
  unsigned basebits = get_bit_offset(bit, y, slice, x);
  unsigned remains = off - basebits;
/*   g_print("remains %i bits finally\n", remains); */
  return remains;
}

void print_pos_from_bit_offset(const altera_bitstream_t *bit,
			       unsigned bitoffset) {
  unsigned x = get_x_from_bit_offset(bit, bitoffset);
  unsigned y = get_y_from_bit_offset(bit, bitoffset);
  unsigned slice = get_slice_from_bit_offset(bit, bitoffset);
  unsigned offset = get_offset_from_bit_offset(bit, bitoffset);

  g_assert( bitoffset == get_bit_offset(bit,y,slice,x) + offset);
  g_print("%i@(%i,%i,%i)+%i\n", bitoffset, x, y, slice, offset);
}

static inline unsigned
get_chunk_len(const altera_bitstream_t *bitstream, unsigned x) {
  return (bitstream->xoffsets[x+1] - bitstream->xoffsets[x]);
}

static inline gboolean
get_bit(const altera_bitstream_t *bitstream,
	unsigned x, unsigned y, unsigned slice, unsigned offset) {
  bitarray_t *bitarray = bitstream->bitarray;
  unsigned bitpos = get_bit_offset(bitstream, y, slice, x) + offset;

  /* Warn if we're looking */
  g_assert(offset < get_chunk_len(bitstream, x));
  return bitarray_is_set(bitarray, bitpos) ? TRUE : FALSE;
}

/*
 * XXX will need more than that at some point
 * Get bits in [start, end[
 */

static inline
guint32 get_chunk(const altera_bitstream_t *bitstream,
		  unsigned x, unsigned y, unsigned slice,
		  unsigned start, unsigned len) {
  bitarray_t *bitarray = bitstream->bitarray;
  unsigned basebit = get_bit_offset(bitstream,y,slice,x) + start;
  guint32 res = 0;
  unsigned i;

  for (i = 0; i < len; i++)
    if (bitarray_is_set(bitarray,basebit++))
      res |= 1 << i;

  return res;
}

static inline void
bitarray_write_bit(bitarray_t *dest, unsigned offset,
		   gboolean value) {
  if (value)
    bitarray_set(dest, offset);
  else
    bitarray_unset(dest, offset);
}

static inline
void set_chunk(const altera_bitstream_t *bitstream,
	       unsigned x, unsigned y, unsigned slice,
	       unsigned start, unsigned len,
	       guint32 data) {
  bitarray_t *bitarray = bitstream->bitarray;
  unsigned basebit = get_bit_offset(bitstream,y,slice,x) + start;
  unsigned i;

  for (i = 0; i < len; i++) {
    gboolean set = (data & (1 << i)) ? TRUE : FALSE;
    bitarray_write_bit(bitarray, basebit++, set);
  }
}

static inline
unsigned slice_from_index(unsigned N) {
  g_assert(N % 2 == 0);

  if (N < 16)
    return 2*N;
  else
    return 69 + 1 - 4 - (N-16)*2;
}

static inline
void zero_truth_table(const altera_bitstream_t *bitstream,
		     unsigned x, unsigned y, unsigned N) {
  unsigned slice, i;

  slice = slice_from_index(N);

  for (i = 0; i < 4; i++)
    set_chunk(bitstream, x, y, slice + i, slice_off, 4, 0);
}

static inline
guint16 get_truth_table(const altera_bitstream_t *bitstream,
			unsigned x, unsigned y, unsigned N) {
  guint16 res = 0;
  unsigned slice, i;

  slice = slice_from_index(N);

/*   g_print("N is %i, and slice index is %i\n", N, slice); */

  /* todo: bit reordering */
  for (i = 0; i < 4; i++) {
    guint32 table = get_chunk(bitstream, x, y,
			      slice + i, slice_off, 4);
    res |= table << (4*i);
  }

  return ~res;
}

static inline void
bitcopy_to_bitarray(bitarray_t *dest, unsigned offset,
		    const guint32 data, unsigned length) {
  unsigned i;
  for ( i = 0; i < length; i++) {
    gboolean isset = (data & (1 << i)) ? TRUE : FALSE;
    bitarray_write_bit(dest, offset+i, isset);
  }
}

/* This is the function that begs optimization -- TODO after a while for
   performance reasons */
static inline void
bitstream_to_bitarray(bitarray_t *dest, unsigned offset,
		      const altera_bitstream_t *bitstream,
		      unsigned x, unsigned y, unsigned site,
		      unsigned length) {
  unsigned read = 0;
  /* For now divise in chunks of 32 bits */
  do {
    unsigned readlen = length > 32 ? 32 : length;
    guint32 table = get_chunk(bitstream, x, y, site, read, readlen);
    bitcopy_to_bitarray(dest, offset + read, table, readlen);

    read += readlen;
    length -= readlen;
  } while (length > 0);
}

/* Maybe have a version with allocation outside so that it can be
   reused.. */

static inline bitarray_t *
get_lab_data(const altera_bitstream_t *bitstream,
	     unsigned x, unsigned y) {
  unsigned width = 35;//get_chunk_len(bitstream, x);
  /* lab size, anyone ? */
  unsigned size = width * 70;
  bitarray_t *bitarray = bitarray_create(size);
  unsigned i, bitindex = 0;

  for (i = 0; i < EP35_SLICE_SITES; i++) {
    bitstream_to_bitarray(bitarray, bitindex,
			  bitstream, x, y, i,
			  width);
    bitindex += width;
  }

  return bitarray;
}

static inline gchar *make_lab_string(unsigned x, unsigned y) {
  GString *string = g_string_sized_new (64);
  g_string_printf(string, "X%uY%u.bin", x, y);
  return g_string_free(string, FALSE);
}

static inline int dump_data(const gchar *odir,
			    const gchar *filename,
			    const char *data,
			    unsigned length) {
  gchar *name;
  GError *error = NULL;
  gboolean ret;

  /* Assemble the filename */
  if (odir)
    name = g_build_filename(odir, filename, NULL);
  else
    name = g_build_filename(filename, NULL);

  /* Actually write the file */
  ret = g_file_set_contents(name, data, length, &error);

  g_free(name);

  if (ret)
    return 0;

  g_print("could not dump file %s: %s",filename,error->message);
  g_error_free (error);
  return -1;
}

static inline int bitarray_dump(const gchar *odir, const gchar *filename,
				const bitarray_t *data) {
  return dump_data(odir, filename, (gchar *)data->array, data->size);
}

typedef void (*lab_iterator_t)(const altera_bitstream_t *bitstream,
			       unsigned x, unsigned y, void *data);

static void
iterate_over_labs(const altera_bitstream_t *bitstream,
		  lab_iterator_t iter, void *data) {
  unsigned x;

  for ( x = 1; x < EP35_X_SITES-1; x++) {
    unsigned chunk_len = get_chunk_len(bitstream,x);
    /* if we're of the right type. This is a rought filter */
    if (chunk_len == 35 || chunk_len == 4 + 35) {
      unsigned y;
      for ( y = 1; y <= EP35_Y_SITES; y++)
	iter(bitstream, x, y, data);
    } else {
      g_print("not considering x value %i\n",x);
    }
  }
}

typedef void (*table_iterator_t)(const altera_bitstream_t *bitstream,
				 unsigned x, unsigned y, unsigned n);

static void
iter_over_tables(const altera_bitstream_t *bitstream,
		 unsigned x, unsigned y, void *data) {
  table_iterator_t iter = data;
  unsigned n;
  for ( n = 0; n < EP35_LAB_SITES; n+=2)
    iter(bitstream, x, y, n);
}

static void
iterate_over_tables(const altera_bitstream_t *bitstream,
		    table_iterator_t iter) {
  iterate_over_labs(bitstream, iter_over_tables, iter);
}

static void
dump_lab(const altera_bitstream_t *bitstream,
	 unsigned x, unsigned y, void *data) {
  bitarray_t *bitarray;
  gchar *name;

  bitarray = get_lab_data(bitstream, x, y);
  name = make_lab_string(x,y);
  bitarray_dump(data, name, bitarray);

  g_free(name);
  (void) bitarray_free(bitarray, FALSE);
}

void
dump_lab_data(const gchar *odir,
	      const altera_bitstream_t *bitstream) {
  void *arg = (void *) odir;
  iterate_over_labs(bitstream, dump_lab, arg);
}

static void
table_display(const altera_bitstream_t *bitstream,
	      unsigned x, unsigned y, unsigned n) {
  guint16 table = get_truth_table(bitstream,x,y,n);
  if (table != 0 && table != 0xffff)
    g_print("lut (%i,%i,%i) is %04x\n",x,y,n,table);
}

void
dump_lut_tables(const altera_bitstream_t *bitstream) {
  iterate_over_tables(bitstream, table_display);
}

void
zero_lut_tables(const altera_bitstream_t *bitstream) {
  iterate_over_tables(bitstream, zero_truth_table);
}

int
dump_raw_bit(const gchar *odir, const gchar *filename,
	     const altera_bitstream_t *bitstream) {
  return dump_data(odir, filename, bitstream->bitdata, bitstream->bitlength);
}

int
dump_raw_m4k(const gchar *odir, const gchar *filename,
	     const altera_bitstream_t *bitstream) {
  return dump_data(odir, filename, bitstream->m4kdata, bitstream->m4klength);
}

altera_bitstream_t *
parse_bitstream(const gchar *filename) {
  /* get the bitstream into a bitarray */
  GError *error = NULL;
  GMappedFile *file = NULL;
  altera_bitstream_t *bit;
  int err;

  bit = g_new0(altera_bitstream_t, 1);

  file = g_mapped_file_new (filename, TRUE, &error);
  bit->file = file;

  if (error != NULL) {
    g_warning("could not map file %s: %s",filename,error->message);
    g_error_free (error);
    goto out_free_dest;
  }

  /* grossly parse the structure */
  err = parse_bitstream_structure(bit, g_mapped_file_get_contents (file),
				  g_mapped_file_get_length (file));

  if (err)
    goto out_free_dest;

  /* load the actual bitstream data into the bitarray */
  bit->bitarray = bitarray_create_data((char *)bit->bitdata, 8 * bit->bitlength);

  /* then do mighty things with it ! */
  bit->xoffsets = offset_array(x_offsets_descr);

  /* do some basic checks */
  check_consistency(bit);

  return bit;

 out_free_dest:
  g_free (bit);
  return NULL;
}

void free_bitstream(altera_bitstream_t *bitstream) {
  g_free(bitstream->xoffsets);
  (void) bitarray_free(bitstream->bitarray, TRUE);
  g_mapped_file_free(bitstream->file);
  g_free(bitstream);
}
