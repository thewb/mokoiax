/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2003-2006, Horizon Wimba, Inc.
 * Copyright (C) 2007, Wimba, Inc.
 *
 * Contributors:
 * Steve Kann <stevek@stevek.com>
 * Mihai Balea <mihai at hates dot ms>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

/*
 * Some comments about Theora streaming
 * Theora video codec has two problems when it comes to streaming
 * and broadcasting video:
 *
 * - Large headers that need to be passed from the encoder to the decoder
 *   to initialize it. The conventional wisdom says we should transfer the
 *   headers out of band, but that complicates things with IAX, which does
 *   not have a separate signalling channel. Also, it makes things really
 *   difficult in a video conference scenario, where video gets switched
 *   between participants regularly. To solve this issue, we initialize
 *   the encoder and the decoder at the same time, using the headers from
 *   the local encoder to initialize the decoder. This works if the
 *   endpoints use the exact same version of Theora and the exact same
 *   parameters for initialization.
 *
 * - No support for splitting the frame into multiple slices.  Frames can
 *   be relatively large. For a 320x240 video stream, you can see key
 *   frames larger than 9KB, which is the maximum UDP packet size on Mac
 *   OS X. We split the encoded frame artificially into slices that will
 *   fit into a typical MTU.  We also add six bytes at the beginning of
 *   each slice.
 *
 *   - version: right now, first bit should be 0, the rest are undefined
 *
 *   - source id: 2 bytes random number used to identify stream changes in
 *     conference applications this number is transmitted in big endian
 *     format over the wire
 *
 *   - frame index number - used to detect a new frame when some of the
 *     slices of the current frame are missing (only the least significant
 *     4 bits are used)
 *
 *   - index of slice in the frame, starting at 0
 *
 *   - total number of slices in the frame
 *
 * Other miscellaneous comments:
 *
 * - For quality reasons, when we detect a video stream switch, we reject all
 *   incoming frames until we receive a key frame.
 *
 * - Theora only accepts video that has dimensions multiple of 16. If we combine
 *   his with a 4:3 aspect ratio requirement, we get a very limited number
 *   of available resolutions. To work around this limitation, we pad the video
 *   on encoding, up to the closest multiple of 16. On the decoding side, we
 *   remove the padding. This way, video resolution can be any multiple of 2
 *
 * We should probably look more into this (how to deal with missing and
 * out of order slices)
 */

#include <stdlib.h>
#include "iaxclient_lib.h"
#include "video.h"
#include "codec_theora.h"
#include <theora/theora.h>

#define MAX_SLICE_SIZE		8000
#define MAX_ENCODED_FRAME_SIZE	48*1024

struct theora_decoder
{
	theora_state	td;
	theora_info	ti;
	theora_comment	tc;
	unsigned char	frame_index;
	unsigned char	slice_count;
	int		frame_size;
	unsigned short	source_id;
	int		got_key_frame;
	unsigned char	buffer[MAX_ENCODED_FRAME_SIZE];
};

struct theora_encoder
{
	theora_state	td;
	theora_info	ti;
	theora_comment	tc;
	int		needs_padding;
	unsigned char	frame_index;
	unsigned short	source_id;
	unsigned char	*pad_buffer;
};

static void destroy( struct iaxc_video_codec *c)
{
	struct theora_encoder *e;
	struct theora_decoder *d;

	if ( !c )
		return;

	if ( c->encstate )
	{
		e = (struct theora_encoder *)c->encstate;
		if ( e->pad_buffer )
			free(e->pad_buffer);
		theora_comment_clear(&e->tc);
		theora_info_clear(&e->ti);
		theora_clear(&e->td);
		free(e);
	}
	if ( c->decstate )
	{
		d = (struct theora_decoder *)c->decstate;
		theora_comment_clear(&d->tc);
		theora_info_clear(&d->ti);
		theora_clear(&d->td);
		free(c->decstate);
	}
	free(c);
}

static void reset_decoder_frame_state(struct theora_decoder * d)
{
	memset(d->buffer, 0, MAX_ENCODED_FRAME_SIZE);
	d->frame_size = 0;
	d->slice_count = 0;
}

