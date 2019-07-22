# Install commandline completion files
if (get_option('bash_completions'))
   with_bash_completions_dir = get_option('with-bash-completions-dir')
   if with_bash_completions_dir == ''
      with_bash_completions_dir = join_paths(path_datadir, 'bash-completion', 'completions')
   endif
   bash_files = files('completions/bash/clr-boot-manager')
   install_data(bash_files, install_dir: with_bash_completions_dir)
endif

if (get_option('zsh_completions'))
   with_zsh_completions_dir = get_option('with-zsh-completions-dir')
   if with_zsh_completions_dir == ''
      with_zsh_completions_dir = join_paths(path_datadir, 'zsh', 'site-functions')
   endif
   zsh_files = files('completions/zsh/_clr-boot-manager')
   install_data(zsh_files, install_dir: with_zsh_completions_dir)
endif

# Write systemd unit
data_conf = configuration_data()
data_conf.set('BINDIR', path_bindir)

configure_file(
    input: 'clr-boot-manager-booted.service.in',
    output: 'clr-boot-manager-booted.service',
    configuration: data_conf,
    install_dir: with_systemd_system_unit_dir,
)
