#include "Tls.h"

#if !defined (_WIN32) && !defined (__MACH__)
#include <cstring>
#include <locale>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

class TlsContext_openssl : public TlsContext {
public:
	TlsContext_openssl();
	~TlsContext_openssl() override;
	TlsSession* createSession() override;
	bool setCertificate(const char* certificate) override;
	bool setPrivateKey(const char* privateKey) override;
	bool addTrustedCertificate(const char* certificate) override;

	SSL_CTX* getContext() { return _context; }

private:
	SSL_CTX* _context = 0;
};

class TlsSession_openssl : public TlsSession {
public:
	TlsSession_openssl(TlsContext_openssl* context);
	~TlsSession_openssl();

	void setHostname(const char* hostname) override;
	void startConnect() override;
	void startAccept() override;

	int getPeerCertificate(char* buffer, size_t size) override;

	void shutdown() override;
	HandshakeResult handshake() override;

	int readPlain(char* buffer, size_t bytes) override;
	int writePlain(const char* buffer, size_t bytes) override;

	int readEncrypted(char* buffer, size_t bytes) override;
	int writeEncrypted(const char* buffer, size_t bytes) override;

	bool getError(char* buffer, size_t bytes) override;

private:
	bool verifyPeerCertificate();
	bool verifyHostname(X509* certificate, const char* hostname);
	bool wildcardMatch(const char* pattern, const char* name);

	TlsContext_openssl* _context = 0;
	BIO* _bioIn = 0;
	BIO* _bioOut = 0;
	SSL* _ssl = 0;
	std::string _hostname;
	enum { kUndetermined, kAccept, kConnect } _direction = kUndetermined;
};

TlsSession* TlsContext_openssl::createSession() {
	return new TlsSession_openssl(this);
}

TlsContext_openssl::TlsContext_openssl() {
	SSL_library_init();
	SSL_load_error_strings();

	_context = SSL_CTX_new(SSLv23_method());
	SSL_CTX_set_default_verify_paths(_context);
}

TlsContext_openssl::~TlsContext_openssl() {
	SSL_CTX_free(_context);
}

bool TlsContext_openssl::setCertificate(const char* certificate) {
	int result = 0;
	BIO* bio = BIO_new(BIO_s_mem());
	BIO_puts(bio, certificate);
	X509* x509 = PEM_read_bio_X509(bio, 0, 0, 0);
	result = SSL_CTX_use_certificate(_context, x509);
	BIO_free(bio);
	return result == 1;
}

bool TlsContext_openssl::setPrivateKey(const char* privateKey) {
	int result = 0;
	BIO* bio = BIO_new(BIO_s_mem());
	BIO_puts(bio, privateKey);
	EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, 0, 0, 0);
	result = SSL_CTX_use_PrivateKey(_context, key);
	BIO_free(bio);
	return result == 1;
}

bool TlsContext_openssl::addTrustedCertificate(const char* certificate) {
	bool result = false;
	BIO* bio = BIO_new_mem_buf(const_cast<char*>(certificate), -1);
	X509* x509 = PEM_read_bio_X509(bio, 0, 0, 0);
	BIO_free(bio);

	if (x509) {
		X509_STORE* store = SSL_CTX_get_cert_store(_context);
		if (store && X509_STORE_add_cert(store, x509) == 1) {
			result = true;
		}
		X509_free(x509);
	}

	return result;
}

TlsContext* TlsContext::create() {
	return new TlsContext_openssl();
}

TlsSession_openssl::TlsSession_openssl(TlsContext_openssl* context) {
	_context = context;
	_bioIn = BIO_new(BIO_s_mem());
	_bioOut = BIO_new(BIO_s_mem());
}

TlsSession_openssl::~TlsSession_openssl() {
	if (_ssl) {
		SSL_free(_ssl);
	}
}

void TlsSession_openssl::setHostname(const char* hostname) {
	_hostname = hostname;
}

void TlsSession_openssl::startAccept() {
	_direction = kAccept;
	_ssl = SSL_new(_context->getContext());
	SSL_set_bio(_ssl, _bioIn, _bioOut);
	SSL_accept(_ssl);
	handshake();
}

void TlsSession_openssl::startConnect() {
	_direction = kConnect;
	_ssl = SSL_new(_context->getContext());
	SSL_set_bio(_ssl, _bioIn, _bioOut);

	SSL_connect(_ssl);
	handshake();
}

void TlsSession_openssl::shutdown() {
	SSL_shutdown(_ssl);
}

