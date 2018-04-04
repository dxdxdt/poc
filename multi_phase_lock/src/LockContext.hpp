#ifndef LOCKCONTEXT_H_
#define LOCKCONTEXT_H_
#include "Globals.hpp"

#include <set>

struct LockContext {
  enum LockState {
    NONE,
    LURKING,
    SOLICITING,
    ACQUIRED
  };

  LockState state = NONE;

  // 내가 락을 얻으려 한 시점에, "MyLock" 명령을 보낸 곳들.
  // 중간에 다른 Context가 접속했으면, 그 Context는 이 컬렉션에 존재하지 않음.
  // 단, 중간에 이미 존재하던 Context가 사라지면, 그 Context를 이 컬렉션에서 제거하는 처리는
  // 함.
  std::set<ContextID> sentMyLock;
  // "YourLock" 명령을 받아야하는 곳들.
  // `SOLICITING` 상태에서 이 컬렉션에 아이템이 없으면 `ACQUIRED` 상태로 진입한다.
  std::set<ContextID> yourLockToRcv;
  // "YourLock" 명령을 보내야할 곳들 (deferred)
  // `SOLICITING` 상태에서 나보다 우선순위가 낮은 곳에서 "MyLock" 명령이 수신되면
  // "기억"할 때 씀.
  std::set<ContextID> yourLockToSend;
  // `MyLock` 명령을 받은 곳들 (락을 얻으려는 곳들)
  std::set<ContextID> rcvMyLock;
};

#endif /* end of include guard: LOCKCONTEXT_H_ */
