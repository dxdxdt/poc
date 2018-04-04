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

// 피어가 lock을 가지고 있지 않을 때, 다시 lock을 획득하기 전까지 대기할 최대 시간.
// ms 단위.
extern uint32_t maxAcquireDelay;
// 피어가 lock을 가지고 있을 최대 시간. 자원 획득 후 처리 시간을 시뮬레이션하기 위해 존재.
// ms 단위.
extern uint32_t maxLockHoldTime;

enum OPCode {
  // 스레드 종료 명령
  OPC_SHUTDOWN,
  // 스레드가 생성되었다는 통보
  OPC_THREAD_SPAWNED,
  // 스레드가 삭제되었다면 통보
  OPC_THREAD_DESPAWNED,
  // "MyLock" 메시지
  OPC_MY_LOCK,
  // "YourLock" 메시지
  OPC_YOUR_LOCK,
  // "LockReset" 메시지
  OPC_LOCK_RESET
};

struct Command {
  OPCode op_code;
  // 메시지를 보낸 피어의 ID. 0일 경우 피어가 보낸 메시지가 아님을 의미.
  ContextID context_from;
  // 메시지를 수신할 피어의 ID. 0일 경우 모든 피어가 수신하는 메시지를 의미.
  ContextID context_to;
};

void addContext (ThreadContext *ctx);
ThreadContext *popContext (const ContextID id);
void clearContexts ();

void sendCommand (Command *cmd);

#endif /* end of include guard: GLOBALS_H_ */
