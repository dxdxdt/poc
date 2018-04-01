#ifndef THREADCONTEXT_H_
#define THREADCONTEXT_H_
#include "CommandQueue.hpp"
#include "EventContext.hpp"
#include "Globals.hpp"
#include "LockContext.hpp"

#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <thread>

#define __REPORT(msg) this->__report(__FILE__, __LINE__, msg)

class ThreadContext {
protected:
  static const EventContext::EventID __STARVATION_EVENT__ = 1;

  ContextID __id = 0;
  size_t __maxCmdQueueSize = 10;
  uint64_t __acquiredCount = 0;

  std::thread __th;
  std::set<ContextID> __others;
  CommandQueue __cmdQueue;
  LockContext __lockCtx;
  EventContext __eventCtx;
  std::mt19937_64 __rnd;

public:
  ThreadContext() {}

  ~ThreadContext() { this->stop(); }

  ContextID id() { return this->__id; }

  uint64_t acquiredCount() { return this->__acquiredCount; }

  void start(const ContextID id) {
    if (this->__th.joinable()) {
      throw std::exception();
    }
    if ((this->__id = id) == 0) {
      throw std::exception();
    }

    this->__th = std::thread([this]() { this->__run(); });
  }

  void stop() {
    if (this->__th.joinable()) {
      auto cmd = new Command;

      cmd->op_code = OPC_SHUTDOWN;
      cmd->context_from = 0;
      cmd->context_to = this->__id;
      this->pushCommand(cmd);

      this->__th.join();
    }
  }

  void pushCommand(Command *cmd) {
    std::unique_lock<std::mutex> ul(this->__cmdQueue.mtx);

    this->__cmdQueue.q.push(cmd);
    this->__cmdQueue.cv_despatch.notify_all();
  }

protected:
  void __report(const char *file, const uint32_t line, const std::string msg) {
    std::lock_guard<std::mutex> lg(::stdioLock);
    std::cerr << msg << " (" << file << ':' << line << ')' << std::endl;
  }

  std::chrono::milliseconds __randomAcquireDelay() {
    return std::chrono::milliseconds(this->__rnd() % (::maxAcquireDelay + 1));
  }

  std::chrono::milliseconds __randomLockHoldTime() {
    return std::chrono::milliseconds(this->__rnd() % (::maxLockHoldTime + 1));
  }

