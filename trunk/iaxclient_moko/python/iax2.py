''' Iaxclient wrapper Library by Brandon Kruse '''

from ctypes import *

''' Codecs '''
IAXC_FORMAT_G723_1 = ((1 << 0))   # G.723.1 compression #
IAXC_FORMAT_GSM = ((1 << 1))   # GSM compression #
IAXC_FORMAT_ULAW = ((1 << 2))   # Raw mu-law data (G.711)) #
IAXC_FORMAT_ALAW = ((1 << 3))   # Raw A-law data (G.711)) #
IAXC_FORMAT_G726 = ((1 << 4))   # ADPCM, 32kbps #
IAXC_FORMAT_ADPCM = ((1 << 5))   # ADPCM IMA #
IAXC_FORMAT_SLINEAR = ((1 << 6))   # Raw 16-bit Signed Linear ((8000 Hz)) PCM #
IAXC_FORMAT_LPC10 = ((1 << 7))   # LPC10, 180 samplesgframe #
IAXC_FORMAT_G729A = ((1 << 8))   # G.729a Audio #
IAXC_FORMAT_SPEEX = ((1 << 9))   # Speex Audio #
IAXC_FORMAT_ILBC = ((1 << 10))  # iLBC Audio #
''' End Codecs '''

''' iaxc_type '''
IAXC_EVENT_TEXT = 1 # Indicates a text event #
IAXC_EVENT_LEVELS = 2 # Indicates a level event #
IAXC_EVENT_STATE = 3 # Indicates a call state change event #
IAXC_EVENT_NETSTAT = 4 # Indicates a network statistics update event #
IAXC_EVENT_URL = 5 # Indicates a URL push via IAX(2) #
IAXC_EVENT_VIDEO = 6 # Indicates a video event #
IAXC_EVENT_REGISTRATION = 8 # Indicates a registration event #
IAXC_EVENT_DTMF = 9 # Indicates a DTMF event #
IAXC_EVENT_AUDIO = 10 # Indicates an audio event #
IAXC_EVENT_VIDEOSTATS = 11 # Indicates a video statistics update event #
IAXC_EVENT_VIDCAP_ERROR = 12 # Indicates a video capture error occurred #
IAXC_EVENT_VIDCAP_DEVICE = 13 # Indicates a possible video capture device insertion/removal #

''' End iaxc_type '''

''' iaxc_event '''
class iaxc_event(Structure):
        _fields_ = [("type", c_int)]

''' End iaxc_event '''
