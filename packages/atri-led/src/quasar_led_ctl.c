#include "atri_led.h"
#include "atri_led_anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *anim_dir = "/etc/atriled/animations";

static void install_anim(const char *name, const char *file)
{
	mkdir("/etc/atriled/animations", 0755);
	
	char dest[256];
	snprintf(dest, sizeof(dest), "/etc/atriled/animations/%s.anim", name);
	
	FILE *out = fopen(dest, "w");
	if (!out) { perror("fopen"); return; }
	
	if (file) {
		FILE *in = fopen(file, "r");
		if (in) {
			char buf[256];
			while (fgets(buf, sizeof(buf), in))
				fprintf(out, "%s", buf);
			fclose(in);
		}
	} else {
		fprintf(out, "%s\n255,100,50 100,255,50 50,100,255\n", name);
	}
	fclose(out);
	printf("Installed animation: %s\n", name);
}

static void list_animations(void)
{
	DIR *d = opendir(anim_dir);
	if (!d) { printf("No animations found\n"); return; }
	
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (strstr(de->d_name, ".anim"))
			printf("%s\n", de->d_name);
	}
	closedir(d);
}

static struct animation *find_builtin(const char *name)
{
	if (strcmp(name, "happy") == 0) return &anim_happy;
	if (strcmp(name, "focused") == 0) return &anim_focused;
	if (strcmp(name, "calming") == 0) return &anim_calming;
	if (strcmp(name, "love") == 0) return &anim_love;
	if (strcmp(name, "night") == 0) return &anim_night;
	if (strcmp(name, "excited") == 0) return &anim_excited;
	return NULL;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Usage: %s <install|list|play|anim> [args]\n", argv[0]);
		printf("  install <name> [file]   Install animation\n");
		printf("  list                    List all animations\n");
		printf("  play <name>             Play animation (builtin or installed)\n");
		printf("  anim <name>             Set static color\n");
		printf("Builtin animations:\n");
		printf("  happy - warm golden waves\n");
		printf("  focused - deep purple flow\n");
		printf("  calming - slow ocean breath\n");
		printf("  love - soft heartbeat\n");
		printf("  night - gentle starlight\n");
		printf("  excited - fiery energy\n");
		return 1;
	}

	if (strcmp(argv[1], "install") == 0 && argc > 2) {
		install_anim(argv[2], argc > 3 ? argv[3] : NULL);
	} else if (strcmp(argv[1], "list") == 0) {
		list_animations();
	} else if (strcmp(argv[1], "play") == 0 && argc > 2) {
		struct atri_led led;
		atri_led_init(&led);
		atri_led_init_animations();
		struct animation *anim = find_builtin(argv[2]);
		if (!anim) {
			printf("Animation not found: %s\n", argv[2]);
			return 1;
		}
		printf("Playing: %s\n", anim->name);
		atri_led_play_animation(&led, anim);
	} else if (strcmp(argv[1], "anim") == 0 && argc > 2) {
		struct atri_led led;
		atri_led_init(&led);
		atri_led_anim_color(&led, argv[2]);
	} else {
		printf("Unknown command: %s\n", argv[1]);
		return 1;
	}
	
	return 0;
}