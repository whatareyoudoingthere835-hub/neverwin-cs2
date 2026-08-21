#pragma once
// ============================================================================
// Пустая реализация SAL2-аннотаций для mingw/clang (в mingw-w64 нет sal.h).
// phnt рассчитан на MSVC sal.h — все аннотации здесь раскрываются в пустоту,
// на ABI/логику это не влияет, это только статический анализ MSVC.
// ============================================================================

#define _Analysis_assume_(expr)
#define _Analysis_noreturn_
#define _Analysis_mode_(expr)

#define _In_
#define _In_opt_
#define _In_z_
#define _In_z_opt_
#define _In_reads_(s)
#define _In_reads_opt_(s)
#define _In_reads_z_(s)
#define _In_reads_bytes_(s)
#define _In_reads_bytes_opt_(s)
#define _In_reads_or_z_(s)
#define _In_reads_or_z_opt_(s)

#define _Out_
#define _Out_opt_
#define _Out_writes_(s)
#define _Out_writes_opt_(s)
#define _Out_writes_z_(s)
#define _Out_writes_bytes_(s)
#define _Out_writes_bytes_opt_(s)
#define _Out_writes_to_(s, c)
#define _Out_writes_to_opt_(s, c)
#define _Out_writes_all_(s)
#define _Out_writes_all_opt_(s)
#define _Out_writes_bytes_to_(s, c)
#define _Out_writes_bytes_to_opt_(s, c)

#define _Inout_
#define _Inout_opt_
#define _Inout_z_
#define _Inout_reads_(s)
#define _Inout_writes_(s)
#define _Inout_writes_opt_(s)
#define _Inout_writes_z_(s)
#define _Inout_writes_bytes_(s)
#define _Inout_writes_bytes_opt_(s)
#define _Inout_writes_to_(s, c)
#define _Inout_writes_bytes_to_(s, c)
#define _Inout_bytecount_(s)
#define _Inout_bytecount_opt_(s)
#define _Inout_updates_(s)
#define _Inout_updates_opt_(s)
#define _Inout_updates_z_(s)
#define _Inout_updates_bytes_(s)
#define _Inout_updates_bytes_opt_(s)
#define _Inout_updates_to_(s, c)
#define _Inout_updates_bytes_to_(s, c)
#define _Inout_updates_bytes_to_opt_(s, c)
#define _In_updates_to_(s, c)
#define _In_updates_bytes_to_(s, c)
#define _Out_writes_bytes_to_opt_(s, c)
#define _Out_bytecount_(s)
#define _Out_bytecount_opt_(s)
#define _Out_bytecount_x_(s)
#define _Out_updates_(s)
#define _Out_updates_opt_(s)
#define _Out_updates_z_(s)
#define _Out_updates_bytes_(s)
#define _Out_updates_bytes_opt_(s)
#define _Out_updates_to_(s, c)
#define _Out_updates_bytes_to_(s, c)
#define _In_bytecount_(s)
#define _In_bytecount_x_(s)
#define _In_updates_(s)
#define _In_updates_bytes_(s)
#define _In_updates_bytes_opt_(s)

#define _Outptr_
#define _Outptr_opt_
#define _Outptr_result_maybenull_
#define _Outptr_opt_result_maybenull_
#define _Outptr_result_nullonfailure_
#define _Outptr_result_maybenull_z_
#define _Outptr_opt_result_nullonfailure_
#define _Outptr_result_buffer_(s)
#define _Outptr_opt_result_buffer_(s)
#define _Outptr_result_buffer_maybenull_(s)
#define _Outptr_result_bytebuffer_(s)
#define _Outptr_opt_result_bytebuffer_(s)

#define _Field_size_(s)
#define _Field_size_opt_(s)
#define _Field_size_full_(s)
#define _Field_size_full_opt_(s)
#define _Field_size_bytes_(s)
#define _Field_size_bytes_opt_(s)
#define _Field_size_bytes_full_(s)
#define _Field_size_bytes_full_opt_(s)
#define _Field_size_part_(s, n)
#define _Field_size_part_opt_(s, n)
#define _Field_size_bytes_part_(s, n)
#define _Field_size_bytes_part_opt_(s, n)
#define _Field_range_(a, b)
#define _Field_z_
#define _Struct_size_bytes_(s)

#define _Ret_maybenull_
#define _Ret_notnull_
#define _Ret_writes_(s)
#define _Ret_writes_opt_(s)
#define _Ret_writes_z_(s)
#define _Ret_writes_bytes_(s)
#define _Ret_writes_bytes_opt_(s)
#define _Ret_writes_to_(s, c)
#define _Ret_z_
#define _Ret_range_(a, b)
#define _Ret_valid_

#define _Success_(expr)
#define _Must_inspect_result_
#define _Post_equal_to_(expr)
#define _Post_satisfies_(expr)
#define _Pre_satisfies_(expr)
#define _Pre_writable_size_(s)
#define _Pre_writable_byte_size_(s)
#define _Post_writable_size_(s)
#define _Post_writable_byte_size_(s)
#define _Pre_valid_
#define _Post_valid_
#define _Pre_readable_size_(s)
#define _Pre_readable_byte_size_(s)
#define _Post_readable_size_(s)
#define _Post_readable_byte_size_(s)

