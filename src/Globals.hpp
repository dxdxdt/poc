#ifndef GLOBALS_H_
#define GLOBALS_H_
#include <cstdint>
#include <vector>
#include <map>
#include <mutex>

typedef uint32_t ContextID;

class ThreadContext;
extern std::vector<ThreadContext*> threads;
extern std::map<ContextID, ThreadContext*> idThreadMap;
extern std::mutex globalLock;

extern std::mutex resourceLock;

extern uint64_t maxAcquireDelay;

enum OPCode {
  OPC_SHUTDOWN,
  OPC_THREAD_SPAWNED,
  OPC_THREAD_DESPAWNED,
  OPC_MY_LOCK,
  OPC_YOUR_LOCK,
  OPC_LOCK_RESET
};

struct Command {
  OPCode op_code;
  ContextID context_from;
  ContextID context_to;
};

void sendCommand (Command *cmd);

#endif /* end of include guard: GLOBALS_H_ */
