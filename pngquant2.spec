Name:           pngquant
Version:        2.3.0
Release:        1%{?dist}
Summary:        PNG quantization tool for reducing image file size
License:        BSD
URL:            http://pngquant.org
Source0:        https://github.com/pornel/pngquant/tarball/%{version}
BuildRequires:  libpng-devel%{?_isa} >= 1.2.46-1
BuildRequires:  zlib-devel%{?_isa} >= 1.2.3-1
BuildRequires:  gcc%{?_isa} >= 4.2-1
Requires:       libpng%{?_isa} >= 1.2.46-1
Requires:       zlib%{?isa} >= 1.2.3-1

%description
pngquant converts 24/32-bit RGBA PNG images to 8-bit palette with
alpha channel preserved. Such images are compatible with all modern web
browsers and a compatibility setting is available to help transparency
degrade well in Internet Explorer 6. Quantized files are often 40-70
percent smaller than their 24/32-bit version. pngquant uses the
median cut algorithm.

%prep
# I'll just leave this here for until rpmbuild starts supporting variables
#GIT_VERSION=`tar -taf ../SOURCES/%{version} | head -n1 | sed 's/.*-//' | sed 's/\///'`
#%setup -q -n pornel-pngquant-$GIT_VERSION
%setup -q -n pornel-pngquant-8d21a45

%build
make %{?_smp_mflags}


%install
rm -rf %{buildroot}
make install PREFIX=%{_prefix} DESTDIR=%{buildroot}
install -Dpm0755 pngquant %{buildroot}/%{_bindir}/pngquant
install -Dpm0644 pngquant.1 %{buildroot}/%{_mandir}/man1/pngquant.1


%files
%defattr(-,root,root,-)
%doc README.md CHANGELOG COPYRIGHT
%{_bindir}/pngquant
%{_mandir}/man1/pngquant.1*


%changelog

* Sat Sep 13 2014 Michael Dec <grepwood@sucs.org> 2.3.0-1
- Adaptation for pngquant2

* Fri Sep 12 2014 Michael Dec <grepwood@sucs.org> 1.8.3-1
- Update to latest upstream version and corrected the .spec

* Thu May 03 2012 Craig Barnes <cr@igbarn.es> - 1.7.2-1
- Update to latest upstream version

* Sun Jan 15 2012 Craig Barnes <cr@igbarn.es> - 1.7.0-1
- Update to latest upstream version

* Mon Jan 09 2012 Craig Barnes <cr@igbarn.es> - 1.6.4-1
- Update to latest version
- Remove Makefile patch (merged upstream)
- Use prefix macro when installing (upstream changed the default prefix)

* Wed Dec 28 2011 Craig Barnes <cr@igbarn.es> - 1.6.2-1
- Initial package
