#!/usr/bin/env python
import os,sys,re

if len(sys.argv) != 2:
	sys.stderr.write("Usage: %s </path/to/telemetry.log>\n" % sys.argv[0])
	sys.exit(1)


lre = re.compile(r"(\d\d\d\d)-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)")
wre = re.compile(r"\s+")


print("""
<!doctype html>
<html>
<head>
<style>

body {
	background: black;
	color: white;
	font-family: monospace;
}

thead th {
	position: sticky;
	top: 0px;
	background: black;
}

tr:hover {
	background-color: #620;
}

.time {
	color: #0aa;
}

.beginend {
	color: yellow;
	font-weight: bold;
}

.note {
	color: #f6a;
	font-family: sans;
}

.edgehi {
	background: #0f6;
	color: black;
	font-weight: bold;
	text-align: center;
}

.hi {
	background: #070;
	color: white;
	font-weight: bold;
}

.edgelo {
	background: #f06;
	color: black;
	font-weight: bold;
	text-align: center;
}

.lo {
	background: #700;
	color: white;
	font-weight: bold;
}

</style>
</head>
<body>
<table>
<thead>
	<tr>
		<th>time</th>
		<th>TAGUNIT</th>
		<th>TAG1</th>
		<th>TAG2</th>
		<th>TAG3</th>
		<th>BIT0</th>
		<th>BIT1</th>
		<th>BIT2</th>
		<th>BIT3</th>
		<th>BIT4</th>
		<th>BIT5</th>
		<th>BIT6</th>
		<th>BIT7</th>
		<th>BIT8</th>
		<th>BIT9</th>
		<th>FAULT</th>
		<th>SEEK ERROR</th>
		<th>ON CYLINDER</th>
		<th>UNIT READY</th>
		<th>UNIT SELECTED</th>
		<th>SEEK END</th>
	</tr>
</thead>
""")

def td(s):
	s = s[2:]
	cls = {">":"edge",":":""}[s[0]] + {"*":"hi",".":"lo"}[s[1]]
	txt = ""
	if s[0] == ">":
		if s[1] == "*": txt = "&uarr;"*5;
		if s[1] == ".": txt = "&darr;"*5;

	print(("<td class=\"%s\">" % cls) + txt + "</td>")


def tdtime(xs):
	print("<td class=\"time\">" + " ".join(xs[0:2]) + "</td>")

for line in open(sys.argv[1], "r"):
	mo = lre.match(line)
	print("<tr>")
	if mo:
		xs = [x for x in wre.split(line) if len(x) > 0]
		if len(xs) == 23: # HAX
			tdtime(xs)
			xs = xs[2:]
			for i in range(14):
				td(xs[0])
				xs = xs[1:]
			assert(xs[0] == "|")
			xs = xs[1:]
			for i in range(6):
				td(xs[0])
				xs = xs[1:]
			assert(len(xs) == 0)
		else:
			tdtime(xs)
			print("<td class=\"beginend\" colspan=\"20\">" + " ".join(xs[2:]) + "</td")
	else:
		if len(line.strip()) > 0:
			print("<td class=\"note\" colspan=\"21\">" + line.strip() + "</td")
	print("</tr>")



print("""
</table>
</html>
""")
