project('tilewords', 'cpp',
  version: '0.1.0-alpha',
  default_options: ['cpp_std=c++17', 'warning_level=2'])

stub_inc = include_directories('stubs', 'src')

executable('tilewords',
  ['src/main.cpp'],
  include_directories: stub_inc,
  link_args: ['-ldl', '-static-libstdc++', '-static-libgcc'],
  install: false)
