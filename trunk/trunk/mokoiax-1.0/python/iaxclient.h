// Stripped iaxclient header -bk
#ifndef _iaxclient_h
#define _iaxclient_h
#endif

#include <sys/time.h>
#include <stdio.h>

typedef int socklen_t;
	typedef int (*iaxc_sendto_t)(int, const void *, size_t, int,
			const struct sockaddr *, socklen_t);
	typedef int (*iaxc_recvfrom_t)(int, void *, size_t, int,
			struct sockaddr *, socklen_t *);
#define IAXC_AUDIO_FORMAT_MASK  ((1<<16)-1)

#define IAXC_VIDEO_FORMAT_MASK  (((1<<25)-1) & ~IAXC_AUDIO_FORMAT_MASK)



#define IAXC_FORMAT_G723_1       (1 << 0)   
#define IAXC_FORMAT_GSM          (1 << 1)   
#define IAXC_FORMAT_ULAW         (1 << 2)   
#define IAXC_FORMAT_ALAW         (1 << 3)   
#define IAXC_FORMAT_G726         (1 << 4)   
#define IAXC_FORMAT_ADPCM        (1 << 5)   
#define IAXC_FORMAT_SLINEAR      (1 << 6)   
#define IAXC_FORMAT_LPC10        (1 << 7)   
#define IAXC_FORMAT_G729A        (1 << 8)   
#define IAXC_FORMAT_SPEEX        (1 << 9)   
#define IAXC_FORMAT_ILBC         (1 << 10)  

#define IAXC_FORMAT_MAX_AUDIO    (1 << 15)  
#define IAXC_FORMAT_JPEG         (1 << 16)  
#define IAXC_FORMAT_PNG          (1 << 17)  
#define IAXC_FORMAT_H261         (1 << 18)  
#define IAXC_FORMAT_H263         (1 << 19)  
#define IAXC_FORMAT_H263_PLUS    (1 << 20)  
#define IAXC_FORMAT_H264         (1 << 21)  
#define IAXC_FORMAT_MPEG4        (1 << 22)  
#define IAXC_FORMAT_THEORA       (1 << 24)  
#define IAXC_FORMAT_MAX_VIDEO    (1 << 24)  

#define IAXC_EVENT_TEXT          1   
#define IAXC_EVENT_LEVELS        2   
#define IAXC_EVENT_STATE         3   
#define IAXC_EVENT_NETSTAT       4   
#define IAXC_EVENT_URL           5   
#define IAXC_EVENT_VIDEO         6   
#define IAXC_EVENT_REGISTRATION  8   
#define IAXC_EVENT_DTMF          9   
#define IAXC_EVENT_AUDIO         10  
#define IAXC_EVENT_VIDEOSTATS    11  
#define IAXC_EVENT_VIDCAP_ERROR  12  
#define IAXC_EVENT_VIDCAP_DEVICE 13  

#define IAXC_CALL_STATE_FREE     0       
#define IAXC_CALL_STATE_ACTIVE   (1<<1)  
#define IAXC_CALL_STATE_OUTGOING (1<<2)  
#define IAXC_CALL_STATE_RINGING  (1<<3)  
#define IAXC_CALL_STATE_COMPLETE (1<<4)  
#define IAXC_CALL_STATE_SELECTED (1<<5)  
#define IAXC_CALL_STATE_BUSY     (1<<6)  
#define IAXC_CALL_STATE_TRANSFER (1<<7)  


#define IAXC_TEXT_TYPE_STATUS     1   

#define IAXC_TEXT_TYPE_NOTICE     2   

#define IAXC_TEXT_TYPE_ERROR      3   
#define IAXC_TEXT_TYPE_FATALERROR 4   

#define IAXC_TEXT_TYPE_IAX        5   


#define IAXC_REGISTRATION_REPLY_ACK     18   
#define IAXC_REGISTRATION_REPLY_REJ     30   
#define IAXC_REGISTRATION_REPLY_TIMEOUT 6    

#define IAXC_URL_URL              1  
#define IAXC_URL_LDCOMPLETE       2  
#define IAXC_URL_LINKURL          3  
#define IAXC_URL_LINKREJECT       4  
#define IAXC_URL_UNLINK           5  


