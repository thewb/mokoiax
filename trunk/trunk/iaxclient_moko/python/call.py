#!/usr/bin/python
''' Sample call using ctypes and the iaxclient
    library by Brandon Kruse '''

''' Import our ctypes and main library '''
from ctypes import *
from iax2 import *
import sys


''' Config Options.
Note: We do not need to set the library
audio levels, as we are taking them from
the parent audio server (pulseaudio) '''

username = "kruz"		    # iax2 user
secret = "blah"			    # iax2 pass
host = "diversity.openmoko.net"	    # iax2 server/host
lines = 1			    # number of lines
codec = IAXC_FORMAT_ULAW	    # bitmask for ulaw

''' End Config Options '''

''' Try to load the iaxclient library '''

try:
    cdll.LoadLibrary("libiaxclient.so")
    iax = CDLL("libiaxclient.so")
except:
    print "Could not load libiaxclient.so"
    sys.exit(1)


''' Function to catch all the iax2 events '''

def iax2_event_cb(e):
    ''' Here you could check the event type
    against the IAXC_EVENT_* definitions.
    Even register a callback for when the specific
    event happens. '''
    print "Event is " + e.Type


''' Try to init the library '''
if iax.iaxc_initialize(lines) != 0:
    print "Iax2 Line Initialization Failed!"
    sys.exit(1)

''' Set callback Function '''
#iax.iaxc_set_event_callback(iax2_event_cb)

''' Watch for incoming calls / second init '''
iax.iaxc_start_processing_thread()

''' Sample call function '''
#iax.iaxc_call("17771234567@diversity.openmoko.net"

reg_id = iax.iaxc_register(username, secret, host)

''' Answer a call '''
iax.iaxc_select_call(0)


''' Send a dtmf digit '''
iax.iaxc_send_dtmf(1234)


''' Send some txt '''
iax.iaxc_send_text("yay, it works.")

''' Shutdown everything '''
iax.iaxc_shutdown()

''' Unregister '''
iax.iaxc_unregister(reg_id)
