#ifndef GLOBALS_H_
#define GLOBALS_H_
#include <cstdint>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

typedef uint32_t ContextID;

class ThreadContext;
extern std::map<ContextID, ThreadContext*> threads;
extern std::mutex globalLock;

extern std::mutex stdioLock;
extern std::atomic<uint32_t> resource;

extern uint32_t maxAcquireDelay; // in ms
extern uint32_t maxLockHoldTime; // in ms

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

void addContext (ThreadContext *ctx);
ThreadContext *popContext (const ContextID id);
void clearContexts ();

void sendCommand (Command *cmd);

#endif /* end of include guard: GLOBALS_H_ */
