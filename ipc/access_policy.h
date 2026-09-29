// Who may do something, decided by the caller's uid.
// Real AOSP: permission checks on Binder.getCallingUid() (checkCallingPermission),
// backed by package permissions and SELinux. Here: an explicit allow-list.
#pragma once

#include <sys/types.h>

#include <initializer_list>
#include <set>

namespace mini {

// Root (uid 0) always passes, like the system uid. A default-constructed list allows
// nobody else: forgetting to configure it fails closed.
class UidAllowList {
 public:
  static UidAllowList anyone() {
    UidAllowList l;
    l.anyone_ = true;
    return l;
  }
  static UidAllowList only(std::initializer_list<uid_t> uids) {
    UidAllowList l;
    l.uids_ = uids;
    return l;
  }

  void add(uid_t uid) { uids_.insert(uid); }
  bool allows(uid_t uid) const { return anyone_ || uid == 0 || uids_.count(uid) != 0; }

 private:
  bool anyone_ = false;
  std::set<uid_t> uids_;
};

}  // namespace mini
