package OpenSSL::safe::installdata;

use strict;
use warnings;
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
    @PREFIX
    @libdir
    @BINDIR @BINDIR_REL_PREFIX
    @LIBDIR @LIBDIR_REL_PREFIX
    @INCLUDEDIR @INCLUDEDIR_REL_PREFIX
    @APPLINKDIR @APPLINKDIR_REL_PREFIX
    @ENGINESDIR @ENGINESDIR_REL_LIBDIR
    @MODULESDIR @MODULESDIR_REL_LIBDIR
    @PKGCONFIGDIR @PKGCONFIGDIR_REL_LIBDIR
    @CMAKECONFIGDIR @CMAKECONFIGDIR_REL_LIBDIR
    $COMMENT $VERSION @LDLIBS
);

our $COMMENT                    = 'This file should be used when building against this OpenSSL build, and should never be installed';
our @PREFIX                     = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src' );
our @libdir                     = ( '' );
our @BINDIR                     = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src/apps' );
our @BINDIR_REL_PREFIX          = ( 'apps' );
our @LIBDIR                     = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src' );
our @LIBDIR_REL_PREFIX          = ( '' );
our @INCLUDEDIR                 = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src/include', '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src/include' );
our @INCLUDEDIR_REL_PREFIX      = ( 'include', './include' );
our @APPLINKDIR                 = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src/ms' );
our @APPLINKDIR_REL_PREFIX      = ( 'ms' );
our @ENGINESDIR                 = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src/engines' );
our @ENGINESDIR_REL_LIBDIR      = ( 'engines' );
our @MODULESDIR                 = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src/providers' );
our @MODULESDIR_REL_LIBDIR      = ( 'providers' );
our @PKGCONFIGDIR               = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src' );
our @PKGCONFIGDIR_REL_LIBDIR    = ( '.' );
our @CMAKECONFIGDIR             = ( '/c/codex/WinXP_Support/Python/cpython/externals/openssl-3.5.9-src' );
our @CMAKECONFIGDIR_REL_LIBDIR  = ( '.' );
our $VERSION                    = '3.5.9';
our @LDLIBS                     =
    # Unix and Windows use space separation, VMS uses comma separation
    $^O eq 'VMS'
    ? split(/ *, */, '-lws2_32 -lgdi32 -lcrypt32 ')
    : split(/ +/, '-lws2_32 -lgdi32 -lcrypt32 ');

1;
