#include "sp.h"

#include "spn/host.h"

#include "commands/commands.h"
#include "host/host.h"
#include "tui/tui.h"
#include "version/version.h"

static void on_wake(void* user_data) {
  sp_sys_event_signal(host.doorbell);
}

static void on_signal(sp_os_signal_t signal, void* userdata) {
  switch (signal) {
    case SP_OS_SIGNAL_INTERRUPT: {
      spn_cli_interrupt(SIGINT);
      break;
    }
    case SP_OS_SIGNAL_TERMINATE: {
      spn_cli_interrupt(SIGTERM);
      break;
    }
    case SP_OS_SIGNAL_ABORT: {
      break;
    }
  }
}

static void help(sp_tty_t* tty, sp_cli_t* cli) {
  sp_cli_write_help(tty, cli);
}

static void version(sp_tty_t* tty) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da(sp_str_t) parts = sp_da_new(scratch.mem, sp_str_t);

  sp_str_t channel = sp_cstr_as_str(spn_build_channel);
  if (!sp_str_equal_cstr(channel, "stable")) {
    sp_da_push(parts, channel);
  }

  sp_str_t commit = sp_cstr_as_str(spn_build_commit);
  if (commit.len) {
    sp_da_push(parts, sp_str_prefix(commit, sp_min((s32)commit.len, 9)));
  }

  if (sp_da_size(parts)) {
    sp_tty_fmt(tty, "spn {} ({})\n", sp_fmt_cstr(SPN_VERSION), sp_fmt_str(sp_str_join_n(scratch.mem, parts, sp_da_size(parts), sp_str_lit(" "))));
  } else {
    sp_tty_fmt(tty, "spn {}\n", sp_fmt_cstr(SPN_VERSION));
  }

  sp_mem_end_scratch(scratch);
}

#if defined(SP_POSIX)
static void on_pipe(s32 signal) {
}

static void on_hangup(s32 signal) {
  spn_cli_interrupt(SIGHUP);
}
#endif

static s32 run(s32 num_args, const c8** args) {
#if defined(SP_POSIX)
  sigaction(SIGPIPE, &(struct sigaction) { .sa_handler = on_pipe }, SP_NULLPTR);
#endif
  sp_mem_heap_t* heap = sp_mem_heap_new();
  host.mem = sp_mem_heap_as_allocator(heap);
  sp_sys_event_open(&host.doorbell);
  host.ctx = spn_ctx_new(on_wake, SP_NULLPTR);
  sp_os_register_signal_handler(SP_OS_SIGNAL_INTERRUPT, on_signal, SP_NULLPTR);
  sp_os_register_signal_handler(SP_OS_SIGNAL_TERMINATE, on_signal, SP_NULLPTR);
#if defined(SP_POSIX)
  // Under nohup the hangup stays ignored, for spn and for everything it starts
  struct sigaction hangup = sp_zero;
  sigaction(SIGHUP, SP_NULLPTR, &hangup);
  if (hangup.sa_handler != SIG_IGN) {
    sigaction(SIGHUP, &(struct sigaction) { .sa_handler = on_hangup }, SP_NULLPTR);
  }
#endif

  sp_cli_t cli = sp_zero;
  sp_cli_parse((sp_cli_desc_t) {
    .root = spn_cli(),
    .args = args,
    .num_args = num_args,
  }, &cli);

  spn_verbosity_t verbosity = SPN_VERBOSITY_NORMAL;
  if (host.args.quiet) {
    verbosity = SPN_VERBOSITY_QUIET;
  } else if (host.args.verbose) {
    verbosity = SPN_VERBOSITY_VERBOSE;
  }

  spn_tui_init(&tui, (spn_tui_desc_t) {
    .ctx = host.ctx,
    .json = host.args.json,
    .no_color = host.args.no_color,
    .verbosity = verbosity,
    .wake = host.doorbell,
  });

  sp_tty_t* out = tui.out;

  if (cli.status == SP_CLI_ERR) {
    spn_tui_usage(&tui, cli.err);
    return spn_cli_shutdown(false);
  }
  if (host.args.version) {
    version(out);
    return spn_cli_shutdown(true);
  }
  if (cli.status == SP_CLI_HELP) {
    help(out, &cli);
    return spn_cli_shutdown(true);
  }

  sp_cli_result_t status = sp_cli_dispatch(&cli);
  if (status == SP_CLI_HELP) {
    help(out, &cli);
  }

  s32 code = spn_cli_shutdown(status != SP_CLI_ERR);
  return host.code ? host.code : code;
}

#if defined(SP_WIN32)
// main gets its arguments in the ANSI code page; everything past here is UTF-8
static const c8** utf8_args(s32* num_args, const c8** args) {
  s32 count = 0;
  LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!wide) {
    return args;
  }

  sp_mem_t mem = sp_mem_os_new();
  const c8** utf8 = sp_alloc_n(mem, const c8*, (u32)count + 1);
  sp_for(it, (u32)count) {
    sp_str_t arg = sp_zero;
    sp_wtf16_to_wtf8(mem, sp_wide_str((const u16*)wide[it], (u32)wcslen(wide[it])), &arg);
    utf8[it] = sp_str_to_cstr(mem, arg);
  }
  utf8[count] = SP_NULLPTR;
  LocalFree(wide);

  *num_args = count;
  return utf8;
}
#endif

s32 spn_main(s32 num_args, const c8** args) {
#if defined(SP_WIN32)
  args = utf8_args(&num_args, args);

  HANDLE job = CreateJobObjectW(SP_NULLPTR, SP_NULLPTR);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {
    .BasicLimitInformation = { .LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE },
  };
  if (job && !(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, (DWORD)sizeof(limits)) && AssignProcessToJobObject(job, GetCurrentProcess()))) {
    CloseHandle(job);
    job = SP_NULLPTR;
  }
#endif

  s32 code = run(num_args, args);

#if defined(SP_WIN32)
  if (job) {
    limits = sp_zero_s(JOBOBJECT_EXTENDED_LIMIT_INFORMATION);
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, (DWORD)sizeof(limits));
    CloseHandle(job);
  }
#endif
  return code;
}
SP_MAIN(spn_main)
