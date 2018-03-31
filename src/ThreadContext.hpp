#ifndef THREADCONTEXT_H_
#define THREADCONTEXT_H_
#include "Globals.hpp"
#include "LockContext.hpp"
#include "CommandQueue.hpp"
#include "EventContext.hpp"

#include <iostream>
#include <sstream>
#include <thread>
#include <set>
#include <random>

#define __REPORT(msg) this->__report(__FILE__, __LINE__, msg)

class ThreadContext {
protected:
  ContextID __id = 0;

  std::thread __th;
  std::set<ContextID> __others;
  CommandQueue __cmdQueue;
  LockContext __lockCtx;
  EventContext __eventCtx;
  std::mt19937_64 __rnd;
  EventContext::ClockType::time_point __tickStart;

public:
  ThreadContext () {
  }

  ~ThreadContext () {
    this->stop();
  }

  ContextID id () {
    return this->__id;
  }

  void start (const ContextID id) {
    if (this->__th.joinable()) {
      throw std::exception();
    }
    if ((this->__id = id) == 0) {
      throw std::exception();
    }

    this->__th = std::thread([this]() {
      this->__run();
    });
  }

  void stop () {
    if (this->__th.joinable()) {
      auto cmd = new Command;

      cmd->op_code = OPC_SHUTDOWN;
      cmd->context_from = 0;
      cmd->context_to = this->__id;
      this->pushCommand(cmd);

      this->__th.join();
    }
  }

  void pushCommand (Command *cmd) {
    std::unique_lock<std::mutex> ul(this->__cmdQueue.mtx);

    this->__cmdQueue.q.push(cmd);
    this->__cmdQueue.cv.notify_all();
  }

protected:
  void __report (const char *file, const uint32_t line, const std::string msg) {
    std::cerr << msg << " (" << file << ':' << line << ')' << std::endl;
  }

  std::chrono::microseconds __randomAcquireDelay () {
    uint64_t ret;

    if (::maxAcquireDelay == 0) {
      ret = 0;
    }
    else {
      ret = this->__rnd() % // overflow aware
        ::maxAcquireDelay == UINT64_MAX ? UINT64_MAX : (::maxAcquireDelay + 1);
    }

    return std::chrono::microseconds(ret);
  }

  void __run () {
    Command *cmd;
    bool runFlag;

    runFlag = true;
    // 랜덤 엔진 초기화
    {
      std::hash<std::thread::id> hasher;
      const auto now = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
      const auto tid = (uint64_t)hasher(std::this_thread::get_id());

      try {
        std::random_device rnd;
        std::seed_seq seq = {
          (uint32_t)((0xFFFFFFFF00000000 & now) >> 32),
          (uint32_t)((0x00000000FFFFFFFF & now)),
          (uint32_t)((0xFFFFFFFF00000000 & tid) >> 32),
          (uint32_t)((0x00000000FFFFFFFF & tid)),
          (uint32_t)rnd()
        };

        this->__rnd.seed(seq);
      }
      catch (std::exception &) {
        std::seed_seq seq = {
          (uint32_t)((0xFFFFFFFF00000000 & now) >> 32),
          (uint32_t)((0x00000000FFFFFFFF & now)),
          (uint32_t)((0xFFFFFFFF00000000 & tid) >> 32),
          (uint32_t)((0x00000000FFFFFFFF & tid))
        };

        this->__rnd.seed(seq);
      }
    }

    // 초기 틱에서 바로 락 얻기 시도.
    this->__eventCtx.addImmediateEvent([this]() {
      this->__acquireLock();
    });

    do {
      {
        std::unique_lock<std::mutex> ul(this->__cmdQueue.mtx);

        this->__tickStart = EventContext::ClockType::now();

        while (this->__cmdQueue.q.empty() && (!this->__eventCtx.hasPendingEvent(this->__tickStart))) {
          if (this->__eventCtx.hasEvent()) {
            this->__cmdQueue.cv.wait_for(ul, this->__eventCtx.nextEvent(this->__tickStart));
          }
          else {
            this->__cmdQueue.cv.wait(ul);
          }

          this->__tickStart = EventContext::ClockType::now();
        }

        if (this->__cmdQueue.q.empty()) {
          cmd = nullptr;
        }
        else {
          cmd = this->__cmdQueue.q.front();
          this->__cmdQueue.q.pop();
        }
      }

      if (cmd != nullptr) {
        switch (cmd->op_code) {
          case OPC_SHUTDOWN: runFlag = false; break;
          case OPC_THREAD_SPAWNED: this->__cmdThreadSpawned(*cmd); break;
          case OPC_THREAD_DESPAWNED: this->__cmdThreadDespawned(*cmd); break;
          case OPC_MY_LOCK: this->__cmdMyLock(*cmd); break;
          case OPC_YOUR_LOCK: this->__cmdYourLock(*cmd); break;
          case OPC_LOCK_RESET: this->__cmdLockReset(*cmd); break;
        }

        delete cmd;
      }

      if (runFlag) {
        this->__eventCtx.handle(this->__tickStart);
      }

      if (this->__lockCtx.state == LockContext::NONE) {
        this->__eventCtx.addEvent(this->__tickStart + this->__randomAcquireDelay(), [this]() {
          this->__acquireLock();
        });
      }

    } while (runFlag);

    // 이벤트 비우기.
    this->__eventCtx.clear();
  }

  Command *__makeMyCommand (const OPCode op_code, const ContextID to) {
    auto ret = new Command;

    ret->op_code = op_code;
    ret->context_from = this->__id;
    ret->context_to = to;

    return ret;
  }

