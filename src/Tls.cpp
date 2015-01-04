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
	enum { kUndetermined, kAccept, kConnect } _direction = kUndetermined;
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
#include <Security/SecIdentity.h>
#include <Security/SecImportExport.h>
#include <Security/SecureTransport.h>
#include <cstring>
#include <locale>
#include <vector>

extern "C" SecIdentityRef SecIdentityCreate(CFAllocatorRef allocator, SecCertificateRef certificate, SecKeyRef privateKey);

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

	CFArrayRef _certificate = 0;
	SSLContextRef _context = 0;
	std::vector<char> _inBuffer;
	std::vector<char> _outBuffer;
	std::string _hostname;
	bool _shutdown = false;
};

Tls_commoncrypto::Tls_commoncrypto(const char* key, const char* certificate) {
	SecKeyRef keyItem = 0;
	SecCertificateRef certificateItem = 0;
	SecIdentityRef identityItem = 0;

	if (key) {
		CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(key), std::strlen(key), kCFAllocatorDefault);
		CFArrayRef keyItems = 0;
		SecExternalFormat format = kSecFormatPEMSequence;
		SecExternalItemType itemType = kSecItemTypePrivateKey;
		OSStatus status = SecItemImport(data, 0, &format, &itemType, 0, 0, 0, &keyItems);
		if (status == noErr && CFArrayGetCount(keyItems) > 0) {
			keyItem = (SecKeyRef)CFArrayGetValueAtIndex(keyItems, 0);
		}
	}

	if (certificate) {
		CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(certificate), std::strlen(certificate), kCFAllocatorDefault);
		CFArrayRef certificateItems = 0;
		SecExternalFormat format = kSecFormatPEMSequence;
		SecExternalItemType itemType = kSecItemTypeCertificate;
		OSStatus status = SecItemImport(data, 0, &format, &itemType, 0, 0, 0, &certificateItems);
		if (status == noErr && CFArrayGetCount(certificateItems) > 0) {
			certificateItem = (SecCertificateRef)CFArrayGetValueAtIndex(certificateItems, 0);
		}
	}

	if (keyItem && certificateItem) {
		identityItem = SecIdentityCreate(kCFAllocatorDefault, certificateItem, keyItem);
	}

	_certificate = CFArrayCreate(kCFAllocatorDefault, (const void**)&identityItem, 1, &kCFTypeArrayCallBacks);
}

Tls_commoncrypto::~Tls_commoncrypto() {
	CFRelease(_certificate);
	CFRelease(_context);
	_context = 0;
}

