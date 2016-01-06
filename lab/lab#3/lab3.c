#include <cnet.h>
#include <stdlib.h>
#include <string.h>

#include "nl_table.h"
#include "dll_basic.h"

#define	MAXHOPS		4

/*  This file implements a better flooding algorithm exhibiting slightly
    more "intelligence" than the naive algorithm in flooding1.c
    These additions, implemented using flood2(), include:

    1) data packets are initially sent on all links.
    2) packets are forwarded on all links except the one on which they arrived.
    3) acknowledgement packets are initially sent on the link on which their
       data packet arrived.

    This algorithm exhibits better efficiency than flooding1.c .  Over the
    8 nodes in the AUSTRALIA.MAP file, the efficiency is typically about 8%.
 */

typedef enum    	{ NL_DATA, NL_ACK }   NL_PACKETKIND;

typedef struct {
    CnetAddr		src;
    CnetAddr		dest;
    NL_PACKETKIND	kind;      	/* only ever NL_DATA or NL_ACK */
    int			seqno;		/* 0, 1, 2, ... */
    int			hopcount;
    size_t		length;       	/* the length of the msg portion only */
    char		msg[MAX_MESSAGE_SIZE];
} NL_PACKET;

typedef struct {
    CnetAddr	address;		// ... of remote node
    int		ackexpected;		// packet sequence numbers to/from node
    int		nextpackettosend;
    int		packetexpected;

    int		minhops;		// minimum known hops to remote node
    int		minhop_link;		// link via which minhops path observed
} NLTABLE;

static	NLTABLE	*NL_table	= NULL;
static	int	NL_table_size	= 0;

typedef struct {
    CnetAddr dest;
    CnetTimerID last_timer;
    NL_PACKET last_pkt;
} TIMEOUT_ENTRY;

#define PACKET_HEADER_SIZE  (sizeof(NL_PACKET) - MAX_MESSAGE_SIZE)
#define PACKET_SIZE(p)	    (PACKET_HEADER_SIZE + p.length)


/* ----------------------------------------------------------------------- */
static int timeout_table_size;
static TIMEOUT_ENTRY *timeout;
static int index;
static int timeoutindex;

/*code copied from nl_table.c*/
static int find_address_timeout(CnetAddr address)
{
//  ATTEMPT TO LOCATE A KNOWN ADDRESS
    for(int t=0 ; t<timeout_table_size; ++t)
    	if(timeout[t].dest == address)
        	return t;

//  UNNOWN ADDRESS, SO WE MUST CREATE AND INITIALIZE A NEW ENTRY
    timeout = realloc(timeout, (timeout_table_size+1)*sizeof(TIMEOUT_ENTRY));
    memset(&timeout[timeout_table_size], 0, sizeof(TIMEOUT_ENTRY));
    timeout[timeout_table_size].dest = address;
    return timeout_table_size++;
}

static int find_address(CnetTimerID timer)
{
    for(int i = 0; i < timeout_table_size; i++){
        if(timeout[i].last_timer == timer)
            return i;
    }
    return -1;
}

/*code copied from nl_table.c*/
//-------------------------------------------------------------------
//  FIND THE LINK ON WHICH PACKETS OF MINIMUM HOP COUNT WERE OBSERVED.
//  IF THE BEST LINK IS UNKNOWN, WE RETURN ALL_LINKS.

int NL_linksofminhops(CnetAddr address) {
    int	t	= find_address_timeout(address);
    int link	= NL_table[t].minhop_link;
    return (link == 0) ? ALL_LINKS : (1 << link);
}

static	bool	given_stats	= false;

void NL_savehopcount(CnetAddr address, int hops, int link)
{
    int	t	= find_address_timeout(address);

    if(NL_table[t].minhops > hops) {
	NL_table[t].minhops	= hops;
	NL_table[t].minhop_link	= link;
	given_stats		= true;
    }
}

// -----------------------------------------------------------------
//print the contents of NL_Table
void DEBUG0_Events()
{
    printf("\n%13s %13s %13s %13s","destination", "ackexpected", "nextpkttosend", "pktexpected");
    if(given_stats==true) printf(" %8s %8s\n", "minhops", "minhop_link");
    for(int t=0 ; t<NL_table_size ; ++t)
    	if(NL_table[t].address != nodeinfo.address) {
        	printf("%13d %13d %13d %13d",(int)NL_table[t].address, NL_table[t].ackexpected,NL_table[t].nextpackettosend, NL_table[t].packetexpected);
        if(NL_table[t].minhop_link != 0) printf(" %8d %8d\n", NL_table[t].minhops,NL_table[t].minhop_link);
    }
}

//TIMERS info
void DEBUG1_Events()
{
    NL_PACKET p;
    char *packet_kind;
    for(int i=0; i < timeout_table_size; i++){
        p = timeout[i].last_pkt;
            if (p.kind == NL_DATA){
                packet_kind = "NL_DATA\0";
                break;
	    }
            if (p.kind == NL_ACK){
                packet_kind = "NL_ACK\0";
                break;
	    }
            else{
                packet_kind = "NEITHER\0";
                break;
	    }
	printf("\n\t Node name: %s.", nodeinfo.nodename);
    	printf("\n\t Node address: %d.", nodeinfo.address);
    	printf("\n\t count_toobusy = %d", count_toobusy);
        printf("\n\t Table Entry: %d", i);
	printf("\n\t Packet timer ID = %d", timeout[i].last_timer);
	printf("\n\t Packet seqno = %d", p.seqno);
        printf("\n\t Packet source = %d", p.src);
        printf("\n\t Packet destination = %d", p.dest);
        printf("\n\t Packet kind = %s", packet_kind);
        printf("\n\t Packet length = %d", p.length);
    }
}


