Summary: Small and fast command line MP3 editor.
Name: cutmp3
Version: 3.0
Release: 1
#Epoch: 1
License: GPL
Group: Applications/Multimedia
Source0: %{name}-%{version}.tar.bz2
URL: http://www.puchalla-online.de/cutmp3.html
BuildRoot: %{_tmppath}/%{name}-root
BuildPreReq: readline-devel
BuildPreReq: ncurses-devel
Requires: /sbin/ldconfig

%description
cutmp3 is a small and fast command line MP3 editor.  It lets you select
sections of an MP3 interactively or via a timetable and save them to
separate files without quality loss.  It uses mpg123 for playback and
works with VBR files and even with files bigger than 2GB.  Other
features are configurable silence seeking and ID3 tag seeking,
which are useful for concatenated mp3s.

%prep
%setup -q

%build
make

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_prefix}/share/apps/konqueror/servicemenus
make PREFIX=%{buildroot}%{_prefix} KDEDIR=%{buildroot}%{_prefix} install

%clean
rm -rf %{buildroot}

%files
%defattr(-, root, root)
%doc BUGS COPYING Changelog README* TODO USAGE
%{_bindir}/*
%{_datadir}/apps/konqueror/servicemenus/%{name}.desktop
%{_mandir}/man1/%{name}.1.gz

%changelog
* Sat Nov 19 2005 Andre Oliveira da Costa <blueser@gmail.com>
- included manpage

* Sat Jun 12 2004 Andre Oliveira da Costa <acosta@ar.microlink.com.br>
- upgraded to version 1.5.2

* Wed Feb 25 2004 Andre Oliveira da Costa <acosta@ar.microlink.com.br>
- Initial RPM release.
