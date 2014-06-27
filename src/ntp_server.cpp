//
// Based on a chat_server.cpp example from the Boost Docs

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include "packet.h"

using boost::asio::ip::tcp;

class ntp_session : public std::enable_shared_from_this<ntp_session> {
public:
	ntp_session(tcp::socket socket) : sessionSocket(std::move(socket)) {
		nextID = 0;
		cwd = boost::filesystem::current_path();
	}

	void start() {
		do_read_header();
	}

private:
	tcp::socket sessionSocket;
	Packet recvPacket;
	boost::filesystem::path cwd;
	uint16_t nextID;

	void do_read_header() {
		auto self(shared_from_this());
		boost::asio::async_read(sessionSocket,
				boost::asio::buffer(recvPacket.data(), Packet::header_length),
				[this, self](boost::system::error_code ec, std::size_t /*length*/) {
					if (!ec && recvPacket.decode_header()) {
						//printf("Header length: %d\n", recvPacket.body_length());
						do_read_body();
					}
				});
	}

	void do_read_body() {
		auto self(shared_from_this());
		boost::asio::async_read(sessionSocket,
				boost::asio::buffer(recvPacket.body(), recvPacket.body_length()),
				[this, self](boost::system::error_code ec, std::size_t /*length*/)  {
					if (!ec) {
						parsePacket(recvPacket);
						do_read_header(); // Read the next one
					}
				});
	}

	void parsePacket(Packet &packet) {
		packet.body()[packet.body_length()] = 0; // add a 0 at the end so its a valid c string
		switch(packet.action) {
			case OPEN_R: {
				char filename[512]; // filename[packet.body_length() + 1] was causing the var to get GC'd, doesn't seem to happen with static length
				strncpy(filename, packet.body(), packet.body_length() + 1);
				printf("Sending file '%s'\n", filename);
				boost::thread thread([this, filename](){ downloadFile(filename); });
				break;
			}
			case LISTDIR: {
				char filename[512];
				strncpy(filename, packet.body(), packet.body_length() + 1);
				printf("Listing directory '%s'\n", filename);
				boost::thread thread([this, filename](){ listdir(filename); });
				break;
			}
			case CHANGEDIR: {
				char filepath[512];
				strncpy(filepath, packet.body(), packet.body_length() + 1);
				printf("Changing directory to '%s'\n", filepath);
				boost::system::error_code fs_error;
				boost::filesystem::path newpath = boost::filesystem::canonical(filepath, cwd, fs_error);
				if(!fs_error) {
					cwd = newpath;
				}
				// No break
			}
			case PRINTDIR: {
				boost::thread thread([this](){ sendTextMessage(("CWD is now " + this->cwd.string()).c_str()); });
				break;
			}
			default: 
				printf("Received Packet: '%s'\n", packet.body());
		}
	}

	void downloadFile(const char* filepath) {
		Packet packet;
		packet.id = nextID++;

		boost::filesystem::path path = boost::filesystem::path(cwd); path /= filepath;
		if(!boost::filesystem::exists(path)) {
			std::string errmsg = std::string("get Error: File not found '") + filepath + "'\n";
			std::cerr << errmsg;
			sendTextMessage(errmsg.c_str());
			return;
		}

		packet.action = OPEN_W;
		std::string file = path.filename().string();
		uint32_t size = boost::filesystem::file_size(path);
		std::memcpy(packet.body(), &size, 4);
		strcpy(packet.body()+4, file.c_str());
		packet.body_length(file.size()+4);
		if(writePacket(packet)) return;


		packet.action = WRITE_CHUNK;
		std::ifstream stream(path.string().c_str(), std::ios::binary);
		while (stream) {
			stream.read(packet.body(), packet.max_body_length);
			packet.body_length(stream.gcount());
			if(writePacket(packet)) break;
		}
		stream.close();


		packet.action = CLOSE;
		packet.body_length(0);
		if(writePacket(packet)) return;
	}

	boost::system::error_code writePacket(Packet &packet) {
		boost::system::error_code ignored_error;

		packet.encode_header();
		boost::asio::write(sessionSocket, boost::asio::buffer(packet.data(), packet.length()),
									boost::asio::transfer_all(), ignored_error);  
		if(ignored_error) {
			std::cerr << "writePacket Error: " << ignored_error.message() << "\n";
			return ignored_error;
		}
	}

	void listdir(const char* str_path) {
		boost::filesystem::path path = boost::filesystem::path(cwd); path /= str_path;
		if(!boost::filesystem::exists(path)) {
			std::string errmsg = "listdir Error: Directory not found '" + path.string() + "'\n";
			std::cerr << errmsg;
			sendTextMessage(errmsg.c_str());
			return;
		}

		Packet packet;
		packet.action = PLAIN_TEXT;
		std::stringstream contents("Directory Contents:\n", std::ios_base::app | std::ios_base::out);
		boost::filesystem::directory_iterator end_iter;
		for ( boost::filesystem::directory_iterator dir_itr( path); dir_itr != end_iter; ++dir_itr ) {
			try {
				std::string filename = dir_itr->path().filename().string();
				if ( boost::filesystem::is_directory( dir_itr->status() ) ) {
					contents << "\t" << filename << "/\n";
				}
				else if ( boost::filesystem::is_regular_file( dir_itr->status() ) ) {
					contents << formatSize(boost::filesystem::file_size(dir_itr->path())) << "\t" << filename << "\n";
				}
				else {
					contents << "\t" << filename << " [other]\n";
				}

			}
			catch ( const std::exception & ex ) {
				contents << dir_itr->path().filename() << " " << ex.what() << std::endl;
			}
		}
		strncpy(packet.body(), contents.str().c_str(), packet.max_body_length);
		packet.body_length(contents.str().length());
		writePacket(packet);
	}

	void sendTextMessage(const char* text) {
		Packet packet;
		packet.action = PLAIN_TEXT;
		packet.body_length(strlen(text));
		strncpy(packet.body(), text, packet.max_body_length);
		writePacket(packet);
	}
};

class ntp_server {
public:
	ntp_server(boost::asio::io_service& io_service, const tcp::endpoint& endpoint)
		: svAcceptor(io_service, endpoint), svSocket(io_service) {
		do_accept();
	}

private:
	tcp::acceptor svAcceptor;
	tcp::socket svSocket;

	void do_accept() {
		svAcceptor.async_accept(svSocket,
				[this](boost::system::error_code ec)  {
					if (!ec) {
						std::make_shared<ntp_session>(std::move(svSocket))->start();
					}

					do_accept();
				});
	}
};

int main(int argc, char* argv[]) {
	try {
		int port = 5000;
		if (argc > 1) {
			port = std::atoi(argv[1]);
			//std::cerr << "Usage: ntp_server <port> [<port> ...]\n";
			//return 1;
		}

		boost::asio::io_service io_service;

		printf("Starting listening on %d\n", port);
		tcp::endpoint endpoint(tcp::v4(), port);
		ntp_server sv = ntp_server(io_service, endpoint);

		io_service.run();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}
