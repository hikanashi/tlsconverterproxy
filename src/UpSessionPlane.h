#ifndef UPSESSIONPLANE_H_
#define UPSESSIONPLANE_H_

#include "UpSession.h"

class ServerConnection;

class UpSessionPlane : public UpSession
{
public:
	UpSessionPlane(ServerConnection& handler);
	virtual ~UpSessionPlane();

	virtual int on_read(unsigned char *data, size_t datalen);
	virtual int on_write();
	virtual int on_event();
	virtual int DoFlush();
	virtual int32_t send(
					HttpMessagePtr&	message);

};

#endif /* UPSESSIONPLANE_H_ */
