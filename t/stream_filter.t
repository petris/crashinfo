#!/usr/bin/perl
# This tests info_output and core_output expansion

use strict;

use Test::More tests => 27;
use File::Temp;
use POSIX qw(strftime);
use Util;
use Cwd;

my @filters = (
	["gzip" => "gunzip"],
	["base64", => "base64 -d"],
	["openssl enc -aes-256-cbc -salt -k blahblah" => "openssl enc -aes-256-cbc -d -k blahblah"],
);

foreach my $stream (qw(info core)) {
	for my $i (0 .. $#filters) {
		my $outputdir = File::Temp->newdir('crashtest.XXXXX', CLEANUP => 1, DIR => getcwd);
		my @conf = ("${stream}_output" => "$outputdir/output");

		push @conf, map +("${stream}_filter" => $_->[0]), @filters[0 .. $i];

		is(crashinfo(@conf), 0, 'Crashinfo return value is 0');

		my @files = map s/.*\/// && $_, glob "$outputdir/*";
		is(@files, 1, 'Only one file is created');
		is($files[0], "output", "$stream filename is output");

		my $reverse = join ' | ', map $_->[1], reverse @filters[0 .. $i];
		$reverse = "cat '$outputdir/output' | $reverse > '$outputdir/reverse'";
		is(system($reverse), 0, 'Reverse filters');

		if ($stream eq 'core') {
			is(system("diff '$outputdir/reverse' 'inputdir/core'"), 0, 'Core is the same');
		}
	}
}
