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

	bool verifyPeerCertificate(const char* hostname) override;

private:
	static int onRead(Tls* tls, char* buffer, size_t bytes);
	static int onWrite(Tls* tls, const char* buffer, size_t bytes);

	bool verifyHostname(X509* certificate, const char* hostname);
	bool wildcardMatch(const char* pattern, const char* name);

	BIO* _bioIn = 0;
	BIO* _bioOut = 0;
	SSL* _ssl = 0;
	SSL_CTX* _context = 0;
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
	_ssl = SSL_new(_context);
	SSL_set_bio(_ssl, _bioIn, _bioOut);
	SSL_accept(_ssl);
	handshake();
}

void Tls_openssl::startConnect() {
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

int Tls_openssl::onRead(Tls* connection, char* buffer, size_t bytes) {
	Tls_openssl* tls = reinterpret_cast<Tls_openssl*>(connection);
	return tls->readEncrypted(buffer, bytes);
}

int Tls_openssl::onWrite(Tls* connection, const char* buffer, size_t bytes) {
	Tls_openssl* tls = reinterpret_cast<Tls_openssl*>(connection);
	return tls->writeEncrypted(buffer, bytes);
}

int Tls_openssl::readEncrypted(char* buffer, size_t bytes) {
	return BIO_read(_bioOut, buffer, bytes);
}

int Tls_openssl::writeEncrypted(const char* buffer, size_t bytes) {
	return BIO_write(_bioIn, buffer, bytes);
}

bool Tls_openssl::verifyPeerCertificate(const char* hostname) {
	bool verified = false;
	X509* certificate = SSL_get_peer_certificate(_ssl);
	if (certificate) {
		if (SSL_get_verify_result(_ssl) == X509_V_OK
			&& verifyHostname(certificate, hostname)) {
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
#else
Tls* Tls::create(const char* key, const char* certificate) {
	return 0;
}
#endif
