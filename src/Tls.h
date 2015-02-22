#ifndef INCLUDED_Tls
#define INCLUDED_Tls

#include <cstddef>

class TlsSession;

class TlsContext {
public:
	static TlsContext* create();
	virtual ~TlsContext() {}

	virtual TlsSession* createSession() { return 0; }
	virtual bool setCertificate(const char* certificate) { return false; }
	virtual bool setPrivateKey(const char* privateKey) { return false; }
	virtual bool addTrustedCertificate(const char* certificate) { return false; }
};

class TlsSession {
public:
	virtual ~TlsSession() {}

	virtual void setHostname(const char* hostname) {}
	virtual void startAccept() = 0;
	virtual void startConnect() = 0;
	virtual void shutdown() = 0;

	virtual int getPeerCertificate(char* buffer, size_t bytes) { return -1; }

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

	virtual bool getError(char* buffer, size_t bytes) { return false; }
};

#endif
