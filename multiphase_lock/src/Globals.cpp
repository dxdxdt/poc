#include "Globals.hpp"
#include "ThreadContext.hpp"

#include <exception>

std::map<ContextID, ThreadContext*> threads;
std::mutex globalLock;

std::mutex stdioLock;
std::atomic<uint32_t> resource;

uint32_t maxAcquireDelay = 0; // in ms
uint32_t maxLockHoldTime = 0; // in ms


void addContext (ThreadContext *ctx) {
  const auto id = ctx->id();
  Command *cmd;

  if (::threads.end() != ::threads.find(id)) {
    throw std::exception();
  }

  {
    std::lock_guard<std::mutex> lg(::globalLock);

    // 최초에 다른 스레드의 정보를 넘겨줌.
    for (const auto &p : ::threads) {
      cmd = new Command;
      cmd->op_code = OPC_THREAD_SPAWNED;
      cmd->context_from = p.first;
      cmd->context_to = id;

      ctx->pushCommand(cmd);
    }

    ::threads.insert(std::make_pair(id, ctx));
  }
}

ThreadContext *popContext (const ContextID id) {
  ThreadContext *ret;

  {
    std::lock_guard<std::mutex> lg(::globalLock);
    auto it = ::threads.find(id);

    if (it == ::threads.end()) {
      ret = nullptr;
    }
    else {
      ret = it->second;
      ::threads.erase(it);
    }
  }

  return ret;
}

void clearContexts () {
  std::map<ContextID, ThreadContext*> map;

  {
    std::lock_guard<std::mutex> lg(::globalLock);
    map.swap(::threads);
  }

  for (auto &p : map) {
    delete p.second;
  }
}

void sendCommand (Command *cmd) {
  std::lock_guard<std::mutex> lg(::globalLock);

  if (cmd->context_to == 0) {
    for (const auto &p : ::threads) {
      if (cmd->context_from != p.first) {
        p.second->pushCommand(new Command(*cmd));
      }
    }

    delete cmd;
  }
  else {
    const auto it = ::threads.find(cmd->context_to);

    if (::threads.end() != it) {
      it->second->pushCommand(cmd);
    }
  }
}