TlsSession::HandshakeResult TlsSession_openssl::handshake() {
	TlsSession::HandshakeResult result = kDone;
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

int TlsSession_openssl::readPlain(char* buffer, size_t bytes) {
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

int TlsSession_openssl::writePlain(const char* buffer, size_t bytes) {
	return SSL_write(_ssl, buffer, bytes);
}

int TlsSession_openssl::readEncrypted(char* buffer, size_t bytes) {
	return BIO_read(_bioOut, buffer, bytes);
}

int TlsSession_openssl::writeEncrypted(const char* buffer, size_t bytes) {
	return BIO_write(_bioIn, buffer, bytes);
}

int TlsSession_openssl::getPeerCertificate(char* buffer, size_t size) {
	int result = -1;
	X509* certificate = SSL_get_peer_certificate(_ssl);
	BIO* bio = BIO_new(BIO_s_mem());
	PEM_write_bio_X509(bio, certificate);
	BUF_MEM* mem;
	BIO_get_mem_ptr(bio, &mem);
	if (mem->length <= size) {
		std::memcpy(buffer, mem->data, mem->length);
		result = mem->length;
	}
	BIO_free(bio);
	return result;
}

bool TlsSession_openssl::verifyPeerCertificate() {
	bool verified = false;
	X509* certificate = SSL_get_peer_certificate(_ssl);
	if (certificate) {

		if (SSL_get_verify_result(_ssl) == X509_V_OK) {
			if (verifyHostname(certificate, _hostname.c_str())) {
				verified = true;
			}
		}
		X509_free(certificate);
	}
	return verified;
}

bool TlsSession_openssl::wildcardMatch(const char* pattern, const char* name) {
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

bool TlsSession_openssl::verifyHostname(X509* certificate, const char* hostname) {
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

	if (!verified) {
		int index = X509_NAME_get_index_by_NID(X509_get_subject_name(certificate), NID_commonName, -1);
		if (index >= 0) {
			X509_NAME_ENTRY* entry = X509_NAME_get_entry(X509_get_subject_name(certificate), index);
			if (entry) {
				ASN1_STRING* asn1 = X509_NAME_ENTRY_get_data(entry);
				if (asn1) {
					const char* commonName = reinterpret_cast<const char*>(ASN1_STRING_data(asn1));
					if (static_cast<size_t>(ASN1_STRING_length(asn1)) == std::strlen(commonName)) {
						verified = wildcardMatch(commonName, hostname);
					}
				}
			}
		}
	}

	return verified;
}

bool TlsSession_openssl::getError(char* buffer, size_t bytes) {
	unsigned long error = ERR_get_error();
	if (error != 0) {
		ERR_error_string_n(error, buffer, bytes);
	}
	return error != 0;
}
#elif defined (__MACH__)
#include <Security/SecIdentity.h>
#include <Security/SecImportExport.h>
#include <Security/SecureTransport.h>
#include <cstring>
#include <locale>
#include <vector>

extern "C" SecIdentityRef SecIdentityCreate(CFAllocatorRef allocator, SecCertificateRef certificate, SecKeyRef privateKey);

class TlsContext_osx : public  TlsContext {
public:
	~TlsContext_osx() override;
	TlsSession* createSession() override;
	bool setCertificate(const char* certificate);
	bool setPrivateKey(const char* privateKey);
	bool addTrustedCertificate(const char* certificate);

	SecKeyRef& getPrivateKey() { return _privateKey; }
	SecCertificateRef& getCertificate() { return _certificate; }
	CFArrayRef getTrustedCertificates() { return _trustedCertificates; }

private:
	SecKeyRef _privateKey = 0;
	SecCertificateRef _certificate = 0;
	CFMutableArrayRef _trustedCertificates = 0;
};

class TlsSession_osx : public TlsSession {
public:
	TlsSession_osx(TlsContext_osx* context);
	~TlsSession_osx();

	void startConnect() override;
	void startAccept() override;
	void shutdown() override;
	HandshakeResult handshake() override;

	int readPlain(char* buffer, size_t bytes) override;
	int writePlain(const char* buffer, size_t bytes) override;

	int readEncrypted(char* buffer, size_t bytes) override;
	int writeEncrypted(const char* buffer, size_t bytes) override;

	void setHostname(const char* hostname) override { _hostname = hostname; }
	virtual int getPeerCertificate(char* buffer, size_t bytes) override;

private:
	static OSStatus writeCallback(SSLConnectionRef connection, const void* data, size_t* dataLength);
	static OSStatus readCallback(SSLConnectionRef connection, void* data, size_t* dataLength);

	TlsContext_osx* _context = 0;
	SSLContextRef _session = 0;
	std::vector<char> _inBuffer;
	std::vector<char> _outBuffer;
	std::string _hostname;
	bool _shutdown = false;
};

TlsContext_osx::~TlsContext_osx() {
	if (_privateKey) {
		CFRelease(_privateKey);
		_privateKey = 0;
	}
	if (_certificate) {
		CFRelease(_certificate);
		_certificate = 0;
	}
	if (_trustedCertificates) {
		CFRelease(_trustedCertificates);
		_trustedCertificates = 0;
	}
}

TlsSession* TlsContext_osx::createSession() {
	return new TlsSession_osx(this);
}

bool TlsContext_osx::setCertificate(const char* certificate) {
	if (_certificate) {
		CFRelease(_certificate);
		_certificate = 0;
	}
	CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(certificate), std::strlen(certificate), kCFAllocatorDefault);
	CFArrayRef items = 0;
	SecExternalFormat format = kSecFormatPEMSequence;
	SecExternalItemType itemType = kSecItemTypeCertificate;
	OSStatus status = SecItemImport(data, 0, &format, &itemType, 0, 0, 0, &items);
	if (status == noErr && CFArrayGetCount(items) > 0) {
		_certificate = (SecCertificateRef)CFArrayGetValueAtIndex(items, 0);
	}
	return _certificate != 0;
}

bool TlsContext_osx::setPrivateKey(const char* privateKey) {
	if (_privateKey) {
		CFRelease(_privateKey);
		_privateKey = 0;
	}

	CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(privateKey), std::strlen(privateKey), kCFAllocatorDefault);
	CFArrayRef items = 0;
	SecExternalFormat format = kSecFormatPEMSequence;
	SecExternalItemType itemType = kSecItemTypePrivateKey;
	OSStatus status = SecItemImport(data, 0, &format, &itemType, 0, 0, 0, &items);
	if (status == noErr && CFArrayGetCount(items) > 0) {
		_privateKey = (SecKeyRef)CFArrayGetValueAtIndex(items, 0);
	}
	return _privateKey != 0;
}

bool TlsContext_osx::addTrustedCertificate(const char* certificate) {
	if (!_trustedCertificates) {
		_trustedCertificates = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
	}
	SecCertificateRef certificateItem = 0;
	CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(certificate), std::strlen(certificate), kCFAllocatorDefault);
	CFArrayRef items = 0;
	SecExternalFormat format = kSecFormatPEMSequence;
	SecExternalItemType itemType = kSecItemTypeCertificate;
	OSStatus status = SecItemImport(data, 0, &format, &itemType, 0, 0, 0, &items);
	if (status == noErr && CFArrayGetCount(items) > 0) {
		certificateItem = (SecCertificateRef)CFArrayGetValueAtIndex(items, 0);
	}
	if (certificateItem) {
		CFArrayAppendValue(_trustedCertificates, certificateItem);
	}
	return certificateItem != 0;
}

