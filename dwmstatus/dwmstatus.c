#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

char *tzshh = "Asia/Shanghai";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	bzero(buf, sizeof(buf));
	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}


enum
{
	_user,
	_nice,
	_system,
	_idle,
	_iowait,
	_irq,
	_softirq,
	_steal_time,
	_virtual,
	_num_lines,
};

char *
getcpu(void)
{
	unsigned long curr[_num_lines];

	FILE *stat_f = fopen("/proc/stat", "r");
	if (! stat_f)
	{
		return "open /proc/stat failed";
	}

	fscanf(stat_f, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu %lu",
	       &curr[_user], &curr[_nice],    &curr[_system],     &curr[_idle],  &curr[_iowait],
	       &curr[_irq],  &curr[_softirq], &curr[_steal_time], &curr[_virtual]);

	fclose(stat_f);

	static unsigned long prev_used = 0;
	static unsigned long prev_idle = 0;

	unsigned long this_idle, this_used;

	unsigned long curr_idle, curr_used;

	//fprintf(stderr, "%lu %lu\n", curr[_user], curr[_system]);

	curr_used = curr[_user] + curr[_nice] + curr[_system] + curr[_iowait] + curr[_irq] + curr[_softirq];
	curr_idle = curr[_idle];

	this_used = curr_used - prev_used;
	this_idle = curr_idle - prev_idle;

	prev_used = curr_used;
	prev_idle = curr_idle;

	long double load = (long double)this_used / (long double)(this_idle + this_used);

	static short busy_times = 0;
	if (load >= 0.9)
		busy_times ++;
	else
		busy_times = 0;

	static short idle_times = 0;
	if (load <= 0.05)
		idle_times ++;
	else
		idle_times = 0;

	char color = '\x01';
	if (busy_times > 4)
		color = '\x08';
	else if (idle_times > 10)
		color = '\x04';

	return smprintf("%c\uE026 %.2llf%%\x01", color, load*100);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

/*
char *
getbattery(char *base)
{
	char *path, line[513];
	FILE *fd;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	path = smprintf("%s/info", base);
	fd = fopen(path, "r");
	if (fd == NULL) {
		perror("fopen");
		exit(1);
	}
	free(path);
	while (!feof(fd)) {
		if (fgets(line, sizeof(line)-1, fd) == NULL)
			break;

		if (!strncmp(line, "present", 7)) {
			if (strstr(line, " no")) {
				descap = 1;
				break;
			}
		}
		if (!strncmp(line, "design capacity", 15)) {
			if (sscanf(line+16, "%*[ ]%d%*[^\n]", &descap))
				break;
		}
	}
	fclose(fd);

	path = smprintf("%s/state", base);
	fd = fopen(path, "r");
	if (fd == NULL) {
		perror("fopen");
		exit(1);
	}
	free(path);
	while (!feof(fd)) {
		if (fgets(line, sizeof(line)-1, fd) == NULL)
			break;

		if (!strncmp(line, "present", 7)) {
			if (strstr(line, " no")) {
				remcap = 1;
				break;
			}
		}
		if (!strncmp(line, "remaining capacity", 18)) {
			if (sscanf(line+19, "%*[ ]%d%*[^\n]", &remcap))
				break;
		}
	}
	fclose(fd);

	if (remcap < 0 || descap < 0)
		return NULL;

	return smprintf("%.0f", ((float)remcap / (float)descap) * 100);
}*/

char *
getbattery(void)
{
	unsigned long charge_now, charge_full;

	FILE *fd;

	fd = fopen("/sys/class/power_supply/BAT0/charge_now", "r");
	if(! fd)
	{
		return "ERR";
	}
	fscanf(fd, "%lu", &charge_now);
	fclose(fd);

	fd = fopen("/sys/class/power_supply/BAT0/charge_full", "r");
	if(! fd)
	{
		return "ERR";
	}
	fscanf(fd, "%lu", &charge_full);
	fclose(fd);

	double val = ((long double)charge_now / (long double)charge_full)*100;

	int ac_online = 0;

	fd = fopen("/sys/class/power_supply/AC/online", "r");
	if (! fd)
		ac_online = 0;
	else
	{
		fscanf(fd, "%d", &ac_online);
		fclose(fd);
	}

	const char *icon = NULL;
	if (ac_online)
		icon = "\uE040";
	else if (val > 90)
		icon = "\uE03F";
	else if (val > 50)
		icon = "\uE03E";
	else if (val > 10)
		icon = "\uE03D";
	else
		icon = "\uE03C";

	if (val == 0)
		return smprintf("\x08""%s %.2lf", icon, val);

	return smprintf("%s %.2lf",  icon, val);
}

int
main(void)
{
	char *status;
	char *cpu;
	char *bat;
	char *tmshh;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(2)) {
		cpu = getcpu();
		bat = getbattery();
		tmshh = mktimes("%H:%M", tzshh);

		status = smprintf("\x06[%s\x06|\x01%s%%\x06|\x01\uE015 %s\x06]""\x01""DWM-6.0#\x04GaoJinpei",
				cpu, bat, tmshh);
		setstatus(status);
		free(cpu);
		free(bat);
		free(tmshh);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

