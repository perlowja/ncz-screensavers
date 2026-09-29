add_languages('vala', required: true)
add_project_arguments('--pkg=posix', '--vapidir=' + get_option('singularity-vapidir'), language: 'vala')

libsingularity_dep = dependency('singularity-1.0')
plugin_dir = get_option('singularity-plugin-dir')

subdir('screensaver')