TlsSession_osx::TlsSession_osx(TlsContext_osx* context) {
	_context = context;
}

TlsSession_osx::~TlsSession_osx() {
	if (_session) {
		CFRelease(_session);
		_session = 0;
	}
}

void TlsSession_osx::startAccept() {
	_session = SSLCreateContext(0, kSSLServerSide, kSSLStreamType);
	if (_context->getCertificate() && _context->getPrivateKey()) {
		SecIdentityRef identity = SecIdentityCreate(kCFAllocatorDefault, _context->getCertificate(), _context->getPrivateKey());
		CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, (const void**)&identity, 1, &kCFTypeArrayCallBacks);
		SSLSetCertificate(_session, array);
	}
	SSLSetIOFuncs(_session, readCallback, writeCallback);
	SSLSetConnection(_session, this);
	handshake();
}

void TlsSession_osx::startConnect() {
	_session = SSLCreateContext(0, kSSLClientSide, kSSLStreamType);
	if (_context->getTrustedCertificates()) {
		// XXX: SSLSetTrustedRoots(_session, _context->getTrustedCertificates(), false);
	}
	SSLSetIOFuncs(_session, readCallback, writeCallback);
	SSLSetConnection(_session, this);
	SSLSetPeerDomainName(_session, _hostname.c_str(), _hostname.size());
	handshake();
}

void TlsSession_osx::shutdown() {
	if (!_outBuffer.size()) {
		SSLClose(_session);
		_shutdown = false;
	} else {
		_shutdown = true;
	}
}

TlsSession::HandshakeResult TlsSession_osx::handshake() {
	TlsSession::HandshakeResult result = TlsSession::kFailed;
	OSStatus status = SSLHandshake(_session);
	switch (status) {
	case noErr:
		result = TlsSession::kDone;
		break;
	case errSSLWouldBlock:
		result = TlsSession::kMore;
		break;
	default:
		result = TlsSession::kFailed;
		break;
	}
	return result;
}

