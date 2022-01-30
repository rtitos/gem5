/*
  Copyright (C) 2016-2021 Rubén Titos <rtitos@um.es>
  Universidad de Murcia

  GPLv2, see file LICENSE.
*/

#ifndef __MEM_RUBY_HTM_HTM_HH__
#define __MEM_RUBY_HTM_HTM_HH__

#include <map>
#include <string>

#include "mem/htm.hh"
#include "params/RubyHTM.hh"

namespace gem5
{

namespace ruby
{

class RubyHTM : public HTM
{
  public:
    PARAMS(RubyHTM);
    RubyHTM(const Params &p);
    void notifyPseudoInst() override;
    void notifyPseudoInstWork(bool begin, int cpuId, uint64_t workid) override;
    bool setupLog(int cpuId, Addr addr) override;
    void endLogUnroll(int cpuId) override;
    int getLogNumEntries(int cpuId) override;

};

} // namespace ruby
} // namespace gem5

#endif // __MEM_RUBY_HTM_HTM_HH__
