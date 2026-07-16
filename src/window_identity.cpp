#include "window_identity.h"

namespace ctm {

bool WindowIdentityIsComplete(const WindowIdentity& identity) noexcept {
    return identity.hwnd != nullptr && identity.process_id != 0 &&
           identity.thread_id != 0 && identity.process_creation_time != 0 &&
           !identity.class_name.empty();
}

bool WindowIdentityValuesMatch(const WindowIdentity& expected,
                               const DWORD process_id,
                               const DWORD thread_id,
                               const std::uint64_t process_creation_time,
                               const std::wstring_view class_name) noexcept {
    return expected.process_id == process_id &&
           expected.thread_id == thread_id &&
           expected.process_creation_time == process_creation_time &&
           expected.class_name == class_name;
}

bool WindowIdentitiesMatch(const WindowIdentity& left,
                           const WindowIdentity& right) noexcept {
    return left.hwnd == right.hwnd &&
           WindowIdentityValuesMatch(
               left,
               right.process_id,
               right.thread_id,
               right.process_creation_time,
               right.class_name);
}

}  // namespace ctm
