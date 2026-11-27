Name:           charcoal
Version:        0.1.1
Release:        1%{?dist}
Summary:        Terminal ASCII art viewer for images, animations and video

License:        MIT
URL:            https://github.com/Zemosis/charcoal
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libwebpdemux)

%description
charcoal shows images, animated images and video as character art in the
terminal, in a full-screen viewer with panels, live controls and playback.
It has five glyph modes (half and quarter blocks, braille, edges and ASCII),
true color, 256 and 16 color output with dithering, themes, a file picker,
and render and export commands for scripts.

%prep
%autosetup

%build
%make_build release

%install
# Fedora's flags are only set for %%build; without them make would rebuild
%set_build_flags
%make_install PREFIX=%{_prefix}

%check
make test

%files
%license LICENSE
%doc README.md
%{_bindir}/charcoal
%{_mandir}/man1/charcoal.1*
%{bash_completions_dir}/charcoal
%{zsh_completions_dir}/_charcoal
%{fish_completions_dir}/charcoal.fish

%changelog
* Fri Nov 27 2026 Zemosis <Zemosis@users.noreply.github.com> - 0.1.1-1
- First package
