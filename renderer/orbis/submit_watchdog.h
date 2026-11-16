// SPDX-License-Identifier: MIT
#ifndef PRIME_SUBMIT_WATCHDOG_H
#define PRIME_SUBMIT_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#define PRIME_SUBMIT_STALL_NS (250ull * 1000 * 1000)

/* Diagnostic state only. Never changes fence completion, ownership or timeouts.
 * The caller serializes access. Submission count alone cannot prove a stall:
 * several healthy submissions can arrive during one ordinary GPU execution.
 */
struct PrimeSubmitWatchdog {
   uint64_t pending_since_ns, last_ns;
   uint32_t label;
   bool initialized, pending, reported;
};

static inline bool
prime_submit_watchdog_poll(struct PrimeSubmitWatchdog *w, uint32_t label,
                           uint64_t submitted, uint64_t now_ns)
{
   /* os_time_get_nano is signed: a failed/invalid clock must not manufacture a
    * huge elapsed interval. A backwards clock starts a fresh observation. */
   if (now_ns > INT64_MAX) {
      *w = (struct PrimeSubmitWatchdog){0};
      return false;
   }
   const bool pending = (int32_t)((uint32_t)submitted-label) > 0;
   if (!w->initialized || now_ns < w->last_ns || label != w->label || !pending || !w->pending) {
      *w = (struct PrimeSubmitWatchdog){.pending_since_ns=now_ns,.last_ns=now_ns,
          .label=label,.initialized=true,.pending=pending,.reported=false};
      return false;
   }
   w->last_ns=now_ns;
   if (!w->reported && now_ns-w->pending_since_ns >= PRIME_SUBMIT_STALL_NS) {
      w->reported=true;
      return true;
   }
   return false;
}

static inline bool
prime_submit_full_dump_requested(const char *value)
{
   return value && value[0]=='1' && value[1]=='\0';
}
#endif
