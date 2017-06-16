/*
 * Copyright (c) 2017 Ichito Nagata
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include "h2o/mruby_.h"

struct st_h2o_mruby_sleep_context_t {
    h2o_mruby_context_t *ctx;
    mrb_value receiver;
    h2o_timeout_t timeout;
    h2o_timeout_entry_t timeout_entry;
    uint64_t started_at;
};

static void on_deferred_timeout(h2o_timeout_entry_t *entry)
{
    struct st_h2o_mruby_sleep_context_t *ctx = H2O_STRUCT_FROM_MEMBER(struct st_h2o_mruby_sleep_context_t, timeout_entry, entry);
    h2o_timeout_dispose(ctx->ctx->shared->ctx->loop, &ctx->timeout);
    free(ctx);
}

static void on_sleep_timeout(h2o_timeout_entry_t *entry)
{
    struct st_h2o_mruby_sleep_context_t *ctx = H2O_STRUCT_FROM_MEMBER(struct st_h2o_mruby_sleep_context_t, timeout_entry, entry);
    assert(! mrb_nil_p(ctx->receiver));
    h2o_mruby_shared_context_t *shared = ctx->ctx->shared;
    mrb_int sleep_sec = (mrb_int)(h2o_now(shared->ctx->loop) - ctx->started_at) / 1000;

    h2o_mruby_run_fiber(ctx->ctx, ctx->receiver, mrb_fixnum_value(sleep_sec), NULL);

    mrb_gc_unregister(shared->mrb, ctx->receiver);
    h2o_timeout_unlink(entry);

    /* defer freeing to avoid concurrent modification onf timeout linklist */
    entry->cb = on_deferred_timeout;
    h2o_timeout_link(shared->ctx->loop, &shared->ctx->zero_timeout, entry);
}

mrb_value h2o_mruby_sleep_callback(h2o_mruby_context_t *mctx, mrb_value receiver, mrb_value args, int *run_again)
{
    mrb_state *mrb = mctx->shared->mrb;

    if (mrb_ary_len(mrb, args) == 0) {
        return mrb_nil_value(); /* sleep forever */
    }
    mrb_value arg_sec = mrb_ary_entry(args, 0);
    uint64_t msec;
    if (mrb_fixnum_p(arg_sec)) {
        msec = mrb_int(mrb, arg_sec) * 1000;
    } else if (mrb_float_p(arg_sec)) {
        msec = mrb_float(arg_sec) * 1000;
    } else {
        *run_again = 1;
        return mrb_exc_new_str_lit(mrb, E_ARGUMENT_ERROR, "argument must be either Fixnum or Float");
    }

    struct st_h2o_mruby_sleep_context_t *ctx = h2o_mem_alloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->ctx = mctx;
    ctx->receiver = receiver;
    h2o_timeout_init(ctx->ctx->shared->ctx->loop, &ctx->timeout, msec);
    ctx->timeout_entry = (h2o_timeout_entry_t){0};
    ctx->timeout_entry.cb = on_sleep_timeout;
    h2o_timeout_link(ctx->ctx->shared->ctx->loop, &ctx->timeout, &ctx->timeout_entry);
    ctx->started_at = ctx->timeout_entry.registered_at;

    mrb_gc_register(mrb, receiver);

    return mrb_nil_value();
}

void h2o_mruby_sleep_init_context(h2o_mruby_shared_context_t *ctx)
{
    mrb_state *mrb = ctx->mrb;

    h2o_mruby_define_callback(mrb, "_h2o__sleep", H2O_MRUBY_CALLBACK_ID_SLEEP);
}
