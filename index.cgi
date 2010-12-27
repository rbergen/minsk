#!/usr/bin/perl

use strict;
use warnings;

use lib '../lib';
use UCW::CGI;
use File::Temp;
use POSIX;

my $src;
my $example;
my $trace;

POSIX::nice(10);

UCW::CGI::parse_args({
	'src' => { 'var' => \$src, 'multiline' => 1 },
	'example' => { 'var' => \$example, 'check' => '\w+' },
	'trace' => { 'var' => \$trace, 'check' => '[0-2]', 'default' => 0 },
});

if ($example ne '' && open EX, "ex-$example") {
	local $/;
	undef $/;
	$src = <EX>;
	close EX;
}

my $src_html = html_escape($src);
my @trsel = map { $trace == $_ ? "selected" : "" } 0..2;

print <<EOF ;
Content-type: text/html; charset=utf-8

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html40/strict.dtd">
<html><head>
<title>Minsk-2 Emulator</title>
<link rel=stylesheet title=Default href="minsk.css" type="text/css" media=all>
</head><body>
<h1>Minsk-2 Emulator</h1>

<p id=top>(see <a href='readme.html'>instructions</a>)

EOF

if ($src ne '') {
	print "<h2>Output</h2>\n\n<pre id=output><code>";
	my $tmpf = new File::Temp();
	print $tmpf $src, "\n";
	$tmpf->flush();
	my $in = $tmpf->filename;
	open SIM, "./minsk --trace=$trace --cpu-quota=1000 --print-quota=100 <$in |" or die;
	while (<SIM>) {
		print html_escape($_);
	}
	close SIM;
	print "</code></pre>\n\n";
}

print <<EOF ;

<h2>Input</h2>
<form action='?' method=POST accept-charset='US-ASCII UTF-8'>
<textarea name=src rows=20 cols=80>$src_html</textarea>
<p><button name=submit type=submit>Run</button>
<select name=trace>
	<option $trsel[0] value=0>Tracing off</option>
	<option $trsel[1] value=1>Brief tracing</option>
	<option $trsel[2] value=2>Detailed tracing</option>
</select>
</form>
EOF

print <<EOF ;
<hr>
<p>Written by <a href='http://mj.ucw.cz/'>Martin Mareš</a>. Version 1.0 (2010-12-27).
</body></html>
EOF