void Tls_commoncrypto::startAccept() {
	_context = SSLCreateContext(0, kSSLServerSide, kSSLStreamType);
	if (_certificate) {
		SSLSetCertificate(_context, _certificate);
	}
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
		std::memcpy(data, tls->_inBuffer.data(), bytes);
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
		std::memcpy(buffer, _outBuffer.data(), size);
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
#elif defined (_WIN32)
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define SECURITY_WIN32
#define NOMINMAX
#include <windows.h>
#include <schannel.h>
#include <security.h>
#undef SECURITY_WIN32
#undef NOMINMAX

class Tls_sspi : public Tls {
public:
	Tls_sspi(const char* key, const char* certificate);
	~Tls_sspi();

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
	static void loadLibrary();
	HandshakeResult handshakeInternal(bool initial);

	static PSecurityFunctionTable _security;
	CredHandle _credentialsHandle;
	CtxtHandle _context;
	PCCERT_CONTEXT _certificate = 0;
	SecPkgContext_StreamSizes _sizes;
	HCRYPTPROV _provider = 0;
	enum { kUndetermined, kConnect, kAccept } _direction = kUndetermined;
	bool _initial = false;

	std::vector<char> _inBuffer;
	std::vector<char> _outBuffer;
	std::vector<char> _decryptedBuffer;
	std::string _hostname;
};

PSecurityFunctionTable Tls_sspi::_security = 0;

void Tls_sspi::loadLibrary() {
	PSecurityFunctionTable (*table)();
	HMODULE module = LoadLibrary("security.dll");
	table = (PSecurityFunctionTable(*)())GetProcAddress(module, "InitSecurityInterfaceA");
	assert(table && "failed to load security.dll");
	_security = table();
	assert(_security && "No function table in security.dll");
}

#include <iostream>

Tls_sspi::Tls_sspi(const char* key, const char* certificate) {
	static bool initialized = false;
	if (!initialized) {
		loadLibrary();
		initialized = true;
	}

	std::vector<BYTE> keyBuffer;
	std::vector<BYTE> keyBlob;


	if (certificate) {
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
			std::cout << "certificate = " << _certificate << "\n";
		}
	}

	if (key) {
		DWORD size = 0;
		if (CryptStringToBinary(key, 0, CRYPT_STRING_BASE64HEADER, 0, &size, 0, 0)) {
			keyBuffer.resize(size);
			if (!CryptStringToBinary(key, 0, CRYPT_STRING_BASE64HEADER, keyBuffer.data(), &size, 0, 0)) {
				std::cout << "CryptStringToBinary failed\n";
				keyBuffer.resize(0);
			}
		}

		size = 0;
		if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, keyBuffer.data(), keyBuffer.size(), 0, 0, 0, &size)) {
			keyBlob.resize(size);
			if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, keyBuffer.data(), keyBuffer.size(), 0, 0, keyBlob.data(), &size)) {
				std::cout << "CryptDecodeObjectEx failed\n";
				keyBlob.resize(0);
			}
		}

		const char* container = "_tmp0";

		_provider = 0;
		if (!CryptAcquireContext(&_provider, container, MS_DEF_RSA_SCHANNEL_PROV, PROV_RSA_SCHANNEL, CRYPT_NEWKEYSET)) {
			if (GetLastError() != NTE_EXISTS || !CryptAcquireContext(&_provider, container, MS_DEF_RSA_SCHANNEL_PROV, PROV_RSA_SCHANNEL, 0)) {
				std::cout << "unable to acquire context\n";
			}
		}

		HANDLE store = CertOpenStore(CERT_STORE_PROV_SYSTEM,
			X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
			_provider,
			CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_NO_CRYPT_RELEASE_FLAG | CERT_STORE_OPEN_EXISTING_FLAG,
			L"MY");
		if (!store) {
			std::cout << "no store\n";
		}

		PCCERT_CONTEXT storeContext;
		if (!CertAddCertificateContextToStore(store, _certificate, CERT_STORE_ADD_REPLACE_EXISTING, &storeContext)) {
			std::cout << "add failed\n";
		}
		//_certificate = storeContext;
		std::cout << "now certificate is " << _certificate << "\n";

		std::cout << "blob: " << keyBlob.size() << "\n";
		HCRYPTKEY cryptKey = 0;
		if (!CryptImportKey(_provider, keyBlob.data(), keyBlob.size(), 0, 0, &cryptKey)) {
			std::cout << "CryptImportKey failed\n";
			std::cout << GetLastError() << "\n";
		} else {
			//CryptDestroyKey(cryptKey);
			std::cout << "key => " << cryptKey << "\n";
			cryptKey = 0;
		}

		WCHAR wname[32];
		mbstowcs(wname, container, sizeof(container) + 1);

		CRYPT_KEY_PROV_INFO info;
		ZeroMemory(&info, sizeof(info));
		info.pwszContainerName = wname;
		info.pwszProvName = MS_DEF_RSA_SCHANNEL_PROV_W;
		info.dwProvType = PROV_RSA_SCHANNEL;
		info.dwKeySpec = AT_KEYEXCHANGE;

		if (!CertSetCertificateContextProperty(_certificate, CERT_KEY_PROV_INFO_PROP_ID, 0, reinterpret_cast<const void*>(&info))) {
			std::cout << "CertSetCertificateContextProperty failed\n";
		}
		/*if (!CertSetCertificateContextProperty(_certificate, CERT_KEY_PROV_HANDLE_PROP_ID, 0, reinterpret_cast<const void*>(&_provider))) {
			std::cout << "CertSetCertificateContextProperty failed\n";
		}*/
	}
}

Tls_sspi::~Tls_sspi() {
}

void Tls_sspi::startAccept() {
	_direction = kAccept;
	_initial = true;
	SCHANNEL_CRED _credentials;
	ZeroMemory(&_credentials, sizeof(_credentials));
	_credentials.dwVersion = SCHANNEL_CRED_VERSION;
	_credentials.cCreds = 1;
	_credentials.paCred = &_certificate;
	SECURITY_STATUS status = _security->AcquireCredentialsHandleA(0, UNISP_NAME_A, SECPKG_CRED_INBOUND, 0, &_credentials, 0, 0, &_credentialsHandle, 0);
	if (status != SEC_E_OK) {
		std::cout << "AcquireCredentialsHandleA => " << status << " " << GetLastError() << "\n";
	}
	handshakeInternal(true);
}

void Tls_sspi::startConnect() {
	_direction = kConnect;
	_initial = true;
	SCHANNEL_CRED _credentials;
	ZeroMemory(&_credentials, sizeof(_credentials));
	_credentials.dwVersion = SCHANNEL_CRED_VERSION;
	_credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;
	SECURITY_STATUS status = _security->AcquireCredentialsHandleA(0, UNISP_NAME_A, SECPKG_CRED_OUTBOUND, 0, &_credentials, 0, 0, &_credentialsHandle, 0);
	if (status != SEC_E_OK) {
		std::cout << "AcquireCredentialsHandleA => " << status << " " << GetLastError() << "\n";
	}
	handshakeInternal(true);
}

