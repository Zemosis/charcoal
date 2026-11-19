# bash completion for charcoal

_charcoal_themes() {
    local dir="${XDG_CONFIG_HOME:-$HOME/.config}/charcoal/themes" f
    echo gruvbox_dark nord tokyo_night mono tty
    for f in "$dir"/*.theme; do
        [ -e "$f" ] && basename "$f" .theme
    done
}

_charcoal() {
    local cur prev media
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    media='@(png|jpg|jpeg|jpe|jfif|webp|avif|gif|apng|bmp|tif|tiff|tga|pnm|ppm|pgm|pbm|qoi|jxl|heic|heif|ico|exr|dds|mp4|m4v|mkv|webm|mov|avi|flv|wmv|ogv|mpg|mpeg|ts|mts|m2ts|3gp)'

    case "$prev" in
        -m|--mode)
            COMPREPLY=($(compgen -W "half ascii quad braille edges" -- "$cur"))
            return ;;
        -p|--palette)
            COMPREPLY=($(compgen -W "standard detailed simple blocks" -- "$cur"))
            return ;;
        -c|--color)
            COMPREPLY=($(compgen -W "auto true 256 16 mono" -- "$cur"))
            return ;;
        -d|--dither)
            COMPREPLY=($(compgen -W "none floyd bayer" -- "$cur"))
            return ;;
        --fit)
            COMPREPLY=($(compgen -W "contain cover stretch" -- "$cur"))
            return ;;
        --theme)
            COMPREPLY=($(compgen -W "$(_charcoal_themes)" -- "$cur"))
            return ;;
        -w|--width|-H|--height|--brightness|--contrast|--gamma|--saturation|-t|--time|--frame)
            return ;;
        -o|--output)
            compopt -o filenames 2>/dev/null
            COMPREPLY=($(compgen -f -X '!*.@(txt|ans|html)' -- "$cur") $(compgen -d -- "$cur"))
            return ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "-m --mode -p --palette -c --color -d --dither
            --fit --theme -w --width -H --height -i --invert --brightness
            --contrast --gamma --saturation -t --time --frame --loop --no-loop
            -o --output -h --help -V --version" -- "$cur"))
        return
    fi

    # Images, videos and folders; render and export only as the first word
    compopt -o filenames 2>/dev/null
    local shopt_save
    shopt_save=$(shopt -p extglob)
    shopt -s extglob
    COMPREPLY=($(compgen -f -X "!*.$media" -- "$cur") $(compgen -d -- "$cur"))
    eval "$shopt_save"
    if [ "$COMP_CWORD" -eq 1 ]; then
        COMPREPLY+=($(compgen -W "render export" -- "$cur"))
    fi
}

complete -F _charcoal charcoal
