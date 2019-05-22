project('gamemode-tester',
	'c',
	version: '0.1.0',
	meson_version: '>= 0.40.0',
	default_options: ['warning_level=1',
			  'c_std=gnu11',
			  'buildtype=debugoptimized']
)

cc = meson.get_compiler('c')

#dependencies

i18n = import('i18n')
gnome = import('gnome')

dl   = cc.find_library('dl', required: true)
dbus = dependency('dbus-1')
gio  = dependency('gio-2.0', version: '>= 2.50')
unix = dependency('gio-unix-2.0', version: '>= 2.50')
gtk3 = dependency('gtk+-3.0', version: '>= 3.22')
gmld = dependency('gamemode-libdbus', version: '>= 0.1')

prefixdir = get_option('prefix')
localedir = join_paths(prefixdir, get_option('localedir'))

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', 'gamemode-tester')
conf.set_quoted('LOCALEDIR', localedir)

configure_file(
  output: 'config.h',
  configuration: conf,
)

add_project_arguments([
  '-I' + meson.build_root(),
], language: 'c')


# app
subdir('app/po')
subdir('app/data')

app_sources = [
  'app/main.c',
  'app/window.c',
]

app_sources += gnome.compile_resources(
  'app-resources',
  'app/app.gresource.xml',
  source_dir: 'app',
  c_name: 'gm_tester'
)

executable('gamemode-tester', app_sources,
  dependencies: [dl, gio, unix, gtk3, gmld],
  install: true,
)

meson.add_install_script('app/postinstall.py')