void Tls_sspi::shutdown() {
/*
	if (!_outBuffer.size()) {
		SSLClose(_context);
		_shutdown = false;
	} else {
		_shutdown = true;
	}
*/
}

Tls::HandshakeResult Tls_sspi::handshake() {
	return handshakeInternal(_initial);
}

Tls::HandshakeResult Tls_sspi::handshakeInternal(bool initial) {
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
		status = _security->InitializeSecurityContextA(
			&_credentialsHandle,
			initial ? 0 : &_context,
			_hostname.size() ? const_cast<char*>(_hostname.c_str()) : 0,
			ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
			0,
			0,
			&inBuffer,
			0,
			&_context,
			&outBuffer,
			&outFlags,
			0);
		std::cout << _direction << " ISC " << buffer.size() << " " << _outBuffer.size() << " => " << status << " " << initial << " " << FAILED(status) << "\n";
	} else if (_direction = kAccept) {
		status = _security->AcceptSecurityContext(
			&_credentialsHandle,
			initial ? 0 : &_context,
			&inBuffer,
			ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
			0,
			&_context,
			&outBuffer,
			&outFlags,
			0);
		std::cout << _direction << " ASC " << buffer.size() << " " << _outBuffer.size() << " => " << std::hex << status << std::dec << " " << initial << " " << FAILED(status) << "\n";
	}

	if (!FAILED(status)) {
		_initial = false;
	}

	Tls::HandshakeResult result = Tls::kFailed;

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
		_security->FreeContextBuffer(outBuffers[0].pvBuffer);
	}

	if (status == SEC_E_OK) {
		result = Tls::kDone;
	} else if (status == SEC_E_INCOMPLETE_MESSAGE
		|| status == SEC_I_CONTINUE_NEEDED) {
		result = Tls::kMore;
	} else if (FAILED(status)) {
		result = Tls::kFailed;
	}

	_inBuffer.erase(_inBuffer.begin(), _inBuffer.end() - extra);

	if (result == Tls::kDone) {
		status = _security->QueryContextAttributesA(&_context, SECPKG_ATTR_STREAM_SIZES, &_sizes);
		if (FAILED(status)) {
			result = Tls::kFailed;
		}
	}

	return result;
}

int Tls_sspi::readPlain(char* buffer, size_t bytes) {
	int result = Tls::kReadFailed;
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
		SECURITY_STATUS status = _security->DecryptMessage(&_context, &bufferDesc, 0, 0);

		if (status == SEC_I_CONTEXT_EXPIRED) {
			_inBuffer.clear();
			result = Tls::kReadZero;
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
			result = Tls::kReadFailed;
		}
	} else {
		size_t actual = std::min(_decryptedBuffer.size(), bytes);
		if (actual > 0) {
			std::memcpy(buffer, _decryptedBuffer.data(), actual);
			_decryptedBuffer.erase(_decryptedBuffer.begin(), _decryptedBuffer.begin() + actual);
			result = actual;
		}
	}
	return result;
}

int Tls_sspi::writePlain(const char* buffer, size_t bytes) {
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
	SECURITY_STATUS status = _security->EncryptMessage(&_context, 0, &bufferDesc, 0);
	for (int i = 0; i < bufferDesc.cBuffers; ++i) {
		if (buffers[i].BufferType != SECBUFFER_EMPTY && buffers[i].pvBuffer && buffers[i].cbBuffer) {
			const char* bufferData = reinterpret_cast<const char*>(buffers[i].pvBuffer);
			_outBuffer.insert(_outBuffer.end(), bufferData, bufferData + buffers[i].cbBuffer);
		}
	}
	return 0;
}

int Tls_sspi::readEncrypted(char* buffer, size_t bytes) {
	size_t size = std::min(bytes, _outBuffer.size());
	if (size > 0) {
		std::memcpy(buffer, _outBuffer.data(), size);
		_outBuffer.erase(_outBuffer.begin(), _outBuffer.begin() + size);
	}
	return size;
}

int Tls_sspi::writeEncrypted(const char* buffer, size_t bytes) {
	_inBuffer.insert(_inBuffer.end(), buffer, buffer + bytes);
	return bytes;
}

Tls* Tls::create(const char* key, const char* certificate) {
	return new Tls_sspi(key, certificate);
}
#else
Tls* Tls::create(const char* key, const char* certificate) {
	return 0;
}
#endif
