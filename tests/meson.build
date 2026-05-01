catch2_with_main_dep = dependency('catch2-with-main', required: true)

atria_tests = executable(
  'atria_tests',
  files(
    'unit/test_http_parser.cpp',
    'unit/test_http_parser_fuzz.cpp',
    'unit/test_json.cpp',
    'unit/test_middleware.cpp',
    'unit/test_openapi.cpp',
    'unit/test_periodic_timer.cpp',
    'unit/test_range.cpp',
    'unit/test_response.cpp',
    'unit/test_router.cpp',
    'unit/test_router_root.cpp',
    'unit/test_url.cpp',
    'unit/test_validation.cpp',
    'integration/test_connection_limits.cpp',
    'integration/test_integration.cpp',
    'integration/test_runtime_chaos.cpp',
    'integration/test_streaming.cpp',
    'integration/test_udp.cpp',
    'integration/test_websocket.cpp',
    'integration/test_worker_pool.cpp',
  ),
  include_directories: atria_inc,
  dependencies: [atria_dep, catch2_with_main_dep],
)

test('atria_tests', atria_tests, protocol: 'exitcode')
