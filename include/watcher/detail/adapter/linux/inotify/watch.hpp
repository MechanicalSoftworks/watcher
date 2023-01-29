#pragma once

/*
  @brief wtr/watcher/<d>/adapter/linux/inotify

  The Linux `inotify` adapter.
*/

/* WATER_WATCHER_PLATFORM_* */
#include <watcher/detail/platform.hpp>

#if defined(WATER_WATCHER_PLATFORM_LINUX_KERNEL_GTE_2_7_0) \
    || defined(WATER_WATCHER_PLATFORM_ANDROID_ANY)
#if !defined(WATER_WATCHER_USE_WARTHOG)

#define WATER_WATCHER_ADAPTER_LINUX_INOTIFY

/* EPOLL*
   epoll_ctl
   epoll_wait
   epoll_event
   epoll_create
   epoll_create1 */
#include <sys/epoll.h>
/* IN_*
   inotify_init
   inotify_init1
   inotify_event
   inotify_add_watch */
#include <sys/inotify.h>
/* open
   read
   close */
#include <unistd.h>
/* path
   is_directory
   directory_options
   recursive_directory_iterator */
#include <filesystem>
/* function */
#include <functional>
/* tuple
   make_tuple */
#include <tuple>
/* unordered_map */
#include <unordered_map>
/* memcpy */
#include <cstring>
/* event
   callback */
#include <watcher/watcher.hpp>

