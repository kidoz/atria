ctorwire_dep = dependency('ctorwire', required: true)
logspine_dep = dependency('logspine', required: true)

executable(
  'atria-di-api',
  files('main.cpp'),
  include_directories: atria_inc,
  dependencies: [atria_dep, ctorwire_dep, logspine_dep],
  install: true,
)
