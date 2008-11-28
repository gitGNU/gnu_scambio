#!/usr/bin/env perl

use Net::SMTP;

$smtp = Net::SMTP->new('scambio.happyleptic.org', Debug=>1);
$smtp->mail($ENV{USER});
$smtp->to('rixed@dangi.happyleptic.org');
$smtp->data();
$smtp->datasend("To: rixed\n");
$smtp->datasend("\n");
$smtp->datasend("A simple test message\n");
$smtp->dataend();

$smtp->quit;

