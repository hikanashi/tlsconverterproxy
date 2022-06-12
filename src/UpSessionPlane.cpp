#include "UpSessionPlane.h"
#include "ServerConnection.h"
#include "ClientConnection.h"

UpSessionPlane::UpSessionPlane(ServerConnection& handler)
	: UpSession(handler)
{

}

UpSessionPlane::~UpSessionPlane()
{
}

int UpSessionPlane::on_read(unsigned char *data, size_t datalen)
{

	if (datalen == 0 /* || handler_->get_should_close_after_write()*/)
	{
		return 0;
	}

	size_t readlen = 0;

	readlen = handler_.getClient()->write(data,datalen);

	return readlen;
}


int32_t UpSessionPlane::send(
		HttpMessagePtr&	message)
{
	return 0;
}

int UpSessionPlane::on_write()
{
  return 0;
}

int UpSessionPlane::on_event()
{
  return 0;
}

int UpSessionPlane::DoFlush()
{

	return 0;
}