static int pass_frame_to_decoder(struct theora_decoder *d, int *outlen, char *out)
{
	ogg_packet	op;
	yuv_buffer	picture;
	unsigned int	line;
	int		my_out_len;
	int		w, h, ph;

	/* decode into an OP structure */
	memset(&op, 0, sizeof(op));
	op.bytes = d->frame_size;
	op.packet = d->buffer;

	/* reject all incoming frames until we get a key frame */
	if ( !d->got_key_frame )
	{
		if ( theora_packet_iskeyframe(&op) )
			d->got_key_frame = 1;
		else
			return 1;
	}

	if ( theora_decode_packetin(&d->td, &op) == OC_BADPACKET )
	{
		fprintf(stderr,
			"codec_theora: warning: theora_decode_packetin says bad packet\n");
		return -1;
	}

	w = d->ti.frame_width;
	h = d->ti.frame_height;
	ph = d->ti.height;

	my_out_len = d->ti.frame_width * d->ti.frame_height * 3 / 2;

	/* make sure we have enough room for the goodies */
	if ( *outlen < my_out_len )
	{
		fprintf(stderr, "codec_theora: not enough room for decoding\n");
		return -1;
	}

	/* finally, here's where we get our goodies */
	if ( theora_decode_YUVout(&d->td, &picture) )
	{
		fprintf(stderr, "codec_theora: error getting our goodies\n");
		return -1;
	}

	//clear output
	memset(out, 127, my_out_len);

	for( line = 0 ; line < d->ti.frame_height / 2 ; line++ )
	{
		// Y-even
		memcpy(out + picture.y_width * 2 * line,
				picture.y + 2 * line * picture.y_stride,
				picture.y_width);
		// Y-odd
		memcpy(out + picture.y_width * (2 * line + 1),
				picture.y + (2 * line + 1) * picture.y_stride,
				picture.y_width);
		// U + V
		memcpy(out + (d->ti.frame_width * d->ti.frame_height) +
				line * d->ti.frame_width / 2,
				picture.u + line * picture.uv_stride,
				picture.uv_width);
		memcpy(out + (d->ti.frame_width * d->ti.frame_height * 5 / 4) +
				line * d->ti.frame_width / 2,
				picture.v + line * picture.uv_stride,
				picture.uv_width);
	}

	*outlen = my_out_len;

	return 0;
}

static int decode(struct iaxc_video_codec *c, int inlen, char *in, int *outlen, char *out)
{
	struct theora_decoder	*d;
	unsigned char		frame_index, slice_index, num_slices, version;
	unsigned short		source_id;

	// Sanity checks
	if ( !c || !c->decstate || !in || inlen <= 0 || !out || !outlen )
		return -1;

	d = (struct theora_decoder *)c->decstate;

	version = *in++;
	source_id = (unsigned short)(*in++) << 8;
	source_id |= *in++;
	frame_index = *in++ & 0x0f;
	slice_index = *in++;
	num_slices = *in++;
	inlen -= 6;

	if ( version & 0x80 )
	{
		fprintf(stderr, "Theora: unknown slice protocol\n");
		return -1;
	}

	if ( source_id == d->source_id )
	{
		/* We use only the least significant bits to calculate delta
		 * this helps with conferencing and video muting/unmuting
		 */
		unsigned char frame_delta = (frame_index - d->frame_index) & 0x0f;

		if ( frame_delta > 8 )
		{
			/* Old slice coming in late, ignore. */
			return 1;
		} else if ( frame_delta > 0 )
		{
			/* Slice belongs to a new frame */
			d->frame_index = frame_index;

			if ( d->slice_count > 0 )
			{
				/* Current frame is incomplete, drop it */
				c->video_stats.dropped_frames++;
				reset_decoder_frame_state(d);
			}
		}
	} else
	{
		/* Video stream was switched, the existing frame/slice
		 * indexes are meaningless.
		 */
		reset_decoder_frame_state(d);
		d->source_id = source_id;
		d->frame_index = frame_index;
		d->got_key_frame = 0;
	}

	// Process current slice
	if ( c->fragsize * slice_index + inlen > MAX_ENCODED_FRAME_SIZE )
	{
		// Frame would be too large, ignore slice
		return -1;
	}

	memcpy(d->buffer + c->fragsize * slice_index, in, inlen);
	d->slice_count++;

	/* We only know the size of the frame when we get the final slice */
	if ( slice_index == num_slices - 1 )
		d->frame_size = c->fragsize * slice_index + inlen;

	if ( d->slice_count < num_slices )
	{
		// we're still waiting for some slices
		return 1;
	} else
	{
		// Frame complete, send to decoder
		int ret = pass_frame_to_decoder(d, outlen, out);

		// Clean up in preparation for next frame
		reset_decoder_frame_state(d);

		return ret;
	}
}