namespace wtr {
namespace watcher {
namespace detail {
namespace adapter {
namespace inotify {

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>
   Anonymous namespace for "private" things. */
namespace {

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>/constants
   - delay
       The delay, in milliseconds, while `epoll_wait` will
       'sleep' for until we are woken up. We usually check
       if we're still alive at that point.
   - event_wait_queue_max
       Number of events allowed to be given to do_event_recv
       (returned by `epoll_wait`). Any number between 1
       and some large number should be fine. We don't
       lose events if we 'miss' them, the events are
       still waiting in the next call to `epoll_wait`.
   - event_buf_len:
       For our event buffer, 4096 is a typical page size
       and sufficiently large to hold a great many events.
       That's a good thumb-rule.
   - in_init_opt
       Use non-blocking IO.
   - in_watch_opt
       Everything we can get.
   @todo
   - Measure perf of IN_ALL_EVENTS
   - Handle move events properly.
     - Use IN_MOVED_TO
     - Use event::<something> */
inline constexpr auto delay_ms = 16;
inline constexpr auto event_wait_queue_max = 1;
inline constexpr auto event_buf_len = 4096;
inline constexpr auto in_init_opt = IN_NONBLOCK;
inline constexpr auto in_watch_opt
    = IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_Q_OVERFLOW;

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>/types
   - path_map_type
       An alias for a map of file descriptors to paths.
   - sys_resource_type
       An object representing an inotify file descriptor,
       an epoll file descriptor, an epoll configuration,
       and whether or not these resources are valid. */
using path_map_type = std::unordered_map<int, std::filesystem::path>;
struct sys_resource_type
{
  bool valid;
  int watch_fd;
  int event_fd;
  epoll_event event_conf;
};

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>/fns/do_path_map_create
   If the path given is a directory
     - find all directories above the base path given.
     - ignore nonexistent directories.
     - return a map of watch descriptors -> directories.
   If `path` is a file
     - return it as the only value in a map.
     - the watch descriptor key should always be 1. */
inline auto do_path_map_create(int const watch_fd,
                               std::filesystem::path const& base_path,
                               event::callback const& callback) noexcept
    -> path_map_type
{
  namespace fs = ::std::filesystem;
  using diter = fs::recursive_directory_iterator;
  using dopt = fs::directory_options;

  /* Follow symlinks, ignore paths which we don't have permissions for. */
  static constexpr auto fs_dir_opt
      = dopt::skip_permission_denied & dopt::follow_directory_symlink;

  static constexpr auto path_map_reserve_count = 256;

  auto dir_ec = std::error_code{};
  auto path_map = path_map_type{};
  path_map.reserve(path_map_reserve_count);

  auto do_mark = [&](fs::path const& d) noexcept -> bool {
    int wd = inotify_add_watch(watch_fd, d.c_str(), in_watch_opt);
    return wd > 0 ? path_map.emplace(wd, d).first != path_map.end() : false;
  };

  if (do_mark(base_path))
    if (fs::is_directory(base_path, dir_ec))
      for (auto dir : diter(base_path, fs_dir_opt, dir_ec))
        if (!dir_ec)
          if (fs::is_directory(dir, dir_ec))
            if (!dir_ec)
              if (!do_mark(dir.path()))
                callback({"w/sys/path_unwatched@" / dir.path(),
                          event::what::other, event::kind::watcher});

  return path_map;
};

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>/fns/do_sys_resource_open
   Produces a `sys_resource_type` with the file descriptors from
   `inotify_init` and `epoll_create`. Invokes `callback` on errors. */
inline auto do_sys_resource_open(event::callback const& callback) noexcept
    -> sys_resource_type
{
  auto do_error = [&callback](auto msg, int watch_fd,
                              int event_fd = -1) noexcept -> sys_resource_type {
    callback({msg, event::what::other, event::kind::watcher});
    return sys_resource_type{
        .valid = false,
        .watch_fd = watch_fd,
        .event_fd = event_fd,
        .event_conf = {.events = 0, .data = {.fd = watch_fd}}};
  };

  int watch_fd
#if defined(WATER_WATCHER_PLATFORM_ANDROID_ANY)
      = inotify_init();
#elif defined(WATER_WATCHER_PLATFORM_LINUX_KERNEL_ANY)
      = inotify_init1(in_init_opt);
#endif

  if (watch_fd >= 0) {
    epoll_event event_conf{.events = EPOLLIN, .data{.fd = watch_fd}};

    int event_fd
#if defined(WATER_WATCHER_PLATFORM_ANDROID_ANY)
        = epoll_create(event_wait_queue_max);
#elif defined(WATER_WATCHER_PLATFORM_LINUX_KERNEL_ANY)
        = epoll_create1(EPOLL_CLOEXEC);
#endif

    if (event_fd >= 0)
      if (epoll_ctl(event_fd, EPOLL_CTL_ADD, watch_fd, &event_conf) >= 0)
        return sys_resource_type{.valid = true,
                                 .watch_fd = watch_fd,
                                 .event_fd = event_fd,
                                 .event_conf = event_conf};
      else
        return do_error("e/sys/epoll_ctl", watch_fd, event_fd);
    else
      return do_error("e/sys/epoll_create", watch_fd, event_fd);
  } else
    return do_error("e/sys/inotify_init", watch_fd);
}

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>/fns/do_sys_resource_close
   Close the file descriptors `watch_fd` and `event_fd`. */
inline auto do_sys_resource_close(sys_resource_type& sr) noexcept -> bool
{
  return !(close(sr.watch_fd) && close(sr.event_fd));
}

/* @brief wtr/watcher/<d>/adapter/linux/inotify/<a>/fns/do_event_recv
   Reads through available (inotify) filesystem events.
   Discerns their path and type.
   Calls the callback.
   Returns false on eventful errors.

   @todo
   Return new directories when they appear,
   Consider running and returning `find_dirs` from here.
   Remove destroyed watches. */
inline auto do_event_recv(int watch_fd, path_map_type& path_map,
                          std::filesystem::path const& base_path,
                          event::callback const& callback) noexcept -> bool
{
  namespace fs = ::std::filesystem;
  using evk = ::wtr::watcher::event::kind;
  using evw = ::wtr::watcher::event::what;

  alignas(inotify_event) char buf[event_buf_len];

  enum class state { eventful, eventless, error };

  /* While inotify has events pending, read them.
     There might be several events from a single read.

     Three possible states:
      - eventful: there are events to read
      - eventless: there are no events to read
      - error: there was an error reading events

     The EAGAIN "error" means there is nothing
     to read. We count that as 'eventless'.

     Forward events and errors to the user.

     Return when eventless. */

recurse:

  ssize_t read_len = read(watch_fd, buf, event_buf_len);

  switch (read_len > 0      ? state::eventful
          : read_len == 0   ? state::eventless
          : errno == EAGAIN ? state::eventless
                            : state::error)
  {
    case state::eventful:
      /* Loop over all events in the buffer. */
      for (auto this_event = (inotify_event*)buf;
           this_event < (inotify_event*)(buf + read_len);
           this_event += this_event->len)
      {
        if (!(this_event->mask & IN_Q_OVERFLOW)) [[likely]] {
          auto path = path_map.find(this_event->wd)->second
                      / fs::path(this_event->name);

          auto kind = this_event->mask & IN_ISDIR ? evk::dir : evk::file;

          auto what = this_event->mask & IN_CREATE   ? evw::create
                      : this_event->mask & IN_DELETE ? evw::destroy
                      : this_event->mask & IN_MOVE   ? evw::rename
                      : this_event->mask & IN_MODIFY ? evw::modify
                                                     : evw::other;

          callback({path, what, kind});

          if (kind == evk::dir && what == evw::create)
            path_map[inotify_add_watch(watch_fd, path.c_str(), in_watch_opt)]
                = path;

          else if (kind == evk::dir && what == evw::destroy) {
            inotify_rm_watch(watch_fd, this_event->wd);
            path_map.erase(this_event->wd);
          }

        } else
          callback({"e/self/overflow@" / base_path, evw::other, evk::watcher});
      }
      /* Same as `return do_event_recv(..., buf)`.
         Our stopping condition is `eventless` or `error`. */
      goto recurse;

    case state::error:
      callback({"e/sys/read@" / base_path, evw::other, evk::watcher});
      return false;

    case state::eventless: return true;
  }

  /* Unreachable */
  return false;
}

} /* namespace */

/*
  @brief wtr/watcher/<d>/adapter/watch
  Monitors `path` for changes.
  Invokes `callback` with an `event` when they happen.
  `watch` stops when asked to or irrecoverable errors occur.
  All events, including errors, are passed to `callback`.

  @param path
    A filesystem path to watch for events.

  @param callback
    A function to invoke with an `event` object
    when the files being watched change.

  @param is_living
    A function to decide whether we're dead.
*/
inline bool watch(std::filesystem::path const& path,
                  event::callback const& callback,
                  std::function<bool()> const& is_living) noexcept
{
  auto do_error
      = [&path, &callback](sys_resource_type& sr, char const* msg) -> bool {
    using evk = ::wtr::watcher::event::kind;
    using evw = ::wtr::watcher::event::what;

    if (!do_sys_resource_close(sr))
      callback({"e/sys/close@" / path, evw::other, evk::watcher});
    callback({msg / path, evw::other, evk::watcher});
    return false;
  };

  /* We have:
       - system resources
           For inotify and epoll
       - event recieve list
           For receiving epoll events
       - path map
           For event to path lookups */

  sys_resource_type sr = do_sys_resource_open(callback);
  if (sr.valid) {
    auto path_map = do_path_map_create(sr.watch_fd, path, callback);

    if (path_map.size() > 0) {
      epoll_event event_recv_list[event_wait_queue_max];

      /* While living:
          - Await filesystem events
          - Invoke `callback` on errors and events */

      while (is_living()) {
        int event_count = epoll_wait(sr.event_fd, event_recv_list,
                                     event_wait_queue_max, delay_ms);

        if (event_count < 0)
          return do_error(sr, "e/sys/epoll_wait@");

        else if (event_count > 0) [[likely]]
          for (int n = 0; n < event_count; n++)
            if (event_recv_list[n].data.fd == sr.watch_fd) [[likely]]
              if (!do_event_recv(sr.watch_fd, path_map, path, callback))
                  [[unlikely]]
                return do_error(sr, "e/self/event_recv@");
      }

      return do_sys_resource_close(sr);

    } else
      return do_error(sr, "e/self/path_map@");

  } else
    return do_error(sr, "e/self/sys_resource@");
}

} /* namespace inotify */
} /* namespace adapter */
} /* namespace detail */
} /* namespace watcher */
} /* namespace wtr */

#endif /* !defined(WATER_WATCHER_USE_WARTHOG) */
#endif /* defined(WATER_WATCHER_PLATFORM_LINUX_KERNEL_GTE_2_7_0) \
          || defined(WATER_WATCHER_PLATFORM_ANDROID_ANY) */
