#ifndef EVENTHANDLER_H_
#define EVENTHANDLER_H_

#include "Common.h"
#include <event.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/listener.h>
#include "TlsWrapper.h"

class EventHandler {
public:
	EventHandler();
	virtual ~EventHandler();

	void loop();

	struct event_base* getEventBase();

private:
	static EventHandler* evHandle;
	struct event_base *evbase_;
};

#endif /* EVENTHANDLER_H_ */
