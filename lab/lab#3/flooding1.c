#include <cnet.h>
#include <stdlib.h>

#include "nl_table.h"
#include "dll_basic.h"

#define	MAXHOPS		4

/*  This is an implementation of a very naive flooding algorithm in cnet.
    Whenever a new Network Layer packet requires delivery, it is
    transmitted on *all* physical links. To limit the combinatoric
    explosion in the number of data packets in the whole network, data
    packets are disgarded after they have travelled MAXHOPS hops.

    The purpose of this example is to demonstrate the flooding process
    itself, and for this reason only a minimal datalink layer protocol is
    required to deliver the packets - provided in dll_basic.c
    Notice the separation of the Network Layer and the Datalink Layer code.

    This naive flooding algorithm exhibits a very poor efficiency.
    Over the 8 nodes in the AUSTRALIA.MAP file, the efficiency is typically
    only about 2%.
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

/*  flood1() IS A VERY BASIC ROUTING STRATEGY WHICH TRANSMITS THE
    OUTGOING PACKET ON EVERY AVAILABLE LINK.
 */
static void flood1(char *packet, size_t length)
{
    int	   link;

    for(link=1 ; link<=nodeinfo.nlinks ; ++link)
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

    flood1((char *)&p, PACKET_SIZE(p));
}

/*  up_to_network() IS CALLED FROM THE DATA LINK LAYER (BELOW) TO ACCEPT
    A PACKET FOR THIS NODE, OR TO RE-ROUTE IT TO THE INTENDED DESTINATION.
 */
int up_to_network(char *packet, size_t length, int arrived_on)
{
    NL_PACKET	*p = (NL_PACKET *)packet;

    ++p->hopcount;			/* took 1 hop to get here */
/*  IS THIS PACKET IS FOR ME? */
    if(p->dest == nodeinfo.address)

	switch (p->kind) {
	case NL_DATA :
	    if(p->seqno == NL_packetexpected(p->src)) {
		CnetAddr	tmpaddr;

		length		= p->length;
		CHECK(CNET_write_application(p->msg, &length));
		inc_NL_packetexpected(p->src);

		tmpaddr		= p->src;   /* swap src and dest addresses */
		p->src		= p->dest;
		p->dest		= tmpaddr;

		p->kind		= NL_ACK;
		p->hopcount	= 0;
		p->length	= 0;
		flood1(packet, PACKET_HEADER_SIZE);	/* flood NL_ACK */
	    }
	    break;
	case NL_ACK :
	    if(p->seqno == NL_ackexpected(p->src)) {
		inc_NL_ackexpected(p->src);
		CHECK(CNET_enable_application(p->src));
	    }
	    break;
	}
/* OTHERWISE, THIS PACKET IS FOR SOMEONE ELSE */
    else {
	if(p->hopcount < MAXHOPS)	/* if not made too many hops... */
	    flood1(packet, length);	/* retransmit - flood the packet */
	else
	    /* silently drop */;
    }
    return(0);
}

/* ----------------------------------------------------------------------- */

EVENT_HANDLER(reboot_node)
{
    reboot_DLL();
    reboot_NL_table();

    CHECK(CNET_set_handler(EV_APPLICATIONREADY, down_to_network, 0));
    CNET_enable_application(ALLNODES);
}
