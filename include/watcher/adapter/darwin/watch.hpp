#pragma once

#include <watcher/platform.hpp>

#if defined(WATER_WATCHER_PLATFORM_MAC_ANY)

/*
  @brief watcher/adapter/darwin

  The Darwin `FSEvent` adapter.
*/

#include <CoreServices/CoreServices.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <watcher/adapter/adapter.hpp>
#include <watcher/event.hpp>

namespace wtr {
namespace watcher {
namespace detail {
namespace adapter {
namespace {

using flag_what_pair_type = std::pair<FSEventStreamEventFlags, event::what>;
using flag_kind_pair_type = std::pair<FSEventStreamEventFlags, event::kind>;

inline constexpr auto delay_ms = 16;
inline constexpr auto flag_what_pair_count = 4;
inline constexpr auto flag_kind_pair_count = 5;

/* basic information about what happened to some path.
   this group is the important one.
   See note [Extra Event Flags] */

/* clang-format off */
inline constexpr std::array<flag_what_pair_type, flag_what_pair_count>
    flag_what_pair{
      flag_what_pair_type(kFSEventStreamEventFlagItemCreated,        event::what::create),
      flag_what_pair_type(kFSEventStreamEventFlagItemModified,       event::what::modify),
      flag_what_pair_type(kFSEventStreamEventFlagItemRemoved,        event::what::destroy),
      flag_what_pair_type(kFSEventStreamEventFlagItemRenamed,        event::what::rename),
    };
/* clang-format on */

/* clang-format off */
inline constexpr std::array<flag_kind_pair_type, flag_kind_pair_count>
    flag_kind_pair{
      flag_kind_pair_type(kFSEventStreamEventFlagItemIsDir,          event::kind::dir),
      flag_kind_pair_type(kFSEventStreamEventFlagItemIsFile,         event::kind::file),
      flag_kind_pair_type(kFSEventStreamEventFlagItemIsSymlink,      event::kind::sym_link),
      flag_kind_pair_type(kFSEventStreamEventFlagItemIsHardlink,     event::kind::hard_link),
      flag_kind_pair_type(kFSEventStreamEventFlagItemIsLastHardlink, event::kind::hard_link),
    };
/* clang-format on */

auto do_make_event_stream(auto const& path, auto const& callback) noexcept
{
  /*  The contortions here are to please darwin.
      importantly, `path_as_refref` and its underlying types
      *are* const qualified. using void** is not ok. but it's also ok. */
  void const* path_cfstring
      = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);

  /* We pass along the path we were asked to watch, */
  auto const translated_path
      = CFArrayCreate(nullptr,               /* not sure */
                      &path_cfstring,        /* path string(s) */
                      1,                     /* number of paths */
                      &kCFTypeArrayCallBacks /* callback */
      );

  /* the time point from which we want to monitor events (which is now), */
  static constexpr auto time_flag = kFSEventStreamEventIdSinceNow;

  /* the delay, in seconds */
  static constexpr auto delay_s = delay_ms > 0 ? delay_ms / 1000.0 : 0.0;

  /* and the event stream flags */
  static constexpr auto event_stream_flags
      = kFSEventStreamCreateFlagFileEvents
        | kFSEventStreamCreateFlagUseExtendedData
        | kFSEventStreamCreateFlagUseCFTypes | kFSEventStreamCreateFlagNoDefer;

