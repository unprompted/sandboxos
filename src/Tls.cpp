#include "Tls.h"

#if !defined (WIN32) && !defined (__MACH__)
#include <cstring>
#include <locale>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

class Tls_openssl : public Tls {
public:
	Tls_openssl(const char* key, const char* certificate);
	~Tls_openssl();

	void startConnect() override;
	void startAccept() override;
	void shutdown() override;
	HandshakeResult handshake() override;

	int readPlain(char* buffer, size_t bytes) override;
	int writePlain(const char* buffer, size_t bytes) override;

	int readEncrypted(char* buffer, size_t bytes) override;
	int writeEncrypted(const char* buffer, size_t bytes) override;

	void setHostname(const char* hostname) override;

private:
	bool verifyPeerCertificate();
	bool verifyHostname(X509* certificate, const char* hostname);
	bool wildcardMatch(const char* pattern, const char* name);

	std::string _hostname;
	BIO* _bioIn = 0;
	BIO* _bioOut = 0;
	SSL* _ssl = 0;
	SSL_CTX* _context = 0;
	enum { kUndetermined, kAccept, kConnect } _direction;
};

Tls_openssl::Tls_openssl(const char* key, const char* certificate) {
	SSL_library_init();
	SSL_load_error_strings();

	_context = SSL_CTX_new(SSLv23_method());
	SSL_CTX_set_default_verify_paths(_context);
	_bioIn = BIO_new(BIO_s_mem());
	_bioOut = BIO_new(BIO_s_mem());

	if (key) {
		BIO* bio = BIO_new(BIO_s_mem());
		BIO_puts(bio, key);
		EVP_PKEY* privateKey = PEM_read_bio_PrivateKey(bio, 0, 0, 0);
		SSL_CTX_use_PrivateKey(_context, privateKey);
		BIO_free(bio);
	}

	if (certificate) {
		BIO* bio = BIO_new(BIO_s_mem());
		BIO_puts(bio, certificate);
		X509* x509 = PEM_read_bio_X509(bio, 0, 0, 0);
		SSL_CTX_use_certificate(_context, x509);
		BIO_free(bio);
	}
}

Tls_openssl::~Tls_openssl() {
	if (_ssl) {
		SSL_free(_ssl);
	}
	SSL_CTX_free(_context);
}

void Tls_openssl::startAccept() {
	_direction = kAccept;
	_ssl = SSL_new(_context);
	SSL_set_bio(_ssl, _bioIn, _bioOut);
	SSL_accept(_ssl);
	handshake();
}

void Tls_openssl::startConnect() {
	_direction = kConnect;
	_ssl = SSL_new(_context);
	SSL_set_bio(_ssl, _bioIn, _bioOut);
	SSL_set_verify(_ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);
	SSL_connect(_ssl);
	handshake();
}

void Tls_openssl::shutdown() {
	SSL_shutdown(_ssl);
}

Tls::HandshakeResult Tls_openssl::handshake() {
	Tls::HandshakeResult result = kDone;
	if (!SSL_is_init_finished(_ssl)) {
		int value = SSL_do_handshake(_ssl);
		if (value <= 0) {
			int error = SSL_get_error(_ssl, value);
			if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
				result = kFailed;
			} else {
				result = kMore;
			}
		}
	}
	if (result == kDone && _direction == kConnect && !verifyPeerCertificate()) {
		result = kFailed;
	}
	return result;
}

int Tls_openssl::readPlain(char* buffer, size_t bytes) {
	int result = SSL_read(_ssl, buffer, bytes);
	if (result <= 0) {
		int error = SSL_get_error(_ssl, result);
		if (error == SSL_ERROR_ZERO_RETURN) {
			if ((SSL_get_shutdown(_ssl) & SSL_RECEIVED_SHUTDOWN) != 0) {
				result = kReadZero;
			} else {
				result = 0;
			}
		} else if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
			result = kReadFailed;
		}
	}
	return result;
}

int Tls_openssl::writePlain(const char* buffer, size_t bytes) {
	return SSL_write(_ssl, buffer, bytes);
}

