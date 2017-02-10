#!/usr/bin/perl
# This tests info_output and core_output expansion

use strict;

use Test::More tests => 100;
use File::Temp;
use POSIX qw(strftime);
use Util;
use Cwd;

foreach my $stream (qw(info core)) {
	my $outputdir = File::Temp->newdir('crashtest.XXXXX', CLEANUP => 1, DIR => getcwd);
	my @args = ("${stream}_output" => $outputdir . '/_@Q-a',
			"${stream}_exists" => 'sequence', "${stream}_exists_seq" => 11);

	# Test sequencing
	foreach my $files (1 .. 11) {
		is(crashinfo(@args), 0, 'Crashinfo returns 0');

		my @files = map s/.*\/// && $_, glob "$outputdir/*";
		my @expected = map sprintf('_%02d-a', $_), 0 .. $files - 1;

		is(@files, $files, "$files files are created");
		is(@files ~~ @expected && @expected ~~ @files, 1, "Expected files are created");
	}

	is(crashinfo(@args) >> 8, 4, 'Crashinfo returns 4');
	is(scalar(map $_, <$outputdir/*>), 11, '11 files are created');

	# Test length of the name
	my %exist_seqs = ( 9 => "0", 10 => "0", 99 => "00", 100 => "00", 101 => "000" );
	while (my ($seq, $expands) = each %exist_seqs) {
		$outputdir = File::Temp->newdir('crashtest.XXXXX', CLEANUP => 1, DIR => getcwd);
		@args = ("${stream}_output" => $outputdir . '/@Q', "${stream}_exists" => 'sequence',
				"${stream}_exists_seq" => $seq);
		is(crashinfo(@args), 0, 'Crashinfo returns 0');
		my @files = map s/.*\/// && $_, glob "$outputdir/*";
		is(scalar @files, 1, "One file is created");
		is($files[0], $expands, "Name expanded to $expands");
	}
}