  /* to the OS, requesting a file event stream which uses our callback. */
  return FSEventStreamCreate(
      nullptr,           /* Allocator */
      callback,          /* Callback; what to do */
      nullptr,           /* Context (see note [event stream context]) */
      translated_path,   /* Where to watch */
      time_flag,         /* Since when (we choose since now) */
      delay_s,           /* Time between fs event scans */
      event_stream_flags /* What data to gather and how */
  );
}

} /* namespace */

inline bool watch(auto const& path, event::callback const& callback,
                  auto const& is_living) noexcept
{
  static auto callback_hook = callback;

  auto const callback_adapter =
      [](ConstFSEventStreamRef const,   /* stream_ref */
         auto*,                         /* callback_info */
         size_t const event_recv_count, /* event count */
         auto* event_recv_paths,        /* paths with events */
         FSEventStreamEventFlags const* event_recv_flags, /* event flags */
         FSEventStreamEventId const*                      /* event stream id */
      ) {
        /* @todo
           What happens if there are several flags?
           Should we send more events?
           --> Yes. */
        auto lift_what_kind_pairs = [](FSEventStreamEventFlags const& flag_recv)
            -> std::vector<std::pair<event::what, event::kind>> {
          // (kFSEventStreamEventFlagItemCreated,
          // event::what::create),
          // (kFSEventStreamEventFlagItemModified,
          // event::what::modify),
          // (kFSEventStreamEventFlagItemRemoved,
          // event::what::destroy),
          // (kFSEventStreamEventFlagItemRenamed,
          // event::what::rename),
          // (kFSEventStreamEventFlagItemIsDir,
          // event::kind::dir),
          // (kFSEventStreamEventFlagItemIsFile,
          // event::kind::file),
          // (kFSEventStreamEventFlagItemIsSymlink,
          // event::kind::sym_link),
          // (kFSEventStreamEventFlagItemIsHardlink,
          // event::kind::hard_link),
          // (kFSEventStreamEventFlagItemIsLastHardlink,
          // event::kind::hard_link),
          std::vector<std::pair<event::what, event::kind>> wks{};
          wks.reserve(flag_what_pair_count + flag_kind_pair_count);

          for (flag_what_pair_type const& what_it : flag_what_pair) {
            if (flag_recv & what_it.first) {
              for (flag_kind_pair_type const& kind_it : flag_kind_pair) {
                if (flag_recv & kind_it.first) {
                  wks.emplace_back(what_it.second, kind_it.second);
                }
              }
            }
          }
          // for (auto const& w : ws) {
          //   for (auto const& k : ks) {
          //     wks.emplace_back(w, k);
          //   }
          // }
          return wks;
        };

        for (size_t i = 0; i < event_recv_count; i++) {
          char const* event_path = [&]() {
            auto const&& event_path_from_cfdict = [&]() {
              auto const&& event_path_from_cfarray
                  = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(
                      static_cast<CFArrayRef>(event_recv_paths),
                      static_cast<CFIndex>(i)));
              return static_cast<CFStringRef>(
                  CFDictionaryGetValue(event_path_from_cfarray,
                                       kFSEventStreamEventExtendedDataPathKey));
            }();
            return CFStringGetCStringPtr(event_path_from_cfdict,
                                         kCFStringEncodingUTF8);
          }();

          if (event_path != nullptr) {
            auto wks = lift_what_kind_pairs(event_recv_flags[i]);
            for (auto& wk : wks) {
              callback_hook(
                  wtr::watcher::event::event{event_path, wk.first, wk.second});
            }
          }
        }
        // clang-format off
            // /* see note [inode and time]
            //    for some extra stuff that can be done here. */
            // auto const lift_event_kind = [](auto const& path) {
            //   return fs::exists(path)
            //              ? fs::is_regular_file(path) ? event::kind::file
            //                : fs::is_directory(path)  ? event::kind::dir
            //                : fs::is_symlink(path)    ? event::kind::sym_link
            //                                          : event::kind::other
            //              : event::kind::other;
            // };
            
            // for (auto const& what_it : decode_flags(event_recv_what[i]))
            //   if (event_path != nullptr)
            //     callback_hook(wtr::watcher::event::event{
            //         event_path, what_it, lift_event_kind(event_path)});
        // clang-format on
        // }
      };

  auto const do_make_event_queue = [](char const* event_queue_name) {
    /* Request a high priority queue */
    dispatch_queue_t event_queue = dispatch_queue_create(
        event_queue_name,
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,
                                                QOS_CLASS_USER_INITIATED, -10));
    return event_queue;
  };

  auto const do_make_event_handler_alive
      = [](FSEventStreamRef const& event_stream,
           dispatch_queue_t const& event_queue) -> bool {
    if (!event_stream || !event_queue) return false;
    FSEventStreamSetDispatchQueue(event_stream, event_queue);
    FSEventStreamStart(event_stream);
    return event_queue ? true : false;
  };

  auto const do_make_event_handler_dead
      = [](FSEventStreamRef const& event_stream,
           dispatch_queue_t const& event_queue) {
          FSEventStreamStop(event_stream);
          FSEventStreamInvalidate(event_stream);
          FSEventStreamRelease(event_stream);
          dispatch_release(event_queue);
          /* Assuming macOS > 10.8 or iOS > 6.0,
             we don't need to check for null on the
             dispatch queue (our `event_queue`).
             https://developer.apple.com/documentation
             /dispatch/1496328-dispatch_release */
        };

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> dis(0,
                                            std::numeric_limits<size_t>::max());

