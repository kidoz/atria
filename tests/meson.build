catch2_with_main_dep = dependency('catch2-with-main', required: true)

atria_tests = executable(
  'atria_tests',
  files(
    'test_connection_limits.cpp',
    'test_http_parser.cpp',
    'test_http_parser_fuzz.cpp',
    'test_integration.cpp',
    'test_json.cpp',
    'test_middleware.cpp',
    'test_openapi.cpp',
    'test_response.cpp',
    'test_router.cpp',
    'test_streaming.cpp',
    'test_url.cpp',
    'test_validation.cpp',
    'test_worker_pool.cpp',
  ),
  include_directories: atria_inc,
  dependencies: [atria_dep, catch2_with_main_dep],
)

test('atria_tests', atria_tests, protocol: 'exitcode')