// Pads a w by h frame to bring it up to pw by ph size using value
static void pad_channel(const char *src, int w, int h, unsigned char *dst,
		int pw, int ph, unsigned char value)
{
	int i;

	if ( w == pw )
	{
		// We don't need to pad each line, just copy the data
		memcpy(dst, src, w * h);
	} else
	{
		// We DO need to pad each line
		for ( i=0 ; i<h ; i++ )
		{
			memcpy(&dst[i*pw], &src[i*w], w);
			memset(&dst[i*pw+w], value, pw-w);
		}
	}
	// Pad the bottom of the frame if necessary
	if ( h < ph )
		memset(dst + pw * h, value, (ph - h) * pw);
}

static int encode(struct iaxc_video_codec *c, int inlen, char *in,
		struct slice_set_t *slice_set)
{
	int			i, size, ssize;
	const unsigned char	*p;
	struct theora_encoder	*e;
	ogg_packet		op;
	yuv_buffer		picture;

	// Sanity checks
	if ( !c || !c->encstate || !in || !slice_set )
		return -1;

	e = (struct theora_encoder *)c->encstate;

	// Prepare the YUV buffer
	if ( e->needs_padding )
	{
		// We copy a padded image into the pad buffer and set up the pointers
		// Use pad_channel for each of the YUV channels
		// Use a pad value of 0 for luma and 128 for chroma
		pad_channel(in,
				e->ti.frame_width,
				e->ti.frame_height,
				e->pad_buffer,
				e->ti.width,
				e->ti.height,
				0);

		pad_channel(in + e->ti.frame_width * e->ti.frame_height,
				e->ti.frame_width / 2,
				e->ti.frame_height / 2,
				e->pad_buffer + e->ti.width * e->ti.height,
				e->ti.width / 2,
				e->ti.height / 2,
				128);

		pad_channel(in + e->ti.frame_width * e->ti.frame_height * 5 / 4,
				e->ti.frame_width / 2,
				e->ti.frame_height / 2,
				e->pad_buffer + e->ti.width * e->ti.height * 5 / 4,
				e->ti.width / 2,
				e->ti.height / 2,
				128);

		picture.y = e->pad_buffer;
	} else
	{
		// use the original buffer
		picture.y = (unsigned char *)in;
	}
	picture.u = picture.y + e->ti.width * e->ti.height;
	picture.v = picture.u + e->ti.width * e->ti.height / 4;
	picture.y_width = e->ti.width;
	picture.y_height = e->ti.height;
	picture.y_stride = e->ti.width;
	picture.uv_width = e->ti.width / 2;
	picture.uv_height = e->ti.height / 2;
	picture.uv_stride = e->ti.width / 2;

	// Send data in for encoding
	if ( theora_encode_YUVin(&e->td, &picture) )
	{
		fprintf(stderr, "codec_theora: failed theora_encode_YUVin\n");
		return -1;
	}

	// Get data from the encoder
	if ( theora_encode_packetout(&e->td, 0, &op) != 1 )
	{
		fprintf(stderr, "codec_theora: failed theora_encode_packetout\n");
		return -1;
	}

	// Check to see if we have a key frame
	slice_set->key_frame = theora_packet_iskeyframe(&op) == 1;

	// We need to split the frame into one or more slices
	p = op.packet;
	size = op.bytes;

	// Figure out how many slices we need
	slice_set->num_slices = (size - 1) / c->fragsize + 1;

	// Copy up to fragsize bytes into each slice
	for ( i = 0; i < slice_set->num_slices; i++ )
	{
		slice_set->data[i][0] = 0;
		slice_set->data[i][1] = (unsigned char)(e->source_id >> 8);
		slice_set->data[i][2] = (unsigned char)(e->source_id & 0xff);
		slice_set->data[i][3] = e->frame_index;
		slice_set->data[i][4] = (unsigned char)i;
		slice_set->data[i][5] = (unsigned char)slice_set->num_slices;
		ssize = (i == slice_set->num_slices - 1) ?
			size % c->fragsize : c->fragsize;
		memcpy(&slice_set->data[i][6], p, ssize);
		slice_set->size[i] = ssize + 6;
		p += ssize;
	}
	e->frame_index++;

	return 0;
}

