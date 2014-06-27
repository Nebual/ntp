//
// Based on a chat_client.cpp example from the Boost Docs

#include <cstdlib>
#include <map>
#include <deque>
#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include "packet.h"

using boost::asio::ip::tcp;

struct FileInfo {
	std::ofstream stream;
	uint32_t finalSize;
	uint32_t curSize;

	FileInfo() {
		curSize = 0;
	}
	~FileInfo() {
		printf("Erasing a fileinfo!\n");
	}
};

class chat_client {
public:
	chat_client(boost::asio::io_service& io_service, tcp::resolver::iterator endpoint_iterator)
													: io_service_(io_service), socket_(io_service) {
		do_connect(endpoint_iterator);
	}

	void write(const Packet& msg) {
		io_service_.post(
				[this, msg]() {
					bool write_in_progress = !write_msgs_.empty();
					write_msgs_.push_back(msg);
					if (!write_in_progress) {
						do_write();
					}
				});
	}

	void close() {
		io_service_.post([this]() { socket_.close(); });
	}

	void displayProgress() {
		while(1) {
			boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
			for (auto& kv : files) {
				printf("%d: %s/%s (%.0f%%)\n", kv.first, formatSize(kv.second.curSize).c_str(), formatSize(kv.second.finalSize).c_str(), (100.0 * kv.second.curSize) / kv.second.finalSize);
			}
		}
	}

private:
	void do_connect(tcp::resolver::iterator endpoint_iterator) {
		boost::asio::async_connect(socket_, endpoint_iterator,
				[this](boost::system::error_code ec, tcp::resolver::iterator) {
					if (!ec) {
						do_read_header();
					}
				});
	}

	// All the "recv()" goes on in these two functions
	void do_read_header() {
		// Read 4 bytes, so we know how large the body of the packet is
		boost::asio::async_read(socket_,
				boost::asio::buffer(curPacket.data(), Packet::header_length),
				[this](boost::system::error_code ec, std::size_t /*length*/) {
					if (!ec && curPacket.decode_header()) {
						do_read_body(); // Now read the body
					}
					else {
						socket_.close();
					}
				});
	}
	void do_read_body() {
		// Read until we have the entire body of the packet (length known from the header)
		boost::asio::async_read(socket_,
				boost::asio::buffer(curPacket.body(), curPacket.body_length()),
				[this](boost::system::error_code ec, std::size_t /*length*/) {
					if (!ec) {
						parsePacket(curPacket); // Actually process the packet
						do_read_header(); // Loop back to waiting for a header
					}
					else {
						socket_.close();
					}
				});
	}

	void do_write() {
		boost::asio::async_write(socket_,
				boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
				[this](boost::system::error_code ec, std::size_t /*length*/) {
					if (!ec) {
						write_msgs_.pop_front();
						if (!write_msgs_.empty()) {
							do_write();
						}
					}
					else {
						socket_.close();
					}
				});
	}

	std::map<short int, FileInfo> files;

	void parsePacket(Packet &packet) {
		switch(packet.action) {
			case OPEN_W: {
				files[packet.id].finalSize = *(reinterpret_cast<uint32_t *> (packet.body()));
				char* filename = packet.body()+4;
				filename[packet.body_length()-4] = 0;
				printf("Beginning download of '%s'\n", filename);
				files[packet.id].stream.open(filename, std::ios::binary);
				break;
			}
			case WRITE_CHUNK: {
				files[packet.id].stream.write(packet.body(), packet.body_length());
				files[packet.id].curSize += packet.body_length();
				break;
			}
			case CLOSE: {
				printf("Download complete!\n");
				files[packet.id].stream.close();
				files.erase(packet.id);
				break;
			}
			default: {
				packet.body()[packet.body_length()] = 0;
				std::cout << packet.body() << "\n";
			}
		}
	}

private:
	boost::asio::io_service& io_service_;
	tcp::socket socket_;
	Packet curPacket;
	std::deque<Packet> write_msgs_;
};

int main(int argc, char* argv[]) {
	try {
		if (argc != 3) {
			std::cerr << "Usage: ntp_client <host> <port>\n";
			return 1;
		}

		boost::asio::io_service io_service;

		tcp::resolver resolver(io_service);
		auto endpoint_iterator = resolver.resolve({ argv[1], argv[2] });
		chat_client client(io_service, endpoint_iterator);

		boost::thread thread([&io_service](){ io_service.run(); });
		boost::thread thread2([&client](){ client.displayProgress(); });

		char line[Packet::max_body_length + 1];
		while (std::cin.getline(line, Packet::max_body_length + 1)) {
			Packet msg;
			if(strncmp(line, "ls", 2) == 0) {
				if(strlen(line) > 3) {
					msg.body_length(std::strlen(line + 3));
					std::memcpy(msg.body(), line + 3, msg.body_length());
				} else {
					msg.body_length(std::strlen("."));
					std::memcpy(msg.body(), ".", msg.body_length());
				}
				msg.action = LISTDIR;
				msg.encode_header();
				client.write(msg);
			} else if(strncmp(line, "cd", 2) == 0 && strlen(line) > 3) {
				msg.body_length(std::strlen(line + 3));
				std::memcpy(msg.body(), line + 3, msg.body_length());
				msg.action = CHANGEDIR;
				msg.encode_header();
				client.write(msg);
			} else if(strncmp(line, "pwd", 3) == 0) {
				msg.action = PRINTDIR;
				msg.body_length(0);
				msg.encode_header();
				client.write(msg);
			} else if(strncmp(line, "get", 3) == 0) {
				char *filename = line + 4;
				msg.action = OPEN_R;
				msg.body_length(std::strlen(filename));
				std::memcpy(msg.body(), filename, msg.body_length());
				msg.encode_header();
				client.write(msg);
			}
		}

		client.close();
		thread.join();
		thread2.join();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}