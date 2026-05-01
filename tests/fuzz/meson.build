fuzz_has_libfuzzer = cxx.links(
  '''
  #include <cstddef>
  #include <cstdint>
  extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
  ''',
  args: ['-fsanitize=fuzzer,address,undefined'],
  name: 'libFuzzer main',
)
fuzz_link_args = []
fuzz_extra_sources = []
if fuzz_has_libfuzzer
  fuzz_link_args = ['-fsanitize=fuzzer,address,undefined']
else
  message('libFuzzer main is unavailable; building fuzz targets with the standalone corpus driver.')
  fuzz_extra_sources = files('fuzz_standalone_main.cpp')
endif

fuzz_targets = {
  'atria-fuzz-http-parser': 'fuzz_http_parser.cpp',
  'atria-fuzz-url-path': 'fuzz_url_path.cpp',
  'atria-fuzz-router': 'fuzz_router.cpp',
  'atria-fuzz-websocket-frame': 'fuzz_websocket_frame.cpp',
}

foreach target_name, source_file : fuzz_targets
  executable(
    target_name,
    files(source_file) + fuzz_extra_sources,
    include_directories: atria_inc,
    dependencies: [atria_dep],
    link_args: fuzz_link_args,
  )
endforeach
