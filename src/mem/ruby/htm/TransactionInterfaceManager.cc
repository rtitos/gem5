/*
  Copyright (C) 2016-2021 Rubén Titos <rtitos@um.es>
  Universidad de Murcia

  GPLv2, see file LICENSE.
*/
#include "mem/ruby/htm/TransactionInterfaceManager.hh"

#include <cassert>
#include <cstdlib>

#include "debug/RubyHTM.hh"
#include "debug/RubyHTMlog.hh"
#include "debug/RubyHTMverbose.hh"
#include "mem/ruby/htm/EagerTransactionVersionManager.hh"
#include "mem/ruby/htm/LazyTransactionCommitArbiter.hh"
#include "mem/ruby/htm/LazyTransactionVersionManager.hh"
#include "mem/ruby/htm/TransactionConflictManager.hh"
#include "mem/ruby/htm/TransactionIsolationManager.hh"
#include "mem/ruby/htm/XactIsolationChecker.hh"
#include "mem/ruby/htm/XactValueChecker.hh"
#include "mem/ruby/profiler/Profiler.hh"
#include "mem/ruby/profiler/XactProfiler.hh"
#include "mem/ruby/structures/CacheMemory.hh"

namespace gem5
{
namespace ruby
{

#define XACT_LAZY_VM config_lazyVM()
#define XACT_EAGER_CD config_eagerCD()
#define XACT_PROFILER m_ruby_system->getProfiler()->getXactProfiler()


TransactionInterfaceManager::TransactionInterfaceManager(const Params &p)
    
