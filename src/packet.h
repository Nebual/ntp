////
// Based on a chat_message.hpp example from the Boost Docs

#pragma once

#include <cstdlib>
#include <string>

enum {
	PLAIN_TEXT,
	CLOSE,
	OPEN_R,
	OPEN_W,
	WRITE_CHUNK,
	LISTDIR,
	CHANGEDIR,
	PRINTDIR
};

class Packet {
public:
	enum { header_length = 6 };
	enum { max_body_length = 8192 - header_length };

	Packet() : body_length_(0) {
		action = 0;
		id = 0;
	}

	const char* data() const {
		return data_;
	}

	char* data() {
		return data_;
	}

	std::size_t length() const {
		return header_length + body_length_;
	}

	const char* body() const {
		return data_ + header_length;
	}

	char* body() {
		return data_ + header_length;
	}

	std::size_t body_length() const {
		return body_length_;
	}

	void body_length(std::size_t new_length) {
		body_length_ = new_length;
		if (body_length_ > max_body_length)
			body_length_ = max_body_length;
	}

	bool decode_header() {
		body_length_ = *(reinterpret_cast<uint16_t *> (data_));
		if (body_length_ > max_body_length) {
			body_length_ = 0;
			return false;
		}
		action 	= *(reinterpret_cast<uint16_t *> (data_+2));
		id 		= *(reinterpret_cast<uint16_t *> (data_+4));

		return true;
	}

	void encode_header() {
		std::memcpy(data_, &body_length_, 2);
		std::memcpy(data_+2, &action, 2);
		std::memcpy(data_+4, &id, 2);
	}

	uint16_t action;
	uint16_t id;


private:
	char data_[header_length + max_body_length];
	uint16_t body_length_;
};

const std::string formatSize(int bytes) {
	if(bytes > 1024*1024) {
		return (std::to_string(bytes / (1024*1024)) + "MB");
	} else if(bytes > 1024) {
		return (std::to_string(bytes / (1024)) + "KB");
	} else {
		return (std::to_string(bytes));
	}
}