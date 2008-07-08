/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2008 - Brandon Kruse
 * Copyright (C) 2008 - Digium, Inc.
 *
 * Contributors:
 * Brandon Kruse
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 *
 */

#include <errno.h>
#include <string.h>

#include "iaxclient_lib.h"

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulsecore/gccmacro.h>

static const pa_sample_spec ss;

static pa_simple *s = NULL;

/*
 * This callback is given to indicate that a sound needs to be added to the
 * current list of sounds to play.  When a sound is active (after this is
 * called but before stop_sound), the audio for this sound should be mixed
 * with the normal output audio, and any other current sounds.
 *
 * It seems like this code would end up being almost identical to the code
 * in the portaudio interface.  Maybe it should be moved up and not handled
 * by the audio interface at all.
 */
static int pulse_play_sound(struct iaxc_sound *inSound, int ring) 
{
	return 0;
}

/*
 * This is called after play_sound to indicate that a sound should no longer
 * be played.
 */
static int pulse_stop_sound(int soundID) 
{
	return 0;
}


/*
 * Start audio I/O.
 */
static int pulse_start(struct iaxc_audio_driver *d) 
{
	return 0;
}

/*
 * Stop audio I/O.
 */
static int pulse_stop(struct iaxc_audio_driver *d) 
{
	return 0;
}


static int pulse_input(struct iaxc_audio_driver *d, void *samples, int *nSamples) 
{
	/* We do not need to loop and keep writing to the buffer,
	as simple write is a blocking function */
	/* Todo: check for errors */
	int error;	

	pa_simple_read(s, samples, sizeof(samples), &error); 

	return nSamples;
}

static int pulse_output(struct iaxc_audio_driver *d, void *samples, int nSamples) 
{
	int error;

	/* We do not need to loop and keep writing to the buffer,
	as simple write is a blocking function */

	/* Todo: check for errors */

	pa_simple_write(s, samples, sizeof(samples), &error);
	
	return nSamples;

}

static int pulse_select_devices(struct iaxc_audio_driver *d, int input, int output, int ring) 
{
	return 0;
}

int pulse_selected_devices(struct iaxc_audio_driver *d, int *input, int *output, int *ring) 
{
	*input = 0;
	*output = 0;
	*ring = 0;
	return 0;
}

static int pulse_destroy(struct iaxc_audio_driver *d)
{
	/* if our resources is open, we are going to
	delete all the data in the buffers before freeing */ 
	if (s)
		pa_simple_flush();

	/* if our resource is still open, close it */
	if (s)
		pa_simple_free(s);

	return 0;
}

static double pulse_input_level_get(struct iaxc_audio_driver *d)
{
	return -1;
}

static double pulse_output_level_get(struct iaxc_audio_driver *d)
{
	return -1;
}

static int pulse_input_level_set(struct iaxc_audio_driver *d, double level)
{
	return -1;
}

static int pulse_output_level_set(struct iaxc_audio_driver *d, double level)
{
	return -1;
}

/* Function: Actually init pa */
int pulse_initialize(struct iaxc_audio_driver *d, int sample_rate) 
{
	/* init our settings for the pulse audio simple setup */
	if (!sample_rate || sample_rate < 8000)
		sample_rate = 8000;

	ss.channels = 2; /* Stereo? */
	ss.rate = sample_rate; /* Most likely 8000... */
	ss.format = PA_SAMPLE_ULAW; /* XXX Change this based on codec choice. */ 

	/* From the simple PA example: */
	s = pa_simple_new(NULL,			   /* Use the default server. */
					  "MokoIax",		   /* Our application's name. */
					  PA_STREAM_FIX_RATE,	/* our rate, maybe use variable rate with speex? */
					  NULL,			   /* Use the default device. */
					  "Iax2-Client",			/* Description of our stream. */
					  &ss,				/* Our sample format. */
					  NULL,			   /* Use default channel map */
					  NULL,			   /* Use default buffering attributes. */
					  NULL,			   /* Ignore error code. */
					  );

	d->initialize = pulse_initialize; /* This function */
	d->destroy = pulse_destroy; /* Implemented to flush buffers and then free resources */
	d->select_devices = pulse_select_devices; /* Bogey function, pulse audio connects via resource thread */
	d->selected_devices = pulse_selected_devices; /* Same as above */
	d->start = pulse_start; /* Start the connection */
	d->stop = pulse_stop; /* Stop the connection */
	d->output = pulse_output;
	d->input = pulse_input;
	d->input_level_get = pulse_input_level_get;
	d->input_level_set = pulse_input_level_set;
	d->output_level_get = pulse_output_level_get;
	d->output_level_set = pulse_output_level_set;
	d->play_sound = pulse_play_sound;
	d->stop_sound = pulse_stop_sound;

	return 0;
}
