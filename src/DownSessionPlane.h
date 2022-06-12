#ifndef DOWNSESSIONPLANE_H_
#define DOWNSESSIONPLANE_H_

#include "DownSession.h"

class DownSessionPlane: public DownSession
{
public:
	DownSessionPlane(ClientConnection& handle);
	virtual ~DownSessionPlane();

	virtual int on_read(unsigned char *data, size_t datalen);
	virtual int on_write();
	virtual int on_event(short events);
	virtual int DoFlush();


	virtual int32_t send(
						HttpMessagePtr&	message);

};

#endif /* DOWNSESSIONPLANE_H_ */