    : ClockedObject(p),
      _params(p)
{
    m_sequencer = p.sequencer;
    m_ruby_system = p.ruby_system;
    m_htm = m_ruby_system->params().system->getHTM();
    assert(m_htm);
    m_version = m_sequencer->getId();
    m_dataCache_ptr = p.dcache;
    m_dataCache_ptr->setTransactionManager(this);

    m_sequencer->setTransactionManager(this);
    m_ruby_system->registerTransactionInterfaceManager(this);

    m_xactIsolationManager = new TransactionIsolationManager(this, m_version);
    m_xactConflictManager  = new TransactionConflictManager(this, m_version);
    if (XACT_LAZY_VM) {
        if (!XACT_EAGER_CD) {
            // Lazy CD + lazy VM in write buffer

            // lazy version manager acts as dedicated write buffer
            m_xactLazyVersionManager   =
                new LazyTransactionVersionManager(this,
                                              m_version,
                                              m_dataCache_ptr);
            // lazy commit arbiter ensures proper detection and
            // resolution of conflicts at commit time
            m_xactLazyCommitArbiter  =
                new LazyTransactionCommitArbiter(this, m_version,
                                                 m_htm->params().
                                                 lazy_arbitration);
        } else {
            // Eager-lazy (eager CD + lazy VM in cache)
        }
        // No wset evictions from private cache are possible
        assert(!m_htm->params().allow_write_set_l0_cache_evictions);
        assert(!m_htm->params().allow_write_set_l1_cache_evictions);
        assert(!m_htm->params().allow_write_set_l2_cache_evictions);
    } else { // Eager CD + Eager VM (LogTM)
        assert(XACT_EAGER_CD);
        m_xactEagerVersionManager   =
            new EagerTransactionVersionManager(this,
                                              m_version,
                                              m_dataCache_ptr);
        // Asume wset evictions from L1 (and L0) are possible
        assert(m_htm->params().allow_write_set_l0_cache_evictions);
        assert(m_htm->params().allow_write_set_l1_cache_evictions);
        assert(m_htm->params().allow_write_set_l2_cache_evictions);
    }

    m_transactionLevel   = 0;
    m_escapeLevel        = 0;
    m_abortFlag          = false;
    m_unrollingLogFlag   = false;
    m_atCommit           = false;
    m_abortCause         = HTMStats::AbortCause::Undefined;
    m_abortSourceNonTransactional = false;
    m_lastFailureCause   = HtmFailureFaultCause::INVALID;
    m_capacityAbortWriteSet = false;
    // Only supported HTM protocols by TransactionInterfaceManager
    assert(m_ruby_system->getProtocol() == "MESI_Two_Level_HTM_umu" ||
           m_ruby_system->getProtocol() == "MESI_Three_Level_HTM_umu");

    if (m_ruby_system->getProtocol() == "MESI_Two_Level_HTM_umu") {
        m_lowerLevelCacheMachineType = MachineType_L1Cache;
        // Sanity checks
        assert(!m_htm->params().allow_read_set_l0_cache_evictions);
        assert(!m_htm->params().l0_downgrade_on_l1_gets);
    } else {
        // Three level
        m_lowerLevelCacheMachineType = MachineType_L0Cache;
        // Sanity checks
        if (config_allowReadSetL1CacheEvictions()) {
            assert(m_htm->params().allow_read_set_l0_cache_evictions);
        }
        if (config_allowWriteSetL1CacheEvictions() ||
            config_allowWriteSetL2CacheEvictions()) {
            // Evicting write set blocks requires eager versioning
            assert(!XACT_LAZY_VM);
        }
    }
    if (m_htm->params().precise_read_set_tracking &&
        getXactConflictManager()->isRequesterStallsPolicy()) {
        // Reload if stale is not compatible with requester stalls as
        // it can lead to livelocks due to an older reader repeatedly
        // getting Data_Stale while preventing the progress of a
        // younger writer
        assert(!m_htm->params().reload_if_stale);
    }

    m_htmstart_tick = 0;
    m_htmstart_instruction = 0;
}


TransactionInterfaceManager::~TransactionInterfaceManager() {
}

void
TransactionInterfaceManager::setVersion(int version) {
    m_version = version;
    m_xactIsolationManager->setVersion(version);
    m_xactConflictManager->setVersion(version);
}


int
TransactionInterfaceManager::getVersion() const {
    return m_version;
}

int
TransactionInterfaceManager::getProcID() const{
    return m_version;
}

TransactionIsolationManager*
TransactionInterfaceManager::getXactIsolationManager(){
    return m_xactIsolationManager;
}

TransactionConflictManager*
TransactionInterfaceManager::getXactConflictManager(){
    return m_xactConflictManager;
}

LazyTransactionVersionManager*
TransactionInterfaceManager::getXactLazyVersionManager(){
  return m_xactLazyVersionManager;
}

EagerTransactionVersionManager*
TransactionInterfaceManager::getXactEagerVersionManager(){
  return m_xactEagerVersionManager;
}

LazyTransactionCommitArbiter*
TransactionInterfaceManager::getXactLazyCommitArbiter(){
    return m_xactLazyCommitArbiter;
}

TransactionalSequencer *
TransactionInterfaceManager::getSequencer() {
    return m_sequencer;
}

void
TransactionInterfaceManager::beginTransaction(PacketPtr pkt)
{
    // No nesting in STAMP: assume close nesting (flattening) though
    // support not yet tested
    assert(m_transactionLevel == 0);
    assert(m_escapeLevel == 0);

    m_transactionLevel++;
    if (m_transactionLevel == 1){
        assert(!m_unrollingLogFlag);

        m_xactIsolationManager->beginTransaction();
        m_xactConflictManager->beginTransaction();
        if (XACT_LAZY_VM) {
            if (XACT_EAGER_CD) {
                // EL system use the L1D cache to store speculative updates
            }
            else {
                m_xactLazyVersionManager->beginTransaction(pkt);
                m_xactLazyCommitArbiter->beginTransaction();
            }
        }
        else { // LogTM
            m_xactEagerVersionManager->beginTransaction();
        }
        XACT_PROFILER->moveTo(getProcID(),
                              AnnotatedRegion_TRANSACTIONAL);


        if (getXactConflictManager()->getNumRetries() == 0) {

        }
        m_htmstart_tick = pkt->req->time();
        m_htmstart_instruction = pkt->req->getInstCount();
        if (isAborting()) {
            DPRINTF(RubyHTM, "HTM: beginTransaction found abort flag set\n");
            XACT_PROFILER->moveTo(getProcID(), AnnotatedRegion_ABORTING);
        }
    }

    DPRINTF(RubyHTM, "HTM: beginTransaction "
            "xact_level=%d \n",  m_transactionLevel);
}

bool
TransactionInterfaceManager::canCommitTransaction(PacketPtr pkt) const
{
    if (XACT_EAGER_CD) {
        return true;
    } else {
        if (m_xactLazyVersionManager->committed()) {
            return true;
        } else if (m_xactLazyVersionManager->committing()) {
            return false;
        } else if (m_xactLazyCommitArbiter->shouldValidateTransaction()) {
            if (m_abortFlag) {
                return true; // Signal abort during validation
            } else if (m_xactLazyCommitArbiter->validated()) {
                return false;
            } else { // Pending validation
                return false;
            }
        } else { // No validation required, but not committing yet
            return false;
        }
    }
}

void
TransactionInterfaceManager::initiateCommitTransaction(PacketPtr pkt)
{
    assert(!XACT_EAGER_CD);
    m_atCommit = true;
    if (!m_xactLazyVersionManager->committed()) {
        if (m_xactLazyCommitArbiter->shouldValidateTransaction() &&
            !m_xactLazyCommitArbiter->validated()) {
            XACT_PROFILER->moveTo(getProcID(),
                                  AnnotatedRegion_ARBITRATION);
            // If validation required, initiate actions to determine
            // if this transaction can commit
            m_xactLazyCommitArbiter->
                initiateValidateTransaction();
        } else {
            if (!getXactLazyVersionManager()->committing()) {
                XACT_PROFILER->moveTo(getProcID(),
                                      AnnotatedRegion_COMMITTING);
            }
            // Arbitration passed, or not required (best-effort):
            // initiate commit actions
            m_xactLazyVersionManager->commitTransaction();
        }
    }
}

void
TransactionInterfaceManager::commitTransaction(PacketPtr pkt)
{
    if (m_transactionLevel < 1){
        DPRINTF(RubyHTM, "HTM: ERROR NOT IN XACT! commitTransaction "
                "xact_level=%d\n",
                m_transactionLevel);
        panic("HTM: Error not inside a transaction, cannot commit!");
    }

    assert(m_transactionLevel >= 1);
    assert(!m_abortFlag);

    if (m_transactionLevel == 1){ // Outermost commit

        /* EL SYSTEM: L1D cache is used for lazy versioning of speculative data.
         * Conflicts were resolved eagerly as each individual store acquires
         * exclusive ownership before it completes, so commit can happen instantly.
         */

        if (!m_atCommit) {
            // Move to committing unless we have already done so
            XACT_PROFILER->moveTo(getProcID(),
                                  AnnotatedRegion_COMMITTING);
        }

        if (config_enableValueChecker()) {
            m_ruby_system->getXactValueChecker()->
                commitTransaction(getProcID(), this, m_dataCache_ptr);
        }
        if (!config_allowReadSetLowerLevelCacheEvictions()) {
            // Sanity checks: All Rset blocks must be cached at commit
            vector<Addr> *rset = getXactIsolationManager()->
                getReadSet();
            for (int i=0; i < rset->size(); i++) {
                // Must have read access permissions
                Addr addr = rset->at(i);
                _unused(addr);
                // TODO assert(m_dataCache_ptr->isTagPresentPermissions(addr));
            }
            _unused(rset);
            delete rset; // Caller must delete this
        }
        if (XACT_LAZY_VM) {
            if (XACT_EAGER_CD) {
                // EL system use the L1D cache to store speculative
                // updates: nothing to do, write set (SM bits) cleared
                // by isolation manager below
            }
            else {
                // Clear committing/committed flags, sanity checks
                assert(m_atCommit);
                m_xactLazyVersionManager->notifyCommittedTransaction();
                m_xactLazyCommitArbiter->commitTransaction();
                m_atCommit = false;; // Reset
            }
        } else {
            m_xactEagerVersionManager->commitTransaction();
            m_dataCache_ptr->checkHtmLogPendingClear();
        }
        m_xactConflictManager->commitTransaction();
        m_xactIsolationManager->commitTransaction();
        if (config_enableIsolationChecker()) {
            m_ruby_system->getXactIsolationChecker()->
                clearReadSet(m_version);
            m_ruby_system->getXactIsolationChecker()->
                clearWriteSet(m_version);
        }
        assert(m_writeSetDiscarded.empty());
        assert(m_abortCause == HTMStats::AbortCause::Undefined);
        m_lastFailureCause = HtmFailureFaultCause::INVALID;

        DPRINTF(RubyHTM, "HTM: commitTransaction "
                "xact_level=%d\n", m_transactionLevel);
        Tick transaction_ticks = pkt->req->time() - m_htmstart_tick;
        Cycles transaction_cycles = ticksToCycles(transaction_ticks);
        m_htm_transaction_cycles.sample(transaction_cycles);
        m_htmstart_tick = 0;
        Counter transaction_instructions =
            pkt->req->getInstCount() - m_htmstart_instruction;
        m_htm_transaction_instructions.sample(
                                              transaction_instructions);
        m_htmstart_instruction = 0;
    }

    m_transactionLevel--;
    XACT_PROFILER->moveTo(getProcID(),
                          AnnotatedRegion_DEFAULT);
}


void
TransactionInterfaceManager::discardWriteSetFromL1DataCache() {
    vector<Addr> *wset = getXactIsolationManager()->
        getWriteSet();

    for (int i=0; i < wset->size(); i++) {
        Addr addr=wset->at(i);
        // Must find all write-set blocks in cache, except perhaps
        // those addresses that have already been discarded before
        // abort completed (including those that triggered it)
        bool discarded = m_writeSetDiscarded.erase(addr);
        // In fact, it must find all blocks with write permissions
        DataBlock* datablock_ptr;
        // must find write hit for all lines in the write buffer
        bool hit = m_dataCache_ptr->
            tryCacheAccess(makeLineAddress(addr),
                           RubyRequestType_ST,
                           datablock_ptr, false);
        if (discarded) {
            assert(!hit);
        } else {
            assert(hit);
            m_dataCache_ptr->deallocate(addr);
            DPRINTF(RubyHTM, "HTM: %d deallocated L1DCache address %x\n",
                    m_version, addr);
        }
        _unused(hit);
    }
    delete wset;
    assert(m_writeSetDiscarded.empty());
}

void
TransactionInterfaceManager::abortTransaction(PacketPtr pkt){
    /* RT: Called when Ruby receives a XactionAbort packet from the CPU.
     * The abort may have been initiated by Ruby via the setAbortFlag method,
     * or it may have been directly triggered by the CPU via txAbort instruction.
     * NOTE: This method shall NOT be used to signal an abort: use setAbortFlag.
     */
    assert(m_transactionLevel == 1);
    assert(m_escapeLevel == 0);

    // Profile before discarding speculative state so that we can
    // perform some sanity checks on read-write sets, etc.
    HtmFailureFaultCause cause = pkt->req->getHtmAbortCause();
    profileHtmFailureFaultCause(cause);

    if (XACT_LAZY_VM) {
        if (config_enableValueChecker()) {
            // Lazy systems can perform value checks immediately
            m_ruby_system->getXactValueChecker()->
                restartTransaction(getProcID());
        }
        if (XACT_EAGER_CD) {
            discardWriteSetFromL1DataCache();
        }
        else {
            if (m_atCommit) {
                if (getXactLazyVersionManager()->committing()) {
                    if (!m_abortFlag) { // If CPU-triggered abort
                        // cancel pending writes, if any left
                        getXactLazyVersionManager()->
                            cancelWriteBufferFlush();
                    }
                    if (m_xactLazyVersionManager->committed()) {
                        DPRINTF(RubyHTM, "Aborted after write buffer"
                                " completely flushed \n");
                    }
                    // Discard cache lines already written during
                    // write buffer flush
                    discardWriteSetFromL1DataCache();
                } else {
                    if (m_xactLazyCommitArbiter->validated()) {
                        // Only admitted cause of a abort for already
                        // validated transaction are:

                        // a) acquisition of fallback lock by
                        // non-transactional thread,
                        // b) non-transactional conflicting access
                        // c) Capacity abort
                        // TODO: will fail for L0/L1 capacity aborts
                        // (written blocks not part of the read set)
                        if (m_abortCause ==
                            HTMStats::AbortCause::FallbackLock) {
                            // Remote killer may be transactional if
                            // reader mistaken for writer (downgrade
                            // on L1 GETS disabled)
                            assert(m_abortSourceNonTransactional ||
                                   !RubySystem::enableL0DowngradeOnL1Gets());
                        } else if ((m_abortCause ==
                                   HTMStats::AbortCause::Conflict) ||
                                   (m_abortCause ==
                                    HTMStats::AbortCause::ConflictStale)) {
                            assert(m_htm->params().conflict_resolution ==
                                   HtmPolicyStrings::requester_wins);
                            //assert(m_abortSourceNonTransactional);
                        } else if (m_abortCause ==
                                   HTMStats::AbortCause::L2Capacity) {
                        } else if (m_abortCause ==
                                   HTMStats::AbortCause::L1Capacity) {
                        } else if (m_abortCause ==
                                   HTMStats::AbortCause::L0Capacity) {
                            assert(m_capacityAbortWriteSet ||
                                   !m_htm->params().
                                   allow_read_set_l0_cache_evictions);
                        } else if (m_abortCause ==
                                   HTMStats::AbortCause::Undefined) {
                            // CPU-triggered abort
                        } else {
                            panic("Unexpected abort cause for"
                                  " validated transaction\n");
                        }
                    } else {
                        assert(m_xactLazyCommitArbiter->validating());
                    }
                }
                m_atCommit = false;
            }
            m_xactLazyVersionManager->restartTransaction();
            m_xactLazyCommitArbiter->restartTransaction();
        }
        // Restart conflict management
        getXactConflictManager()->restartTransaction();
    }
    else {
        // LogTM: we need to pass log size to the abort handler (as
        // part of the abort status): do not reset log state now, wait
        // until log unroll done (reset via endLogUnroll)
    }

    if (XACT_LAZY_VM) {
        // Release isolation (clear filters/signatures)
        getXactIsolationManager()->releaseIsolation();
        if (config_enableIsolationChecker()) {
            m_ruby_system->getXactIsolationChecker()->
                clearReadSet(m_version);
            m_ruby_system->getXactIsolationChecker()->
                clearWriteSet(m_version);
        }
    }
    else { // LogTM
        if (m_xactEagerVersionManager->getLogNumEntries() == 0) {
            // No log unroll required: abort completes now

            // Release isolation (clear filters/signatures)
            getXactIsolationManager()->releaseIsolation();
            if (config_enableIsolationChecker()) {
                m_ruby_system->getXactIsolationChecker()->
                    clearReadSet(m_version);
                m_ruby_system->getXactIsolationChecker()->
                    clearWriteSet(m_version);
            }
            // Reset log num entries
            m_xactEagerVersionManager->restartTransaction();
            // Restart conflict management
            getXactConflictManager()->restartTransaction();
        } else {
            // Only release isolation over read set
            getXactIsolationManager()->releaseReadIsolation();
            if (config_enableIsolationChecker()) {
                m_ruby_system->getXactIsolationChecker()->
                    clearReadSet(m_version);
            }
            int wsetsize = getXactIsolationManager()->
                getWriteSetSize();
            int logsize =m_xactEagerVersionManager->getLogNumEntries();
            if (wsetsize != logsize) {
                // It is possible that a logged trans store does not
                // complete after its target block has been added to the
                // log. The following assert may fail in non-TSO since
                // more than one of such pending but already logged stores
                // exists upon abort
                assert(wsetsize+1 == logsize);
            }

            // Keep detecting conflicts on Wset despite xact_level being
            // 0. We could use escape actions, but then we would need to
            // leave xact level > 0 until we get the "endLogUnroll" signal
            m_unrollingLogFlag = true;
            // All log unroll accesses will be escaped (not marked as
            // transactional)
            m_escapeLevel = 1;
            // Leaves xact level to 1 until log unrolled in order to
            // detect conflicts on Wset
            assert(m_transactionLevel == 1);
        }
    }

    if (!XACT_EAGER_CD && m_atCommit) {
        // aborting lazy transaction that has reached commit
        if (m_xactLazyCommitArbiter->validated()) {
            assert(m_xactLazyCommitArbiter->shouldValidateTransaction());
            assert(config_conflictResPolicy() !=
                   HtmPolicyStrings::committer_wins);
        } else if (m_xactLazyCommitArbiter->shouldValidateTransaction()) {
            // Abort before tx validated
            DPRINTF(RubyHTM, "Abort before lazy validation passed\n");
        } else {
            // No validation performed (best-effort lazy commit)
            panic("Best-effort lazy commit not tested!\n");
        }
    } else {
        assert(!m_atCommit);
    }

    // Update transaction level unless going into log unroll
    if (!m_unrollingLogFlag) {
        m_transactionLevel = 0;
    } else {
        assert(!XACT_LAZY_VM); // LogTM
        assert(m_transactionLevel == 1);
        assert(m_xactEagerVersionManager->getLogNumEntries() > 0);
    }

    if (m_abortFlag) {
        // This abort was triggered from ruby (conflict/overflow)
        m_abortFlag = false; // Reset
        m_abortCause = HTMStats::AbortCause::Undefined;
        m_abortAddress = Addr(0);
    } else {
        // CPU-triggered abort (fault, interrupt, lsq conflict)
        XACT_PROFILER->moveTo(getProcID(), AnnotatedRegion_ABORTING);
    }
    XACT_PROFILER->moveTo(getProcID(), AnnotatedRegion_ABORT_HANDLER);
}

int
TransactionInterfaceManager::getTransactionLevel(){
    return m_transactionLevel;
}

bool
TransactionInterfaceManager::inTransaction(){
    return (m_transactionLevel > 0 && m_escapeLevel == 0);
}

Addr
TransactionInterfaceManager::getAbortAddress(){
    assert(m_abortAddress != Addr(0));
    return m_abortAddress;
}


void
TransactionInterfaceManager::isolateTransactionLoad(Addr addr){
    // Ignore transaction level (may be 0) since trans loads may
    // overtake xbegin in O3CPU if not using precise read set tracking
    // (loads isolated as soon as issued by sequencer)
    if (m_transactionLevel == 0) {
        assert(!m_htm->params().precise_read_set_tracking);
    } else {
        // Nesting not tested
        assert(m_transactionLevel == 1);
    }

    Addr physicalAddr = makeLineAddress(addr);

    m_xactIsolationManager->
        addToReadSetPerfectFilter(physicalAddr); // default TL is 1

    DPRINTF(RubyHTMverbose, "isolateTransactionLoad "
            "address=%x\n", physicalAddr);
}

void
TransactionInterfaceManager::addToRetiredReadSet(Addr addr){
    assert(m_transactionLevel == 1);
    Addr physicalAddr = makeLineAddress(addr);
    m_xactIsolationManager->
        addToRetiredReadSet(physicalAddr);
    DPRINTF(RubyHTMverbose, "retiredTransactionLoad "
            "address=%x\n", physicalAddr);

    if (config_enableIsolationChecker()) {
        m_ruby_system->getXactIsolationChecker()->
            addToReadSet(m_version,
                         physicalAddr);
    }
}

bool
TransactionInterfaceManager::inRetiredReadSet(Addr addr)
{
    return m_xactIsolationManager->
        inRetiredReadSet(makeLineAddress(addr));
}

void
TransactionInterfaceManager::profileTransactionAccess(bool miss, bool isWrite,
                                                      MachineType respMach,
                                                      Addr addr,
                                                      Addr pc, int bytes)
{
}


void
TransactionInterfaceManager::isolateTransactionStore(Addr addr){
    assert(m_transactionLevel > 0);

    Addr physicalAddr = makeLineAddress(addr);

    m_xactIsolationManager->
        addToWriteSetPerfectFilter(physicalAddr);
    if (config_enableIsolationChecker()) {
        m_ruby_system->getXactIsolationChecker()->
            addToWriteSet(m_version, physicalAddr);
    }
    DPRINTF(RubyHTMverbose, "HTM: isolateTransactionStore "
            "address=%x\n", physicalAddr);
}

void
TransactionInterfaceManager::
profileHtmFailureFaultCause(HtmFailureFaultCause cause)
{
    assert(cause != HtmFailureFaultCause::INVALID);

    HtmFailureFaultCause preciseFaultCause = cause;
    m_lastFailureCause = cause;

    switch (m_abortCause) {
    case HTMStats::AbortCause::Undefined:
        // CPU-triggered abort due to fault, interrupt, lsq conflict
        assert(!isAborting());
        assert((cause == HtmFailureFaultCause::EXCEPTION) ||
               (cause == HtmFailureFaultCause::INTERRUPT) ||
               (cause == HtmFailureFaultCause::DISABLED) ||
               (cause == HtmFailureFaultCause::LSQ));

        if (cause == HtmFailureFaultCause::LSQ) {
            // "True" LSQ conflict: precise read set tracking, do not
            // reload if stale, block not yet in read set (NOTE:
            // m_abortAddress is unset, so need to pass address in CPU
            // pkt for further sanity addr-specific checks
            assert(!m_htm->params().reload_if_stale);
            // Note that it is possible to have a block in the
            // write-set which is then targeted by the first load when
            // the conflicting invalidation arrives
        }
        break;
    case HTMStats::AbortCause::Explicit:
        // HTMCancel
        assert(cause == HtmFailureFaultCause::EXPLICIT ||
               cause == HtmFailureFaultCause::EXPLICIT_FALLBACKLOCK);
        break;
    case HTMStats::AbortCause::L0Capacity:
    case HTMStats::AbortCause::L1Capacity:
    case HTMStats::AbortCause::L2Capacity:
    case HTMStats::AbortCause::WrongL0:
        // Capacity
        if (cause == HtmFailureFaultCause::SIZE) {
            if (m_abortCause == HTMStats::AbortCause::L2Capacity) {
                preciseFaultCause = HtmFailureFaultCause::SIZE_LLC;
            } else if (m_abortCause == HTMStats::AbortCause::WrongL0) {
                preciseFaultCause = HtmFailureFaultCause::SIZE_WRONG_CACHE;
            } else if (m_ruby_system->getProtocol() == "MESI_Three_Level_HTM_umu" &&
                       m_abortCause == HTMStats::AbortCause::L1Capacity) {
                preciseFaultCause = HtmFailureFaultCause::SIZE_L1PRIV;
            } else if (m_capacityAbortWriteSet) {
                preciseFaultCause = HtmFailureFaultCause::SIZE_WSET;
            } else {
                preciseFaultCause = HtmFailureFaultCause::SIZE_RSET ;
            }
        } else {
            // Conflicting snoops can race with replacements of
            // read-write set blocks, so LSQ is also possible
            // here. CPU-determined cause prevails over Ruby
            assert((cause == HtmFailureFaultCause::EXCEPTION) ||
                   (cause == HtmFailureFaultCause::LSQ) ||
                   (cause == HtmFailureFaultCause::INTERRUPT));

            DPRINTF(RubyHTM, "CPU signaled abort (reason=%s)"
                    " before observing capacity overflow\n",
                    htmFailureToStr(preciseFaultCause));
        }
        break;
    case HTMStats::AbortCause::FallbackLock:
    case HTMStats::AbortCause::ConflictStale:
    case HTMStats::AbortCause::Conflict:
        // Conflict
        if (cause == HtmFailureFaultCause::MEMORY ||
            // Can also get LSQ cause if block in R/W set and CPU
            // found outstanding load in lsq (see checkSnoop) and
            // HTM config says not to reload stale data
            (cause == HtmFailureFaultCause::LSQ)) {
            Addr addr = m_abortAddress;
            // Sanity checks
            if (m_htm->params().precise_read_set_tracking &&
                getXactConflictManager()->isRequesterStallsPolicy()){
#if 0 // Some of these checks do not always hold
                // It is possible to have conflict-induced aborts on
                // addresses that are not yet part of the read set
                // because the trans load has been repeatedly nacked
                bool hybrid_policy =
                    ((config_conflictResPolicy() ==
                      HtmPolicyStrings::requester_stalls_cda_hybrid) ||
                     (config_conflictResPolicy() ==
                      HtmPolicyStrings::requester_stalls_cda_hybrid_ntx));
                assert(getXactConflictManager()->nackReceived() ||
                       (hybrid_policy  && !checkWriteSignature(addr)) ||
                       m_abortSourceNonTransactional);
                assert(checkWriteSignature(addr) ||
                       checkReadSignature(addr) ||
                       (addr == getXactConflictManager()->
                        getNackedPossibleCycleAddr()));
#endif
            } else {
                assert(checkWriteSignature(addr) ||
                       checkReadSignature(addr));
            }

            if (cause == HtmFailureFaultCause::LSQ) {
                // "False" LSQ conflicts, will be categorized as
                // MEMORY since Ruby set abortCause to Conflict (block
                // was in r/w set). Note that "true" LSQ conflicts
                // reported as "lsq_conflicts" are only possible with
                // precise read sets (when block not yet in Rset) and
                // must find abortCause undefined (see case above)

                // Aborts of this type are only possible when CPU does
                // not re-execute conflicting loads
                assert(!m_htm->params().reload_if_stale);
            }

            if (m_abortCause == HTMStats::AbortCause::FallbackLock) {
                preciseFaultCause = HtmFailureFaultCause::MEMORY_FALLBACKLOCK;
            } else if (m_abortCause ==
                       HTMStats::AbortCause::ConflictStale) {
                preciseFaultCause = HtmFailureFaultCause::MEMORY_STALEDATA;
            } else {
                preciseFaultCause = HtmFailureFaultCause::MEMORY;
                if (!XACT_EAGER_CD &&
                    (m_htm->params().lazy_arbitration ==
                     HtmPolicyStrings::token)) {
                    if ((getXactLazyVersionManager()->
                         getNumReadBytesWrittenRemotely() == 0) &&
                        (getXactLazyVersionManager()->
                         getNumWrittenBytesWrittenRemotely() == 0)) {
                        preciseFaultCause =
                            HtmFailureFaultCause::MEMORY_FALSESHARING;
                    }
                }
            }
        } else { // CPU has aborted for another reason before
                 // observing the conflict
            assert((cause == HtmFailureFaultCause::EXCEPTION) ||
                   (cause == HtmFailureFaultCause::INTERRUPT));

            DPRINTF(RubyHTM, "CPU signaled abort (reason=%s)"
                    " before observing memory conflict\n",
                    htmFailureToStr(preciseFaultCause));
        }
        break;
    default:
        panic("Invalid htm failure fault cause\n");
    }
    auto cause_idx = static_cast<int>(preciseFaultCause);
    m_htm_transaction_abort_cause[cause_idx]++;
    DPRINTF(RubyHTM, "htmAbort - reason=%s\n",
            htmFailureToStr(preciseFaultCause));
}

HtmCacheFailure
TransactionInterfaceManager::getHtmTransactionalReqResponseCode()
{
    switch (m_abortCause) {
    case HTMStats::AbortCause::Undefined:
        assert(!isAborting());
        return HtmCacheFailure::NO_FAIL;
    case HTMStats::AbortCause::Explicit:
        // HTMCancel: Must return NO_FAIL for CPU to call
        // XabortCompleteAcc, which sets the abort cause and generates
        // the fault to abort the transaction
        return HtmCacheFailure::NO_FAIL;
    case HTMStats::AbortCause::L0Capacity:
    case HTMStats::AbortCause::L1Capacity:
    case HTMStats::AbortCause::L2Capacity:
    case HTMStats::AbortCause::WrongL0:
        return HtmCacheFailure::FAIL_SELF;
    case HTMStats::AbortCause::Conflict:
    case HTMStats::AbortCause::ConflictStale:
    case HTMStats::AbortCause::FallbackLock:
        return HtmCacheFailure::FAIL_REMOTE;
    default:
        panic("Invalid htm return code\n");
        return HtmCacheFailure::FAIL_OTHER;
    }
}

void
TransactionInterfaceManager::setAbortFlag(Addr addr,
                                          MachineID abortSource,
                                          bool remoteTrans,
                                          bool capacity, bool wset)
{
    /* RT: This method is called from any ruby method to signal an
     * abort.  (e.g. conflict detected from the protocol, eviction of
     * speculative data...)  This kind of "asynchronous" aborts have to
     * be communicated to the CPU so that the required actions (e.g
     * restore reg. file) take place at the right time. This is
     * different for CPU-initiated aborts, which happen "synchronously"
     * (see abortTransaction method).
     */

    /* We use this other flag in Ruby to keep track of this pending
     * abort: the actual abort actions are triggered when the abort
     * command is received from the CPU by
     * TransactionalSequencer::notifyXactionEvent(PacketPtr), which
     * then calls xact_mgr->abortTransaction.
     */
    if (m_transactionLevel == 0) {
        assert(!m_htm->params().precise_read_set_tracking);
        assert(getXactIsolationManager()->
               wasOvertakingRead(addr));
        DPRINTF(RubyHTM, "HTM: setAbortFlag for address=%#x"
                " with TL=0\n", addr);
        panic("setAbortFlag with TL=0!");
    } else {
        if (config_enableIsolationChecker()) {
            if (checkReadSignature(addr)) {
                m_ruby_system->getXactIsolationChecker()->
                    removeFromReadSet(m_version, addr);
            }
            if (checkWriteSignature(addr)) {
                m_ruby_system->getXactIsolationChecker()->
                    removeFromWriteSet(m_version, addr);
            }
        }
    }

    if (!XACT_LAZY_VM) { // LogTM
        assert(!isUnrollingLog());
    }
    if (!m_abortFlag) { // Only send abort signal to CPU once
        m_abortFlag = true;

        m_abortAddress = makeLineAddress(addr);

        if (m_transactionLevel > 0) {
            // Do not move to aborting until  TL > 0
            XACT_PROFILER->moveTo(getProcID(), AnnotatedRegion_ABORTING);
        }
        if (!XACT_EAGER_CD) { // Lazy-lazy
            if (getXactLazyVersionManager()->committing()) {
                // cancel pending writes, if any left
                getXactLazyVersionManager()->
                    cancelWriteBufferFlush();
            }
        }
        assert(m_abortCause == HTMStats::AbortCause::Undefined);

        MachineType machTypeSpecVersioning;
        HTMStats::AbortCause abortCauseCapacity;
        if (m_ruby_system->getProtocol() == "MESI_Two_Level_HTM") {
            machTypeSpecVersioning = MachineType_L1Cache;
            abortCauseCapacity = HTMStats::AbortCause::L1Capacity;
        } else {
            assert(m_ruby_system->getProtocol() == "MESI_Three_Level_HTM_umu");
            machTypeSpecVersioning = MachineType_L0Cache;
            abortCauseCapacity = HTMStats::AbortCause::L0Capacity;
        }

        if (machineIDToNodeID(abortSource) == getProcID() &&
            machineIDToMachineType(abortSource) != MachineType_L2Cache) {
            // Source of abort is self L0/L1 cache
            if (machineIDToMachineType(abortSource) ==
                machTypeSpecVersioning) {
                if (!capacity) {
                    // Transactional block evicted because it was in the
                    // wrong L0 cache (e.g. trans ST to Rset block in Icache)
                    m_abortCause = HTMStats::AbortCause::WrongL0;
                    DPRINTF(RubyHTM, "HTM: setAbortFlag for address=%#x"
                            " in wrong L0 cache\n", addr);
                } else {
                    // Source of abort is self at cache level used for
                    // speculative versioning: L0/L1 overflow
                    m_abortCause = abortCauseCapacity;
                    m_capacityAbortWriteSet = wset;
                }
            } else {
                // L1 replacement of L0 transactional block
                assert(machineIDToMachineType(abortSource) ==
                       MachineType_L1Cache);
                assert(m_ruby_system->getProtocol() ==
                       "MESI_Three_Level_HTM_umu");
                assert(!capacity); // L1 overflows not signaled as capacity
                m_abortCause = HTMStats::AbortCause::L1Capacity;
            }
        } else if (machineIDToMachineType(abortSource) ==
                   MachineType_L2Cache) {
            m_abortCause = HTMStats::AbortCause::L2Capacity;
        } else if (machineIDToNodeID(abortSource) != getProcID()) {
            // Remote conflicting requestor, for now assume L1 cache
            assert(machineIDToMachineType(abortSource) == MachineType_L1Cache);
            m_abortSourceNonTransactional = !remoteTrans;
            // Conflict-induced aborts are split into fallback-lock
            // conflicts vs rest
            assert(m_abortAddress);
            if (m_abortAddress == m_htm->getFallbackLockPAddr()) {
                m_abortCause = HTMStats::AbortCause::FallbackLock;
            }
            else if (machineIDToNodeID(abortSource) ==
                     machineCount(MachineType_L1Cache)) {
                // Stale_Data events (conflicting invalidation by L1 for
                // pending load miss) are distinguishable via abortSource
                // {L1Cache:machineCount}
                m_abortCause = HTMStats::AbortCause::ConflictStale;
            } else if (!checkWriteSignature(m_abortAddress) &&
                       checkReadSignature(m_abortAddress) &&
                       !inRetiredReadSet(m_abortAddress)) {
                // Conflict on read-set block that is not part of the
                // "retired read set", i.e. referenced by outstanding
                // load(s) but data not yet "consumed" by the transaction
                m_abortCause = HTMStats::AbortCause::ConflictStale;
            } else {
                m_abortCause = HTMStats::AbortCause::Conflict;
#if 0
                // Add this abort to the remote killer's remote abort count
                TransactionInterfaceManager *remote_mgr =
                    m_ruby_system->getTransactionInterfaceManager(killer);
                remote_mgr->profileRemoteAbort();
#endif
            }
        }

        // Do not send packet to CPU now, transaction failure will be
        // notified to CPU upon subsequent memory access or HTM command
        DPRINTF(RubyHTM, "HTM: setAbortFlag cause=%s address=%#x"
                " source=%d\n",
                HTMStats::AbortCause_to_string(m_abortCause),
                addr, machineIDToNodeID(abortSource));
    }
    else {
        DPRINTF(RubyHTM, "HTM: setAbortFlag "
                "for address=%x but abort flag was already set\n",
                addr);
    }
}

void
TransactionInterfaceManager::cancelTransaction(PacketPtr pkt)
{
    assert(!m_abortFlag);
    m_abortFlag = true;
    assert(m_abortCause == HTMStats::AbortCause::Undefined);
    m_abortCause = HTMStats::AbortCause::Explicit;
    XACT_PROFILER->moveTo(getProcID(), AnnotatedRegion_ABORTING);
    DPRINTF(RubyHTM, "HTM: cancelTransaction explicitly aborts transaction\n");
}

bool
TransactionInterfaceManager::isCancelledTransaction()
{
    return (m_abortFlag &&
            m_abortCause == HTMStats::AbortCause::Explicit);
}

void
TransactionInterfaceManager::setAbortCause(HTMStats::AbortCause cause)
{
    if (!m_abortFlag) { // CPU-triggered abort
        m_abortCause = cause;
    }
    else { // Ruby-triggered abort, cause already set
        if (m_abortCause != cause) {
            warn("HTM: setAbortCause found mismatch in causes: "
                 "CPU cause is %s, Ruby cause is %s\n",
                 HTMStats::AbortCause_to_string(cause),
                 HTMStats::AbortCause_to_string(m_abortCause));
        }
    }
}

AnnotatedRegion_t
TransactionInterfaceManager::
getWaitForRetryRegionFromPreviousAbortCause()
{
    // Meant to be called when fallback lock acquired, to figure out
    // reason for taking fallback path
    assert(m_transactionLevel == 0);
    if (m_lastFailureCause == HtmFailureFaultCause::SIZE) {
        return AnnotatedRegion_ABORT_HANDLER_WAITFORRETRY_SIZE;
    } else if (m_lastFailureCause == HtmFailureFaultCause::EXCEPTION) {
        return AnnotatedRegion_ABORT_HANDLER_WAITFORRETRY_EXCEPTION;
    } else {
        return AnnotatedRegion_ABORT_HANDLER_WAITFORRETRY_THRESHOLD;
    }
}


bool
TransactionInterfaceManager::isAborting() {
    return inTransaction() && m_abortFlag;
}

bool TransactionInterfaceManager::isDoomed() {
    if (!XACT_LAZY_VM) { // LogTM
        if (isUnrollingLog()) {
            // Abort flag already cleared but TL>0 so consider it
            // doomed
            assert(!m_abortFlag);
            return true;
        }
    }
    return m_abortFlag;
}

void
TransactionInterfaceManager::xactReplacement(Addr addr, MachineID source,
                                             bool capacity) {
    bool wset = false;
    assert(makeLineAddress(addr) == addr);
    if (checkWriteSignature(addr)) {
        if (!XACT_LAZY_VM) { // LogTM
            // Allowed, do not abort
            DPRINTF(RubyHTMlog, "HTM: tolerated xactReplacement"
                    " of logged write-set address=%x \n", addr);
            return;
        }
        wset = true;
        DPRINTF(RubyHTM, "HTM: xactReplacement "
                "for write-set address=%x \n", addr);
        if (capacity) {
            // Keep track of overflows for sanity checks when
            // discarding write set (expect not present)
            m_writeSetDiscarded.
                insert(std::pair<Addr,char>(addr, 'y'));
        }
    }
    else {
        assert(checkReadSignature(addr));
        DPRINTF(RubyHTM, "HTM: xactReplacement "
                "for read-set address=%x \n", addr);
        if (!XACT_LAZY_VM) { // LogTM
            if (isUnrollingLog()) {
                DPRINTF(RubyHTMlog, "HTM: read-set eviction"
                        " during log unroll is ignored\n");
                return;
            }
        }
        MachineType sourceMachType = machineIDToMachineType(source);
        if (machineIDToNodeID(source) == getProcID() &&
            sourceMachType != MachineType_L2Cache) {
            // Self L0/L1
            if (config_allowReadSetLowerLevelCacheEvictions() &&
                (sourceMachType == m_lowerLevelCacheMachineType)) {
                // Lower level cache: allowed
                DPRINTF(RubyHTM, "HTM: read-set eviction tolerated"
                        " for address %#x \n", addr);
                return;
            }
            if (m_ruby_system->getProtocol() == "MESI_Three_Level_HTM_umu" &&
                config_allowReadSetL1CacheEvictions()) {
                // L1 cache: allowed
                DPRINTF(RubyHTM, "HTM: read-set eviction from L1 tolerated"
                        " for address %#x \n", addr);
                return;
            }
        }
    }
    setAbortFlag(addr, source, false, capacity, wset);
}

void
TransactionInterfaceManager::profileCommitCycle()
{

}

bool
TransactionInterfaceManager::shouldNackLoad(Addr addr,
                                            MachineID requestor,
                                            Cycles remote_timestamp,
                                            bool remote_trans)
{
    return getXactConflictManager()->shouldNackLoad(addr, requestor,
                                                    remote_timestamp,
                                                    remote_trans);

}

bool
TransactionInterfaceManager::shouldNackStore(Addr addr,
                                             MachineID requestor,
                                             Cycles remote_timestamp,
                                             bool remote_trans,
                                             bool local_is_exclusive)
{
    return getXactConflictManager()->
        shouldNackStore(addr, requestor,
                        remote_timestamp,
                        remote_trans,
                        local_is_exclusive);
}

void
TransactionInterfaceManager::notifyReceiveNack(Addr addr,
                                               Cycles remote_timestamp,
                                               MachineID remote_id)
{
    getXactConflictManager()->notifyReceiveNack(addr,
                                                remote_timestamp,
                                                remote_id);
}
Cycles
TransactionInterfaceManager::getOldestTimestamp()
{
    return getXactConflictManager()->getOldestTimestamp();
}

bool
TransactionInterfaceManager::checkReadSignature(Addr addr)
{
    return getXactIsolationManager()->isInReadSetPerfectFilter(addr);
}

bool
TransactionInterfaceManager::checkWriteSignature(Addr addr)
{
    return getXactIsolationManager()->isInWriteSetPerfectFilter(addr);
}

bool
TransactionInterfaceManager::hasConflictWith(TransactionInterfaceManager *o)
{
    return getXactIsolationManager()->
        hasConflictWith(o->getXactIsolationManager());
}

void
TransactionInterfaceManager::redirectStoreToWriteBuffer(PacketPtr pkt)
{
    Addr addr = pkt->getAddr();

    assert(getTransactionLevel() > 0);
    getXactLazyVersionManager()->
        addToWriteBuffer(addr, pkt->getSize(),
                         pkt->getPtr<uint8_t>());

    DPRINTF(RubyHTM, "Redirecting store to lazy write buffer,"
            " vaddr %#x paddr %#x\n",
            pkt->req->getVaddr(),
            addr);


    Addr request_address = makeLineAddress(pkt->getAddr());
    getXactIsolationManager()->
        redirectedStoreToWriteBuffer(request_address);
}

void
TransactionInterfaceManager::bypassLoadFromWriteBuffer(PacketPtr pkt,
                                                       DataBlock& datablock) {
  assert(XACT_LAZY_VM && !XACT_EAGER_CD);

  uint8_t buffer[256];
  _unused(buffer);
  bool forwarding = false;
  assert(pkt->isRead() && !pkt->req->isInstFetch());
  std::vector<uint8_t> data = getXactLazyVersionManager()->
      forwardData(pkt->getAddr(),
                  pkt->getSize(),
                  datablock,
                  forwarding);
  if (forwarding) { // Load "hit" in write buffer
      // Sanity checks: only forward spec data to transactional
      // loads. In O3CPU, xend is membarrier so non-transactional
      // loads should not reorder w.r.t. xend
      assert(pkt->isHtmTransactional());
      assert(data.size() == pkt->getSize());
      assert(data.size() < sizeof(buffer));
      for (unsigned int i = 0; i < pkt->getSize(); i++){
          buffer[i] = (uint8_t)data[i];
      }
      //  Replace loaded data with data from write buffer
      memcpy(pkt->getPtr<uint8_t>(),
             buffer,
             pkt->getSize());
  }
}


void
TransactionInterfaceManager::mergeDataFromWriteBuffer(PacketPtr pkt,
                                                      DataBlock& datablock)
{
    Addr address = pkt->getAddr();
    getXactLazyVersionManager()->
        mergeDataFromWriteBuffer(address, datablock);
}

bool
TransactionInterfaceManager::isLogReady()
{
    return m_xactEagerVersionManager->isReady();
}

bool
TransactionInterfaceManager::isAccessToLog(Addr addr)
{
    return m_xactEagerVersionManager->isAccessToLog(addr);
}

bool
TransactionInterfaceManager::isEndLogUnrollSignal(PacketPtr pkt)
{
    return m_xactEagerVersionManager->isEndLogUnrollSignal(pkt);
}

void
TransactionInterfaceManager::setupLogTranslation(Addr vaddr,
                                                 Addr paddr)
{
    m_xactEagerVersionManager->setupLogTranslation(vaddr, paddr);
}

Addr
TransactionInterfaceManager::translateLogAddress(Addr vaddr) const
{
    return m_xactEagerVersionManager->translateLogAddress(vaddr);
}

Addr
TransactionInterfaceManager::addLogEntry()
{
    return m_xactEagerVersionManager->addLogEntry();
}

int
TransactionInterfaceManager::getLogNumEntries()
{
    return m_xactEagerVersionManager->getLogNumEntries();
}

bool
TransactionInterfaceManager::isUnrollingLog(){
    if (!XACT_LAZY_VM) // LogTM
        return m_unrollingLogFlag;
    else
        return false;
}

void
TransactionInterfaceManager::endLogUnroll(){
    assert(m_transactionLevel == 1);
    assert(m_escapeLevel == 1);
    assert(!XACT_LAZY_VM); // LogTM
    assert(m_unrollingLogFlag);

    // Reset log num entries
    m_xactEagerVersionManager->restartTransaction();
    // Restart conflict management
    getXactConflictManager()->restartTransaction();

    // Release isolation over write set
    getXactIsolationManager()->releaseIsolation();
    if (config_enableIsolationChecker()) {
        m_ruby_system->getXactIsolationChecker()->
            clearReadSet(m_version);
        m_ruby_system->getXactIsolationChecker()->
            clearWriteSet(m_version);
    }

    m_escapeLevel = 0;
    m_transactionLevel = 0;
    m_unrollingLogFlag = false;
    DPRINTF(RubyHTMlog, "HTM: done unrolling log, abort"
            " is now complete!\n");

    // Value sanity checks done after log unrolled
    if (config_enableValueChecker()) {
        m_ruby_system->getXactValueChecker()->
            restartTransaction(getProcID());
    }
}

void
TransactionInterfaceManager::beginEscapeAction()
{
    assert(m_escapeLevel == 0);
    m_escapeLevel++;
}

void
TransactionInterfaceManager::endEscapeAction()
{
    assert(m_escapeLevel == 1);
    m_escapeLevel--;
}

bool
TransactionInterfaceManager::inEscapeAction()
{
    return m_escapeLevel > 0;
}

void
TransactionInterfaceManager::regStats()
{
    ClockedObject::regStats();
    // hardware transactional memory
    m_htm_transaction_cycles
        .init(10)
        .name(name() + ".htm_transaction_cycles")
        .desc("number of cycles spent in an outer transaction")
        .flags(Stats::pdf | Stats::dist | Stats::nozero | Stats::nonan)
        ;
    m_htm_transaction_instructions
        .init(10)
        .name(name() + ".htm_transaction_instructions")
        .desc("number of instructions spent in an outer transaction")
        .flags(Stats::pdf | Stats::dist | Stats::nozero | Stats::nonan)
        ;
    auto num_causes = static_cast<int>(HtmFailureFaultCause::NUM_CAUSES);
    m_htm_transaction_abort_cause
        .init(num_causes)
        .name(name() + ".htm_transaction_abort_cause")
        .desc("cause of htm transaction abort")
        .flags(Stats::total | Stats::pdf | Stats::dist | Stats::nozero)
        ;

    for (unsigned cause_idx = 0; cause_idx < num_causes; ++cause_idx) {
        m_htm_transaction_abort_cause.subname(
            cause_idx,
            htmFailureToStr(HtmFailureFaultCause(cause_idx)));
    }
}

std::vector<TransactionInterfaceManager*>
TransactionInterfaceManager::getRemoteTransactionManagers() const
{
    return m_ruby_system->getTransactionInterfaceManagers();
}

} // namespace ruby
} // namespace gem5