struct iaxc_video_codec *codec_video_theora_new(int format, int w, int h,
		int framerate, int bitrate, int fragsize)
{
	struct iaxc_video_codec	*c;
	struct theora_encoder	*e;
	struct theora_decoder	*d;
	ogg_packet headerp, commentp, tablep;

	/* Basic sanity checks */
	if ( w <= 0 || h <= 0 || framerate <= 0 || bitrate <= 0 || fragsize <= 0 )
	{
		fprintf(stderr, "codec_theora: bogus codec params: %d %d %d %d %d\n",
				w, h, framerate, bitrate, fragsize);
		return NULL;
	}

	if ( w % 2 || h % 2 )
	{
		fprintf(stderr, "codec_theora: video dimensions must be multiples of 2\n");
		return NULL;
	}

	if ( fragsize > MAX_SLICE_SIZE )
		fragsize = MAX_SLICE_SIZE;

	c = (struct iaxc_video_codec *)calloc(sizeof(struct iaxc_video_codec), 1);

	if ( !c )
		goto bail;

	c->decstate = calloc(sizeof(struct theora_decoder), 1);

	if ( !c->decstate )
		goto bail;

	c->encstate = calloc(sizeof(struct theora_encoder), 1);

	if ( !c->encstate )
		goto bail;

	video_reset_codec_stats(c);

	c->format = format;
	c->width = w;
	c->height = h;
	c->framerate = framerate;
	c->bitrate = bitrate;
	c->fragsize = fragsize;

	c->encode = encode;
	c->decode = decode;
	c->destroy = destroy;

	e = (struct theora_encoder *)c->encstate;
	d = (struct theora_decoder *)c->decstate;

	/* set up some parameters in the contexts */

	theora_info_init(&e->ti);

	/* set up common parameters */
	e->ti.frame_width = w;
	e->ti.frame_height = h;
	e->ti.width = ((w - 1) / 16 + 1) * 16;
	e->ti.height = ((h - 1) / 16 + 1) * 16;
	e->ti.offset_x = 0;
	e->ti.offset_y = 0;

	// We set up a padded frame with dimensions that are multiple of 16
	// We allocate a buffer to hold this frame
	e->needs_padding = e->ti.width != e->ti.frame_width ||
		e->ti.height != e->ti.frame_height;

	if ( e->needs_padding )
	{
		e->pad_buffer = (unsigned char *)
			malloc(e->ti.width * e->ti.height * 3 / 2);

		if ( !e->pad_buffer )
			goto bail;
	}
	else
	{
		e->pad_buffer = 0;
	}

	e->ti.fps_numerator = framerate;
	e->ti.fps_denominator = 1;

	e->ti.aspect_numerator = 1;
	e->ti.aspect_denominator = 1;

	e->ti.colorspace = OC_CS_UNSPECIFIED;
	e->ti.pixelformat = OC_PF_420;

	e->ti.target_bitrate = bitrate;

	e->ti.quality = 0;

	e->ti.dropframes_p = 0;
	e->ti.quick_p = 1;
	e->ti.keyframe_auto_p = 0;
	e->ti.keyframe_frequency = framerate;
	e->ti.keyframe_frequency_force = framerate;
	e->ti.keyframe_data_target_bitrate = bitrate * 3;
	e->ti.keyframe_auto_threshold = 80;
	e->ti.keyframe_mindistance = 8;
	e->ti.noise_sensitivity = 0;

	if ( theora_encode_init(&e->td, &e->ti) )
		goto bail;

	// Obtain the encoder headers and set up the decoder headers from
	// data in the encoder headers
	memset(&headerp, 0, sizeof(headerp));
	memset(&commentp, 0, sizeof(commentp));
	memset(&tablep, 0, sizeof(tablep));

	// Set up the decoder using the encoder headers
	theora_info_init(&d->ti);
	theora_comment_init(&d->tc);
	theora_comment_init(&e->tc);

	if ( theora_encode_header(&e->td, &headerp) )
		goto bail;

	headerp.b_o_s = 1;

	if ( theora_decode_header(&d->ti, &d->tc, &headerp) )
		goto bail;

	if ( theora_encode_comment(&e->tc, &commentp) )
		goto bail;

	if ( theora_decode_header(&d->ti, &d->tc, &commentp) )
		goto bail;

	theora_comment_clear(&e->tc);

	if ( theora_encode_tables(&e->td, &tablep) )
		goto bail;

	if ( theora_decode_header(&d->ti, &d->tc, &tablep) )
		goto bail;

	if ( theora_decode_init(&d->td, &d->ti) )
		goto bail;

	// Generate random source id
	srand((unsigned int)time(0));
	e->source_id = rand() & 0xffff;

	d->got_key_frame = 0;

	strcpy(c->name, "Theora");
	return c;

bail:
	fprintf(stderr, "codec_theora: failed to initialize encoder or decoder\n");

	if ( c )
	{
		if ( c->encstate )
			free(c->encstate);

		if ( c->decstate )
			free(c->decstate);

		free(c);
	}

	return NULL;
}

