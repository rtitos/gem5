import collections
############################################################################
##  HTM system Configuration
############################################################################

fallback_lock_file = "ckpt/fallback_lock"

# HTM config options:              config name, abbrev, gem5 option name,               bool, description)
HtmOption = collections.namedtuple('HtmOption', ['name','abbrev','gem5opt','isbool','descr', 'siminfo'])
# HTM config options:                       config name,                         abbrev,    gem5 option name,                 is bool, descr, simInfo
htm_disable_speculation        = HtmOption("htm_disable_speculation",           "NoSpec",   "disable-speculation",               True,  False, True  )
htm_binary_suffix              = HtmOption("htm_binary_suffix",                 "BinSfx",   None,                                False, False,  True  )
htm_lazy_vm                    = HtmOption("htm_lazy_vm",                       "LV",       "lazy-vm",                           True,  True , True  )
htm_eager_cd                   = HtmOption("htm_eager_cd",                      "ED",       "eager-cd",                          True,  True , True  )
htm_conflict_resolution        = HtmOption("htm_conflict_resolution",           "CR",       "conflict-resolution",               False, True , True  )
htm_lazy_arbitration           = HtmOption("htm_lazy_arbitration",              "LArb",     "lazy-arbitration",                  False, True , True  ) 
htm_allow_read_set_l0_evictions= HtmOption("htm_allow_read_set_l0_evictions",   "RSL0Ev",   "allow-read-set-l0-cache-evictions", True,  True , True  )
htm_allow_read_set_l1_evictions= HtmOption("htm_allow_read_set_l1_evictions",   "RSL1Ev",   "allow-read-set-l1-cache-evictions", True,  True , True  )
htm_allow_write_set_l0_evictions= HtmOption("htm_allow_write_set_l0_evictions", "WSL0Ev",   "allow-write-set-l0-cache-evictions", True,  True , True  )
htm_allow_write_set_l1_evictions= HtmOption("htm_allow_write_set_l1_evictions", "WSL1Ev",   "allow-write-set-l1-cache-evictions", True,  True , True  )
htm_allow_read_set_l2_evictions = HtmOption("htm_allow_read_set_l2_evictions",  "RSL2Ev",   "allow-read-set-l2-cache-evictions", True,  True , True  )
htm_allow_write_set_l2_evictions = HtmOption("htm_allow_write_set_l2_evictions","WSL2Ev",   "allow-write-set-l2-cache-evictions", True,  True , True  )
htm_precise_read_set_tracking  = HtmOption("htm_precise_read_set_tracking",     "RSPrec",   "precise-read-set-tracking",         True,  True , True  )
htm_trans_aware_l0_replacements= HtmOption("htm_trans_aware_l0_replacements",   "L0Repl",   "trans-aware-l0-replacements",       True,  True , True  )
htm_allow_load_delaying        = HtmOption("htm_allow_load_delaying",           "LDelay",   "allow-load-delaying",               True,  True , True  )
htm_reload_if_stale            = HtmOption("htm_reload_if_stale",               "RldStale", "reload-if-stale",                   True,  True , True  )
htm_l0_downgrade_on_l1_gets    = HtmOption("htm_l0_downgrade_on_l1_gets",       "DwnG",     "l0-downgrade-on-l1-gets",           True,  True , True  )
htm_value_checker              = HtmOption("htm_value_checker",                 "ValChk",   "value-checker",                     True,  False, False )
htm_isolation_checker          = HtmOption("htm_isolation_checker",             "IsolChk",  "isolation-checker",                 True,  False, False )
htm_visualizer                 = HtmOption("htm_visualizer",                    "Visual",   "visualizer",                        True,  False, False )
# HTM library options
htm_max_retries                = HtmOption("htm_max_retries",                   "Rtry",      None,                               False, True, True  )
htm_backoff                    = HtmOption("htm_backoff",                       "Bkoff",     None,                               True,  True, True  )
htm_heap_prefault              = HtmOption("htm_heap_prefault",                 "Pflt",      None,                               True,  True, True  )