int TlsSession_osx::readPlain(char* buffer, size_t bytes) {
	int result = 0;
	size_t processed = bytes;
	OSStatus status = SSLRead(_session, buffer, bytes, &processed);
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

int TlsSession_osx::writePlain(const char* buffer, size_t bytes) {
	int result = 0;
	size_t processed;
	OSStatus status = SSLWrite(_session, buffer, bytes, &processed);
	if (status == noErr) {
		result = processed;
	} else {
		result = -1;
	}
	return result;
}

OSStatus TlsSession_osx::writeCallback(SSLConnectionRef connection, const void* data, size_t* dataLength) {
	TlsSession_osx* tls = reinterpret_cast<TlsSession_osx*>(const_cast<void*>(connection));
	tls->_outBuffer.insert(tls->_outBuffer.end(), reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + *dataLength);
	if (tls->_shutdown && !tls->_outBuffer.size()) {
		SSLClose(tls->_session);
		tls->_shutdown = false;
	}
	return noErr;
}

OSStatus TlsSession_osx::readCallback(SSLConnectionRef connection, void* data, size_t* dataLength) {
	TlsSession_osx* tls = reinterpret_cast<TlsSession_osx*>(const_cast<void*>(connection));
	OSStatus result = noErr;
	size_t bytes = std::min(tls->_inBuffer.size(), *dataLength);
	if (bytes > 0) {
		std::memcpy(data, tls->_inBuffer.data(), bytes);
		tls->_inBuffer.erase(tls->_inBuffer.begin(), tls->_inBuffer.begin() + bytes);
	}
	if (bytes < *dataLength) {
		result = errSSLWouldBlock;
	}
	*dataLength = bytes;
	return result;
}

int TlsSession_osx::readEncrypted(char* buffer, size_t bytes) {
	size_t size = std::min(bytes, _outBuffer.size());
	if (size > 0) {
		std::memcpy(buffer, _outBuffer.data(), size);
		_outBuffer.erase(_outBuffer.begin(), _outBuffer.begin() + size);
	}
	return size;
}

int TlsSession_osx::writeEncrypted(const char* buffer, size_t bytes) {
	_inBuffer.insert(_inBuffer.end(), buffer, buffer + bytes);
	return bytes;
}

int TlsSession_osx::getPeerCertificate(char* buffer, size_t size) {
	int result = -1;
	SecTrustRef trust = 0;
	if (SSLCopyPeerTrust(_session, &trust) == noErr) {
		if (SecTrustGetCertificateCount(trust) > 0) {
			SecCertificateRef certificate = SecTrustGetCertificateAtIndex(trust, 0);
			CFDataRef data = 0;
			if (SecItemExport(certificate, kSecFormatX509Cert, kSecItemPemArmour, nil, &data) == noErr) {
				size_t actualSize = CFDataGetLength(data);
				if (actualSize <= size) {
					CFDataGetBytes(data, CFRangeMake(0, actualSize), reinterpret_cast<UInt8*>(buffer));
					result = actualSize;
				}
			}
		}
		CFRelease(trust);
	}
	return result;
}

TlsContext* TlsContext::create() {
	return new TlsContext_osx();
}
#elif defined (_WIN32)
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <string>
#include <vector>

#define SECURITY_WIN32
#define NOMINMAX
#include <windows.h>
#include <schannel.h>
#include <security.h>
#undef SECURITY_WIN32
#undef NOMINMAX

PSecurityFunctionTable getSecurityLibrary();

class TlsContext_sspi : public TlsContext {
public:
	TlsContext_sspi();
	~TlsContext_sspi();

	TlsSession* createSession() override;
	bool setCertificate(const char* certificate) override;
	bool setPrivateKey(const char* privateKey) override;
	bool addTrustedCertificate(const char* certificate) { return false; }

	PCCERT_CONTEXT& getCertificate() { return _certificate; }

private:
	static const char* kContainerName;
	void importKeyAndCertificate();

	HCRYPTPROV _provider = 0;
	HCERTSTORE _store = INVALID_HANDLE_VALUE;
	PCCERT_CONTEXT _certificate = 0;
	bool _dirty = true;
};

const char* TlsContext_sspi::kContainerName = "_tmp0";

class TlsSession_sspi : public TlsSession {
public:
	TlsSession_sspi(TlsContext_sspi* context);
	~TlsSession_sspi();

	void startConnect() override;
	void startAccept() override;
	int getPeerCertificate(char* buffer, size_t size) override;
	void shutdown() override;
	HandshakeResult handshake() override;

	int readPlain(char* buffer, size_t bytes) override;
	int writePlain(const char* buffer, size_t bytes) override;

	int readEncrypted(char* buffer, size_t bytes) override;
	int writeEncrypted(const char* buffer, size_t bytes) override;

	void setHostname(const char* hostname) { _hostname = hostname; }

	static void setError(HRESULT error);
	bool getError(char* buffer, size_t bytes) override;

private:
	HandshakeResult handshakeInternal(bool initial);

	TlsContext_sspi* _context;
	CredHandle _credentialsHandle;
	CtxtHandle _securityContext;
	SecPkgContext_StreamSizes _sizes;
	enum { kUndetermined, kConnect, kAccept } _direction = kUndetermined;
	bool _initial = false;
	static HRESULT _lastError;

	std::vector<char> _inBuffer;
	std::vector<char> _outBuffer;
	std::vector<char> _decryptedBuffer;
	std::string _hostname;
};

HRESULT TlsSession_sspi::_lastError = S_OK;

PSecurityFunctionTable getSecurityLibrary() {
	PSecurityFunctionTable (*table)();
	static PSecurityFunctionTable security;
	static bool loaded;
	if (!loaded) {
		HMODULE module = LoadLibrary("security.dll");
		table = (PSecurityFunctionTable(*)())GetProcAddress(module, "InitSecurityInterfaceA");
		assert(table && "failed to load security.dll");
		security = table();
		assert(security && "No function table in security.dll");
		loaded = true;
	}
	return security;
}

TlsContext_sspi::TlsContext_sspi() {
	if (!CryptAcquireContext(&_provider, kContainerName, MS_DEF_RSA_SCHANNEL_PROV, PROV_RSA_SCHANNEL, CRYPT_NEWKEYSET)) {
		if (GetLastError() != NTE_EXISTS || !CryptAcquireContext(&_provider, kContainerName, MS_DEF_RSA_SCHANNEL_PROV, PROV_RSA_SCHANNEL, 0)) {
			TlsSession_sspi::setError(GetLastError());
		}
	}

	_store = CertOpenStore(CERT_STORE_PROV_SYSTEM,
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		_provider,
		CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_NO_CRYPT_RELEASE_FLAG | CERT_STORE_OPEN_EXISTING_FLAG,
		L"MY");
}

TlsContext_sspi::~TlsContext_sspi() {
	if (_store) {
		CertCloseStore(_store, 0);
		_store = INVALID_HANDLE_VALUE;
	}
	if (_provider) {
		CryptReleaseContext(_provider, 0);
		_provider = 0;
	}
	if (_certificate) {
		CertFreeCertificateContext(_certificate);
		_certificate = 0;
	}
}

bool TlsContext_sspi::setCertificate(const char* certificate) {
	if (_certificate) {
		CertFreeCertificateContext(_certificate);
		_certificate = 0;
	}

	std::vector<BYTE> certificateBuffer;
	DWORD size = 0;
	if (CryptStringToBinary(certificate, 0, CRYPT_STRING_BASE64HEADER, 0, &size, 0, 0)) {
		certificateBuffer.resize(size);
		if (!CryptStringToBinary(certificate, 0, CRYPT_STRING_BASE64HEADER, certificateBuffer.data(), &size, 0, 0)) {
			certificateBuffer.resize(0);
		}
	}

	if (certificateBuffer.size()) {
		_certificate = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certificateBuffer.data(), certificateBuffer.size());
	}

	if (!CertAddCertificateContextToStore(_store, _certificate, CERT_STORE_ADD_REPLACE_EXISTING, 0)) {
		TlsSession_sspi::setError(GetLastError());
	}

	_dirty = true;

	return _certificate != 0;
}

