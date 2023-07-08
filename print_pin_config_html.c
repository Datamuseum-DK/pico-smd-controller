// $ make print_pin_config_html && ./print_pin_config_html > doc/pin_config.html

#include <stdlib.h>
#include <stdio.h>
#include "config.h"

const char* HEADER =
"<!doctype html>\n"
"<head>\n"
"<style>\n"
"body {\n"
"	font-family: monospace;\n"
"}\n"
"\n"
"table {\n"
"	margin: 1em;\n"
"}\n"
"\n"
".rj {\n"
"	text-align: right;\n"
"	padding-right: 0.5em;\n"
"}\n"
"\n"
".lj {\n"
"	text-align: left;\n"
"	padding-left:  0.5em;\n"
"}\n"
"\n"
".label {\n"
"	width: 10em;\n"
"	font-weight: bold;\n"
"}\n"
"\n"
".input {\n"
"	background: #edc;\n"
"}\n"
"\n"
".output {\n"
"	background: #cde;\n"
"}\n"
"\n"
".legend {\n"
"	display: inline-block;\n"
"	width: 1em;\n"
"	height: 1em;\n"
"	margin-right: 0.2em;\n"
"}\n"
"\n"
".mid {\n"
"	width: 10em;\n"
"	text-align: center;\n"
"}\n"
"\n"
".logic {\n"
"	width: 3.5em;\n"
"	color: #ddd;\n"
"	background-color: #111;\n"
"	text-align: center;\n"
"}\n"
"\n"
".gpio {\n"
"	color: yellow;\n"
"	font-weight: bold;\n"
"}\n"
"\n"
".pin {\n"
"	color: #999;\n"
"	text-align: center;\n"
"}\n"
"\n"
".GND {\n"
"	color: #777;\n"
"}\n"
".AUX {\n"
"	color: #966;\n"
"}\n"
".GPIO {\n"
"	color: #ff0;\n"
"	font-weight: bold\n"
"}\n"
"</style>\n"
"</head>\n"
"<html>\n"
;

const char* FOOTER =
"<ul style=\"list-style-type: none;\">\n"
"<li><div class=\"legend input\"></div>input from drive</li>"
"<li><div class=\"legend output\"></div>output to drive</li>"
"</ul>\n"
"</html>\n"
;

enum {
	GND,
	RUN,
	VREF,
	P3V3OUT,
	P3V3EN,
	VSYS,
	VBUS,
	GP0,
};

#define GP(x)   (GP0+(x))

#define ROWS \
	ROW(   GP(0)    ,   VBUS     ) \
	ROW(   GP(1)    ,   VSYS     ) \
	ROW(   GND      ,   GND      ) \
	ROW(   GP(2)    ,   P3V3EN   ) \
	ROW(   GP(3)    ,   P3V3OUT  ) \
	ROW(   GP(4)    ,   VREF     ) \
	ROW(   GP(5)    ,   GP(28)   ) \
	ROW(   GND      ,   GND      ) \
	ROW(   GP(6)    ,   GP(27)   ) \
	ROW(   GP(7)    ,   GP(26)   ) \
	ROW(   GP(8)    ,   RUN      ) \
	ROW(   GP(9)    ,   GP(22)   ) \
	ROW(   GND      ,   GND      ) \
	ROW(   GP(10)   ,   GP(21)   ) \
	ROW(   GP(11)   ,   GP(20)   ) \
	ROW(   GP(12)   ,   GP(19)   ) \
	ROW(   GP(13)   ,   GP(18)   ) \
	ROW(   GND      ,   GND      ) \
	ROW(   GP(14)   ,   GP(17)   ) \
	ROW(   GP(15)   ,   GP(16)   )


static const char* get_label_class(int e)
{
	const char* in = "label input";
	const char* out = "label output";
	#define PIN(TYPE, NAME, GPN) if (GPN == (e-GP0)) return TYPE==DATA?in:TYPE==STATUS?in:TYPE==CTRL?out:"";
	PINS
	#undef PIN
	return "";
}

static const char* get_label_text(int e)
{
	#define PIN(TYPE, NAME, GPN) if (GPN == (e-GP0)) return #NAME;
	PINS
	#undef PIN
	return "";
}

static const char* get_logic_class(int e)
{
	switch (e) {
	case GND: return "GND";
	case RUN: case VREF: case P3V3OUT: case P3V3EN: case VSYS: case VBUS: return "AUX";
	}
	if (e >= GP0) return "GPIO";
	return "";
}

static char* get_logic_text(int e)
{
	switch (e) {
	case GND:     return "GND";
	case RUN:     return "RUN";
	case VREF:    return "VREF";
	case P3V3OUT: return "3v3OUT";
	case P3V3EN:  return "3v3EN";
	case VSYS:    return "VSYS";
	case VBUS:    return "VBUS";
	}
	if (e >= GP0) {
		const size_t sz=16;
		char* s = malloc(sz);
		snprintf(s, sz, "GP%d", e-GP0);
		return s;
	}
	return "";
}

int main(int argc, char** argv)
{
	puts(HEADER);
	puts("<table>\n");

	int left_pin = 1;
	int right_pin = 40;

	#define ROW(L,R)                                                          \
		printf("<tr>\n");                                                 \
		printf("<td class=\"%s rj\">%s</td>\n", get_label_class(L), get_label_text(L));         \
		printf("<td class=\"logic %s\">%s</td>\n", get_logic_class(L), get_logic_text(L)); \
		printf("<td class=\"pin\">%.2d</td>\n", left_pin); \
		printf("<td class=\"mid\">%s</td>\n", left_pin == 1 ? "&uarr;&uarr;USB&uarr;&uarr;" : ""); \
		printf("<td class=\"pin\">%.2d</td>\n", right_pin); \
		printf("<td class=\"logic %s\">%s</td>\n", get_logic_class(R), get_logic_text(R));    \
		printf("<td class=\"%s lj\">%s</td>\n", get_label_class(R), get_label_text(R)); \
		printf("</tr>\n");                                                \
		left_pin++; \
		right_pin--;
	ROWS
	#undef ROW

	puts("</table>\n");
	puts(FOOTER);
	return EXIT_SUCCESS;
}
