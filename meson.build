project('kuznix-tui-de', 'cpp', version : '0.1.0')
add_project_arguments('-DMESON_BUILD_VERSION="@0@"'.format(meson.project_version()), language : 'cpp')

ncurses_dep = dependency('ncurses', required : true)

executable('kuznix-tui-de', 'src/main.cpp',
  dependencies : ncurses_dep,
  install : true
)
