use strict;
use Cwd;

package Util;

use base 'Exporter';
our @EXPORT = qw(crashinfo $exe $exe_exclamation);

our $exe = Cwd::getcwd() . '/inputdir/crash';
our $exe_exclamation = $exe;
$exe_exclamation =~ tr|/|!|;

sub crashinfo {
	my @args = qw(-ocore=inputdir/core -oproc_path=inputdir/proc);
	while(my ($opt, $val) = splice(@_, 0, 2)) {
		push @args, "-o$opt=$val";
	}
	return system '../crashinfo', @args;
}

1;
