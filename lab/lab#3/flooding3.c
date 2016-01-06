#include <cnet.h>
#include <stdlib.h>

#include "nl_table.h"
#include "dll_basic.h"

#define	MAXHOPS		4

/*  This file implements a much better flooding algorithm than those in
    both flooding1.c and flooding2.c. As Network Layer packets are processed,
    the information in their headers is used to update the NL table.

    The minimum observed hopcount to each (potential) remote destination
    is remembered by the NL table, as is the link on which these packets
    arrive. This link is later used to route packets leaving for that node.

    The routine NL_savehopcount() is called for both NL_DATA and NL_ACK
    packets, and we even "steal" information from Network Layer packets
    that don't belong to us!

    I don't think it's a flooding algorithm any more Toto.

    This flooding algorithm exhibits an efficiency which improves over time
    (as the NL table "learns" more).  Over the 8 nodes in the AUSTRALIA.MAP
    file, the initial efficiency is the same as that of flooding1.c (about
    2%) but as the NL table improves, the efficiency rises to over 64%.
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

/*  flood3() IS A BASIC ROUTING STRATEGY WHICH TRANSMITS THE OUTGOING PACKET
    ON EITHER THE SPECIFIED LINK, OR ALL BEST-KNOWN LINKS WHILE AVOIDING
    ANY OTHER SPECIFIED LINK.
 */
static void flood3(char *packet, size_t length, int choose_link, int avoid_link)
{
/*  REQUIRED LINK IS PROVIDED - USE IT */
    if(choose_link != 0)
	CHECK(down_to_datalink(choose_link, packet, length));

/*  OTHERWISE, CHOOSE THE BEST KNOWN LINKS, AVOIDING ANY SPECIFIED ONE */
    else {
	NL_PACKET	*p = (NL_PACKET *)packet;
	int		links_wanted = NL_linksofminhops(p->dest);
	int		link;

	for(link=1 ; link<=nodeinfo.nlinks ; ++link) {
	    if(link == avoid_link)		/* possibly avoid this one */
		continue;
	    if( links_wanted & (1<<link) )	/* use this link if wanted */
		CHECK(down_to_datalink(link, packet, length));
	}
    }
}

/*  down_to_network() RECEIVES NEW MESSAGES FROM THE APPLICATION LAYER AND
    PREPARES THEM FOR TRANSMISSION TO OTHER NODES.
 */
static EVENT_HANDLER(down_to_network)
{
    NL_PACKET	p;

    p.length	= sizeof(p.msg);
    CHECK(CNET_read_application(&p.dest, p.msg, &p.length));
    CNET_disable_application(p.dest);

    p.src	= nodeinfo.address;
    p.kind	= NL_DATA;
    p.hopcount	= 0;
    p.seqno	= NL_nextpackettosend(p.dest);

    flood3((char *)&p, PACKET_SIZE(p), 0, 0);
}

/*  up_to_network() IS CALLED FROM THE DATA LINK LAYER (BELOW) TO ACCEPT
    A PACKET FOR THIS NODE, OR TO RE-ROUTE IT TO THE INTENDED DESTINATION.
 */
int up_to_network(char *packet, size_t length, int arrived_on_link)
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

		NL_savehopcount(p->src, p->hopcount, arrived_on_link);

		tmpaddr	 	= p->src;  /* swap src and dest addresses */
		p->src	 	= p->dest;
		p->dest	 	= tmpaddr;

		p->kind	 	= NL_ACK;
		p->hopcount	= 0;
		p->length	= 0;
		flood3(packet, PACKET_HEADER_SIZE, arrived_on_link, 0);
	    }
	    break;

	case NL_ACK:
	    if(p->seqno == NL_ackexpected(p->src)) {
		inc_NL_ackexpected(p->src);
		NL_savehopcount(p->src, p->hopcount, arrived_on_link);
		CHECK(CNET_enable_application(p->src));
	    }
	    break;
	}
    }
/* THIS PACKET IS FOR SOMEONE ELSE */
    else {
	if(p->hopcount < MAXHOPS) {		/* if not too many hops... */
	    NL_savehopcount(p->src, p->hopcount, arrived_on_link);
	    /* retransmit on best links *except* the one on which it arrived */
	    flood3(packet, length, 0, arrived_on_link);
	}
	else
	    /* silently drop */;
    }
    return(0);
}


/* ----------------------------------------------------------------------- */

EVENT_HANDLER(reboot_node)
{
    if(nodeinfo.nlinks > 32) {
	fprintf(stderr,"flood3 flooding will not work here\n");
	exit(1);
    }

    reboot_DLL();
    reboot_NL_table();

    CHECK(CNET_set_handler(EV_APPLICATIONREADY, down_to_network, 0));
    CHECK(CNET_enable_application(ALLNODES));
}