  void __run() {
    Command *cmd;
    bool runFlag;

    runFlag = true;
    // 랜덤 엔진 초기화
    {
      std::hash<std::thread::id> hasher;
      const auto now =
          (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
      const auto tid = (uint64_t)hasher(std::this_thread::get_id());

      try {
        std::random_device rnd;
        std::seed_seq seq = {(uint32_t)((0xFFFFFFFF00000000 & now) >> 32),
                             (uint32_t)((0x00000000FFFFFFFF & now)),
                             (uint32_t)((0xFFFFFFFF00000000 & tid) >> 32),
                             (uint32_t)((0x00000000FFFFFFFF & tid)),
                             (uint32_t)rnd()};

        this->__rnd.seed(seq);
      } catch (std::exception &) {
        std::seed_seq seq = {(uint32_t)((0xFFFFFFFF00000000 & now) >> 32),
                             (uint32_t)((0x00000000FFFFFFFF & now)),
                             (uint32_t)((0xFFFFFFFF00000000 & tid) >> 32),
                             (uint32_t)((0x00000000FFFFFFFF & tid))};

        this->__rnd.seed(seq);
      }
    }

    // 내가 태어났다는 것을 방송.
    cmd = new Command;
    cmd->op_code = OPC_THREAD_SPAWNED;
    cmd->context_from = this->__id;
    cmd->context_to = 0;
    ::sendCommand(cmd);

    // 조금 기다렸다가 락 걸기 시도
    this->__eventCtx.clear();
    this->__eventCtx.setTime();
    this->__eventCtx.addDelayedEvent(std::chrono::milliseconds(100),
                                     [this]() { this->__acquireLock(); });

    do {
      {
        std::unique_lock<std::mutex> ul(this->__cmdQueue.mtx);

        this->__eventCtx.setTime();

        while (this->__cmdQueue.q.empty() &&
               (!this->__eventCtx.hasPendingEvent())) {
          if (this->__eventCtx.hasEvent()) {
            this->__cmdQueue.cv_despatch.wait_for(
                ul, this->__eventCtx.timeToNextEvent());
          } else {
            this->__cmdQueue.cv_despatch.wait(ul);
          }

          this->__eventCtx.setTime();
        }

        if (this->__cmdQueue.q.empty()) {
          cmd = nullptr;
        } else {
          cmd = this->__cmdQueue.q.front();
          this->__cmdQueue.q.pop();
        }
      }

      if (cmd != nullptr) {
        switch (cmd->op_code) {
        case OPC_SHUTDOWN:
          runFlag = false;
          break;
        case OPC_THREAD_SPAWNED:
          this->__cmdThreadSpawned(*cmd);
          break;
        case OPC_THREAD_DESPAWNED:
          this->__cmdThreadDespawned(*cmd);
          break;
        case OPC_MY_LOCK:
          this->__cmdMyLock(*cmd);
          break;
        case OPC_YOUR_LOCK:
          this->__cmdYourLock(*cmd);
          break;
        case OPC_LOCK_RESET:
          this->__cmdLockReset(*cmd);
          break;
        }

        delete cmd;
      }

      if (runFlag) {
        this->__eventCtx.handle();
      }
    } while (runFlag);

    // 내가 죽는다는 것을 방송.
    cmd = new Command;
    cmd->op_code = OPC_THREAD_DESPAWNED;
    cmd->context_from = this->__id;
    cmd->context_to = 0;
    ::sendCommand(cmd);

    // 이벤트 비우기.
    this->__eventCtx.clear();
  }

  Command *__makeMyCommand(const OPCode op_code, const ContextID to) {
    auto ret = new Command;

    ret->op_code = op_code;
    ret->context_from = this->__id;
    ret->context_to = to;

    return ret;
  }

  void __cmdThreadSpawned(const Command &cmd) {
    this->__others.insert(cmd.context_from);
  }

  void __cmdThreadDespawned(const Command &cmd) {
    this->__others.erase(cmd.context_from);

    this->__lockCtx.sentMyLock.erase(cmd.context_from);
    this->__lockCtx.yourLockToRcv.erase(cmd.context_from);
    this->__lockCtx.yourLockToSend.erase(cmd.context_from);
    this->__lockCtx.rcvMyLock.erase(cmd.context_from);

    // 락에 대한 예외처리.
    switch (this->__lockCtx.state) {
    case LockContext::LURKING:
      if (this->__others.empty()) {
        // 혼자 남음. 바로 락을 얻은 것으로 처리.
        this->__lockCtx.state = LockContext::ACQUIRED;
        this->__onLockAcquired();
      } else if (this->__lockCtx.rcvMyLock.empty()) {
        this->__lockCtx.state = LockContext::SOLICITING;
        this->__solicitLock();
      }
      break;
    case LockContext::SOLICITING:
      if (this->__lockCtx.yourLockToRcv.empty()) {
        // 내가 "MyLock" 명령을 보냈던 곳이 사라짐.
        this->__lockCtx.state = LockContext::ACQUIRED;
        this->__onLockAcquired();
      }
      break;
    }
  }

  void __cmdMyLock(const Command &cmd) {
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
      } else { // 나보다 낮은 놈이 락을 원함.
        // 락을 풀때 준다.
        this->__lockCtx.yourLockToSend.insert(cmd.context_from);
      }
      break;
    }
  }

  void __cmdYourLock(const Command &cmd) {
    if (this->__lockCtx.state == LockContext::SOLICITING) {
      this->__lockCtx.yourLockToRcv.erase(cmd.context_from);
      if (this->__lockCtx.yourLockToRcv.empty()) {
        this->__lockCtx.state = LockContext::ACQUIRED;
        this->__onLockAcquired();
      }
    } else { // WHAT??
      // 내가 보낸 "LockReset" 명령이 이 end에 전달이 안 된 상태에서 보낸 것일
      // 수 있음. 일단 보고하기.
      std::stringstream ss;

      ss << "* Rogue 'YourLock' command received from context "
         << cmd.context_from << " by " << this->__id << '.';
      __REPORT(ss.str());
    }
  }

  void __cmdLockReset(const Command &cmd) {
    this->__lockCtx.rcvMyLock.erase(cmd.context_from);
    this->__lockCtx.yourLockToSend.erase(cmd.context_from);

    if (this->__lockCtx.state == LockContext::LURKING &&
        this->__lockCtx.rcvMyLock.empty()) {
      // 엿듣던 중 - 아무도 락을 걸려 하지 않음.
      // 내가 락을 얻을 차례.
      this->__lockCtx.state = LockContext::SOLICITING;
      this->__solicitLock();
    }
  }

  void __solicitLock() {
    this->__lockCtx.yourLockToRcv.clear();

    for (const auto &other : this->__others) {
      ::sendCommand(this->__makeMyCommand(OPC_MY_LOCK, other));
      this->__lockCtx.sentMyLock.insert(other);
      this->__lockCtx.yourLockToRcv.insert(other);
    }
  }

  void __acquireLock() {
    if (this->__lockCtx.state == LockContext::NONE) {
      uint32_t starveTimeout;

      if (this->__others.empty()) {
        // 혼자 있음. 바로 락을 얻은 것으로 처리.
        this->__lockCtx.state = LockContext::ACQUIRED;
        this->__onLockAcquired();
      } else {
        if (this->__lockCtx.rcvMyLock.empty()) {
          // 아무도 락을 얻으려 하지 않음.
          this->__lockCtx.state = LockContext::SOLICITING;
          this->__solicitLock();
        } else {
          // 이미 누군가 락을 얻으려 하고 있음.
          // 다 끝날 때까지 기다림.
          this->__lockCtx.state = LockContext::LURKING;
        }
      }

      if (::maxLockHoldTime == 0) {
        starveTimeout = 1000;
      }
      else {
        starveTimeout = ::maxLockHoldTime * (uint32_t)::threads.size() * 10;
      }

      this->__eventCtx.addDelayedEvent(std::chrono::milliseconds(starveTimeout), []() {
        std::lock_guard<std::mutex> lg(::stdioLock);

        std::cerr << "*** Starvation detected!" << std::endl;
        ::abort();
      }, __STARVATION_EVENT__);
    }
  }

  void __releaseLock() {
    switch (this->__lockCtx.state) { // 이미 뭔가를 보냈을 때.
    case LockContext::ACQUIRED:
      // 락을 주지 않은 다른 곳에 이제 줌.
      for (const auto &other : this->__lockCtx.yourLockToSend) {
        ::sendCommand(this->__makeMyCommand(OPC_YOUR_LOCK, other));
      }
      this->__lockCtx.yourLockToSend.clear();
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

  void __onLockAcquired() {
    std::stringstream ss;
    uint32_t rsrc;

    this->__eventCtx.cancelEvent(__STARVATION_EVENT__);
    this->__acquiredCount += 1;

    rsrc = ::resource.fetch_add(1);
    if (rsrc != 0) {
      ss << "* Race state detected(" << rsrc << ") by thread " << this->__id;
      __REPORT(ss.str());
    }

    // 처리 지연을 시뮬레이션한 뒤 락을 해제함.
    this->__eventCtx.addDelayedEvent(this->__randomLockHoldTime(), [this]() {
      ::resource -= 1;
      this->__releaseLock();
      this->__eventCtx.addDelayedEvent(this->__randomAcquireDelay(),
                                       [this]() { this->__acquireLock(); });
    });
  }
};

#endif /* end of include guard: THREADCONTEXT_H_ */