#define IAXC_SOURCE_LOCAL  1 
#define IAXC_SOURCE_REMOTE 2 

#define IAXC_EVENT_BUFSIZ 256

struct iaxc_ev_levels {
	float input;
	float output;
};

struct iaxc_ev_text {
	int type;

	int callNo; 

	char message[IAXC_EVENT_BUFSIZ];
};

struct iaxc_ev_call_state {
	int callNo;

	int state;
	
	int format;
	
	int vformat;

	char remote[IAXC_EVENT_BUFSIZ];

	char remote_name[IAXC_EVENT_BUFSIZ];
	
	char local[IAXC_EVENT_BUFSIZ];

	char local_context[IAXC_EVENT_BUFSIZ];
};

struct iaxc_netstat {
	int jitter;
	int losspct;

	int losscnt;

	int packets;

	int delay;
	
	int dropped;

	int ooo;
};

struct iaxc_ev_netstats {
	int callNo;
	
	int rtt;

	struct iaxc_netstat local;

	struct iaxc_netstat remote;
};

struct iaxc_video_stats
{
	unsigned long received_slices;  
	unsigned long acc_recv_size;    
	unsigned long sent_slices;      
	unsigned long acc_sent_size;    

	unsigned long dropped_frames;   
	unsigned long inbound_frames;   
	unsigned long outbound_frames;  

	float         avg_inbound_fps;  
	unsigned long avg_inbound_bps;  
	float         avg_outbound_fps; 
	unsigned long avg_outbound_bps; 

	struct timeval start_time;      
};

struct iaxc_ev_video_stats {
	int callNo;

	struct iaxc_video_stats stats;
};

struct iaxc_ev_url {
	int callNo;

	int type;

	char url[IAXC_EVENT_BUFSIZ];
};

struct iaxc_ev_video {
	int callNo;

	unsigned int ts;

	int format;
	
	int width;
	
	int height;

	int encoded;

	int source;

	int size;

	char *data;
};

struct iaxc_ev_audio
{
	int callNo;

	unsigned int ts;

	int format;

	int encoded;

	int source;

	int size;
	unsigned char *data;
};

struct iaxc_ev_registration {
	int id;

	int reply;

	int msgcount;
};

struct iaxc_ev_dtmf {
	int  callNo;

	char digit;
};

typedef struct iaxc_event_struct {
	struct iaxc_event_struct *next;

	int type; 
	
	union {
		
		struct iaxc_ev_levels           levels;
		
		struct iaxc_ev_text             text;       
		
		struct iaxc_ev_call_state       call;       
		
		struct iaxc_ev_netstats         netstats;   
		
		struct iaxc_ev_video_stats      videostats; 
		
		struct iaxc_ev_url              url;        
		
		struct iaxc_ev_video            video;      
		
		struct iaxc_ev_audio            audio;      
		
		struct iaxc_ev_registration     reg;
		
		struct iaxc_ev_dtmf             dtmf;
	} ev;
} iaxc_event;

typedef int (*iaxc_event_callback_t)(iaxc_event e);

struct iaxc_video_device {
	const char *name;

	const char *id_string;

	int id;
};

#define IAXC_AD_INPUT           (1<<0) 
#define IAXC_AD_OUTPUT          (1<<1) 
#define IAXC_AD_RING            (1<<2) 
#define IAXC_AD_INPUT_DEFAULT   (1<<3) 
#define IAXC_AD_OUTPUT_DEFAULT  (1<<4) 
#define IAXC_AD_RING_DEFAULT    (1<<5) 

struct iaxc_audio_device {
	const char * name;

	long capabilities;      

	int devID;
};

struct iaxc_sound {
	short   *data;           
	long    len;             
	int     malloced;        
	int     channel;         
	int     repeat;          
	long    pos;             
	int     id;              
	struct iaxc_sound *next; 
};

#define IAXC_FILTER_DENOISE     (1<<0) 
#define IAXC_FILTER_AGC         (1<<1) 
#define IAXC_FILTER_ECHO        (1<<2) 
#define IAXC_FILTER_AAGC        (1<<3) 
#define IAXC_FILTER_CN          (1<<4) 