bool TlsContext_sspi::setPrivateKey(const char* privateKey) {
	bool result = false;
	std::vector<BYTE> keyBuffer;
	std::vector<BYTE> keyBlob;

	DWORD size = 0;
	if (CryptStringToBinary(privateKey, 0, CRYPT_STRING_BASE64HEADER, 0, &size, 0, 0)) {
		keyBuffer.resize(size);
		if (!CryptStringToBinary(privateKey, 0, CRYPT_STRING_BASE64HEADER, keyBuffer.data(), &size, 0, 0)) {
			TlsSession_sspi::setError(GetLastError());
			keyBuffer.resize(0);
		}
	}

	size = 0;
	if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, keyBuffer.data(), keyBuffer.size(), 0, 0, 0, &size)) {
		keyBlob.resize(size);
		if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, keyBuffer.data(), keyBuffer.size(), 0, 0, keyBlob.data(), &size)) {
			TlsSession_sspi::setError(GetLastError());
			keyBlob.resize(0);
		}
	}

	HCRYPTKEY cryptKey = 0;
	if (CryptImportKey(_provider, keyBlob.data(), keyBlob.size(), 0, 0, &cryptKey)) {
		CryptDestroyKey(cryptKey);
		cryptKey = 0;
		result = true;
	} else {
		TlsSession_sspi::setError(GetLastError());
	}

	_dirty = true;

	return result;
}

