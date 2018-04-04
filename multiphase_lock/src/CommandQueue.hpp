#ifndef COMMANDQUEUE_H_
#define COMMANDQUEUE_H_
#include "Globals.hpp"

#include <mutex>
#include <condition_variable>
#include <queue>

struct CommandQueue {
  std::mutex mtx;
  std::condition_variable cv_despatch;
  std::queue<Command*> q;

  ~CommandQueue () {
    this->clear();
  }

  void clear () {
    while (!q.empty()) {
      delete q.front();
      q.pop();
    }
  }
};

#endif /* end of include guard: COMMANDQUEUE_H_ */
