atria_sources = files(
  'application/application.cpp',
  'application/websocket.cpp',
  'http/headers.cpp',
  'http/method.cpp',
  'http/parser.cpp',
  'http/range.cpp',
  'http/request.cpp',
  'http/response.cpp',
  'http/status.cpp',
  'http/url.cpp',
  'json/json.cpp',
  'middleware/builtins.cpp',
  'net/connection.cpp',
  'net/notifier.cpp',
  'net/network_interface.cpp',
  'net/periodic_timer.cpp',
  'net/server_runtime.cpp',
  'net/tcp_listener.cpp',
  'net/udp_socket.cpp',
  'net/worker_pool.cpp',
  'net/websocket_protocol.cpp',
  'routing/router.cpp',
  'validation/validator.cpp',
)

atria_lib = static_library(
  'atria',
  atria_sources + platform_sources,
  include_directories: atria_inc,
  dependencies: platform_deps,
  install: true,
)

atria_dep = declare_dependency(
  link_with: atria_lib,
  include_directories: atria_inc,
  dependencies: platform_deps,
)
