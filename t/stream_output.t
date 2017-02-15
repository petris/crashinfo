#!/usr/bin/perl
# This tests info_output and core_output expansion

use strict;

use Test::More tests => 48;
use File::Temp;
use POSIX qw(strftime);
use Util;
use Cwd;

my %outputs = (
	"output" => "output",
	"%Y-%m-%d-output" => strftime("%Y-%m-%d-output", gmtime),
	'@@@@@a' => '@@@a',
	'@@@@@e' => '@@crash',
	'@@@@@@e' => '@@@e',
	'%Y@e@e' => strftime("%Ycrashcrash", gmtime),
	'@E' => $exe_exclamation,
	'=@p=' => "=$Util::pid=",
);

foreach my $stream (qw(info core)) {
	foreach my $output (keys %outputs) {
		my $outputdir = File::Temp->newdir('crashtest.XXXXX', CLEANUP => 1, DIR => getcwd);

		is(crashinfo("${stream}_output" => "$outputdir/$output"), 0, "Crashinfo return value is 0");

		my @files = map s/.*\/// && $_, glob "$outputdir/*";
		is(@files, 1, "Only one file is created");
		is($files[0], $outputs{$output}, "$stream filename is $outputs{$output}");
	}
}