void TlsContext_sspi::importKeyAndCertificate() {
	if (_certificate) {
		WCHAR wname[32];
		mbstowcs(wname, kContainerName, sizeof(kContainerName) + 1);

		CRYPT_KEY_PROV_INFO info;
		ZeroMemory(&info, sizeof(info));
		info.pwszContainerName = wname;
		info.pwszProvName = MS_DEF_RSA_SCHANNEL_PROV_W;
		info.dwProvType = PROV_RSA_SCHANNEL;
		info.dwKeySpec = AT_KEYEXCHANGE;

		if (!CertSetCertificateContextProperty(_certificate, CERT_KEY_PROV_INFO_PROP_ID, 0, reinterpret_cast<const void*>(&info))) {
			TlsSession_sspi::setError(GetLastError());
		}
	}
	_dirty = false;
}

TlsSession* TlsContext_sspi::createSession() {
	if (_dirty) {
		importKeyAndCertificate();
	}
	return new TlsSession_sspi(this);
}

TlsSession_sspi::TlsSession_sspi(TlsContext_sspi* context) {
	_context = context;
	ZeroMemory(&_credentialsHandle, sizeof(_credentialsHandle));
	ZeroMemory(&_securityContext, sizeof(_securityContext));
	ZeroMemory(&_sizes, sizeof(_sizes));
}

TlsSession_sspi::~TlsSession_sspi() {
	getSecurityLibrary()->FreeCredentialsHandle(&_credentialsHandle);
	getSecurityLibrary()->DeleteSecurityContext(&_securityContext);
}

void TlsSession_sspi::startAccept() {
	_direction = kAccept;
	_initial = true;
	SCHANNEL_CRED credentials;
	ZeroMemory(&credentials, sizeof(credentials));
	credentials.dwVersion = SCHANNEL_CRED_VERSION;
	credentials.cCreds = 1;
	credentials.paCred = &_context->getCertificate();
	_lastError = getSecurityLibrary()->AcquireCredentialsHandleA(0, UNISP_NAME_A, SECPKG_CRED_INBOUND, 0, &credentials, 0, 0, &_credentialsHandle, 0);
	if (_lastError == S_OK) {
		handshakeInternal(true);
	}
}

void TlsSession_sspi::startConnect() {
	_direction = kConnect;
	_initial = true;
	SCHANNEL_CRED credentials;
	ZeroMemory(&credentials, sizeof(credentials));
	credentials.dwVersion = SCHANNEL_CRED_VERSION;
	credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;
	_lastError = getSecurityLibrary()->AcquireCredentialsHandleA(0, UNISP_NAME_A, SECPKG_CRED_OUTBOUND, 0, &credentials, 0, 0, &_credentialsHandle, 0);
	if (_lastError == S_OK) {
		handshakeInternal(true);
	}
}

int TlsSession_sspi::getPeerCertificate(char* buffer, size_t size) {
	int result = -1;
	PCCERT_CONTEXT certificate;
	HRESULT status = getSecurityLibrary()->QueryContextAttributesA(&_securityContext, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate);
	if (FAILED(status)) {
		_lastError = status;
	} else {
		DWORD bufferSize = size;
		if (CryptBinaryToString(certificate->pbCertEncoded, certificate->cbCertEncoded, CRYPT_STRING_BASE64HEADER, buffer, &bufferSize)) {
			return bufferSize;
		}
	}
	return result;
}

void TlsSession_sspi::shutdown() {
	DWORD type = SCHANNEL_SHUTDOWN;
	SecBufferDesc bufferDesc;
	SecBuffer buffers[1];
	buffers[0].pvBuffer = &type;
	buffers[0].BufferType = SECBUFFER_TOKEN;
	buffers[0].cbBuffer = sizeof(type);
	bufferDesc.cBuffers = 1;
	bufferDesc.pBuffers = buffers;
	bufferDesc.ulVersion = SECBUFFER_TOKEN;

	SECURITY_STATUS status = getSecurityLibrary()->ApplyControlToken(&_securityContext, &bufferDesc);
	if (!FAILED(status)) {
		buffers[0].pvBuffer = 0;
		buffers[0].BufferType = SECBUFFER_TOKEN;
		buffers[0].cbBuffer = 0;
		bufferDesc.cBuffers = 1;
		bufferDesc.pBuffers = buffers;
		bufferDesc.ulVersion = SECBUFFER_VERSION;

		DWORD outFlags = 0;

		status = getSecurityLibrary()->InitializeSecurityContextA(
			&_credentialsHandle,
			&_securityContext,
			0,
			ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
			0,
			0,
			0,
			0,
			&_securityContext,
			&bufferDesc,
			&outFlags,
			0);

		if (!FAILED(status) && buffers[0].pvBuffer && buffers[0].cbBuffer) {
			const char* data = reinterpret_cast<const char*>(buffers[0].pvBuffer);
			_outBuffer.insert(_outBuffer.end(), data, data + buffers[0].cbBuffer);
		}
	}
}

