# Watcher

An arbitrary filesystem event watcher which is:

- simple
- efficient
- dependency free
- runnable anywhere
- header only

*Watcher* is extremely efficient. In most cases,
even when scanning millions of paths, this library
uses a near-zero amount of resources. *[1]*

If you don't want to use it in another project,
don't worry. It comes with one. Just build and
run and you've got yourself a filesystem watcher
which prints filesystem events as JSON, which is
pretty cool.

You could, for example, run this program, await events,
and filter through the noise:

```bash
# get
git clone https://github.com/e-dant/watcher.git && cd watcher

# build
cmake -S build/in -B build/out && cmake --build build/out --config Release
# or, from with the "water" project: `tell build`

# use
build/out/water.watcher | grep -oE 'needle-in-a-haystack/.+"'
```

*Watcher* is trivially easy to use, include,
and run. (I hope. If not, let me know.)

## Usage

There is only one user-facing function:
  - `water::watcher::run`

`run` takes a path, which is a string-like thing, and a
callback, with is a function-like thing.

So, passing `run` a string and a lambda would work well.

`run` will happily keep continue watching until it is
asked to stop or it hits an unrecoverable error.

It is trivial to build programs that yield something useful.

Here is a snapshot of the output taken while preparing this
commit, right before writing this paragraph.

```json
"water.watcher.stream": {
    "1663556133054707000": {
      "where": "water/watcher/.git/objects/cc/tmp_obj_sfbyd6",
      "what": "destroy",
      "kind": "other"
    },
    "1663556133054710000": {
      "where": "water/watcher/.git/HEAD.lock",
      "what": "create",
      "kind": "other"
    },
    "1663556133054713000": {
      "where": "water/watcher/.git/refs/heads/next.lock",
      "what": "create",
      "kind": "other"
    },
    "1663556133054716000": {
      "where": "water/watcher/.git/refs/heads/next.lock",
      "what": "modify",
      "kind": "other"
    },
    "1663556133069940000": {
      "where": "water/watcher/.git/logs/HEAD",
      "what": "modify",
      "kind": "file"
    }
}
```

Which is pretty cool.

A `main` program suitable for this task:

```cpp
#include <iostream>             /* std::cout, std::endl */
#include <thread>               /* std::this_thread::sleep_for */
#include <watcher/watcher.hpp>  /* water::watcher::run, water::watcher::event */

using namespace water::watcher::literal;
using std::cout, std::flush, std::endl;

const auto show_event = [](const event& ev) {
  /* The event's << operator will print as json. */
  cout << ev << "," << endl;
};

int main(int argc, char** argv) {

  static constexpr auto delay_ms = 16;

  const auto path = argc > 1
                        /* we have been given a path,
                           and we will use it. */
                        ? argv[1]
                        /* otherwise, default to the
                           current directory. */
                        : ".";

  return run<delay_ms>(/* scan the path, forever... */
                       path,
                       /* printing what we find,
                          every 16 milliseconds. */
                       show_event);
}
```

## Content

- `watcher.hpp`:
    Public interface. Someday, this will be a module.
- `main.cpp`:
    Build this to use it as a CLI program.

## Consume

### In Your Project

Download this project. Include `watcher.hpp`. That's all.

This is a `FetchContent`-able project, if you're into that
kind of thing.

### As A CLI Program

#### Tell

```sh
cd water/water/watcher
`tell build`
# watches the current directory
`tell run`
# watches some path
# `tell run` your/favorite/path
# or, to do all of the above:
# `tell bun` your/favorite/path
```

This will take care of:
  - Building a compiler if one is not found
  - Linking the `compile_commands.json` file
    to this project's root
  - Actually building the project

#### CMake

```sh
cd water/water/watcher/build
cmake -S 'in' -B 'out' -G 'Ninja' -Wno-dev
cmake --build out --config Release
cd out
# watches the current directory
./water.watcher
# watches some path
# ./water.watcher 'your/favorite/path'
```

## Notes

### [1] Exceptions to Efficient Scanning

There are two cases where *Watcher*'s efficiency takes a hit:

1. The platform-independant `warthog` adapter will be used on
platforms which lack better alternative. This will also on
systems which I haven't implemented OS-level calls into just yet.
2. On embedded systems (where resources matter).

*Watcher* is still efficient in these cases. However, depending
on your hardware and whether you need to watch 10-million paths
or not, a longer `delay_ms` (such as in the seconds-range) might
be necessary to prevent your machine from entering-the-atmosphere
temperature.

In all cases, this is a best-in-class filesystem watcher.
The `warthog` is a capable watcher.
