#ifndef INCLUDED_PacketStream
#define INCLUDED_PacketStream

#include <uv.h>
#include <vector>

class PacketStream {
public:
	PacketStream();
	~PacketStream();

	void accept(uv_stream_t* stream);
	void createFrom(uv_loop_t* loop, uv_os_sock_t sock);

	typedef void (OnReceive)(int packetType, const char* begin, size_t length, void* userData);
	void send(int packetType, char* begin, size_t length);
	void setOnReceive(OnReceive* onReceiveCallback, void* userData);
	void close();

private:
	OnReceive* _onReceive;
	void* _onReceiveUserData;
	uv_tcp_t _stream;
	std::vector<char> _buffer;

	void processMessages();

	static void onAllocate(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer);
	static void onRead(uv_stream_t* handle, ssize_t count, const uv_buf_t* buffer);
	static void onWrite(uv_write_t* request, int status);
};

#endif
