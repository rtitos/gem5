/*
 * Copyright (C) 2016-2021 Rubén Titos <rtitos@um.es>
 * Universidad de Murcia
 *
 * Copyright (c) 2020 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/htm.hh"
#include "mem/ruby/profiler/Profiler.hh"
#include "mem/ruby/profiler/XactProfiler.hh"
#include "mem/ruby/system/RubySystem.hh"

namespace gem5
{

const std::string HtmPolicyStrings::requester_wins = "requester_wins";
const std::string HtmPolicyStrings::committer_wins = "committer_wins";
const std::string HtmPolicyStrings::requester_stalls = "requester_stalls";
const std::string HtmPolicyStrings::magic = "magic";
const std::string HtmPolicyStrings::token = "token";
const std::string HtmPolicyStrings::requester_stalls_cda_base =
                                   "requester_stalls_cda_base";
const std::string HtmPolicyStrings::requester_stalls_cda_base_ntx =
                                   "requester_stalls_cda_base_ntx";
const std::string HtmPolicyStrings::requester_stalls_cda_hybrid =
                                   "requester_stalls_cda_hybrid";
const std::string HtmPolicyStrings::requester_stalls_cda_hybrid_ntx =
                                   "requester_stalls_cda_hybrid_ntx";

HtmFailureFaultCause
getIsaVisibleHtmFailureCause(HtmFailureFaultCause cause)
{
    HtmFailureFaultCause isaVisibleCause = cause;
    switch(cause) {
    case HtmFailureFaultCause::EXPLICIT:
    case HtmFailureFaultCause::NEST:
    case HtmFailureFaultCause::SIZE:
    case HtmFailureFaultCause::EXCEPTION:
    case HtmFailureFaultCause::MEMORY:
    case HtmFailureFaultCause::OTHER:
    case HtmFailureFaultCause::DISABLED:
        break;
    case HtmFailureFaultCause::INTERRUPT:
        isaVisibleCause = HtmFailureFaultCause::OTHER;
        break;
    case HtmFailureFaultCause::LSQ:
    case HtmFailureFaultCause::EXPLICIT_FALLBACKLOCK:
    case HtmFailureFaultCause::MEMORY_FALLBACKLOCK:
    case HtmFailureFaultCause::MEMORY_STALEDATA:
    case HtmFailureFaultCause::MEMORY_FALSESHARING:
        isaVisibleCause = HtmFailureFaultCause::MEMORY;
        break;
    case HtmFailureFaultCause::SIZE_RSET:
    case HtmFailureFaultCause::SIZE_WSET:
    case HtmFailureFaultCause::SIZE_L1PRIV:
    case HtmFailureFaultCause::SIZE_LLC:
        isaVisibleCause = HtmFailureFaultCause::SIZE;
        break;
    case HtmFailureFaultCause::SIZE_WRONG_CACHE:
        isaVisibleCause = HtmFailureFaultCause::OTHER;
        break;
    default:
        panic("Unexpected HtmFailureFault cause %s\n",
              htmFailureToStr(cause));
    }
    return isaVisibleCause;
};


std::string
htmFailureToStr(HtmFailureFaultCause cause)
{
    static const std::map<HtmFailureFaultCause, std::string> cause_to_str = {
        { HtmFailureFaultCause::EXPLICIT, "explicit" },
        { HtmFailureFaultCause::NEST, "nesting_limit" },
        { HtmFailureFaultCause::SIZE, "transaction_size" },
        { HtmFailureFaultCause::EXCEPTION, "exception" },
        { HtmFailureFaultCause::INTERRUPT, "interrupt" },
        { HtmFailureFaultCause::DISABLED, "htm_disabled" },
        { HtmFailureFaultCause::MEMORY, "memory_conflict" },
        { HtmFailureFaultCause::LSQ, "lsq_conflict" },
        { HtmFailureFaultCause::SIZE_RSET, "transaction_size_rset" },
        { HtmFailureFaultCause::SIZE_WSET, "transaction_size_wset" },
        { HtmFailureFaultCause::SIZE_LLC, "transaction_size_llc" },
        { HtmFailureFaultCause::SIZE_L1PRIV, "transaction_size_l1priv" },
        { HtmFailureFaultCause::SIZE_WRONG_CACHE, "transaction_size_wrongcache" },
        { HtmFailureFaultCause::EXPLICIT_FALLBACKLOCK, "explicit_fallbacklock" },
        { HtmFailureFaultCause::MEMORY_FALLBACKLOCK,
          "memory_conflict_fallbacklock" },
        { HtmFailureFaultCause::MEMORY_STALEDATA,
          "memory_conflict_staledata" },
        { HtmFailureFaultCause::MEMORY_FALSESHARING,
          "memory_conflict_falsesharing" },
        { HtmFailureFaultCause::OTHER, "other" }
    };

    auto it = cause_to_str.find(cause);
    return it == cause_to_str.end() ? "Unrecognized Failure" : it->second;
}

std::string
htmFailureToStr(HtmCacheFailure rc)
{
    static const std::map<HtmCacheFailure, std::string> rc_to_str = {
        { HtmCacheFailure::NO_FAIL, "NO_FAIL" },
        { HtmCacheFailure::FAIL_SELF, "FAIL_SELF" },
        { HtmCacheFailure::FAIL_REMOTE, "FAIL_REMOTE" },
        { HtmCacheFailure::FAIL_OTHER, "FAIL_OTHER" }
    };

    auto it = rc_to_str.find(rc);
    return it == rc_to_str.end() ? "Unrecognized Failure" : it->second;
}


HTM::HTM(const Params &p)
    : ClockedObject(p),
      _params(p),
      m_fallbackLockPhysicalAddress(0),
      m_fallbackLockVirtualAddress(0)
{
    if (p.fallbacklock_addr != "") {
        try {
            m_fallbackLockVirtualAddress =
                std::stol(p.fallbacklock_addr, NULL, 0);
        }
        catch (const std::invalid_argument& ia) {
            panic("Illegal fallback lock address %s\n",
                  p.fallbacklock_addr);
            m_fallbackLockVirtualAddress = 0;
        }
    } else {
        warn("Fallback lock address not specified!\n");
    }
}

void
HTM::requestCommitToken(int proc_no)
{
    for (int i = 0 ; i < m_commitTokenRequestList.size(); i++)
        assert(m_commitTokenRequestList[i] != proc_no);

    m_commitTokenRequestList.push_back(proc_no);
}

void HTM::releaseCommitToken(int proc_no)
{
    assert(m_commitTokenRequestList.size() > 0);
    assert(proc_no == m_commitTokenRequestList[0]);
    m_commitTokenRequestList.erase(m_commitTokenRequestList.begin());
}

void HTM::removeCommitTokenRequest(int proc_no)
{
  bool found = false;

  if (getTokenOwner() == proc_no){
    releaseCommitToken(proc_no);
    return;
  }

  for (std::vector<int>::iterator it = m_commitTokenRequestList.begin() ;
       it != m_commitTokenRequestList.end() && !found; ++it) {
    if (*it == proc_no){
      m_commitTokenRequestList.erase(it);
      found = true;
    }
  }
  assert(found);
}

bool HTM::existCommitTokenRequest(int proc_no)
{
  bool found = false;
  int  curr_size = m_commitTokenRequestList.size();
  for (int i = 0; (i < curr_size) && !found; i++){
    if (m_commitTokenRequestList[i] == proc_no){
      found = true;
    }
  }
  return found;
}

int HTM::getTokenOwner()
{
  int owner = -1;
  if (m_commitTokenRequestList.size() > 0)
   owner = m_commitTokenRequestList[0];

  return owner;
}

int HTM::getNumTokenRequests()
{
 return m_commitTokenRequestList.size();
}

} // namespace gem5