int Tls_openssl::readEncrypted(char* buffer, size_t bytes) {
	return BIO_read(_bioOut, buffer, bytes);
}

int Tls_openssl::writeEncrypted(const char* buffer, size_t bytes) {
	return BIO_write(_bioIn, buffer, bytes);
}

void Tls_openssl::setHostname(const char* hostname) {
	_hostname = hostname;
}

bool Tls_openssl::verifyPeerCertificate() {
	bool verified = false;
	X509* certificate = SSL_get_peer_certificate(_ssl);
	if (certificate) {
		if (SSL_get_verify_result(_ssl) == X509_V_OK
			&& verifyHostname(certificate, _hostname.c_str())) {
			verified = true;
		}
		X509_free(certificate);
	}
	return verified;
}

bool Tls_openssl::wildcardMatch(const char* pattern, const char* name) {
	while (*pattern && *name) {
		if (*pattern == '*') {
			for (const char* p = name; *p; ++p) {
				if (wildcardMatch(pattern + 1, p)) {
					return true;
				}
			}
			return false;
		} else if (std::tolower(*pattern) == std::tolower(*name)) {
			++pattern;
			++name;
		} else {
			break;
		}
	}
	return *pattern == 0 && *name == 0;
}

bool Tls_openssl::verifyHostname(X509* certificate, const char* hostname) {
	bool verified = false;
	void* names = X509_get_ext_d2i(certificate, NID_subject_alt_name, 0, 0);
	if (names) {
		int count = sk_GENERAL_NAME_num(names);
		for (int i = 0; i < count; ++i) {
			const GENERAL_NAME* check = sk_GENERAL_NAME_value(names, i);
			const char* name = reinterpret_cast<const char*>(ASN1_STRING_data(check->d.ia5));
			size_t length = ASN1_STRING_length(check->d.ia5);
			if (wildcardMatch(std::string(name, length).c_str(), hostname)) {
				verified = true;
				break;
			}
		}
	}
	return verified;
}

Tls* Tls::create(const char* key, const char* certificate) {
	return new Tls_openssl(key, certificate);
}
#elif defined (__MACH__)
#include <Security/SecureTransport.h>
#include <cstring>
#include <locale>
#include <vector>

class Tls_commoncrypto : public Tls {
public:
	Tls_commoncrypto(const char* key, const char* certificate);
	~Tls_commoncrypto();

	void startConnect() override;
	void startAccept() override;
	void shutdown() override;
	HandshakeResult handshake() override;

	int readPlain(char* buffer, size_t bytes) override;
	int writePlain(const char* buffer, size_t bytes) override;

	int readEncrypted(char* buffer, size_t bytes) override;
	int writeEncrypted(const char* buffer, size_t bytes) override;

	void setHostname(const char* hostname) { _hostname = hostname; }

private:
	static OSStatus writeCallback(SSLConnectionRef connection, const void* data, size_t* dataLength);
	static OSStatus readCallback(SSLConnectionRef connection, void* data, size_t* dataLength);

	SSLContextRef _context = 0;
	std::vector<char> _inBuffer;
	std::vector<char> _outBuffer;
	std::string _hostname;
	bool _shutdown = false;
};

Tls_commoncrypto::Tls_commoncrypto(const char* key, const char* certificate) {
	/*_context = SSL_CTX_new(SSLv23_method());
	SSL_CTX_set_default_verify_paths(_context);
	_bioIn = BIO_new(BIO_s_mem());
	_bioOut = BIO_new(BIO_s_mem());

	if (key) {
		BIO* bio = BIO_new(BIO_s_mem());
		BIO_puts(bio, key);
		EVP_PKEY* privateKey = PEM_read_bio_PrivateKey(bio, 0, 0, 0);
		SSL_CTX_use_PrivateKey(_context, privateKey);
		BIO_free(bio);
	}

	if (certificate) {
		BIO* bio = BIO_new(BIO_s_mem());
		BIO_puts(bio, certificate);
		X509* x509 = PEM_read_bio_X509(bio, 0, 0, 0);
		SSL_CTX_use_certificate(_context, x509);
		BIO_free(bio);
	}*/
}

