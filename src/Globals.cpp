#include "Globals.hpp"
#include "ThreadContext.hpp"

std::vector<ThreadContext*> threads;
std::map<ContextID, ThreadContext*> idThreadMap;
std::mutex globalLock;

std::mutex resourceLock;

// in microseconds
uint64_t maxAcquireDelay = 0;

void sendCommand (Command *cmd) {
  std::lock_guard<std::mutex> lg(::globalLock);

  if (cmd->context_to == 0) {
    for (auto &v : ::threads) {
      if (cmd->context_from != v->id()) {
        v->pushCommand(new Command(*cmd));
      }
    }

    delete cmd;
  }
  else {
    const auto it = ::idThreadMap.find(cmd->context_to);

    if (::idThreadMap.end() != it) {
      it->second->pushCommand(cmd);
    }
  }
}
