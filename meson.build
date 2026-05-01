project(
  'atria',
  'cpp',
  version: '0.1.0',
  license: 'MIT OR Apache-2.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'werror=true',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ],
)

cxx = meson.get_compiler('cpp')

extra_warnings = []
if cxx.get_id() == 'gcc' or cxx.get_id() == 'clang'
  extra_warnings += [
    '-Wcast-align',
    '-Wconversion',
    '-Wdouble-promotion',
    '-Wformat=2',
    '-Wnon-virtual-dtor',
    '-Wnull-dereference',
    '-Wold-style-cast',
    '-Woverloaded-virtual',
    '-Wshadow',
    '-Wsign-conversion',
    '-Wundef',
    '-Wunused',
  ]
elif cxx.get_id() == 'msvc'
  extra_warnings += ['/permissive-', '/Zc:__cplusplus', '/Zc:preprocessor']
endif

add_project_arguments(cxx.get_supported_arguments(extra_warnings), language: 'cpp')

if get_option('fuzz')
  if cxx.get_id() != 'clang'
    error('The fuzz harness requires Clang. Configure with CC=clang CXX=clang++ -Dfuzz=true.')
  endif
  fuzz_instrument_args = ['-fsanitize=fuzzer-no-link,address,undefined', '-fno-omit-frame-pointer']
  add_project_arguments(cxx.get_supported_arguments(fuzz_instrument_args), language: 'cpp')
  add_project_link_arguments(
    cxx.get_supported_link_arguments(['-fsanitize=fuzzer-no-link,address,undefined']),
    language: 'cpp',
  )
endif

threads_dep = dependency('threads')

platform_deps = [threads_dep]

if host_machine.system() == 'windows'
  platform_sources = files(
    'src/platform/windows/windows_socket.cpp',
    'src/net/event_loop_poll.cpp',
  )
  platform_deps += cxx.find_library('ws2_32')
  platform_deps += cxx.find_library('iphlpapi')
elif host_machine.system() == 'linux'
  platform_sources = files(
    'src/platform/posix/posix_socket.cpp',
    'src/net/event_loop_epoll.cpp',
  )
elif host_machine.system() in ['darwin', 'freebsd', 'netbsd', 'openbsd', 'dragonfly']
  platform_sources = files(
    'src/platform/posix/posix_socket.cpp',
    'src/net/event_loop_kqueue.cpp',
  )
else
  platform_sources = files(
    'src/platform/posix/posix_socket.cpp',
    'src/net/event_loop_poll.cpp',
  )
endif

atria_inc = include_directories('include', 'src')

subdir('include')
subdir('src')
subdir('tests')
subdir('examples/todo_api')
subdir('examples/di_api')
subdir('examples/websocket_chat')
subdir('examples/upnp_skeleton')

if get_option('benchmarks')
  subdir('benchmarks')
endif

if get_option('fuzz')
  subdir('tests/fuzz')
endif
