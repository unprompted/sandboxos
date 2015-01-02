#ifndef INCLUDED_Tls
#define INCLUDED_Tls

#include <cstddef>

class Tls {
public:
	typedef int (ReadCallback)(Tls* connection, char* buffer, size_t bytes);
	typedef int (WriteCallback)(Tls* connection, const char* buffer, size_t bytes);

	static Tls* create(const char* key, const char* certificate);
	virtual ~Tls() {}

	virtual void startAccept() = 0;
	virtual void startConnect() = 0;
	virtual void shutdown() = 0;

	enum HandshakeResult {
		kDone,
		kMore,
		kFailed,
	};
	virtual HandshakeResult handshake() = 0;

	enum ReadResult {
		kReadZero = -1,
		kReadFailed = -2,
	};
	virtual int readPlain(char* buffer, size_t bytes) = 0;
	virtual int writePlain(const char* buffer, size_t bytes) = 0;

	virtual int readEncrypted(char* buffer, size_t bytes) = 0;
	virtual int writeEncrypted(const char* buffer, size_t bytes) = 0;

	virtual void setHostname(const char* hostname) = 0;
};

#endif
