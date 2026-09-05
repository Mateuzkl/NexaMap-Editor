// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_transport.h"
#include <wx/app.h>
#include <wx/timer.h>
#include <chrono>
#include <algorithm>
#include <thread>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <iphlpapi.h>
	#pragma comment(lib, "iphlpapi.lib")
#else
	#include <ifaddrs.h>
	#include <arpa/inet.h>
	#include <net/if.h>
	#include <netdb.h>
#endif

namespace Multiplayer {
	std::shared_ptr<AddressResolution> resolveIPv4(const std::string& hostname) {
		auto result = std::make_shared<AddressResolution>();
		in_addr numeric {};
		if (inet_pton(AF_INET, hostname.c_str(), &numeric) == 1) {
			result->address = hostname;
			result->done = true;
			return result;
		}
		// Repeated cancelled joins must not accumulate an unbounded number of DNS workers.
		static std::atomic<unsigned> resolving = 0;
		if (resolving.fetch_add(1) >= 4) {
			--resolving;
			throw Error("Too many hostname lookups pending. Wait or enter an IPv4 address.");
		}
		try {
			std::thread([result, hostname] {
				addrinfo hints {};
				hints.ai_family = AF_INET;
				hints.ai_socktype = SOCK_STREAM;
				addrinfo* addresses = nullptr;
				if (getaddrinfo(hostname.c_str(), nullptr, &hints, &addresses) == 0) {
					char ip[INET_ADDRSTRLEN] {};
					if (addresses && inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(addresses->ai_addr)->sin_addr, ip, sizeof(ip))) {
						result->address = ip;
					}
					freeaddrinfo(addresses);
				}
				result->done.store(true, std::memory_order_release);
				--resolving;
			}).detach();
		} catch (...) {
			--resolving;
			throw;
		}
		return result;
	}
	namespace {
		unsigned closingSockets = 0; // GUI thread only; also bounds peers which never acknowledge FIN.
		class ClosingSocket final : public wxEvtHandler {
		public:
			ClosingSocket(wxSocketBase* socket, std::deque<Bytes> writes, size_t offset, const std::string& reason) :
				socket(socket), writes(std::move(writes)), offset(offset), timer(this), deadline(std::chrono::steady_clock::now() + std::chrono::seconds(2)) {
				// Never interrupt the frame already on the wire, but discard all later work.
				if (offset && !this->writes.empty()) {
					this->writes.erase(std::next(this->writes.begin()), this->writes.end());
				} else {
					this->writes.clear();
					this->offset = 0;
				}
				Writer out;
				out.string(reason.substr(0, MaxChat), MaxChat);
				this->writes.push_back(frame(Packet::Disconnect, out.data));
				socket->SetFlags(wxSOCKET_NOWAIT);
				socket->SetEventHandler(*this);
				socket->SetNotify(wxSOCKET_OUTPUT_FLAG | wxSOCKET_LOST_FLAG);
				socket->Notify(true);
				Bind(wxEVT_SOCKET, [this](wxSocketEvent& event) {
					if (event.GetSocketEvent() == wxSOCKET_LOST) {
						finish();
					} else {
						pump();
					}
				});
				Bind(wxEVT_TIMER, [this](wxTimerEvent&) { pump(); });
				timer.Start(20);
				++closingSockets;
				pump(); // Small shutdown messages reach TCP even when the main frame is closing.
			}
			~ClosingSocket() override {
				--closingSockets;
				if (socket) {
					socket->Destroy();
				}
			}

		private:
			void finish() {
				if (!socket) {
					return;
				}
				timer.Stop();
				socket->Notify(false);
				socket->Destroy();
				socket = nullptr;
				DeletePendingEvents();
				wxTheApp->ScheduleForDestruction(this);
			}
			void pump() {
				if (!socket) {
					return;
				}
				if (std::chrono::steady_clock::now() >= deadline) {
					finish();
					return;
				}
				size_t budget = 256 * 1024;
				while (!writes.empty() && budget) {
					auto& bytes = writes.front();
					socket->Write(bytes.data() + offset, static_cast<wxUint32>(std::min(budget, bytes.size() - offset)));
					const auto sent = socket->LastWriteCount();
					offset += sent;
					budget -= sent;
					if (socket->Error() && socket->LastError() != wxSOCKET_WOULDBLOCK) {
						finish();
						return;
					}
					if (offset == bytes.size()) {
						writes.pop_front();
						offset = 0;
					}
					if (!sent) {
						return;
					}
				}
				if (writes.empty() && !shutdownSent) {
					shutdownSent = true;
					// FIN follows the queued rejection; wait for the peer or the deadline.
#ifdef _WIN32
					::shutdown(socket->GetSocket(), SD_SEND);
#else
					::shutdown(socket->GetSocket(), SHUT_WR);
#endif
				}
			}
			wxSocketBase* socket;
			std::deque<Bytes> writes;
			size_t offset;
			wxTimer timer;
			std::chrono::steady_clock::time_point deadline;
			bool shutdownSent = false;
		};
	}
	void closeSocket(wxSocketBase* socket, std::deque<Bytes> writes, size_t offset, const std::string& reason) {
		if (!socket) {
			return;
		}
		socket->Notify(false);
		if (!wxTheApp || !socket->IsConnected() || closingSockets >= 64) {
			socket->Destroy();
			return;
		}
		new ClosingSocket(socket, std::move(writes), offset, reason);
	}
	std::string socketFailure(wxSocketBase& socket, bool connecting) {
		int error = 0;
		int size = sizeof(error);
		socket.GetOption(SOL_SOCKET, SO_ERROR, &error, &size);
#ifdef _WIN32
		if (error == WSAECONNREFUSED) {
			return "Connection refused.";
		}
		if (error == WSAETIMEDOUT) {
			return "Connection timed out.";
		}
#else
		if (error == ECONNREFUSED) {
			return "Connection refused.";
		}
		if (error == ETIMEDOUT) {
			return "Connection timed out.";
		}
#endif
		// wxWidgets may consume the native error before delivering wxSOCKET_LOST.
		return connecting ? "Connection failed (refused or host unreachable). Check the host address, listening port and firewall." : "Host disconnected or the connection was lost.";
	}
	std::vector<std::string> localIPv4Addresses() {
		std::vector<std::string> addresses;
		auto add = [&](const sockaddr* address) {
			if (!address || address->sa_family != AF_INET) {
				return;
			}
			const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
			const auto value = ntohl(ipv4->sin_addr.s_addr);
			if (!value || (value >> 24) == 127) {
				return;
			}
			addresses.push_back(std::to_string(value >> 24) + "." + std::to_string((value >> 16) & 255) + "." + std::to_string((value >> 8) & 255) + "." + std::to_string(value & 255));
		};
#ifdef _WIN32
		ULONG size = 16384;
		std::vector<unsigned char> buffer(size);
		auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		auto result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
		if (result == ERROR_BUFFER_OVERFLOW) {
			buffer.resize(size);
			adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
			result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
		}
		if (result == NO_ERROR) {
			for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
				if (adapter->OperStatus != IfOperStatusUp) {
					continue;
				}
				for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
					add(address->Address.lpSockaddr);
				}
			}
		}
#else
		ifaddrs* interfaces = nullptr;
		if (getifaddrs(&interfaces) == 0) {
			for (auto* entry = interfaces; entry; entry = entry->ifa_next) {
				if (entry->ifa_flags & IFF_UP) {
					add(entry->ifa_addr);
				}
			}
			freeifaddrs(interfaces);
		}
#endif
		std::sort(addresses.begin(), addresses.end());
		addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
		return addresses;
	}
}
