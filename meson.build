project('gnome-recipes', 'c', version: '0.20.1',
        meson_version : '>=0.36.0')

i18n = import('i18n')

conf = configuration_data()

# Needed to find config.h
top_inc = include_directories('.')

libgd = subproject('libgd',
           default_options: [
                'with-tagged-entry=true',
                'static=true'
           ])
libgd_dep = libgd.get_variable('libgd_dep')

# Extract LIBGD_INFO
libgd_path = join_paths([meson.source_root(), 'subprojects', 'libgd'])
git_run = run_command('git', ['-C', libgd_path, 'log', '-1', '--pretty=%h'])
if git_run.returncode() == 0
  conf.set_quoted('LIBGD_INFO', git_run.stdout().strip())
else
  message('Failed to extract git commit hash for libgd:\n' + git_run.stderr())
  conf.set_quoted('LIBGD_INFO', 'unknown')
endif

git_run = run_command('git', ['-C', meson.source_root(), 'describe'])
if git_run.returncode() == 0
  conf.set_quoted('COMMIT_ID', git_run.stdout().strip())
else
  message('Failed to extract git commit ID for recipes:\n' + git_run.stderr())
  conf.set_quoted('COMMIT_ID', 'unknown')
endif

# Options + dependency checking is overly verbose and repetitive right now, but
# there is work ongoing to fix it: https://github.com/mesonbuild/meson/pull/1330
autoar_dep = []
if get_option('autoar') != 'no'
  autoar_dep = dependency('gnome-autoar-0', required : false)
  if autoar_dep.found()
    conf.set('ENABLE_AUTOAR', true)
    conf.set_quoted('AUTOAR_VERSION', autoar_dep.version())
  elif get_option('autoar') == 'yes'
    error('Support for gnome-autoar was requested but not found')
  endif
endif

gspell_dep = []
if get_option('gspell') != 'no'
  gspell_dep = dependency('gspell-1', required : false)
  if gspell_dep.found()
    conf.set('ENABLE_GSPELL', true)
    conf.set_quoted('GSPELL_VERSION', gspell_dep.version())
  elif get_option('gspell') == 'yes'
    error('Support for gspell was requested but not found')
  endif
endif

canberra_dep = []
if get_option('canberra') != 'no'
  canberra_dep = dependency('libcanberra', required : false)
  if canberra_dep.found()
    conf.set('ENABLE_CANBERRA', true)
    conf.set_quoted('CANBERRA_VERSION', canberra_dep.version())
  elif get_option('canberra') == 'yes'
    error('Support for canberra was requested but not found')
  endif
endif

deps = [ dependency('gtk+-3.0', version : '>=3.22'),
         dependency('gmodule-export-2.0'),
         autoar_dep,
         gspell_dep,
         canberra_dep,
         libgd_dep ]

datadir = join_paths([ get_option('prefix'),
                       get_option('datadir') ])
pkgdatadir = join_paths([ get_option('prefix'),
                          get_option('datadir'),
                          'gnome-recipes' ])
bindir = join_paths([ get_option('prefix'), 'bin' ])
schemadir = join_paths([ datadir, 'glib-2.0', 'schemas' ])

conf.set('G_LOG_USE_STRUCTURED', true)
conf.set_quoted('G_LOG_DOMAIN', 'org.gnome.Recipes')
conf.set_quoted('PACKAGE_NAME', 'gnome-recipes')
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', 'gnome-recipes')
conf.set_quoted('DATADIR', datadir)
conf.set_quoted('PKGDATADIR', pkgdatadir)
conf.set_quoted('LOCALEDIR', join_paths([ get_option('prefix'),
                                          get_option('datadir'),
                                          'locale' ]))

# Used while generating cuisine.css and the dbus service file
conf.set('pkgdatadir', pkgdatadir)
conf.set('bindir', bindir)

configure_file(output : 'config.h', configuration : conf)

subdir('src')
subdir('tools')
subdir('data')
subdir('po')
subdir('po-data')
subdir('tests')

meson.add_install_script('tools/meson_post_install.py')
