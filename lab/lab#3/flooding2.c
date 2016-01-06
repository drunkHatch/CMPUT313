#include <cnet.h>
#include <stdlib.h>

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

#define PACKET_HEADER_SIZE  (sizeof(NL_PACKET) - MAX_MESSAGE_SIZE)
#define PACKET_SIZE(p)	    (PACKET_HEADER_SIZE + p.length)


/* ----------------------------------------------------------------------- */

/*  flood2() IS A BASIC ROUTING STRATEGY WHICH TRANSMITS THE OUTGOING PACKET
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
}