TlsSession::HandshakeResult TlsSession_sspi::handshake() {
	return handshakeInternal(_initial);
}

TlsSession::HandshakeResult TlsSession_sspi::handshakeInternal(bool initial) {
	SecBufferDesc outBuffer;
	SecBuffer outBuffers[1];
	SecBufferDesc inBuffer;
	SecBuffer inBuffers[2];
	DWORD outFlags = 0;
	outBuffers[0].pvBuffer = 0;
	outBuffers[0].BufferType = SECBUFFER_TOKEN;
	outBuffers[0].cbBuffer = 0;
	outBuffer.cBuffers = 1;
	outBuffer.pBuffers = outBuffers;
	outBuffer.ulVersion = SECBUFFER_VERSION;
	std::vector<char> buffer(_inBuffer);
	inBuffers[0].pvBuffer = buffer.data();
	inBuffers[0].cbBuffer = buffer.size();
	inBuffers[0].BufferType = SECBUFFER_TOKEN;
	inBuffers[1].pvBuffer = 0;
	inBuffers[1].cbBuffer = 0;
	inBuffers[1].BufferType = SECBUFFER_EMPTY;
	inBuffer.cBuffers = 2;
	inBuffer.pBuffers = inBuffers;
	inBuffer.ulVersion = SECBUFFER_VERSION;

	SECURITY_STATUS status = SEC_E_OK;
	
	if (_direction == kConnect) {
		status = getSecurityLibrary()->InitializeSecurityContextA(
			&_credentialsHandle,
			initial ? 0 : &_securityContext,
			_hostname.size() ? const_cast<char*>(_hostname.c_str()) : 0,
			ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
			0,
			0,
			&inBuffer,
			0,
			&_securityContext,
			&outBuffer,
			&outFlags,
			0);
	} else if (_direction = kAccept) {
		status = getSecurityLibrary()->AcceptSecurityContext(
			&_credentialsHandle,
			initial ? 0 : &_securityContext,
			&inBuffer,
			ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
			0,
			&_securityContext,
			&outBuffer,
			&outFlags,
			0);
	}

	if (!FAILED(status)) {
		_initial = false;
	}

	TlsSession::HandshakeResult result = TlsSession::kFailed;

	size_t extra = 0;
	for (int i = 0; i < inBuffer.cBuffers; ++i) {
		if (inBuffers[i].BufferType == SECBUFFER_EXTRA && inBuffers[i].cbBuffer) {
			extra += inBuffers[i].cbBuffer;
		}
	}
	size_t missing = 0;
	for (int i = 0; i < inBuffer.cBuffers; ++i) {
		if (inBuffers[i].BufferType == SECBUFFER_MISSING && inBuffers[i].cbBuffer) {
			missing += inBuffers[i].cbBuffer;
		}
	}

	if (outBuffers[0].cbBuffer && outBuffers[0].pvBuffer) {
		const char* data = reinterpret_cast<const char*>(outBuffers[0].pvBuffer);
		_outBuffer.insert(_outBuffer.end(), data, data + outBuffers[0].cbBuffer);
		getSecurityLibrary()->FreeContextBuffer(outBuffers[0].pvBuffer);
	}

	if (status == SEC_E_OK) {
		result = TlsSession::kDone;
	} else if (status == SEC_E_INCOMPLETE_MESSAGE
		|| status == SEC_I_CONTINUE_NEEDED) {
		result = TlsSession::kMore;
	} else if (FAILED(status)) {
		result = TlsSession::kFailed;
	}

	_inBuffer.erase(_inBuffer.begin(), _inBuffer.end() - extra);

	if (result == TlsSession::kDone) {
		status = getSecurityLibrary()->QueryContextAttributesA(&_securityContext, SECPKG_ATTR_STREAM_SIZES, &_sizes);
		if (FAILED(status)) {
			result = TlsSession::kFailed;
		}
	}

	return result;
}

