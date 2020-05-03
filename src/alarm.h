#ifndef ALARM_H_
#define ALARM_H_

#include <ebbrt/Timer.h>

class Alarm : public ebbrt::Timer::Hook {
public:
  Alarm(int v) {
    var = v;
  }

  void enable_timer();
  void disable_timer();
  void Fire() override;
  
private:
  int var;
};
#endif
