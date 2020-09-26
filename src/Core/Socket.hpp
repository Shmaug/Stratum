#pragma once

#include <Util/Util.hpp>

#ifndef WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#else
#include <WS2tcpip.h>
#endif

namespace stm {

enum SocketType {
	Listen, Connect
};

class Socket {
public:
	STRATUM_API Socket(int socket);
	STRATUM_API Socket(SocketType type, std::string host, uint32_t port, uint32_t family = AF_INET, uint32_t sockType = SOCK_STREAM);
	STRATUM_API ~Socket();

	STRATUM_API int SetSockopt(int level, int optname, void* val, socklen_t len);
	
	STRATUM_API bool NoDelay(bool b);
	STRATUM_API bool ReuseAddress(bool b);
	STRATUM_API bool Blocking(bool b);

	STRATUM_API bool Bind(); // Binds Listen socket
	STRATUM_API bool Accept(); // Accept Listen socket
	STRATUM_API bool Listen(int backlog = 5); // Listen Listen socket
	STRATUM_API bool Connect(int timeout = 0); // Connect Connect socket
	STRATUM_API bool Send(const void* buf, size_t len, int flags = 0);
	STRATUM_API int Receive(void* buf, size_t len, int flags = 0);

	inline bool Valid() const { return mSocket >= 0; }
	inline void SocketFD(int socket) { mSocket = socket; }
	inline int SocketFD() const { return mSocket; }

protected:
	int mSocket;
	SocketType mType;
	uint32_t mFamily;
	uint32_t mSocketType;
	std::string mHost;
	uint32_t mPort;
	struct addrinfo* mRes;

	bool mBlockingState;
};

}