  void __cmdThreadSpawned (const Command &cmd) {
    this->__others.insert(cmd.context_from);
  }

  void __cmdThreadDespawned (const Command &cmd) {
    this->__others.erase(cmd.context_from);
    this->__lockCtx.sentMyLock.erase(cmd.context_from);
    this->__lockCtx.yourLockToRcv.erase(cmd.context_from);
    this->__lockCtx.rcvMyLock.erase(cmd.context_from);

    // 락에 대한 예외처리: "My Lock" 명령을 보낸 애가 사라짐.
    switch (this->__lockCtx.state) {
      case LockContext::LURKING:
        if (this->__lockCtx.rcvMyLock.empty()) {
          this->__lockCtx.state = LockContext::SOLICITING;
          this->__solicitLock();
        }
        break;
      case LockContext::SOLICITING:
        if (this->__lockCtx.yourLockToRcv.empty()) {
          this->__lockCtx.state = LockContext::ACQUIRED;
          this->__onLockAcquired();
        }
        break;
    }
  }

  void __cmdMyLock (const Command &cmd) {
    this->__lockCtx.rcvMyLock.insert(cmd.context_from);

    switch (this->__lockCtx.state) {
      // 락을 얻으려하지 않는 상태일 때.
      case LockContext::NONE:
      case LockContext::LURKING:
        // 락을 그냥 준다.
        ::sendCommand(this->__makeMyCommand(OPC_YOUR_LOCK, cmd.context_from));
        break;
      case LockContext::SOLICITING: // 내가 락을 얻고 싶은 상태일 떄.
        if (cmd.context_from > this->__id) { // 나보다 높은 놈이 락을 원함.
          // 락을 준다.
          ::sendCommand(this->__makeMyCommand(OPC_YOUR_LOCK, cmd.context_from));
        }
        break;
    }
  }

  void __cmdYourLock (const Command &cmd) {
    if (this->__lockCtx.state == LockContext::SOLICITING) {
      this->__lockCtx.yourLockToRcv.erase(cmd.context_from);
      if (this->__lockCtx.yourLockToRcv.empty()) {
        this->__lockCtx.state = LockContext::ACQUIRED;
        this->__onLockAcquired();
      }
    }
    else { // WHAT??
      // 내가 보낸 "LockReset" 명령이 이 end에 전달이 안 된 상태에서 보낸 것일 수 있음.
      // 일단 보고하기.
      std::stringstream ss;

      ss
        << "* Rogue 'YourLock' command received from context "
        << cmd.context_from << " by " << this->__id << '.';
      __REPORT(ss.str());
    }
  }

  void __cmdLockReset (const Command &cmd) {
    this->__lockCtx.rcvMyLock.erase(cmd.context_from);

    if (this->__lockCtx.state == LockContext::LURKING &&
      this->__lockCtx.rcvMyLock.empty()) {
      // 엿듣던 중 - 아무도 락을 걸려 하지 않음.
      // 내가 락을 얻을 차례.
      this->__lockCtx.state = LockContext::SOLICITING;
      this->__solicitLock();
    }
  }

  void __solicitLock () {
    this->__lockCtx.yourLockToRcv.clear();

    for (const auto &other : this->__others) {
      ::sendCommand(this->__makeMyCommand(OPC_MY_LOCK, other));
      this->__lockCtx.sentMyLock.insert(other);
      this->__lockCtx.yourLockToRcv.insert(other);
    }
  }

  void __acquireLock () {
    if (this->__lockCtx.state == LockContext::NONE) {
      if (this->__others.empty()) {
        // 혼자 있음. 바로 락을 얻은 것으로 처리.
        this->__lockCtx.state = LockContext::ACQUIRED;
      }
      else {
        if (this->__lockCtx.rcvMyLock.empty()) {
          // 아무도 락을 얻으려 하지 않음.
          this->__lockCtx.state = LockContext::SOLICITING;
          this->__solicitLock();
        }
        else {
          // 이미 누군가 락을 얻으려 하고 있음.
          // 다 끝날 때까지 기다림.
          this->__lockCtx.state = LockContext::LURKING;
        }
      }
    }
  }

  void __releaseLock () {
    switch (this->__lockCtx.state) { // 이미 뭔가를 보냈을 때.
      case LockContext::ACQUIRED:
        // 락을 주지 않은 다른 곳에 이제 줌.
        for (const auto &other : this->__lockCtx.rcvMyLock) {
          ::sendCommand(this->__makeMyCommand(OPC_YOUR_LOCK, other));
        }
        /* fall through */
      case LockContext::SOLICITING:
        // 다른 이에게 내가 락을 풀었다는 것을 통보.
        for (const auto &other : this->__lockCtx.sentMyLock) {
          ::sendCommand(this->__makeMyCommand(OPC_LOCK_RESET, other));
        }
        this->__lockCtx.sentMyLock.clear();
        break;
    }

    this->__lockCtx.state = LockContext::NONE;
  }

  void __onLockAcquired () {
    // 처리 지연을 시뮬레이션한 뒤 락을 해제함.
    const auto releaseDelay = this->__rnd() % 100;
    std::function<void()> event;
    bool myResource;

    myResource = ::resourceLock.try_lock();
    if (!myResource) {
      __REPORT("* Race state detected - lock not acquired with resource.");
    }

    event = [this, myResource]() {
      if (myResource) {
        ::resourceLock.unlock();
      }
      this->__releaseLock();
    };
    this->__eventCtx.addEvent(this->__tickStart + std::chrono::milliseconds(releaseDelay), event);
  }
};

#endif /* end of include guard: THREADCONTEXT_H_ */
