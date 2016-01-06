#include <cnet.h>

/* ------- DECLARATIONS FOR A MINIMAL RELIABLE DATALINK LAYER -------- */

#define	MAX_FRAME_SIZE	(MAX_MESSAGE_SIZE + 1024)

extern int count_toobusy;

extern	int	down_to_datalink(int link, char *packet, size_t length);
extern	void	reboot_DLL(void);
