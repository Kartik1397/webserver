# HTTP-Webserver

ws is a zero-configuration command-line http server which can server php files.

# Performance

### ws1 - cgi

- Serving index.html
```bash
➜  ~ siege http://127.0.0.1:8080
** SIEGE 4.0.4
** Preparing 25 concurrent users for battle.
The server is now under siege...^C
Lifting the server siege...
Transactions:		      170306 hits
Availability:		      100.00 %
Elapsed time:		       71.40 secs
Data transferred:	    90976.02 MB
Response time:		        0.01 secs
Transaction rate:	     2385.24 trans/sec
Throughput:		     1274.17 MB/sec
Concurrency:		       24.60
Successful transactions:      170306
Failed transactions:	           0
Longest transaction:	        0.14
Shortest transaction:	        0.00
```

- Serving index.php
```bash
** SIEGE 4.0.4
** Preparing 25 concurrent users for battle.
The server is now under siege...^C
Lifting the server siege...
Transactions:		        2028 hits
Availability:		      100.00 %
Elapsed time:		       10.01 secs
Data transferred:	        0.46 MB
Response time:		        0.11 secs
Transaction rate:	      202.60 trans/sec
Throughput:		        0.05 MB/sec
Concurrency:		       23.27
Successful transactions:        2028
Failed transactions:	           0
Longest transaction:	        2.96
Shortest transaction:	        0.00
```

### ws2 - fast-cgi
- Serving index.html
```bash
➜  ~ siege http://127.0.0.1:8080
** SIEGE 4.0.4
** Preparing 25 concurrent users for battle.
The server is now under siege...^C
Lifting the server siege...
Transactions:		      164006 hits
Availability:		      100.00 %
Elapsed time:		       65.32 secs
Data transferred:	    87607.92 MB
Response time:		        0.01 secs
Transaction rate:	     2510.81 trans/sec
Throughput:		     1341.21 MB/sec
Concurrency:		       24.60
Successful transactions:      164006
Failed transactions:	           0
Longest transaction:	        0.22
Shortest transaction:	        0.00
```

- Serving index.php
```bash
➜  ~ siege -c 2 -r 40 http://127.0.0.1:8080/index.php
** SIEGE 4.0.4
** Preparing 2 concurrent users for battle.
The server is now under siege...
Transactions:		         149 hits
Availability:		       97.39 %
Elapsed time:		        0.20 secs
Data transferred:	        0.04 MB
Response time:		        0.00 secs
Transaction rate:	      745.00 trans/sec
Throughput:		        0.18 MB/sec
Concurrency:		        1.20
Successful transactions:         149
Failed transactions:	           4
Longest transaction:	        0.05
Shortest transaction:	        0.00
```
