#include "Globals.hpp"
#include "ThreadContext.hpp"

#include <csignal>
#include <getopt.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <thread>

static int caughtSignal;

int main(const int argc, const char **args) {
  const auto PROGRAM_START = std::chrono::steady_clock::now();
  const auto signalHandler = [](int SN) { caughtSignal = SN; };
  static const option __OPTS__[] = {
      {"help", no_argument, nullptr, 0},
      {"initial-threads", required_argument, nullptr, 0},
      {"max-acquire-delay", required_argument, nullptr, 0},
      {"max-lock-hold-time", required_argument, nullptr, 0},
      {nullptr, 0, nullptr, 0}};
  unsigned int i, nb_initialThreads;
  int ec;
  ContextID counter;
  bool loopFlag;
  ThreadContext *ctx;
  std::string signalAckMsg, signalName;
  std::stringstream ss;

  nb_initialThreads = std::thread::hardware_concurrency();
  ec = 1;
  counter = 0;
  ::signal(SIGTERM, signalHandler);
  ::signal(SIGINT, signalHandler);
  ::signal(SIGRTMIN + 0, signalHandler);
  ::signal(SIGRTMIN + 1, signalHandler);
  ::signal(SIGRTMIN + 2, signalHandler);

  {
    // 옵션 파싱
    int opt_index, opt_char;

    while ((opt_char = getopt_long_only(argc, (char **)args, "", __OPTS__,
                                        &opt_index)) >= 0) {
      if (opt_char == '?') {
        return 2;
      }

      if (optarg == nullptr) {
        switch (opt_index) {
        case 0:
          std::cerr << "--help: 이 메시지를 출력." << std::endl
                    << "--initial-threads=N:(uint) 초기 스레드 개수."
                    << std::endl
                    << "--max-acquire-delay=N:(uint32_t) 스레드가 락을 "
                       "해제했을 때, N ms 이후 "
                       "다시 락 획득을 시작함. 0 <= N < UINT32_MAX"
                    << std::endl
                    << "--max-lock-hold-time=N:(uint32_t) 스레드가 락을 "
                       "획득했을 때, N ms 이후 "
                       "락을 해제함. 0 <= N < UINT32_MAX"
                    << std::endl;
          return 0;
        }
      } else {
        ss.clear();
        ss.str(optarg);

        switch (opt_index) {
        case 1:
          ss >> nb_initialThreads;
          break;
        case 2:
          ss >> ::maxAcquireDelay;
          break;
        case 3:
          ss >> ::maxLockHoldTime;
          break;
        default:
          ::abort();
        }

        if (ss.fail()) {
          std::cerr << "** 잘못된 '" << __OPTS__[opt_index].name
                    << "' 옵션 값 형식." << std::endl;
          return 2;
        }
      }
    }
  }
  ss.clear();
  ss.str("");

  try {
    if (::maxAcquireDelay == UINT32_MAX) {
      throw std::string("--max-acquire-delay");
    }
    if (::maxLockHoldTime == UINT32_MAX) {
      throw std::string("--max-acquire-delay");
    }
  } catch (std::string &msg) {
    std::cerr << "잘못된 '" << msg << "' 옵션 값 범위." << std::endl;
    return 2;
  }

  // 스레드 생성
  for (i = 0; i < nb_initialThreads; i += 1) {
    ctx = new ThreadContext();
    ctx->start(++counter);
    ::addContext(ctx);
  }

  loopFlag = true;
  do {
    caughtSignal = -1;
    ::pause();

    switch (caughtSignal) {
    case SIGTERM:
      signalName = "SIGTERM";
      break;
    case SIGINT:
      signalName = "SIGINT";
      break;
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
             << std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - PROGRAM_START)
                    .count()
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
          } else {
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

    if (!signalAckMsg.empty()) {
      {
        std::lock_guard<std::mutex> lg(::stdioLock);
        std::cerr << signalAckMsg << std::endl;
      }
      signalAckMsg.clear();
    }
  } while (loopFlag);

  return ec;
}