Tls_commoncrypto::~Tls_commoncrypto() {
	CFRelease(_context);
	_context = 0;
}

void Tls_commoncrypto::startAccept() {
	_context = SSLCreateContext(0, kSSLServerSide, kSSLStreamType);
	SSLSetIOFuncs(_context, readCallback, writeCallback);
	SSLSetConnection(_context, this);
	handshake();
}

void Tls_commoncrypto::startConnect() {
	_context = SSLCreateContext(0, kSSLClientSide, kSSLStreamType);
	SSLSetIOFuncs(_context, readCallback, writeCallback);
	SSLSetConnection(_context, this);
	SSLSetPeerDomainName(_context, _hostname.c_str(), _hostname.size());
	handshake();
}

void Tls_commoncrypto::shutdown() {
	if (!_outBuffer.size()) {
		SSLClose(_context);
		_shutdown = false;
	} else {
		_shutdown = true;
	}
}

Tls::HandshakeResult Tls_commoncrypto::handshake() {
	Tls::HandshakeResult result = Tls::kFailed;
	OSStatus status = SSLHandshake(_context);
	switch (status) {
	case noErr:
		result = Tls::kDone;
		break;
	case errSSLWouldBlock:
		result = Tls::kMore;
		break;
	default:
		result = Tls::kFailed;
		break;
	}
	return result;
}

int Tls_commoncrypto::readPlain(char* buffer, size_t bytes) {
	int result = 0;
	size_t processed = bytes;
	OSStatus status = SSLRead(_context, buffer, bytes, &processed);
	if (status == noErr) {
		result = processed;
	} else if (status == errSSLWouldBlock) {
		result = processed;
	} else if (status == errSSLClosedGraceful) {
		result = kReadZero;
	} else {
		result = kReadFailed;
	}
	return result;
}

int Tls_commoncrypto::writePlain(const char* buffer, size_t bytes) {
	int result = 0;
	size_t processed;
	OSStatus status = SSLWrite(_context, buffer, bytes, &processed);
	if (status == noErr) {
		result = processed;
	} else {
		result = -1;
	}
	return result;
}

OSStatus Tls_commoncrypto::writeCallback(SSLConnectionRef connection, const void* data, size_t* dataLength) {
	Tls_commoncrypto* tls = reinterpret_cast<Tls_commoncrypto*>(const_cast<void*>(connection));
	tls->_outBuffer.insert(tls->_outBuffer.end(), reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + *dataLength);
	if (tls->_shutdown && !tls->_outBuffer.size()) {
		SSLClose(tls->_context);
		tls->_shutdown = false;
	}
	return noErr;
}

OSStatus Tls_commoncrypto::readCallback(SSLConnectionRef connection, void* data, size_t* dataLength) {
	Tls_commoncrypto* tls = reinterpret_cast<Tls_commoncrypto*>(const_cast<void*>(connection));
	OSStatus result = noErr;
	size_t bytes = std::min(tls->_inBuffer.size(), *dataLength);
	if (bytes > 0) {
		std::memcpy(data, &*tls->_inBuffer.begin(), bytes);
		tls->_inBuffer.erase(tls->_inBuffer.begin(), tls->_inBuffer.begin() + bytes);
	}
	if (bytes < *dataLength) {
		result = errSSLWouldBlock;
	}
	*dataLength = bytes;
	return result;
}

int Tls_commoncrypto::readEncrypted(char* buffer, size_t bytes) {
	size_t size = std::min(bytes, _outBuffer.size());
	if (size > 0) {
		std::memcpy(buffer, &*_outBuffer.begin(), size);
		_outBuffer.erase(_outBuffer.begin(), _outBuffer.begin() + size);
	}
	return size;
}

int Tls_commoncrypto::writeEncrypted(const char* buffer, size_t bytes) {
	_inBuffer.insert(_inBuffer.end(), buffer, buffer + bytes);
	return bytes;
}

Tls* Tls::create(const char* key, const char* certificate) {
	return new Tls_commoncrypto(key, certificate);
}
#else
Tls* Tls::create(const char* key, const char* certificate) {
	return 0;
}
#endif
