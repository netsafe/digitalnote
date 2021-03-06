// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "TcpConnector.h"
#include <cassert>
#include <iostream>
#include <random>
#include <sstream>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include "InterruptedException.h"
#include "Dispatcher.h"
#include "TcpConnection.h"

using namespace System;

namespace {

struct ConnectorContext : public Dispatcher::ContextExt {
  int connection;
  bool interrupted;
};

}

TcpConnector::TcpConnector() : dispatcher(nullptr) {
}

TcpConnector::TcpConnector(Dispatcher& dispatcher, const std::string& address, uint16_t port) : dispatcher(&dispatcher), address(address), port(port), stopped(false), context(nullptr) {
}

TcpConnector::TcpConnector(TcpConnector&& other) : dispatcher(other.dispatcher) {
  if (other.dispatcher != nullptr) {
    address = other.address;
    port = other.port;
    stopped = other.stopped;
    context = other.context;
    other.dispatcher = nullptr;
  }
}

TcpConnector::~TcpConnector() {
}

TcpConnector& TcpConnector::operator=(TcpConnector&& other) {
  dispatcher = other.dispatcher;
  if (other.dispatcher != nullptr) {
    address = other.address;
    port = other.port;
    stopped = other.stopped;
    context = other.context;
    other.dispatcher = nullptr;
  }

  return *this;
}

void TcpConnector::start() {
  assert(dispatcher != nullptr);
  assert(stopped);
  stopped = false;
}

void TcpConnector::stop() {
  assert(dispatcher != nullptr);
  assert(!stopped);
  if (context != nullptr) {
    ConnectorContext* context2 = static_cast<ConnectorContext*>(context);
    if (!context2->interrupted) {
      struct kevent event;
      EV_SET(&event, context2->connection, EVFILT_WRITE, EV_DELETE | EV_DISABLE, 0, 0, NULL);

      if (kevent(dispatcher->getKqueue(), &event, 1, NULL, 0, NULL) == -1) {
        std::cerr << "kevent() failed, errno=" << errno << '.' << std::endl;
        throw std::runtime_error("TcpConnector::stop");
      }

      dispatcher->pushContext(context2->context);
      context2->interrupted = true;
    }
  }

  stopped = true;
}

TcpConnection TcpConnector::connect() {
  assert(dispatcher != nullptr);
  assert(context == nullptr);
  if (stopped) {
    throw InterruptedException();
  }

  std::ostringstream portStream;
  portStream << port;
  addrinfo hints = { 0, AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL };
  addrinfo *addressInfos;
  int result = getaddrinfo(address.c_str(), portStream.str().c_str(), &hints, &addressInfos);
  if (result == -1) {
    std::cerr << "getaddrinfo failed, errno=" << errno << '.' << std::endl;
  } else {
    std::size_t count = 0;
    for (addrinfo* addressInfo = addressInfos; addressInfo != nullptr; addressInfo = addressInfo->ai_next) {
      ++count;
    }

    std::random_device randomDevice;
    std::mt19937 generator(randomDevice());
    std::uniform_int_distribution<std::size_t> distribution(0, count - 1);
    std::size_t index = distribution(generator);
    addrinfo* addressInfo = addressInfos;
    for (std::size_t i = 0; i < index; ++i) {
      addressInfo = addressInfo->ai_next;
    }

    sockaddr_in addressData = *reinterpret_cast<sockaddr_in*>(addressInfo->ai_addr);
    freeaddrinfo(addressInfo);
    int connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection == -1) {
      std::cerr << "socket failed, errno=" << errno << '.' << std::endl;
    } else {
      sockaddr_in bindAddress;
      bindAddress.sin_family = AF_INET;
      bindAddress.sin_port = 0;
      bindAddress.sin_addr.s_addr = INADDR_ANY;
      if (bind(connection, reinterpret_cast<sockaddr*>(&bindAddress), sizeof bindAddress) != 0) {
        std::cerr << "bind failed, errno=" << errno << '.' << std::endl;
      } else {
        int flags = fcntl(connection, F_GETFL, 0);
        if (flags == -1 || (fcntl(connection, F_SETFL, flags | O_NONBLOCK) == -1)) {
          std::cerr << "fcntl() failed errno=" << errno << std::endl;
        } else {
          int result = ::connect(connection, reinterpret_cast<sockaddr *>(&addressData), sizeof addressData);
          if (result == -1) {
            if (errno == EINPROGRESS) {

              ConnectorContext context2;
              context2.context = dispatcher->getCurrentContext();
              context2.interrupted = false;
              context2.connection = connection;

              struct kevent event;
              EV_SET(&event, connection, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_ONESHOT | EV_CLEAR, 0, 0, &context2);
              if (kevent(dispatcher->getKqueue(), &event, 1, NULL, 0, NULL) == -1) {
                std::cerr << "kevent() failed, errno=" << errno << '.' << std::endl;
              } else {
                context = &context2;
                dispatcher->yield();
                assert(dispatcher != nullptr);
                assert(context2.context == dispatcher->getCurrentContext());
                assert(context == &context2);
                context = nullptr;
                context2.context = nullptr;
                if (context2.interrupted) {
                  if (close(connection) == -1) {
                    std::cerr << "close failed, errno=" << errno << std::endl;
                  }

                  throw InterruptedException();
                }
                struct kevent event;
                EV_SET(&event, connection, EVFILT_WRITE, EV_ADD | EV_DISABLE, 0, 0, NULL);

                if (kevent(dispatcher->getKqueue(), &event, 1, NULL, 0, NULL) == -1) {
                  std::cerr << "kevent() failed, errno=" << errno << '.' << std::endl;
                } else {
                  int retval = -1;
                  socklen_t retValLen = sizeof(retval);
                  int s = getsockopt(connection, SOL_SOCKET, SO_ERROR, &retval, &retValLen);
                  if (s == -1) {
                    std::cerr << "getsockopt() failed, errno=" << errno << '.' << std::endl;
                  } else {
                    if (retval != 0) {
                      std::cerr << "connect failed; getsockopt retval = " << retval << std::endl;
                    } else {
                      return TcpConnection(*dispatcher, connection);
                    }
                  }
                }
              }
            }
          } else {
            return TcpConnection(*dispatcher, connection);
          }
        }
      }

      if (close(connection) == -1) {
        std::cerr << "close failed, errno=" << errno << std::endl;
      }
    }
  }

  throw std::runtime_error("TcpConnector::connect");
}