  auto const event_queue_name
      = ("wtr.watcher.event_queue." + std::to_string(dis(gen)));

  auto const event_stream = do_make_event_stream(path, callback_adapter);
  auto const event_queue = do_make_event_queue(event_queue_name.c_str());

  if (!do_make_event_handler_alive(event_stream, event_queue)) return false;

  while (is_living())
    if constexpr (delay_ms > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

  do_make_event_handler_dead(event_stream, event_queue);

  /* Here, `true` means we were alive at all.
     Errors are handled through the callback. */
  return true;
}

inline bool watch(char const* path, event::callback const& callback,
                  auto const& is_living) noexcept
{
  return watch(std::string(path), callback, is_living);
}

/* clang-format off */
/*
# Notes

## Event Stream Context

To set up a context with some parameters, something like this, from the
`fswatch` project repo, could be used:

  ```cpp
  std::unique_ptr<FSEventStreamContext> context(
      new FSEventStreamContext());
  context->version         = 0;
  context->info            = nullptr;
  context->retain          = nullptr;
  context->release         = nullptr;
  context->copyDescription = nullptr;
  ```

## Inode and Time

To grab the inode and time information about an event, something like this, also
from `fswatch`, could be used:

  ```cpp
  time_t curr_time;
  time(&curr_time);
  auto cf_inode = static_cast<CFNumberRef>(CFDictionaryGetValue(
      _path_info_dict, kFSEventStreamEventExtendedFileIDKey));
  unsigned long inode;
  CFNumberGetValue(cf_inode, kCFNumberLongType, &inode);
  std::cout << "_path_cfstring "
            << std::string(CFStringGetCStringPtr(_path_cfstring,
            kCFStringEncodingUTF8))
            << " (time/inode " << curr_time << "/" << inode << ")"
            << std::endl;
  ```

## Extra Event Flags

```
    // path information, i.e. whether the path is a file, directory, etc.
    // we can get this info much more easily later on in `wtr/watcher/event`.

    // flag_pair(kFSEventStreamEventFlagItemIsDir,          event::what::dir),
    // flag_pair(kFSEventStreamEventFlagItemIsFile,         event::what::file),
    // flag_pair(kFSEventStreamEventFlagItemIsSymlink,      event::what::sym_link),
    // flag_pair(kFSEventStreamEventFlagItemIsHardlink,     event::what::hard_link),
    // flag_pair(kFSEventStreamEventFlagItemIsLastHardlink, event::what::hard_link),

    // path attribute events, such as the owner and some xattr data.
    // will be worthwhile soon to implement these.
    // @todo this.
    // flag_pair(kFSEventStreamEventFlagItemXattrMod,       event::what::other),
    // flag_pair(kFSEventStreamEventFlagOwnEvent,           event::what::other),
    // flag_pair(kFSEventStreamEventFlagItemFinderInfoMod,  event::what::other),
    // flag_pair(kFSEventStreamEventFlagItemInodeMetaMod,   event::what::other),

    // some edge-cases which may be interesting later on.
    // flag_pair(kFSEventStreamEventFlagNone,               event::what::other),
    // flag_pair(kFSEventStreamEventFlagMustScanSubDirs,    event::what::other),
    // flag_pair(kFSEventStreamEventFlagUserDropped,        event::what::other),
    // flag_pair(kFSEventStreamEventFlagKernelDropped,      event::what::other),
    // flag_pair(kFSEventStreamEventFlagEventIdsWrapped,    event::what::other),
    // flag_pair(kFSEventStreamEventFlagHistoryDone,        event::what::other),
    // flag_pair(kFSEventStreamEventFlagRootChanged,        event::what::other),
    // flag_pair(kFSEventStreamEventFlagMount,              event::what::other),
    // flag_pair(kFSEventStreamEventFlagUnmount,            event::what::other),
    // flag_pair(kFSEventStreamEventFlagItemFinderInfoMod,  event::what::other),
    // flag_pair(kFSEventStreamEventFlagItemIsLastHardlink, event::what::other),
    // flag_pair(kFSEventStreamEventFlagItemCloned,         event::what::other),
```
*/
/* clang-format on */

} /* namespace adapter */
} /* namespace detail */
} /* namespace watcher */
} /* namespace wtr   */

#endif /* if defined(WATER_WATCHER_PLATFORM_MAC_ANY) */
