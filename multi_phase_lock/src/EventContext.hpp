#ifndef EVENTCONTEXT_H_
#define EVENTCONTEXT_H_
#include <chrono>
#include <map>
#include <set>
#include <list>
#include <functional>

class EventContext {
public:
  typedef std::chrono::steady_clock ClockType;
  typedef std::function<void()> FuncType;

protected:
  std::list<FuncType> __immediate;
  std::multimap<ClockType::time_point, FuncType> __slot;

public:
  bool hasEvent () {
    return (!this->__slot.empty()) || (!this->__immediate.empty());
  }

  bool hasPendingEvent (ClockType::time_point tp) {
    if (!this->__immediate.empty()) {
      return true;
    }
    if (this->__slot.empty()) {
      return false;
    }

    return this->__slot.cbegin()->first <= tp;
  }

  ClockType::duration nextEvent (ClockType::time_point tp) {
    ClockType::duration ret;

    if (this->__immediate.empty()) {
      ret = this->__slot.cbegin()->first - tp;
    }
    else {
      ret = ClockType::duration(0);
    }

    return ret;
  }

  void addEvent (ClockType::time_point tp, FuncType func) {
    this->__slot.insert(std::make_pair(tp, func));
  }

  void addImmediateEvent (FuncType func) {
    this->__immediate.push_back(func);
  }

  void handle (ClockType::time_point tp) {
    std::set<ClockType::time_point> toErase;

    for (const auto &v : this->__immediate) {
      v();
    }
    this->__immediate.clear();

    for (const auto &p : this->__slot) {
      if (p.first > tp) {
        break;
      }

      toErase.insert(p.first);
      p.second();
    }

    for (const auto &v : toErase) {
      this->__slot.erase(v);
    }
  }

  void clear () {
    this->__slot.clear();
    this->__immediate.clear();
  }
};

#endif /* end of include guard: DELAYEDEVENTCONTEXT_H_ */
