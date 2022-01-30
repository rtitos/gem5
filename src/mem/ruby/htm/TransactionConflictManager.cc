/*
  Copyright (C) 2016-2021 Rubén Titos <rtitos@um.es>
  Universidad de Murcia

  GPLv2, see file LICENSE.
*/

#include "mem/ruby/htm/TransactionConflictManager.hh"

#include <cassert>
#include <cstdlib>

#include "debug/RubyHTM.hh"
#include "mem/ruby/htm/LazyTransactionCommitArbiter.hh"
#include "mem/ruby/htm/TransactionInterfaceManager.hh"
#include "mem/ruby/htm/TransactionIsolationManager.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "mem/ruby/system/Sequencer.hh"

namespace gem5
{
namespace ruby
{


#define XACT_CONFLICT_RES m_policy

TransactionConflictManager::
TransactionConflictManager(TransactionInterfaceManager *xact_mgr,
                           int version) {
  m_version = version;
  m_xact_mgr = xact_mgr;

  // The cycle at which Ruby simulation starts is substracted from
  // current time to generate timestamps in order to avoid overflowing
  // the Timestamp field (int) in SLICC protocol messages (SLICC
  // doesn't support long types). Must set it only once at the
  // beginning, as RubySystem::resetStats updates the start cycle

  m_timestamp      = Cycles(0);
  m_possible_cycle = false;
  m_lock_timestamp = false;
  m_numRetries     = 0;
  m_sentNack         = false;
  m_receivedNack         = false;
  m_doomed         = false;

  m_policy = xact_mgr->config_conflictResPolicy();
  if (m_policy == HtmPolicyStrings::requester_stalls_cda_base ||
      m_policy == HtmPolicyStrings::requester_stalls_cda_hybrid ||
      m_policy == HtmPolicyStrings::requester_stalls_cda_hybrid_ntx ||
      m_policy == HtmPolicyStrings::requester_stalls_cda_base_ntx) {
      m_policy_is_req_stalls_cda = true;
  } else {
      m_policy_is_req_stalls_cda = false;
  }
  if (m_policy == HtmPolicyStrings::requester_stalls_cda_base_ntx ||
      m_policy == HtmPolicyStrings::requester_stalls_cda_hybrid_ntx) {
      m_policy_nack_non_transactional = true;
  } else {
      m_policy_nack_non_transactional = false;
  }
}

TransactionConflictManager::~TransactionConflictManager() {
}

void
TransactionConflictManager::setVersion(int version) {
  m_version = version;
}

int
TransactionConflictManager::getVersion() const {
  return m_version;
}

int
TransactionConflictManager::getProcID() const{
  return m_xact_mgr->getProcID();
}

void
TransactionConflictManager::beginTransaction(){
  int transactionLevel = m_xact_mgr->getTransactionLevel();
  assert(transactionLevel >= 1);

  if ((transactionLevel == 1) && !(m_lock_timestamp)){
    string conflict_res_policy(XACT_CONFLICT_RES);
    m_timestamp = m_xact_mgr->curCycle();
    m_lock_timestamp = true;
  }
}

void
TransactionConflictManager::commitTransaction(){
  int transactionLevel = m_xact_mgr->getTransactionLevel();
  assert(transactionLevel >= 1);
  assert(!m_doomed);

  if (transactionLevel == 1){
    m_lock_timestamp = false;
    m_numRetries     = 0;
    m_receivedNack         = false;
    clearPossibleCycle();

  }
}

void
TransactionConflictManager::restartTransaction(){
  m_numRetries++;
  clearPossibleCycle();
  m_sentNack         = false;
  m_receivedNack = false;
  m_doomed = false;
}

int
TransactionConflictManager::getNumRetries(){
  return m_numRetries;
}

bool
TransactionConflictManager::possibleCycle(){
  return m_possible_cycle;
}

void
TransactionConflictManager::setPossibleCycle(){
  m_possible_cycle = true;
}

void
TransactionConflictManager::clearPossibleCycle(){
  m_possible_cycle = false;
}

bool
TransactionConflictManager::nackReceived(){
  return m_receivedNack;
}

bool
TransactionConflictManager::doomed(){
  return m_doomed;
}

void
TransactionConflictManager::setDoomed(){
    m_doomed = true;
}

Cycles
TransactionConflictManager::getTimestamp(){
  if (m_xact_mgr->getTransactionLevel() > 0)
    return m_timestamp;
  else {
      Cycles ts = m_xact_mgr->curCycle();
      assert(ts > 0); // Detect overflows
      return ts;
  }
}

bool
TransactionConflictManager::isRequesterStallsPolicy(){
    return m_policy_is_req_stalls_cda;
}

Cycles
TransactionConflictManager::getOldestTimestamp(){
  Cycles currentTime = m_xact_mgr->curCycle();
  Cycles oldestTime = currentTime;

  if ((m_xact_mgr->getTransactionLevel() > 0) &&
      (m_timestamp < oldestTime)) {
      oldestTime = m_timestamp;
  }
  assert(oldestTime > 0);
  return oldestTime;
}

bool
TransactionConflictManager::isRemoteOlder(Cycles local_timestamp,
                                          Cycles remote_timestamp,
                                          MachineID remote_id){

  bool older = false;

  if (local_timestamp == remote_timestamp){
      assert(getProcID() != (int) machineIDToNodeID(remote_id));
      older = (int) machineIDToNodeID(remote_id) < getProcID();
  } else {
    older = (remote_timestamp < local_timestamp);
  }
  return older;
}

bool
TransactionConflictManager::shouldNackLoad(Addr addr,
                                           MachineID remote_id,
                                           Cycles remote_timestamp,
                                           bool remote_trans)
{
  string conflict_res_policy(XACT_CONFLICT_RES);

  bool shouldNack; // Leave uninitialize so that compiler warns us if
                   // we ever miss a case
  bool remoteNonTransWins = false;
  bool existConflict = m_xact_mgr->
    getXactIsolationManager()->isInWriteSetPerfectFilter(addr);
  if (existConflict) {
      assert(machineIDToMachineType(remote_id) == MachineType_L1Cache);
      assert(remote_timestamp > 0);
      DPRINTF(RubyHTM, "Conflict detected by shouldNackLoad,"
              " requestor=%d addr %#lx (%s)\n",
              machineIDToNodeID(remote_id), addr,
              remote_trans ? "trans" : "non-trans");
      if (m_xact_mgr->isUnrollingLog()) {
          assert(!m_xact_mgr->config_lazyVM()); // LogTM
          shouldNack = true;
#if 0 // TODO: Check
      } else if (m_xact_mgr->isDoomed()) {
          shouldNack = false;
#endif
      } else if (m_policy_is_req_stalls_cda) {
          if (!remote_trans &&
              !m_policy_nack_non_transactional) {
              if (!m_xact_mgr->config_lazyVM()) { // LogTM
                  shouldNack = true; // Nack until old value restored
                  // Abort but keep nacking until old value restored
                  // from log (or read signature cleared before log unroll)
                  m_xact_mgr->setAbortFlag(addr, remote_id,
                                           remote_trans);
                  DPRINTF(RubyHTM,"Abort due to non-transactional conflicting"
                          " access to address %#x from remote reader %d\n",
                          addr, machineIDToNodeID(remote_id));
              } else {
                  shouldNack = false;
                  remoteNonTransWins = true;

                  DPRINTF(RubyHTM,"Cannot nack non-transactional conflicting"
                          " access to address %#x from remote reader %d\n",
                          addr, machineIDToNodeID(remote_id));
              }
          } else { // trans-trans conflict
              shouldNack = true;
          }
      } else if (!m_xact_mgr->config_eagerCD() && // Lazy conflict detection
                 m_xact_mgr->getXactLazyCommitArbiter()->validated() &&
                 conflict_res_policy == HtmPolicyStrings::committer_wins) {
          shouldNack = true;
      }
      else {
          assert(conflict_res_policy ==
                 HtmPolicyStrings::requester_wins ||
                 (!m_xact_mgr->config_eagerCD() &&
                  !m_xact_mgr->getXactLazyCommitArbiter()->validated()));
          DPRINTF(RubyHTM, "Conflict (%s):  Local writer"
                  " %d vs remote reader %d for addr %#lx\n",
                  conflict_res_policy, getProcID(),
                  machineIDToNodeID(remote_id), addr);
          shouldNack = false;
          if (!m_xact_mgr->config_lazyVM()) { // LogTM+reqwins
              panic("Eager VM + requester wins not tested!\n");
          }
      }
      DPRINTF(RubyHTM,"HTM: PROC %d shouldNackLoad detected conflict "
              "with PROC %d for address %#x, %s %d\n", getProcID(),
              machineIDToNodeID(remote_id), addr,
              shouldNack ? "nacks" : "aborted by",
              machineIDToNodeID(remote_id));

      // Finally, if req not nacked, resolve by aborting local tx
      if (!shouldNack) {
          assert(!m_policy_is_req_stalls_cda ||
                 (!hasHighestPriority() ||
                  remoteNonTransWins ||
                  (machineIDToMachineType(remote_id) == MachineType_L2Cache)));
          m_xact_mgr->setAbortFlag(addr,
                                   remote_id, remote_trans);
      } else{
          notifySendNack(addr, remote_timestamp, remote_id);
      }
  }
  else { // No conflict
      shouldNack = false;
  }

  return shouldNack;
}

bool
TransactionConflictManager::shouldNackStore(Addr addr,
                                            MachineID remote_id,
                                            Cycles remote_timestamp,
                                            bool remote_trans,
                                            bool local_is_exclusive)
{
  string conflict_res_policy(XACT_CONFLICT_RES);
  bool shouldNack;
  bool local_is_writer = m_xact_mgr->getXactIsolationManager()->
      isInWriteSetPerfectFilter(addr);
  bool existConflict = local_is_writer ||
    m_xact_mgr->getXactIsolationManager()->
    isInReadSetPerfectFilter(addr);
  bool localIsYoungerReader = false;
  bool remoteNonTransWins = false;

  if (existConflict) {
      if (machineIDToMachineType(remote_id) == MachineType_L2Cache) {
          // LLC replacement
          assert(remote_timestamp == 0);
          DPRINTF(RubyHTM, "L2 cache eviction of transactional block"
                  " addr %#lx (local is writer: %d)\n", addr,
                  local_is_writer);
          if ((!local_is_writer &&
               !m_xact_mgr->config_allowReadSetL2CacheEvictions()) ||
              (local_is_writer &&
               !m_xact_mgr->config_allowWriteSetL2CacheEvictions())) {
              m_xact_mgr->setAbortFlag(addr, remote_id,
                                       remote_trans);
          }
          return false;
      } else {
          assert(machineIDToMachineType(remote_id) == MachineType_L1Cache);
          assert(remote_timestamp > 0);
      }
      DPRINTF(RubyHTM, "Conflict detected by shouldNackStore,"
              " requestor=%d addr %#lx (%s)\n",
              machineIDToNodeID(remote_id), addr,
              remote_trans ? "trans" : "non-trans");

      if (m_xact_mgr->isUnrollingLog()) {
          assert(!m_xact_mgr->config_lazyVM()); // LogTM
          shouldNack = true;
#if 0 // TODO: Check
      } else if (m_xact_mgr->isDoomed()) {
          shouldNack = false;
#endif
      } else if (m_policy_is_req_stalls_cda) {
          if (!remote_trans &&
              !m_policy_nack_non_transactional) {
              if (!m_xact_mgr->config_lazyVM() && // LogTM
                  local_is_writer) {
                  shouldNack = true;

                  // Abort but keep nacking until old value restored
                  // from log (or read signature cleared before log unroll)
                  m_xact_mgr->setAbortFlag(addr, remote_id,
                                           remote_trans);
                  DPRINTF(RubyHTM,"Abort due to non-transactional conflicting"
                          " access to address %#x from remote writer %d\n",
                          addr, machineIDToNodeID(remote_id));
              } else {
                  shouldNack = false;
                  remoteNonTransWins = true;

                  DPRINTF(RubyHTM,"Cannot nack non-transactional conflicting"
                          " access to address %#x from remote writer %d\n",
                          addr, machineIDToNodeID(remote_id));
              }
          } else { // trans-trans conflict
              shouldNack = true;
              if (m_policy == HtmPolicyStrings::requester_stalls_cda_hybrid &&
                  !local_is_writer &&
                  isRemoteOlder(getTimestamp(),
                                remote_timestamp, remote_id)) {
                  // See Bobba ISCA 2007: CDA hybrid allows an elder
                  // writer to simultanously abort a number of younger
                  // readers
                  localIsYoungerReader = true;
                  shouldNack = false;
              }
          }
      } else if (!m_xact_mgr->config_eagerCD() && // Lazy conflict detection
                 m_xact_mgr->getXactLazyCommitArbiter()->validated() &&
                 conflict_res_policy == HtmPolicyStrings::committer_wins) {
          shouldNack = true;
      } else {
          assert(conflict_res_policy ==
                 HtmPolicyStrings::requester_wins ||
                 (!m_xact_mgr->config_eagerCD() &&
                  !m_xact_mgr->getXactLazyCommitArbiter()->validated()));

          DPRINTF(RubyHTM, "Conflict (%s):  Local %d %d "
                  "vs remote writer %d for addr %#lx\n",
                  conflict_res_policy,
                  local_is_writer ? "writer" : "reader", getProcID(),
                  machineIDToNodeID(remote_id), addr);
          shouldNack = false;
          if (!m_xact_mgr->config_lazyVM()) { // LogTM+reqwins
              panic("Eager VM + requester wins not tested!\n");
          }
      }
      DPRINTF(RubyHTM,"HTM: PROC %d shouldNackStore detected conflict "
              "with PROC %d for address %#x, %s %d\n", getProcID(),
              machineIDToNodeID(remote_id), addr,
              shouldNack ? "nacks" : "aborted by",
              machineIDToNodeID(remote_id));

      // Finally, if req not nacked, resolve by aborting local tx
      if (!shouldNack) {
          assert(!m_policy_is_req_stalls_cda ||
                 (!hasHighestPriority() ||
                  localIsYoungerReader ||
                  remoteNonTransWins ||
                  (machineIDToMachineType(remote_id) == MachineType_L2Cache)));
          m_xact_mgr->setAbortFlag(addr, remote_id,
                                   remote_trans);
      } else{
          notifySendNack(addr, remote_timestamp, remote_id);
      }
  }
  else {
       shouldNack = false;
  }

  return shouldNack;
}


void
TransactionConflictManager::notifySendNack(Addr addr,
                                           Cycles remote_timestamp,
                                           MachineID remote_id){
  // This method is used to update the deadlock avoidance logic, if used
  string conflict_res_policy(XACT_CONFLICT_RES);

  if (m_policy_is_req_stalls_cda) {
      if (m_xact_mgr->getXactIsolationManager()->
          isInReadSetPerfectFilter(addr) ||
          m_xact_mgr->getXactIsolationManager()->
          isInWriteSetPerfectFilter(addr)) {
          if (isRemoteOlder(getTimestamp(),
                            remote_timestamp, remote_id)){
              m_sentNack = true;
              m_sentNackAddr = addr;
              setPossibleCycle();
              DPRINTF(RubyHTM,"HTM: PROC %d notifySendNack "
                      "sets possible cycle after conflict "
                      "with PROC %d for address %#x\n", getProcID(),
                      machineIDToNodeID(remote_id), addr);
          }
      }
  }
}

void
TransactionConflictManager::notifyReceiveNack(Addr addr,
                                              Cycles remote_timestamp,
                                              MachineID remote_id){
    int transactionLevel = m_xact_mgr->getTransactionLevel();
    if (transactionLevel == 0) return;

    Cycles local_timestamp = getTimestamp();
    string conflict_res_policy(XACT_CONFLICT_RES);

    if (m_policy_is_req_stalls_cda) {
        if (possibleCycle() &&
            isRemoteOlder(local_timestamp,
                          remote_timestamp, remote_id)){
            if (m_xact_mgr->isUnrollingLog()) {
                assert(!m_xact_mgr->config_lazyVM()); // LogTM
                // Already aborted
                return;
            }
            m_xact_mgr->setAbortFlag(m_sentNackAddr,
                                     remote_id);
            DPRINTF(RubyHTM,"HTM: PROC %d notifyReceiveNack "
                    "found possible cycle set after conflict "
                    "with PROC %d for address %#x, aborting "
                    "local (%d) as it not older  than remote (%d)"
                    "%s\n",
                    getProcID(), machineIDToNodeID(remote_id),
                    addr, local_timestamp, remote_timestamp,
                    (local_timestamp == remote_timestamp) ?
                    " and remote has lower proc ID" : "");
        }
        else if (possibleCycle()) {
            DPRINTF(RubyHTM,"HTM: PROC %d notifyReceiveNack "
                    "found possible cycle set after conflict "
                    "with PROC %d for address %#x, "
                    "but local (%d) is older than remote (%d) \n",
                    getProcID(), machineIDToNodeID(remote_id),
                    addr, local_timestamp, remote_timestamp);
        }
    }
}

bool
TransactionConflictManager::hasHighestPriority()
{
    // Magic conflict detection at commit time
    std::vector<TransactionInterfaceManager*> mgrs =
        m_xact_mgr->getRemoteTransactionManagers();

    Cycles local_ts = getOldestTimestamp();
    for (int i=0; i < mgrs.size(); i++) {
        TransactionInterfaceManager* remote_mgr=mgrs[i];
        Cycles remote_ts = remote_mgr->
            getXactConflictManager()->getOldestTimestamp();
        if (remote_ts < local_ts)
            return false;
    }
    return true;
}

} // namespace ruby
} // namespace gem5
