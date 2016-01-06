#include <cnet.h>

#define	ALL_LINKS	(-1)

extern	void	reboot_NL_table(void);

extern	int	NL_ackexpected(CnetAddr address);
extern	int	NL_nextpackettosend(CnetAddr address);
extern	int	NL_packetexpected(CnetAddr address);

extern	void	inc_NL_packetexpected(CnetAddr address);
extern	void	inc_NL_ackexpected(CnetAddr address);

extern	int	NL_linksofminhops(CnetAddr address);
extern	void	NL_savehopcount(CnetAddr address, int hops, int link);
