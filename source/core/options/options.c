#include "options/options.h"

#include "intern/intern.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "when/when.h"

typedef struct {
  const spn_when_t* clauses;
  spn_option_setter_t setter;
  bool defaults_declined;
} option_set_t;

static option_set_t find_set(spn_option_subject_t subject, const spn_profile_info_t* profile, si_da(spn_pkg_config_entry_t) config) {
  spn_pkg_config_t* entry = spn_pkg_config_find(config, subject.name);
  return (option_set_t) {
    .clauses = subject.is_root ? &profile->options : entry ? &entry->options : SP_NULLPTR,
    .setter = { .kind = subject.is_root ? SPN_OPTION_SETTER_PROFILE : SPN_OPTION_SETTER_ROOT_MANIFEST },
    .defaults_declined = entry && entry->defaults_declined,
  };
}

typedef struct {
  sp_str_t option;
  spn_option_setter_t setter;
  spn_option_value_t value;
  bool negated;
} claim_t;

static spn_option_setter_t setter_consumer(sp_str_t name) {
  return (spn_option_setter_t) { .kind = SPN_OPTION_SETTER_CONSUMER, .name = name };
}

static void gather_claims(
  spn_option_subject_t subject,
  const spn_when_t* set,
  spn_option_setter_t setter,
  sp_da(claim_t)* claims,
  spn_option_violations_t* violations
) {
  si_da_for(set->clauses, it) {
    const spn_when_clause_t* clause = &set->clauses[it];
    spn_option_info_t** option = si_om_getp(subject.options, spn_intern(clause->key).id);
    if (!option) {
      sp_da_push(*violations, ((spn_option_violation_t) {
        .kind = SPN_OPTION_ERR_UNDECLARED,
        .pkg = subject.name,
        .option = clause->key,
        .a = setter,
      }));
      continue;
    }
    if (!spn_option_value_ok(*option, clause->value)) {
      sp_da_push(*violations, ((spn_option_violation_t) {
        .kind = SPN_OPTION_ERR_BAD_VALUE,
        .pkg = subject.name,
        .option = clause->key,
        .value = clause->value,
        .a = setter,
      }));
      continue;
    }
    if (clause->negated && setter.kind != SPN_OPTION_SETTER_CONSUMER) {
      continue;
    }
    sp_da_push(*claims, ((claim_t) {
      .option = clause->key,
      .setter = setter,
      .value = clause->value,
      .negated = clause->negated,
    }));
  }
}

void spn_options_merge(
  sp_mem_t mem,
  spn_option_subject_t subject,
  const spn_profile_info_t* profile,
  si_da(spn_pkg_config_entry_t) config,
  spn_option_requests_t requests,
  spn_merged_options_t* merged
) {
  option_set_t set = find_set(subject, profile, config);

  merged->options = sp_da_new(mem, spn_resolved_option_t);
  merged->violations = sp_da_new(mem, spn_option_violation_t);

  sp_da(claim_t) claims = sp_da_new(mem, claim_t);
  if (set.clauses) {
    gather_claims(subject, set.clauses, set.setter, &claims, &merged->violations);
  }
  sp_da_for(requests, rt) {
    gather_claims(subject, requests[rt].options, setter_consumer(requests[rt].consumer), &claims, &merged->violations);
  }

  spn_when_env_t env;
  spn_profile_env(mem, profile, &env);

  si_om_for(subject.options, it) {
    spn_option_info_t* option = si_om_at(subject.options, it);

    spn_option_value_t fallback = sp_zero;
    if (!set.defaults_declined) {
      fallback = spn_option_resolve(option, &env);
    }
    if (fallback.kind == SPN_OPTION_VALUE_NONE && option->type == SPN_OPTION_TYPE_BOOL) {
      fallback = spn_option_value_bool(false);
    }

    spn_resolved_option_t resolved = { .name = option->name.str, .setter = { .kind = SPN_OPTION_SETTER_DEFAULT } };
    bool settled = false;
    bool value_union = false;
    const claim_t* last = SP_NULLPTR;

    sp_da_for(claims, ct) {
      const claim_t* claim = &claims[ct];
      if (!sp_str_equal(claim->option, option->name.str) || claim->negated) {
        continue;
      }
      if (claim->setter.kind != SPN_OPTION_SETTER_CONSUMER) {
        resolved.value = claim->value;
        resolved.setter = claim->setter;
        settled = true;
        break;
      }
      if (option->additive) {
        value_union |= claim->value.kind == SPN_OPTION_VALUE_BOOL && claim->value.b;
        resolved.value = spn_option_value_bool(value_union);
        resolved.setter = (spn_option_setter_t) { .kind = SPN_OPTION_SETTER_UNION };
        settled = true;
        continue;
      }
      if (last && !spn_option_value_equal(last->value, claim->value)) {
        sp_da_push(merged->violations, ((spn_option_violation_t) {
          .kind = SPN_OPTION_ERR_CONFLICT,
          .pkg = subject.name,
          .option = option->name.str,
          .value = claim->value,
          .a = last->setter,
          .b = claim->setter,
        }));
      }
      resolved.value = claim->value;
      resolved.setter = claim->setter;
      settled = true;
      last = claim;
    }

    if (!settled) {
      resolved.value = fallback;
    }

    if (resolved.value.kind == SPN_OPTION_VALUE_NONE) {
      sp_da_push(merged->violations, ((spn_option_violation_t) {
        .kind = SPN_OPTION_ERR_NO_VALUE,
        .pkg = subject.name,
        .option = option->name.str,
      }));
    }

    sp_da_for(claims, ct) {
      const claim_t* claim = &claims[ct];
      if (!sp_str_equal(claim->option, option->name.str) || !claim->negated) {
        continue;
      }
      if (spn_option_value_equal(resolved.value, claim->value)) {
        sp_da_push(merged->violations, ((spn_option_violation_t) {
          .kind = SPN_OPTION_ERR_VETO,
          .pkg = subject.name,
          .option = option->name.str,
          .value = claim->value,
          .a = claim->setter,
          .b = resolved.setter,
        }));
      }
    }

    resolved.is_default = spn_option_value_equal(resolved.value, fallback);
    if (resolved.value.kind != SPN_OPTION_VALUE_NONE) {
      spn_when_env_set(&env, resolved.name, resolved.value);
    }
    sp_da_push(merged->options, resolved);
  }
}

void spn_options_env_set(spn_when_env_t* env, const spn_resolved_options_t* options) {
  sp_da_for(*options, it) {
    if ((*options)[it].value.kind == SPN_OPTION_VALUE_NONE) {
      continue;
    }
    spn_when_env_set(env, (*options)[it].name, (*options)[it].value);
  }
}
