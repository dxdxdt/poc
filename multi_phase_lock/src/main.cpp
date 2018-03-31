#include "Globals.hpp"
#include "ThreadContext.hpp"

#include <unistd.h>
#include <csignal>

#include <string>
#include <sstream>
#include <thread>

static int caughtSignal;

int main () {
  const auto PROGRAM_START = std::chrono::steady_clock::now();
  const auto signalHandler = [](int SN) {
    caughtSignal = SN;
  };
  unsigned int i;
  int ec;
  ContextID counter;
  bool loopFlag;
  ThreadContext *ctx;
  std::string signalAckMsg, signalName;
  std::stringstream ss;

  ::maxAcquireDelay = 0; // TODO

  ec = 1;
  counter = 0;
  ::signal(SIGTERM, signalHandler);
  ::signal(SIGINT, signalHandler);
  ::signal(SIGRTMIN + 0, signalHandler);
  ::signal(SIGRTMIN + 1, signalHandler);
  ::signal(SIGRTMIN + 2, signalHandler);

  {
    const auto cnt = std::thread::hardware_concurrency();

    for (i = 0; i < cnt; i += 1) {
      ctx = new ThreadContext();
      ctx->start(++counter);
      ::addContext(ctx);
    }
  }

  loopFlag = true;
  do {
    caughtSignal = -1;
    ::pause();

    switch (caughtSignal) {
      case SIGTERM: signalName = "SIGTERM"; break;
      case SIGINT: signalName = "SIGINT"; break;
      default:
        if (SIGRTMIN <= caughtSignal && caughtSignal <= SIGRTMAX) {
          ss << "SIGRT_" << caughtSignal - SIGRTMIN;
          signalName = ss.str();
          ss.clear();
          ss.str("");
        }
    }

    switch (caughtSignal) {
      case SIGTERM:
      case SIGINT:
        ss << signalName << " caught. Send again to terminate.";
        signalAckMsg = ss.str();
        ss.clear();
        ss.str("");

        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGINT, SIG_DFL);
        loopFlag = false;
        ec = 0;
        break;
      default:
        if (SIGRTMIN <= caughtSignal && caughtSignal <= SIGRTMAX) {
          switch (caughtSignal - SIGRTMIN) {
            case 0:
              ss << "Up "
                << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - PROGRAM_START).count()
                << "s." << std::endl;

              ss << "[Lock Acquire Count]" << std::endl;
              for (const auto &p : ::threads) {
                ss << p.second->id() << ": " << p.second->acquiredCount()
                  << std::endl;
              }

              signalAckMsg = ss.str();
              ss.clear();
              ss.str("");

              break;
            case 1:
              ss << signalName << " caught. Adding another context ...";
              signalAckMsg = ss.str();
              ss.clear();
              ss.str("");

              ctx = new ThreadContext();
              ctx->start(++counter);
              ::addContext(ctx);
              break;
            case 2:
              if (::threads.empty()) {
                ss << signalName << " caught, but no context to delete.";
              }
              else {
                ss << signalName << " caught. Deleting one context ...";

                ctx = ::popContext(::threads.begin()->first);
                delete ctx;
              }
              signalAckMsg = ss.str();
              ss.clear();
              ss.str("");
              break;
          }
        }
    }

    if (!signalAckMsg.empty())
    {
      {
        std::lock_guard<std::mutex> lg(::stdioLock);
        std::cerr << signalAckMsg << std::endl;
      }
      signalAckMsg.clear();
    }
  } while (loopFlag);

  return ec;
}
