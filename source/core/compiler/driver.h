#ifndef spn_compiler_driver_h
#define spn_compiler_driver_h

#include "compiler/types.h"
#include "sp.h"
#include "spn/core.h"

spn_cc_compile_plan_t si_cc_render_compile(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, spn_lang_t lang, const spn_cc_compile_t* compile);
spn_cc_archive_plan_t si_cc_render_archive(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile);
spn_cc_link_plan_t si_cc_render_link(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, const spn_cc_link_t* link);


spn_cc_link_refusal_t spn_cc_validate_link(const spn_cc_t* toolchain, spn_triple_t host, const spn_profile_info_t* profile, const spn_cc_link_t* link);
spn_invocation_t spn_cc_render_compile_source(sp_mem_t mem, const spn_invocation_t* base, spn_path_t source);
spn_invocation_t spn_cc_render_compile_command(sp_mem_t mem, const spn_cc_compile_plan_t* plan, const spn_cc_compile_files_t* files);
spn_invocation_t spn_cc_render_link_command(sp_mem_t mem, const spn_cc_link_plan_t* plan, const spn_cc_link_files_t* files);
spn_invocation_t spn_cc_render_archive_command(sp_mem_t mem, const spn_cc_archive_plan_t* plan, const spn_cc_archive_files_t* files);
bool spn_cc_has(const spn_cc_t* toolchain, spn_cc_cap_t cap);
spn_err_t spn_cc_parse_depfile(sp_mem_t mem, spn_cc_driver_t driver, sp_str_t content, sp_da(sp_str_t)* prereqs);
spn_cc_exports_format_t spn_cc_exports_format(spn_cc_output_kind_t kind, spn_obj_format_t format);
sp_str_t spn_cc_exports_extension(spn_cc_exports_format_t format);
void spn_cc_render_exports(sp_io_writer_t* io, const spn_path_roots_t* roots, spn_cc_exports_format_t format, spn_rsp_style_t rsp, sp_str_t library, si_da(sp_str_t) symbols);
bool spn_cc_link_implib(const spn_profile_info_t* profile, spn_cc_output_kind_t kind);

spn_cc_compile_plan_t spn_gnu_render_compile(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, spn_lang_t lang, const spn_cc_compile_t* compile);
void spn_gnu_render_compile_locations(sp_mem_t mem, const spn_cc_compile_plan_t* plan, const spn_cc_compile_files_t* files, spn_invocation_t* invocation);
spn_cc_link_plan_t spn_gnu_render_link(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, const spn_cc_link_t* link);
void spn_gnu_render_link_locations(sp_mem_t mem, const spn_cc_link_plan_t* plan, const spn_cc_link_files_t* files, spn_invocation_t* invocation);
sp_da(spn_arg_t) spn_gnu_render_exports(sp_mem_t mem, si_da(sp_str_t) symbols);
spn_cc_archive_plan_t spn_gnu_render_archive(sp_mem_t mem, const spn_cc_t* toolchain);
void spn_gnu_render_archive_locations(sp_mem_t mem, const spn_cc_archive_plan_t* plan, const spn_cc_archive_files_t* files, spn_invocation_t* invocation);
void spn_gnu_render_flags(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, spn_cc_flags_t* flags);
bool spn_gnu_link_static(const spn_profile_info_t* profile, spn_cc_output_kind_t kind);
spn_err_t spn_gnu_parse_depfile(sp_mem_t mem, sp_str_t content, sp_da(sp_str_t)* prereqs);

spn_cc_compile_plan_t spn_msvc_render_compile(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, spn_lang_t lang, const spn_cc_compile_t* compile);
void spn_msvc_render_compile_locations(sp_mem_t mem, const spn_cc_compile_plan_t* plan, const spn_cc_compile_files_t* files, spn_invocation_t* invocation);
spn_cc_link_plan_t spn_msvc_render_link(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, const spn_cc_link_t* link);
void spn_msvc_render_link_locations(sp_mem_t mem, const spn_cc_link_plan_t* plan, const spn_cc_link_files_t* files, spn_invocation_t* invocation);
spn_cc_archive_plan_t spn_msvc_render_archive(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile);
void spn_msvc_render_archive_locations(sp_mem_t mem, const spn_cc_archive_plan_t* plan, const spn_cc_archive_files_t* files, spn_invocation_t* invocation);
void spn_msvc_render_flags(sp_mem_t mem, const spn_profile_info_t* profile, spn_cc_flags_t* flags);
spn_err_t spn_msvc_parse_depfile(sp_mem_t mem, sp_str_t content, sp_da(sp_str_t)* prereqs);

#endif
