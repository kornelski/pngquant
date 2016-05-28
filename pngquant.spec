Name:           pngquant
Version:        2.7.1
Release:        1%{?dist}
Summary:        PNG quantization tool for reducing image file size
# New code is under GPL, forked from old BSD-like
License:        GPLv3+ and BSD
URL:            https://pngquant.org
Source0:        https://github.com/pornel/pngquant/archive/%{version}.tar.gz
BuildRequires:  libpng-devel%{?_isa} >= 1.2.46-1
BuildRequires:  zlib-devel%{?_isa} >= 1.2.3-1
BuildRequires:  gcc%{?_isa} >= 4.2-1
Requires:       libpng%{?_isa} >= 1.2.46-1
Requires:       zlib%{?isa} >= 1.2.3-1

%description
pngquant converts 24/32-bit RGBA PNG images to high-quality 8-bit palette
with alpha channel preserved. Quantization significantly reduces file sizes.
Such images are fully standards-compliant and supported by all web browsers.

%prep
%setup -q -n pngquant-%{version}

%build
./configure --prefix=%{_prefix}
make %{?_smp_mflags}


%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/%{_bindir}
make install PREFIX=%{_prefix} DESTDIR=%{buildroot}
install -Dpm0755 pngquant %{buildroot}/%{_bindir}/pngquant
install -Dpm0644 pngquant.1 %{buildroot}/%{_mandir}/man1/pngquant.1


%files
%defattr(-,root,root,-)
%doc README.md CHANGELOG COPYRIGHT
%{_bindir}/pngquant
%{_mandir}/man1/pngquant.1*


%changelog

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
