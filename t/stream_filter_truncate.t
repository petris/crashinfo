#!/usr/bin/perl
# This tests handling of stream interruption when one of the filters terminates.

use strict;

use Test::More tests => 30;
use File::Temp;
use POSIX qw(strftime WIFEXITED WEXITSTATUS);
use Util;
use Cwd;

my $size = 1000;

my @filters = (
	["cat", "head -c $size"],
	["head -c $size", "cat"],
	["head -c $size"],
);

foreach my $stream (qw(info core)) {
	foreach my $filter (@filters) {
		my $outputdir = File::Temp->newdir('crashtest.XXXXX', CLEANUP => 1, DIR => getcwd);
		my @conf = ("${stream}_output" => "$outputdir/output");

		push @conf, map +("${stream}_filter" => $_), @$filter;

		local $, = ' ';
		diag("Configuration: @conf");

		my $rtn = crashinfo(@conf);
		ok(WIFEXITED($rtn), 'Crashinfo exitted successfully');
		cmp_ok(WEXITSTATUS($rtn), '<=', 1, 'Crashinfo return value is 0 or 1 (SIGPIPE may be received)');

		my @files = map s/.*\/// && $_, glob "$outputdir/*";
		is(@files, 1, 'Only one file is created');
		is($files[0], "output", "$stream filename is output");
		is(-s "$outputdir/output", $size, "File is truncated to $size bytes");
	}
}