int TlsSession_sspi::readPlain(char* buffer, size_t bytes) {
	int result = TlsSession::kReadFailed;
	if (bytes <= _decryptedBuffer.size()) {
		std::memcpy(buffer, _decryptedBuffer.data(), bytes);
		_decryptedBuffer.erase(_decryptedBuffer.begin(), _decryptedBuffer.begin() + bytes);
		result = bytes;
	} else if (_inBuffer.size()) {
		SecBufferDesc bufferDesc;
		SecBuffer buffers[4];
		std::vector<char> data(_inBuffer);
		buffers[0].pvBuffer = data.data();
		buffers[0].cbBuffer = data.size();
		buffers[0].BufferType = SECBUFFER_DATA;
		buffers[1].BufferType = SECBUFFER_EMPTY;
		buffers[2].BufferType = SECBUFFER_EMPTY;
		buffers[3].BufferType = SECBUFFER_EMPTY;
		bufferDesc.ulVersion = SECBUFFER_VERSION;
		bufferDesc.cBuffers = 4;
		bufferDesc.pBuffers = buffers;
		SECURITY_STATUS status = getSecurityLibrary()->DecryptMessage(&_securityContext, &bufferDesc, 0, 0);

		if (status == SEC_I_CONTEXT_EXPIRED) {
			_inBuffer.clear();
			result = TlsSession::kReadZero;
		} else if (status == SEC_E_INCOMPLETE_MESSAGE) {
			result = 0;
		} else if (status == SEC_E_OK) {
			result = 0;
			size_t extra = 0;
			for (int i = 0; i < bufferDesc.cBuffers; ++i) {
				if (buffers[i].BufferType == SECBUFFER_DATA) {
					const char* decrypted = reinterpret_cast<const char*>(buffers[i].pvBuffer);
					_decryptedBuffer.insert(_decryptedBuffer.end(), decrypted, decrypted + buffers[i].cbBuffer);
				} else if (buffers[i].BufferType == SECBUFFER_EXTRA) {
					extra += buffers[i].cbBuffer;
				}
			}
			_inBuffer.erase(_inBuffer.begin(), _inBuffer.end() - extra);

			size_t actual = std::min(_decryptedBuffer.size(), bytes);
			if (actual > 0) {
				std::memcpy(buffer, _decryptedBuffer.data(), actual);
				_decryptedBuffer.erase(_decryptedBuffer.begin(), _decryptedBuffer.begin() + actual);
				result = actual;
			}
		} else {
			_inBuffer.clear();
			result = TlsSession::kReadFailed;
		}
	} else {
		size_t actual = std::min(_decryptedBuffer.size(), bytes);
		if (actual > 0) {
			std::memcpy(buffer, _decryptedBuffer.data(), actual);
			_decryptedBuffer.erase(_decryptedBuffer.begin(), _decryptedBuffer.begin() + actual);
			result = actual;
		} else {
			result = 0;
		}
	}
	return result;
}

int TlsSession_sspi::writePlain(const char* buffer, size_t bytes) {
	SecBufferDesc bufferDesc;
	SecBuffer buffers[4];
	std::vector<char> data(_sizes.cbHeader + _sizes.cbTrailer + bytes);
	std::memcpy(data.data() + _sizes.cbHeader, buffer, bytes);

	buffers[0].pvBuffer = data.data();
	buffers[0].cbBuffer = _sizes.cbHeader;
	buffers[0].BufferType = SECBUFFER_STREAM_HEADER;

	buffers[1].pvBuffer = data.data() + _sizes.cbHeader;
	buffers[1].cbBuffer = bytes;
	buffers[1].BufferType = SECBUFFER_DATA;

	buffers[2].pvBuffer = data.data() + _sizes.cbHeader + bytes;
	buffers[2].cbBuffer = _sizes.cbTrailer;
	buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;

	buffers[3].BufferType = SECBUFFER_EMPTY;

	bufferDesc.ulVersion = SECBUFFER_VERSION;
	bufferDesc.cBuffers = 4;
	bufferDesc.pBuffers = buffers;
	SECURITY_STATUS status = getSecurityLibrary()->EncryptMessage(&_securityContext, 0, &bufferDesc, 0);
	for (int i = 0; i < bufferDesc.cBuffers; ++i) {
		if (buffers[i].BufferType != SECBUFFER_EMPTY && buffers[i].pvBuffer && buffers[i].cbBuffer) {
			const char* bufferData = reinterpret_cast<const char*>(buffers[i].pvBuffer);
			_outBuffer.insert(_outBuffer.end(), bufferData, bufferData + buffers[i].cbBuffer);
		}
	}
	return 0;
}

int TlsSession_sspi::readEncrypted(char* buffer, size_t bytes) {
	size_t size = std::min(bytes, _outBuffer.size());
	if (size > 0) {
		std::memcpy(buffer, _outBuffer.data(), size);
		_outBuffer.erase(_outBuffer.begin(), _outBuffer.begin() + size);
	}
	return size;
}

int TlsSession_sspi::writeEncrypted(const char* buffer, size_t bytes) {
	_inBuffer.insert(_inBuffer.end(), buffer, buffer + bytes);
	return bytes;
}

void TlsSession_sspi::setError(HRESULT error) {
	_lastError = error;
}

bool TlsSession_sspi::getError(char* buffer, size_t bytes) {
	bool result = false;
	if (_lastError != S_OK) {
		DWORD length = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, _lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)buffer, bytes, 0);
		_lastError = S_OK;
		if (length > 0) {
			result = true;
		}
	}
	return result;
}

TlsContext* TlsContext::create() {
	return new TlsContext_sspi();
}
#else
TlsContext* TlsContext::create() {
	return 0;
}
#endif
