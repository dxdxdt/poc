#ifndef EVENTCONTEXT_H_
#define EVENTCONTEXT_H_
#include <cstdint>

#include <chrono>
#include <map>
#include <set>
#include <list>
#include <vector>
#include <functional>

class EventContext {
public:
  typedef std::chrono::steady_clock ClockType;
  typedef std::function<void()> FuncType;
  typedef uint_fast32_t EventID;

protected:
  enum __Type {
    T_IMMEDIATE,
    T_DELAYED
  };
  struct __Event {
    bool hasID;
    __Type type;
    FuncType func;
    std::list<__Event>::iterator posInList;
    std::multimap<ClockType::time_point, __Event*>::iterator posInSlot;
  };

  ClockType::time_point __now = ClockType::now();
  std::list<__Event> __list;
  std::set<__Event*> __immediate;
  std::multimap<ClockType::time_point, __Event*> __slot;
  std::map<EventID, __Event*> __idEventMap;
  std::map<__Event*, EventID> __eventIDMap;

  __Event *__allocEvent () {
    std::list<__Event>::iterator ret;

    this->__list.resize(this->__list.size() + 1);
    ret = std::prev(this->__list.end());
    ret->posInList = ret;

    return &*ret;
  }

  void __freeEvent (__Event *e) {
    switch (e->type) {
      case T_IMMEDIATE: this->__immediate.erase(e); break;
      case T_DELAYED: this->__slot.erase(e->posInSlot); break;
    }

    if (e->hasID) {
        auto it = this->__eventIDMap.find(e);

        if (this->__eventIDMap.end() != it) {
          this->__idEventMap.erase(it->second);
          this->__eventIDMap.erase(it);
        }
    }

    this->__list.erase(e->posInList);
  }

public:
  void setTime (const ClockType::time_point *tp = nullptr) {
    if (tp != nullptr) {
      this->__now = *tp;
    }
    else {
      this->__now = ClockType::now();
    }
  }

  bool hasEvent () {
    return (!this->__slot.empty()) || (!this->__immediate.empty());
  }

  bool hasPendingEvent () {
    if (!this->__immediate.empty()) {
      return true;
    }
    if (this->__slot.empty()) {
      return false;
    }

    return this->__slot.cbegin()->first <= this->__now;
  }

  ClockType::duration timeToNextEvent () {
    ClockType::duration ret;

    if (this->__immediate.empty()) {
      ret = this->__slot.cbegin()->first - this->__now;
    }
    else {
      ret = ClockType::duration(0);
    }

    return ret;
  }

  void cancelEvent (const EventID id) {
    auto it = this->__idEventMap.find(id);

    if (this->__idEventMap.end() != it) {
      this->__freeEvent(it->second);
    }
  }

  void addDelayedEvent (const ClockType::duration &delay, const FuncType &func, const EventID *id = nullptr) {
    auto e = this->__allocEvent();

    if (id != nullptr) {
      this->cancelEvent(*id);
      this->__idEventMap[*id] = e;
      this->__eventIDMap[e] = *id;

      e->hasID = true;
    }
    else {
      e->hasID = false;
    }

    e->type = T_DELAYED;
    e->func = func;
    e->posInSlot = this->__slot.insert(std::make_pair(this->__now + delay, e));
  }

  void addImmediateEvent (const FuncType &func, const EventID *id = nullptr) {
    auto e = this->__allocEvent();

    if (id != nullptr) {
      this->cancelEvent(*id);
      this->__idEventMap[*id] = e;
      this->__eventIDMap[e] = *id;

      e->hasID = true;
    }
    else {
      e->hasID = false;
    }

    e->type = T_IMMEDIATE;
    e->func = func;
    this->__immediate.insert(e);
  }

  void clear () {
    this->__list.clear();
    this->__immediate.clear();
    this->__slot.clear();
    this->__idEventMap.clear();
    this->__eventIDMap.clear();
  }

  void handle () {
    std::vector<__Event*> fired;

    for (const auto &v : this->__immediate) {
      v->func();
      fired.push_back(v);
    }

    for (const auto &p : this->__slot) {
      if (p.first > this->__now) {
        break;
      }

      p.second->func();
      fired.push_back(p.second);
    }

    for (const auto &v : fired) {
      this->__freeEvent(v);
    }
  }
};

#endif /* end of include guard: DELAYEDEVENTCONTEXT_H_ */