htm_config_options = []
htm_config_options.append(htm_disable_speculation)
htm_config_options.append(htm_binary_suffix)
htm_config_options.append(htm_lazy_vm)
htm_config_options.append(htm_eager_cd)
htm_config_options.append(htm_conflict_resolution)
htm_config_options.append(htm_lazy_arbitration)
htm_config_options.append(htm_allow_read_set_l0_evictions)
htm_config_options.append(htm_allow_read_set_l1_evictions)
htm_config_options.append(htm_allow_write_set_l0_evictions)
htm_config_options.append(htm_allow_write_set_l1_evictions)
htm_config_options.append(htm_allow_read_set_l2_evictions)
htm_config_options.append(htm_allow_write_set_l2_evictions)
htm_config_options.append(htm_precise_read_set_tracking)
htm_config_options.append(htm_trans_aware_l0_replacements)
htm_config_options.append(htm_allow_load_delaying)
htm_config_options.append(htm_reload_if_stale)
htm_config_options.append(htm_l0_downgrade_on_l1_gets)
htm_config_options.append(htm_value_checker)
htm_config_options.append(htm_isolation_checker)
htm_config_options.append(htm_visualizer)
htm_config_options.append(htm_max_retries)
htm_config_options.append(htm_backoff)
htm_config_options.append(htm_heap_prefault)

# To be used by non-UMU (non-HTM or gem5 HTM) system configurations
config_empty = collections.OrderedDict()

# Baseline: All options disabled by except default HTM policies, set
# to eager CD and lazy VM (no logging)
cfg1_base = collections.OrderedDict()
cfg1_base[htm_disable_speculation]=False
cfg1_base[htm_binary_suffix]='.htm.fallbacklock'
cfg1_base[htm_lazy_vm]=True
cfg1_base[htm_eager_cd]=True
cfg1_base[htm_conflict_resolution]='requester_wins'
cfg1_base[htm_lazy_arbitration]=None
cfg1_base[htm_allow_read_set_l0_evictions]=False
cfg1_base[htm_allow_read_set_l1_evictions]=False
cfg1_base[htm_allow_write_set_l0_evictions]=False
cfg1_base[htm_allow_write_set_l1_evictions]=False
cfg1_base[htm_allow_read_set_l2_evictions]=False
cfg1_base[htm_allow_write_set_l2_evictions]=False
cfg1_base[htm_precise_read_set_tracking]=False
cfg1_base[htm_trans_aware_l0_replacements]=False
cfg1_base[htm_allow_load_delaying]=False
cfg1_base[htm_reload_if_stale]=False
cfg1_base[htm_l0_downgrade_on_l1_gets]=False
cfg1_base[htm_value_checker]=False
cfg1_base[htm_isolation_checker]=True
cfg1_base[htm_visualizer]=True
cfg1_base[htm_max_retries]=6
cfg1_base[htm_backoff]=True
cfg1_base[htm_heap_prefault]=False

# Vary one parameter at a time w.r.t. baseline, to determine its impact
# No need to try every combination, but rather "guide" the search...

cfg1_l0rsetevict = collections.OrderedDict(cfg1_base)
cfg1_l0rsetevict[htm_allow_read_set_l0_evictions]=True

cfg1_l1rsetevict = collections.OrderedDict(cfg1_l0rsetevict)
cfg1_l1rsetevict[htm_allow_read_set_l1_evictions]=True

cfg1_l1rsetevict_pf = collections.OrderedDict(cfg1_l1rsetevict)
cfg1_l1rsetevict_pf[htm_heap_prefault]=True

cfg1_l1rsetevict_pf_dwng = collections.OrderedDict(cfg1_l1rsetevict_pf)
cfg1_l1rsetevict_pf_dwng[htm_l0_downgrade_on_l1_gets]=True


