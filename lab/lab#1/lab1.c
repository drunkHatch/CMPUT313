#include <cnet.h>

/*  This function will be invoked each time the Application Layer has a
    message to deliver. Once we read the message from the application layer
    with CNET_read_application(), the Application Layer will be able to supply
    us with another message. The rate of new messages is controlled by
    the 'messagerate' node attribute.
 */

static EVENT_HANDLER(application_ready)
{
    CnetAddr	destaddr;
    char	buffer[MAX_MESSAGE_SIZE];
    size_t	length;
    int         link = 1;


/*  Firstly, indicate the maximum sized message we are willing to receive */
    length = sizeof(buffer);

/*  Accept the message from the Application Layer.  We will be informed of the
    message's destination address and length and buffer will be filled in. */

    //CNET_disable_application(ALLNODES);

    CNET_read_application(&destaddr, buffer, &length);
    CNET_write_physical(link, buffer, &length);

    printf("\tI have a message of %4d bytes for address %d\n",
			    length, (int)destaddr);
}

static EVENT_HANDLER(button_pressed)
{
    printf("\n Node name       : %s\n",	nodeinfo.nodename);
    printf(" Node number     : %d\n",	nodeinfo.nodenumber);
    printf(" Node address    : %d\n",	nodeinfo.address);
    printf(" Number of links : %d\n\n",	nodeinfo.nlinks);
}

///////////////////////////////////////////////////////////////////
static EVENT_HANDLER(physical_ready) {
  // CnetAddr   destaddr;
    int link;
    char buffer[MAX_MESSAGE_SIZE];
    size_t length;
    /*Firstly, indicate the maximum sized message we are willing to receive */
    length = sizeof(buffer);   

    /*Accept the message from the Application Layer. We will be informed of the
      message's destination address and length and buffer will be filled in. */
    CNET_read_physical(&link, buffer, &length);
    CNET_write_application(buffer, &length);
    printf("\tI receive a message of %4d bytes\n", length);
    
}

///////////////////////////////////////////////////////////////////
EVENT_HANDLER(reboot_node)
{
/*  Indicate that we are interested in hearing about the Application Layer
    having messages for delivery and indicate which function to invoke for them.
 */
    
    CNET_set_handler(EV_APPLICATIONREADY, application_ready, 0);
    if (nodeinfo.address == 200){
      CNET_set_handler(EV_PHYSICALREADY, physical_ready, 0);
    }
    CNET_set_handler(EV_DEBUG0, button_pressed, 0);
    CNET_set_debug_string(EV_DEBUG0, "Node Info");

    CNET_enable_application(200);
}
