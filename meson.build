project('tilewords', 'cpp',
  version: '0.1.0-alpha',
  default_options: ['cpp_std=c++17', 'warning_level=2'])

cpp = meson.get_compiler('cpp')
gtk_dep = dependency('gtk+-2.0')
thread_dep = dependency('threads')

executable('tilewords',
  ['src/main.cpp'],
  dependencies: [gtk_dep, thread_dep],
  link_args: ['-static-libstdc++', '-static-libgcc'],
  install: false)