/*-----------------------------------------------------------------------------
    Following code transfers from flooding2.c

    flood2() IS A BASIC ROUTING STRATEGY WHICH TRANSMITS THE OUTGOING PACKET
    ON EVERY LINK SPECIFIED IN THE BITMAP NAMED links_wanted.
 */
static void flood2(char *packet, size_t length, int links_wanted)
{
    int	   link;

    for(link=1 ; link<=nodeinfo.nlinks ; ++link)
	if( links_wanted & (1<<link) )
	    CHECK(down_to_datalink(link, packet, length));
}

/*  down_to_network() RECEIVES NEW MESSAGES FROM THE APPLICATION LAYER AND
    PREPARES THEM FOR TRANSMISSION TO OTHER NODES.
 */
static EVENT_HANDLER(down_to_network)
{
    NL_PACKET	p;

    p.length	= sizeof(p.msg);
    CHECK(CNET_read_application(&p.dest, p.msg, &p.length));
    CHECK(CNET_disable_application(p.dest));

    p.src	= nodeinfo.address;
    p.kind	= NL_DATA;
    p.hopcount	= 0;
    p.seqno	= NL_nextpackettosend(p.dest);

    flood2((char *)&p, PACKET_SIZE(p), ALL_LINKS);

    //Add timer
    timeoutindex = find_address_timeout(p.dest);
    memmove(&(timeout[timeoutindex].last_pkt), &p, sizeof(NL_PACKET));
    timeout[timeoutindex].last_timer = CNET_start_timer(EV_TIMER1, 80000000, 0);
}

/*  up_to_network() IS CALLED FROM THE DATA LINK LAYER (BELOW) TO ACCEPT
    A PACKET FOR THIS NODE, OR TO RE-ROUTE IT TO THE INTENDED DESTINATION.
 */
int up_to_network(char *packet, size_t length, int arrived_on)
{
    NL_PACKET	*p = (NL_PACKET *)packet;

    ++p->hopcount;			/* took 1 hop to get here */
/*  IS THIS PACKET IS FOR ME? */
    if(p->dest == nodeinfo.address) {
	switch (p->kind) {
	case NL_DATA:
	    if(p->seqno == NL_packetexpected(p->src)) {
		  CnetAddr	tmpaddr;

		  length		= p->length;
		  CHECK(CNET_write_application(p->msg, &length));
		  inc_NL_packetexpected(p->src);

		  tmpaddr		= p->src; /* swap src and dest addresses */
		  p->src		= p->dest;
		  p->dest		= tmpaddr;

		  p->kind		= NL_ACK;
		  p->hopcount	= 0;
		  p->length	= 0;
		  /* send the NL_ACK via the link on which the NL_DATA arrived */
		  flood2(packet, PACKET_HEADER_SIZE, (1<<arrived_on) );
	    }
	    break;
	case NL_ACK:
	    if(p->seqno == NL_ackexpected(p->src)) {
		  inc_NL_ackexpected(p->src);
		  CHECK(CNET_enable_application(p->src));
          index = find_address_timeout(p->src);
          CNET_stop_timer(timeout[index].last_timer);
	    }
	    break;
	}
    }
/* THIS PACKET IS FOR SOMEONE ELSE */
    else {
	   if(p->hopcount < MAXHOPS) 		/* if not too many hops... */
	    /* retransmit on all links *except* the one on which it arrived */
	       flood2(packet, length, ALL_LINKS & ~(1<<arrived_on) );
	   else
	    /* silently drop */;
    }
    return(0);
}
/*--------------------------------------------------------------------------------*/

/* Event Handler*/
EVENT_HANDLER(show_NL_table)
{
    DEBUG0_Events();
}

void reboot_NL_table()
{
    CHECK(CNET_set_handler(EV_DEBUG0, show_NL_table, 0));
    CHECK(CNET_set_debug_string(EV_DEBUG0, "NL info"));

    NL_table		= calloc(1, sizeof(NLTABLE));
    NL_table_size	= 0;
}

EVENT_HANDLER(timers_events)
{
    CNET_clear();
    DEBUG1_Events();
}

EVENT_HANDLER(timeout_events)
{
    timeoutindex = find_address(timer);
    NL_PACKET p = timeout[timeoutindex].last_pkt;
    timeout[timeoutindex].last_timer = CNET_start_timer(EV_TIMER1, 80000000, 0);
    flood2((char *)&p, PACKET_SIZE(p), ALL_LINKS);
}

EVENT_HANDLER(periodic_events)
{
    DEBUG0_Events();
    DEBUG1_Events();
}


/* ----------------------------------------------------------------------- */

EVENT_HANDLER(reboot_node)
{
    if(nodeinfo.nlinks >= 32) {
	fprintf(stderr,"flood2 flooding will not work here\n");
	exit(1);
    }
    reboot_DLL();
    reboot_NL_table();

    CHECK(CNET_set_handler(EV_APPLICATIONREADY, down_to_network, 0));
    CNET_enable_application(ALLNODES);

    CHECK(CNET_set_handler(EV_PERIODIC, periodic_events, 0));
    CHECK(CNET_set_handler(EV_TIMER1, timeout_events, 0));
    CHECK(CNET_set_handler(EV_DEBUG1, timers_events, 0));
    CHECK(CNET_set_debug_string( EV_DEBUG1, "TIMERS info"));
}
