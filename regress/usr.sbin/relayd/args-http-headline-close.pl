# test http connection over http relay
# The client writes an incomplete header line and closes the connection.
# Check that the relay establishes and also closes the session.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    print "GET ";  # missing new line
	},
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    "match request header log foo",
	    "match response header log bar",
	],
	loggrep => {
	    qr/session 1 .*, done/ => 1,
	},
    },
    server => {
	noserver => 1,
	nocheck => 1,
    },
);

1;