cfg1_pf_lazycd_magic_cw = collections.OrderedDict(cfg1_base)
cfg1_pf_lazycd_magic_cw[htm_heap_prefault]=True
cfg1_pf_lazycd_magic_cw[htm_eager_cd]=False
cfg1_pf_lazycd_magic_cw[htm_lazy_arbitration]='magic'
cfg1_pf_lazycd_magic_cw[htm_conflict_resolution]='committer_wins'
cfg1_pf_lazycd_magic_cw[htm_isolation_checker]=False

cfg1_pf_dwng_lazycd_magic_cw = collections.OrderedDict(cfg1_pf_lazycd_magic_cw)
cfg1_pf_dwng_lazycd_magic_cw[htm_l0_downgrade_on_l1_gets]=True

cfg1_l0rsetevict_pf_dwng_lazycd_magic_cw = collections.OrderedDict(cfg1_pf_dwng_lazycd_magic_cw)
cfg1_l0rsetevict_pf_dwng_lazycd_magic_cw[htm_allow_read_set_l0_evictions]=True

cfg1_l1rsetevict_pf_dwng_lazycd_magic_cw = collections.OrderedDict(cfg1_l0rsetevict_pf_dwng_lazycd_magic_cw)
cfg1_l1rsetevict_pf_dwng_lazycd_magic_cw[htm_allow_read_set_l1_evictions]=True

cfg1_l2rsetevict_pf_dwng_lazycd_magic_cw = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng_lazycd_magic_cw)
cfg1_l2rsetevict_pf_dwng_lazycd_magic_cw[htm_allow_read_set_l2_evictions]=True

cfg1_l1rsetevict_pf_dwng_lazycd_magic_rw = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng_lazycd_magic_cw)
cfg1_l1rsetevict_pf_dwng_lazycd_magic_rw[htm_conflict_resolution]='requester_wins'

cfg1_l1rsetevict_pf_dwng_lazycd_token_cw = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng_lazycd_magic_cw)
cfg1_l1rsetevict_pf_dwng_lazycd_token_cw[htm_lazy_arbitration]='token'

cfg1_l1rsetevict_pf_dwng_precise  = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng)
cfg1_l1rsetevict_pf_dwng_precise[htm_precise_read_set_tracking]=True

cfg1_l1rsetevict_pf_dwng_precise_reqstalls  = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng_precise)
cfg1_l1rsetevict_pf_dwng_precise_reqstalls[htm_conflict_resolution]='requester_stalls_cda_hybrid'

cfg1_l1rsetevict_pf_dwng_precise_reqstalls_retry64 = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng_precise_reqstalls)
cfg1_l1rsetevict_pf_dwng_precise_reqstalls_retry64[htm_max_retries]=64

cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm = collections.OrderedDict(cfg1_l1rsetevict_pf_dwng_precise_reqstalls)
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_lazy_vm]=False
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_allow_read_set_l2_evictions]=True
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_allow_write_set_l0_evictions]=True
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_allow_write_set_l1_evictions]=True
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_allow_write_set_l2_evictions]=True
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_isolation_checker]=True
cfg1_l2rwsetevict_pf_dwng_precise_reqstalls_eagervm[htm_max_retries]=128

## Abbreviations used for HTM options string values, to generate more
## concise HTM config string description
htm_option_str_abbreviations  = {}
htm_option_str_abbreviations['requester_wins'] = "rw"
htm_option_str_abbreviations['magic'] = "mg"
htm_option_str_abbreviations['token'] = "tkn"
htm_option_str_abbreviations['committer_wins'] = "cw"
htm_option_str_abbreviations['requester_stalls_cda_base'] = "cdab"
htm_option_str_abbreviations['requester_stalls_cda_base_ntx'] = "cdabntx"
htm_option_str_abbreviations['requester_stalls_cda_hybrid'] = "cdah"
# NOTE: ntx allows nacking non-transactional requests and can result
# in deadlocks when using lock subscription as a non-transactional
# thread cannot acquire the lock in the presence of active
# transactions (readers of the lock)
htm_option_str_abbreviations['requester_stalls_cda_hybrid_ntx'] = "cdahntx"
