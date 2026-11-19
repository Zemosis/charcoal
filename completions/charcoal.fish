# fish completion for charcoal

function __charcoal_themes
    printf '%s\n' gruvbox_dark nord tokyo_night mono tty
    set -l dir (set -q XDG_CONFIG_HOME; and echo $XDG_CONFIG_HOME; or echo $HOME/.config)/charcoal/themes
    for f in $dir/*.theme
        basename $f .theme
    end
end

complete -c charcoal -n __fish_use_subcommand -a render -d 'Print once and exit'
complete -c charcoal -n __fish_use_subcommand -a export -d 'Write .txt, .ans or .html'

complete -c charcoal -s m -l mode -x -a 'half ascii quad braille edges' -d 'Glyph mode'
complete -c charcoal -s p -l palette -x -a 'standard detailed simple blocks' -d 'Characters, densest first'
complete -c charcoal -s c -l color -x -a 'auto true 256 16 mono' -d 'Color depth'
complete -c charcoal -s d -l dither -x -a 'none floyd bayer' -d 'Dithering'
complete -c charcoal -l fit -x -a 'contain cover stretch' -d 'How the viewer fills its panel'
complete -c charcoal -l theme -x -a '(__charcoal_themes)' -d 'Color theme'
complete -c charcoal -s w -l width -x -d 'Output width in characters'
complete -c charcoal -s H -l height -x -d 'Output height in characters'
complete -c charcoal -s i -l invert -d 'Flip brightness, for light terminals'
complete -c charcoal -l brightness -x -d '-1 to 1'
complete -c charcoal -l contrast -x -d '0 to 4'
complete -c charcoal -l gamma -x -d '0.1 to 5'
complete -c charcoal -l saturation -x -d '0 to 4'
complete -c charcoal -s t -l time -x -d 'Frame shown at this time'
complete -c charcoal -l frame -x -d 'The N-th frame, from 0'
complete -c charcoal -l loop -d 'Loop animations and video'
complete -c charcoal -l no-loop -d 'Play once'
complete -c charcoal -s o -l output -r -d 'Export target (.txt, .ans, .html)'
complete -c charcoal -s h -l help -d 'Show the usage'
complete -c charcoal -s V -l version -d 'Show the version'
