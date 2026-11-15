/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef ENGINE_HTTP_H
#define ENGINE_HTTP_H

#include "kernel.h"

class CHttpRequest;

// Runs the HTTP requests of the process on one thread with libcurl's multi
// interface, so a transfer no longer occupies a job thread of the engine.
class IHttp : public IInterface
{
	MACRO_INTERFACE("http", 0)
public:
	// Hands the request to the http thread. The caller keeps ownership of the
	// object; the transfer starts as soon as the thread gets to it.
	virtual void Start(CHttpRequest *pRequest) = 0;

	// Cancels the transfer and waits until the http thread no longer touches
	// the object. Called by ~CHttpRequest(), so deleting a request that is
	// still running is safe.
	virtual void Abort(CHttpRequest *pRequest) = 0;

	// Cancels everything that is in flight, marks the requests as done and
	// stops the http thread. Requests that are still alive afterwards are not
	// touched again.
	virtual void Shutdown() = 0;
};

// IHttp implementations must outlive every request they were given.
IHttp *CreateHttp();

#endif // ENGINE_HTTP_H
