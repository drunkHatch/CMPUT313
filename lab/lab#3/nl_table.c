#include <cnet.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "nl_table.h"

// ---- A SIMPLE NETWORK LAYER SEQUENCE TABLE AS AN ABSTRACT DATA TYPE ----

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

// -----------------------------------------------------------------

//  GIVEN AN ADDRESS, LOCATE OR CREATE ITS ENTRY IN THE NL_table
static int find_address(CnetAddr address)
{
//  ATTEMPT TO LOCATE A KNOWN ADDRESS
    for(int t=0 ; t<NL_table_size ; ++t)
	if(NL_table[t].address == address)
	    return t;

//  UNNOWN ADDRESS, SO WE MUST CREATE AND INITIALIZE A NEW ENTRY
    NL_table	= realloc(NL_table, (NL_table_size+1)*sizeof(NLTABLE));
    memset(&NL_table[NL_table_size], 0, sizeof(NLTABLE));
    NL_table[NL_table_size].address	= address;
    NL_table[NL_table_size].minhops	= INT_MAX;
    return NL_table_size++;
}

int NL_ackexpected(CnetAddr address) {
    int	t	= find_address(address);
    return NL_table[t].ackexpected;
}

void inc_NL_ackexpected(CnetAddr address) {
    int	t	= find_address(address);
    NL_table[t].ackexpected++;
}

int NL_nextpackettosend(CnetAddr address) {
    int	t	= find_address(address);
    return NL_table[t].nextpackettosend++;
}

int NL_packetexpected(CnetAddr address) {
    int	t	= find_address(address);
    return NL_table[t].packetexpected;
}

void inc_NL_packetexpected(CnetAddr address) {
    int	t	= find_address(address);
    NL_table[t].packetexpected++;
}


