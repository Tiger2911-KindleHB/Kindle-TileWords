project('tilewords', 'cpp',
  version: '0.1.0-alpha',
  default_options: ['cpp_std=c++17', 'warning_level=2'])

gtk_dep = dependency('gtk+-2.0', required: true)

executable('tilewords',
  ['src/main.cpp'],
  dependencies: [gtk_dep],
  link_args: ['-static-libstdc++', '-static-libgcc'],
  install: false)
