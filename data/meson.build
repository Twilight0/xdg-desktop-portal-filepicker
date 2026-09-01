libexecdir_conf = configuration_data()
libexecdir_conf.set('libexecdir', prefix / libexecdir)

# Portal
install_data(
  [ 'filepicker.portal', 'filepicker-gnome-keyring.portal' ],
  install_dir: datadir / 'xdg-desktop-portal' / 'portals'
)

# Desktop portal configs
install_data(
  [ 'portals.conf', 'qtile-portals.conf' ],
  install_dir: datadir / 'xdg-desktop-portal'
)

# dbus service
service_name = 'org.freedesktop.impl.portal.desktop.filepicker'
configure_file(
  input: service_name + '.service.in',
  output: service_name + '.service',
  configuration: libexecdir_conf,
  install_dir: dbus_service_dir,
)

filename = 'xdg-desktop-portal-xapp-filepicker'
# systemd unit
configure_file(
  input: filename + '.service.in',
  output: filename + '.service',
  configuration: libexecdir_conf,
  install_dir: systemduserunitdir,
)

# Desktop file
configure_file(
  input: filename + '.desktop.in',
  output: filename + '.desktop',
  configuration: libexecdir_conf,
  install: true,
  install_dir: datadir / 'applications',
)
