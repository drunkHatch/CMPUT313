#include <cnet.h>
#include <stdlib.h>
#include <string.h>

/*  This is an implementation of a stop-and-wait data link protocol.
    It is based on Tanenbaum's `protocol 4', 2nd edition, p227
    (or his 3rd edition, p205).
    This protocol employs only data and acknowledgement frames -
    piggybacking and negative acknowledgements are not used.

    It is currently written so that only one node (number 0) will
    generate and transmit messages and the other (number 1) will receive
    them. This restriction seems to best demonstrate the protocol to
    those unfamiliar with it.
    The restriction can easily be removed by "commenting out" the line

	    if(nodeinfo.nodenumber == 0)

    in reboot_node(). Both nodes will then transmit and receive (why?).

    Note that this file only provides a reliable data-link layer for a
    network of 2 nodes.
 */

typedef enum    { DL_DATA, DL_ACK }   FRAMEKIND;

typedef struct {
    char        data[MAX_MESSAGE_SIZE];
} MSG;

typedef struct {
    FRAMEKIND    kind;      	// only ever DL_DATA or DL_ACK
    size_t	 len;       	// the length of the msg field only
    int          checksum;  	// checksum of the whole frame
    int          seq;       	// only ever 0 or 1
    MSG          msg;
} FRAME;

#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)


static  MSG       	*lastmsg;
static size_t lastlength[5] = {0};
static  CnetTimerID	lasttimer		= NULLTIMER;

static  int       	ackexpected		= 0;
static	int		nextframetosend		= 0;
static	int		frameexpected		= 0;


static int buffer = 0; // indicates if buffer is full or not


static void transmit_frame(MSG *msg, FRAMEKIND kind, size_t length, int seqno)
{
    FRAME       f;
    int link = 1;
    f.kind      = kind;
    f.seq       = seqno;
    f.checksum  = 0;
    f.len       = length;
    
   
    switch (kind) {
    case DL_ACK :
        printf("ACK transmitted, seq=%d, link=%d\n", seqno, link);
	break;

    case DL_DATA: {
      if (nodeinfo.nodetype == NT_HOST) link = 1; // Host --> link = 1
      else link = 2; // Router --> link = 2

	CnetTime	timeout;

        printf(" \nDATA transmitted, seq=%d, link=%d\n", seqno, link);
        memcpy(&f.msg, msg, (int)length);

	timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
				linkinfo[link].propagationdelay;

        lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
	break;
      }
    }
    length      = FRAME_SIZE(f);
    f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
    CHECK(CNET_write_physical(link, &f, &length));
}

static EVENT_HANDLER(application_ready)
{
    CnetAddr destaddr;

    lastlength[nodeinfo.nodenumber]  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, &lastmsg[nodeinfo.nodenumber], &lastlength[nodeinfo.nodenumber]));
    CNET_disable_application(ALLNODES);

    printf("down from application, seq=%d\n", nextframetosend);
    transmit_frame(&lastmsg[nodeinfo.nodenumber], DL_DATA, lastlength[nodeinfo.nodenumber], nextframetosend);
    nextframetosend = 1-nextframetosend;
}

static EVENT_HANDLER(physical_ready)
{
    FRAME        f;
    size_t	 len;
    int          link, checksum;

    len         = sizeof(FRAME);
    CHECK(CNET_read_physical(&link, &f, &len));

    checksum    = f.checksum;
    f.checksum  = 0;
    if(CNET_ccitt((unsigned char *)&f, (int)len) != checksum) {
        printf("\t\t\t\tBAD checksum - frame ignored\n");
        return;           // bad checksum, ignore frame
    }

    switch (f.kind) {
    case DL_ACK :
        if(f.seq == ackexpected) {
            printf("\t\t\t\tACK received, seq=%d\n", f.seq);
            CNET_stop_timer(lasttimer);
            ackexpected = 1-ackexpected;
	    //get ACK and send next data
            buffer = 0;
            if(nodeinfo.nodenumber == 0){
                CNET_enable_application(ALLNODES);
            }
        }
	break;
    case DL_DATA :
      
        printf("\t\t\t\tDATA received, seq=%d, ", f.seq);
        if(f.seq == frameexpected) {
	    // if the end is a host, it won't transmit data
            if(nodeinfo.nodenumber == 4){
                printf("up to application\n");
                frameexpected = 1-frameexpected;
                len = f.len;
                CHECK(CNET_write_application(&f.msg, &len));
            } else {
	        //Don't send if our buffer is full
                if(buffer){
                    printf("Buffer Full :  seq %d.\n", f.seq);
                    return;
                } else {
		    //lock the full buffer
		    buffer= 1;
                    frameexpected = 1-frameexpected;
                    lastlength[nodeinfo.nodenumber] = f.len;
                    lastmsg[nodeinfo.nodenumber] = f.msg;
                    transmit_frame(&lastmsg[nodeinfo.nodenumber], DL_DATA, lastlength[nodeinfo.nodenumber], ackexpected);
                }
            }
        }
        else
            printf("ignored (seq num)\n");
        transmit_frame(NULL, DL_ACK, 0, f.seq);
	break;
    }
}

static EVENT_HANDLER(timeouts)
{
    printf("timeout, seq=%d\n", ackexpected);
    transmit_frame(&lastmsg[nodeinfo.nodenumber], DL_DATA, lastlength[nodeinfo.nodenumber], ackexpected);
}

static EVENT_HANDLER(showstate)
{
    printf(
    "\n\tackexpected\t= %d\n\tnextframetosend\t= %d\n\tframeexpected\t= %d\n",
		    ackexpected, nextframetosend, frameexpected);
}

EVENT_HANDLER(reboot_node)
{
    lastmsg = calloc(5, sizeof(MSG));

    if(nodeinfo.nodenumber == 0)
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));

    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));

    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    if(nodeinfo.nodenumber == 0)
        CNET_enable_application(ALLNODES);
}