#define _Check_return_
#define _When_(expr, ann)
#define _At_(expr, ann)
#define _On_failure_(ann)
#define _Always_(ann)
#define _Return_type_success_(expr)

#define _Reserved_
#define _Strict_type_match_
#define _Literal_
#define _Notliteral_
#define _Const_
#define _Notref_
#define _Maybenull_
#define _Null_terminated_
#define _NullNull_terminated_

#define _Printf_format_string_
#define _Scanf_format_string_
#define _Printf_format_string_params_(x)
#define _Scanf_format_string_params_(x)

#define _Acquires_lock_(l)
#define _Releases_lock_(l)
#define _Requires_lock_held_(l)
#define _Requires_no_locks_held_
#define _Requires_shared_lock_held_(l)
#define _Acquires_shared_lock_(l)
#define _Releases_shared_lock_(l)
#define _Guarded_by_(l)
#define _Interlocked_
#define _Post_same_lock_(a, b)
#define _Creates_lock_level_(l)

#define _IRQL_requires_(x)
#define _IRQL_requires_min_(x)
#define _IRQL_requires_max_(x)
#define _IRQL_raises_(x)
#define _IRQL_saves_
#define _IRQL_restores_
#define _IRQL_always_function_min_(x)
#define _IRQL_always_function_max_(x)
#define _IRQL_is_(x)

#define _Function_class_(x)
#define _Dispatch_type_(x)
#define _Callback_
#define _Use_decl_annotations_
#define _Enum_is_bitflag_
#define _Bitflag_
#define _On_failure_(ann)
#define _Deref_out_range_(a, b)
#define _Deref_in_range_(a, b)
#define _Deref_inout_range_(a, b)
#define _Deref_out_opt_range_(a, b)
#define _Deref_in_opt_range_(a, b)
#define _Deref_inout_opt_range_(a, b)
#define _Out_range_(a, b)
#define _In_range_(a, b)
#define _Inout_range_(a, b)
#define _Out_opt_range_(a, b)
#define _In_opt_range_(a, b)
#define _Inout_opt_range_(a, b)
#define _Outptr_result_buffer_all_(s)
#define _Outptr_result_buffer_all_maybenull_(s)
#define _Outptr_result_bytebuffer_all_(s)
#define _Outptr_result_bytebuffer_all_maybenull_(s)
#define _Outptr_opt_result_buffer_all_(s)
#define _Outptr_opt_result_bytebuffer_all_(s)
#define _Out_cap_(s)
#define _Out_cap_c_(s)
#define _Inout_cap_(s)
#define _Inout_cap_c_(s)
#define _Out_cap_x_(s)
#define _Inout_cap_x_(s)
#define _Out_capcount_(s)
#define _Inout_capcount_(s)
#define _Out_bytecap_(s)
#define _Inout_bytecap_(s)
#define _Out_bytecap_x_(s)
#define _Inout_bytecap_x_(s)
#define _Out_bytecapcount_(s)
#define _Inout_bytecapcount_(s)
#define _Outptr_opt_result_buffer_to_(s, c)
#define _Outptr_result_buffer_to_(s, c)
#define _Outptr_opt_result_bytebuffer_to_(s, c)
#define _Outptr_result_bytebuffer_to_(s, c)
#define _Outptr_opt_result_buffer_all_maybenull_(s)
#define _Outptr_result_buffer_all_maybenull_(s)
#define _Outptr_opt_result_bytebuffer_all_maybenull_(s)
#define _Outptr_result_bytebuffer_all_maybenull_(s)

#define _Raises_SEH_exception_
#define _Post_ptr_invalid_
#define _Result_zeroonfailure_
#define _Result_nullonfailure_
#define _Null_terminated_
#define _Ret_maybenull_z_

#define _Post_invalid_
#define _Pre_maybenull_
#define _Pre_null_
#define _Post_maybenull_
#define _Pre_notnull_
#define _Post_notnull_
#define _Pre_defensive_
#define _In_defensive_(s)
#define _Out_defensive_(s)
#define _Inout_defensive_(s)
#define _Null_terminated_
#define _Maybenull_
#define _Notnull_
#define _Deref_pre_maybenull_
#define _Deref_post_maybenull_
#define _Deref_pre_notnull_
#define _Deref_post_notnull_
#define _Deref_pre_valid_
#define _Deref_post_valid_
#define _Deref_pre_readonly_
#define _Deref_pre_writable_
#define _Deref_post_readonly_
#define _Deref_post_writable_
#define _Writable_elements_(s)
#define _Readable_elements_(s)
#define _Writable_bytes_(s)
#define _Readable_bytes_(s)
#define _Acquires_exclusive_lock_(...)
#define _Deref_post_count_(...)
#define _Deref_post_opt_count_(...)
#define _Out_writes_bytes_all_(...)
#define _Releases_exclusive_lock_(...)
#define _Unchanged_(...)

#define DECLSPEC_ALLOCATOR __declspec(allocator)
#define DECLSPEC_RESTRICT __declspec(restrict)
#define _In_opt_z_
#define _Maybe_raises_SEH_exception_
#define __callback
