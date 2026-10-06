# usage: perl apply_defaults.pl "Name=val Name=val ..."   rewrites TUNEP defaults in src/search.h
undef $/;
open(F,"<","src/search.h"); binmode F; $s=<F>; close F;
for my $kv (split(/\s+/, $ARGV[0])) {
  my ($k,$v) = split(/=/, $kv);
  next unless $k;
  if ($s =~ s/(TUNEP\($k, )(-?\d+)(,)/$1$v$3/) { print "$k -> $v\n"; } else { print "NOT FOUND $k\n"; }
}
open(F,">","src/search.h"); binmode F; print F $s; close F;
