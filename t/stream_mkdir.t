#!/usr/bin/perl
# This tests info_output and core_output expansion

use strict;

use Test::More tests => 30;
use File::Temp;
use POSIX qw(strftime);
use Util;
use Cwd;

my %outputs = (
	"%Y-%m-%d/output" => strftime("%Y-%m-%d/output", gmtime),
	'@@@@/@a' => '@@/@a',
	'@e/@e/@e' => 'crash/crash/crash',
);

foreach my $stream (qw(info core)) {
	foreach my $output (keys %outputs) {
		my $outputdir = File::Temp->newdir('crashtest.XXXXX', CLEANUP => 1, DIR => getcwd);

		is(crashinfo("${stream}_output" => "$outputdir/$output") >> 8, 4, "Crashinfo with mkdir disabled returns 4");

		my @files = map s/.*\/// && $_, glob "$outputdir/*";
		is(@files, 0, "No file is created");


		is(crashinfo("${stream}_output" => "$outputdir/$output", "${stream}_mkdir" => 1), 0, "Crashinfo with mkdir enabled return 0");
		@files = map s/.*\/// && $_, glob "$outputdir/*";
		is(@files, 1, "One directory is created");

		is(-e "$outputdir/$outputs{$output}", 1, "Output $outputs{$output} exists");
	}
}
